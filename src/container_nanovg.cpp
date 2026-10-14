#include "container_nanovg.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{
NVGcolor to_nvg(const litehtml::web_color& c)
{
    return nvgRGBA(c.red, c.green, c.blue, c.alpha);
}

std::string to_lower(std::string s)
{
    for(auto& ch : s)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string trim_family(std::string s)
{
    auto is_junk = [](char c) { return std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\''; };
    while(!s.empty() && is_junk(s.front()))
    {
        s.erase(s.begin());
    }
    while(!s.empty() && is_junk(s.back()))
    {
        s.pop_back();
    }
    return to_lower(s);
}

// Map common family names onto the generic families we register.
std::string generic_alias(const std::string& f)
{
    if(f == "sans" || f == "arial" || f == "helvetica" || f == "verdana" || f == "tahoma" || f == "system-ui" ||
       f == "segoe ui" || f == "roboto")
    {
        return "sans-serif";
    }
    if(f == "times" || f == "times new roman" || f == "georgia")
    {
        return "serif";
    }
    if(f == "mono" || f == "courier" || f == "courier new" || f == "consolas" || f == "menlo")
    {
        return "monospace";
    }
    return f;
}

bool file_exists(const std::string& path)
{
    std::ifstream f(path);
    return f.good();
}

// Converts litehtml color points to NanoVGX gradient stops.
std::vector<NVGcolorStop> to_stops(const litehtml::background_layer::gradient_base& g)
{
    std::vector<NVGcolorStop> stops;
    stops.reserve(g.color_points.size());
    for(const auto& p : g.color_points)
    {
        NVGcolorStop s{};
        s.offset = p.offset;
        s.color  = to_nvg(p.color);
        s.hint   = p.hint ? *p.hint : 0.0f;
        stops.push_back(s);
    }
    return stops;
}
} // namespace

container_nanovg::container_nanovg(NVGcontext* vg) :
    m_vg(vg)
{
}

container_nanovg::~container_nanovg()
{
    for(auto& it : m_images)
    {
        if(it.second.id >= 0)
        {
            nvgDeleteImage(m_vg, it.second.id);
        }
    }
}

bool container_nanovg::add_font_face(const std::string& family, bool bold, bool italic, const std::string& path,
                                     int index)
{
    std::string name = family + (bold ? "-b" : "") + (italic ? "-i" : "");
    int         id   = nvgCreateFontAtIndex(m_vg, name.c_str(), path.c_str(), index);
    if(id < 0)
    {
        return false;
    }
    // Every face falls back to the first registered face, then to the fallback fonts (glyph coverage).
    if(!m_faces.empty())
    {
        nvgAddFallbackFontId(m_vg, id, m_faces.front().id);
    }
    for(int fb : m_fallbacks)
    {
        nvgAddFallbackFontId(m_vg, id, fb);
    }
    m_faces.push_back({to_lower(family), bold, italic, id});
    return true;
}

bool container_nanovg::add_fallback_font(const std::string& path, int index)
{
    std::string name = "fallback-" + std::to_string(m_fallbacks.size());
    int         id   = nvgCreateFontAtIndex(m_vg, name.c_str(), path.c_str(), index);
    if(id < 0)
    {
        return false;
    }
    for(const auto& face : m_faces)
    {
        nvgAddFallbackFontId(m_vg, face.id, id);
    }
    m_fallbacks.push_back(id);
    return true;
}

void container_nanovg::set_default_font(const std::string& family, float size_px)
{
    m_default_family = family;
    m_default_size   = size_px;
}

void container_nanovg::begin_frame(int width, int height)
{
    set_viewport(width, height);
    m_clips.clear();
}

void container_nanovg::set_viewport(int width, int height)
{
    m_width  = width;
    m_height = height;
}

// ---------------------------------------------------------------------------------------------
// Fonts / text
// ---------------------------------------------------------------------------------------------

int container_nanovg::find_face(const std::string& families, int weight, bool italic, bool& fake_italic) const
{
    bool                     bold = weight >= 600;
    std::vector<std::string> wanted;
    std::stringstream        ss(families);
    std::string              item;
    while(std::getline(ss, item, ','))
    {
        auto f = trim_family(item);
        if(!f.empty())
        {
            wanted.push_back(f);
            wanted.push_back(generic_alias(f));
        }
    }
    wanted.push_back(to_lower(m_default_family));

    for(const auto& fam : wanted)
    {
        const face_t* best       = nullptr;
        int           best_score = -1;
        for(const auto& face : m_faces)
        {
            if(face.family != fam)
            {
                continue;
            }
            int score = (face.bold == bold ? 2 : 0) + (face.italic == italic ? 1 : 0);
            if(score > best_score)
            {
                best_score = score;
                best       = &face;
            }
        }
        if(best)
        {
            fake_italic = italic && !best->italic;
            return best->id;
        }
    }
    fake_italic = italic;
    return m_faces.empty() ? -1 : m_faces.front().id;
}

litehtml::uint_ptr container_nanovg::create_font(const litehtml::font_description& descr,
                                                 const litehtml::document* /*doc*/, litehtml::font_metrics* fm)
{
    auto* f = new font_t();
    f->size = descr.size;
    f->face = find_face(descr.family, descr.weight, descr.style != litehtml::font_style_normal, f->fake_italic);
    f->decoration       = descr.decoration_line;
    f->decoration_color = descr.decoration_color;

    nvgSave(m_vg);
    nvgFontFaceId(m_vg, f->face);
    nvgFontSize(m_vg, f->size);
    nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    float asc = 0, desc = 0, lineh = 0;
    nvgTextMetrics(m_vg, &asc, &desc, &lineh);
    float bounds[4] = {0, 0, 0, 0};
    nvgTextBounds(m_vg, 0, 0, "x", nullptr, bounds);
    float x_height = -bounds[1];
    float ch_width = nvgTextBounds(m_vg, 0, 0, "0", nullptr, nullptr);
    nvgRestore(m_vg);

    f->ascent   = asc;
    f->descent  = -desc;
    f->x_height = x_height;

    if(fm)
    {
        fm->font_size   = f->size;
        fm->ascent      = f->ascent;
        fm->descent     = f->descent;
        fm->height      = f->ascent + f->descent;
        fm->x_height    = x_height;
        fm->ch_width    = ch_width;
        fm->draw_spaces = (f->decoration != litehtml::text_decoration_line_none);
        fm->sub_shift   = f->size / 5;
        fm->super_shift = f->size / 3;
    }
    return reinterpret_cast<litehtml::uint_ptr>(f);
}

void container_nanovg::delete_font(litehtml::uint_ptr hFont)
{
    delete reinterpret_cast<font_t*>(hFont);
}

litehtml::pixel_t container_nanovg::text_width(const char* text, litehtml::uint_ptr hFont)
{
    auto* f = reinterpret_cast<font_t*>(hFont);
    nvgSave(m_vg);
    nvgFontFaceId(m_vg, f->face);
    nvgFontSize(m_vg, f->size);
    nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    float w = nvgTextBounds(m_vg, 0, 0, text, nullptr, nullptr);
    nvgRestore(m_vg);
    return w;
}

void container_nanovg::draw_text(litehtml::uint_ptr /*hdc*/, const char* text, litehtml::uint_ptr hFont,
                                 litehtml::web_color color, const litehtml::position& pos)
{
    auto* f = reinterpret_cast<font_t*>(hFont);
    nvgSave(m_vg);
    apply_clip();

    float x        = pos.x;
    float baseline = pos.bottom() - f->descent;

    nvgFontFaceId(m_vg, f->face);
    nvgFontSize(m_vg, f->size);
    nvgTextAlign(m_vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(m_vg, to_nvg(color));

    float width;
    if(f->fake_italic)
    {
        nvgSave(m_vg);
        nvgTranslate(m_vg, x, baseline);
        nvgSkewX(m_vg, -0.2f);
        width = nvgText(m_vg, 0, 0, text, nullptr);
        nvgRestore(m_vg);
        width += x;
    } else
    {
        width = nvgText(m_vg, x, baseline, text, nullptr);
    }
    width -= x; // nvgText returns the end x position

    if(f->decoration != litehtml::text_decoration_line_none)
    {
        float thick = std::max(1.0f, std::round(f->size / 16.0f));
        auto  dc    = f->decoration_color.is_current_color ? color : f->decoration_color;
        nvgFillColor(m_vg, to_nvg(dc));
        auto line = [&](float y) {
            nvgBeginPath(m_vg);
            nvgRect(m_vg, x, std::round(y), width, thick);
            nvgFill(m_vg);
        };
        if(f->decoration & litehtml::text_decoration_line_underline)
        {
            line(baseline + thick + 1.0f);
        }
        if(f->decoration & litehtml::text_decoration_line_overline)
        {
            line(baseline - f->ascent);
        }
        if(f->decoration & litehtml::text_decoration_line_line_through)
        {
            line(baseline - f->x_height / 2.0f - thick / 2.0f);
        }
    }
    nvgRestore(m_vg);
}

litehtml::pixel_t container_nanovg::pt_to_px(float pt) const
{
    return pt * 96.0f / 72.0f;
}

litehtml::pixel_t container_nanovg::get_default_font_size() const
{
    return m_default_size;
}

const char* container_nanovg::get_default_font_name() const
{
    return m_default_family.c_str();
}

void container_nanovg::transform_text(std::string& text, litehtml::text_transform tt)
{
    // ASCII only; good enough for a demo.
    switch(tt)
    {
    case litehtml::text_transform_uppercase:
        for(auto& c : text)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        break;
    case litehtml::text_transform_lowercase:
        for(auto& c : text)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        break;
    case litehtml::text_transform_capitalize:
        if(!text.empty())
        {
            text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// Clipping helpers
// ---------------------------------------------------------------------------------------------

void container_nanovg::set_clip(const litehtml::position& pos, const litehtml::border_radiuses& /*bdr_radius*/)
{
    // NanoVG scissoring is rectangular, rounded clip radii are ignored.
    m_clips.push_back(pos);
}

void container_nanovg::del_clip()
{
    if(!m_clips.empty())
    {
        m_clips.pop_back();
    }
}

void container_nanovg::apply_clip()
{
    for(const auto& c : m_clips)
    {
        nvgIntersectScissor(m_vg, c.x, c.y, c.width, c.height);
    }
}

void container_nanovg::clip_layer(const litehtml::background_layer& layer)
{
    apply_clip();
    nvgIntersectScissor(m_vg, layer.clip_box.x, layer.clip_box.y, layer.clip_box.width, layer.clip_box.height);
}

void container_nanovg::path_rounded_rect(const litehtml::position& pos, const litehtml::border_radiuses& r)
{
    nvgBeginPath(m_vg);
    nvgRoundedRectVarying(m_vg, pos.x, pos.y, pos.width, pos.height, (r.top_left_x + r.top_left_y) / 2.0f,
                          (r.top_right_x + r.top_right_y) / 2.0f, (r.bottom_right_x + r.bottom_right_y) / 2.0f,
                          (r.bottom_left_x + r.bottom_left_y) / 2.0f);
}

// ---------------------------------------------------------------------------------------------
// Backgrounds
// ---------------------------------------------------------------------------------------------

void container_nanovg::draw_solid_fill(litehtml::uint_ptr /*hdc*/, const litehtml::background_layer& layer,
                                      const litehtml::web_color& color)
{
    if(color.alpha == 0)
    {
        return;
    }
    nvgSave(m_vg);
    if(layer.is_root)
    {
        // The root background paints the whole canvas.
        nvgBeginPath(m_vg);
        nvgRect(m_vg, 0, 0, static_cast<float>(m_width), static_cast<float>(m_height));
    } else
    {
        clip_layer(layer);
        path_rounded_rect(layer.border_box, layer.border_radius);
    }
    nvgFillColor(m_vg, to_nvg(color));
    nvgFill(m_vg);
    nvgRestore(m_vg);
}

// NanoVGX evaluates multi-stop gradients per pixel, so each one is a single paint.
void container_nanovg::fill_gradient(const litehtml::background_layer& layer, NVGpaint paint)
{
    nvgSave(m_vg);
    clip_layer(layer);
    path_rounded_rect(layer.border_box, layer.border_radius);
    nvgFillPaint(m_vg, paint);
    nvgFill(m_vg);
    nvgRestore(m_vg);
}

void container_nanovg::draw_linear_gradient(litehtml::uint_ptr /*hdc*/, const litehtml::background_layer& layer,
                                           const litehtml::background_layer::linear_gradient& gradient)
{
    auto stops = to_stops(gradient);
    fill_gradient(layer, nvgLinearGradientStops(m_vg, gradient.start.x, gradient.start.y, gradient.end.x,
                                                gradient.end.y, stops.data(), static_cast<int>(stops.size()), 0));
}

void container_nanovg::draw_radial_gradient(litehtml::uint_ptr /*hdc*/, const litehtml::background_layer& layer,
                                           const litehtml::background_layer::radial_gradient& gradient)
{
    auto stops = to_stops(gradient);
    fill_gradient(layer, nvgRadialGradientStops(m_vg, gradient.position.x, gradient.position.y, gradient.radius.x,
                                                gradient.radius.y, stops.data(), static_cast<int>(stops.size()), 0));
}

void container_nanovg::draw_conic_gradient(litehtml::uint_ptr /*hdc*/, const litehtml::background_layer& layer,
                                          const litehtml::background_layer::conic_gradient& gradient)
{
    // CSS measures the angle from the top, NanoVGX from the +x axis.
    auto  stops = to_stops(gradient);
    float angle = nvgDegToRad(gradient.angle) - NVG_PI / 2.0f;
    fill_gradient(layer, nvgConicGradient(m_vg, gradient.position.x, gradient.position.y, angle, stops.data(),
                                          static_cast<int>(stops.size()), 0));
}

// ---------------------------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------------------------

std::string container_nanovg::resolve_url(const char* src, const char* baseurl) const
{
    std::string s = src ? src : "";
    if(s.rfind("file://", 0) == 0)
    {
        return s.substr(7);
    }
    if(!s.empty() && s[0] == '/')
    {
        return s;
    }
    std::string base = (baseurl && *baseurl) ? baseurl : m_base_dir;
    if(base.rfind("file://", 0) == 0)
    {
        base = base.substr(7);
    }
    // If base points to a file, use its directory.
    if(!base.empty() && base.back() != '/' && file_exists(base) && base.find('.') != std::string::npos)
    {
        auto slash = base.find_last_of('/');
        base       = slash == std::string::npos ? "." : base.substr(0, slash);
    }
    if(!base.empty() && base.back() != '/')
    {
        base += '/';
    }
    return base + s;
}

container_nanovg::image_t* container_nanovg::get_image(const std::string& path)
{
    auto it = m_images.find(path);
    if(it != m_images.end())
    {
        return it->second.id >= 0 ? &it->second : nullptr;
    }
    image_t img;
    if(path.rfind("http://", 0) != 0 && path.rfind("https://", 0) != 0)
    {
        img.id = nvgCreateImage(m_vg, path.c_str(), NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
        if(img.id > 0)
        {
            nvgImageSize(m_vg, img.id, &img.w, &img.h);
        } else
        {
            img.id = -1;
            fprintf(stderr, "litehtml-nanovg: cannot load image '%s'\n", path.c_str());
        }
    }
    auto& slot = m_images[path] = img;
    return slot.id >= 0 ? &slot : nullptr;
}

void container_nanovg::load_image(const char* src, const char* baseurl, bool /*redraw_on_ready*/)
{
    get_image(resolve_url(src, baseurl));
}

void container_nanovg::get_image_size(const char* src, const char* baseurl, litehtml::size& sz)
{
    auto* img = get_image(resolve_url(src, baseurl));
    sz.width  = img ? img->w : 0;
    sz.height = img ? img->h : 0;
}

void container_nanovg::draw_image(litehtml::uint_ptr /*hdc*/, const litehtml::background_layer& layer,
                                  const std::string& url, const std::string& base_url)
{
    auto* img = get_image(resolve_url(url.c_str(), base_url.c_str()));
    if(!img)
    {
        return;
    }
    const auto& o = layer.origin_box;
    const auto& c = layer.clip_box;

    nvgSave(m_vg);
    clip_layer(layer);
    // Restrict to the area the image is allowed to repeat in.
    switch(layer.repeat)
    {
    case litehtml::background_repeat_no_repeat:
        nvgIntersectScissor(m_vg, o.x, o.y, o.width, o.height);
        break;
    case litehtml::background_repeat_repeat_x:
        nvgIntersectScissor(m_vg, c.x, o.y, c.width, o.height);
        break;
    case litehtml::background_repeat_repeat_y:
        nvgIntersectScissor(m_vg, o.x, c.y, o.width, c.height);
        break;
    default:
        break;
    }
    path_rounded_rect(layer.border_box, layer.border_radius);
    nvgFillPaint(m_vg, nvgImagePattern(m_vg, o.x, o.y, o.width, o.height, 0.0f, img->id, 1.0f));
    nvgFill(m_vg);
    nvgRestore(m_vg);
}

void container_nanovg::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
    const auto& p = marker.pos;
    if(!marker.image.empty())
    {
        if(auto* img = get_image(resolve_url(marker.image.c_str(), marker.baseurl)))
        {
            nvgSave(m_vg);
            apply_clip();
            nvgBeginPath(m_vg);
            nvgRect(m_vg, p.x, p.y, static_cast<float>(img->w), static_cast<float>(img->h));
            nvgFillPaint(m_vg, nvgImagePattern(m_vg, p.x, p.y, static_cast<float>(img->w),
                                               static_cast<float>(img->h), 0.0f, img->id, 1.0f));
            nvgFill(m_vg);
            nvgRestore(m_vg);
        }
        return;
    }
    (void)hdc;
    nvgSave(m_vg);
    apply_clip();
    nvgBeginPath(m_vg);
    switch(marker.marker_type)
    {
    case litehtml::list_style_type_circle:
        nvgEllipse(m_vg, p.x + p.width / 2.0f, p.y + p.height / 2.0f, p.width / 2.0f - 0.5f, p.height / 2.0f - 0.5f);
        nvgStrokeColor(m_vg, to_nvg(marker.color));
        nvgStrokeWidth(m_vg, 1.0f);
        nvgStroke(m_vg);
        break;
    case litehtml::list_style_type_disc:
        nvgEllipse(m_vg, p.x + p.width / 2.0f, p.y + p.height / 2.0f, p.width / 2.0f, p.height / 2.0f);
        nvgFillColor(m_vg, to_nvg(marker.color));
        nvgFill(m_vg);
        break;
    case litehtml::list_style_type_square:
        nvgRect(m_vg, p.x, p.y, p.width, p.height);
        nvgFillColor(m_vg, to_nvg(marker.color));
        nvgFill(m_vg);
        break;
    default:
        break; // numbered markers are emitted as text by litehtml
    }
    nvgRestore(m_vg);
}

// ---------------------------------------------------------------------------------------------
// Borders
// ---------------------------------------------------------------------------------------------

void container_nanovg::draw_borders(litehtml::uint_ptr /*hdc*/, const litehtml::borders& borders,
                                   const litehtml::position& draw_pos, bool /*root*/)
{
    const auto& L = borders.left;
    const auto& T = borders.top;
    const auto& R = borders.right;
    const auto& B = borders.bottom;
    auto visible  = [](const litehtml::border& b) {
        return b.width > litehtml::pixel_t(0) && b.style != litehtml::border_style_none && b.style != litehtml::border_style_hidden &&
               b.color.alpha > 0;
    };

    nvgSave(m_vg);
    apply_clip();

    const auto& r          = borders.radius;
    bool        has_radius = r.top_left_x > litehtml::pixel_t(0) || r.top_right_x > litehtml::pixel_t(0) || r.bottom_right_x > litehtml::pixel_t(0) ||
                      r.bottom_left_x > litehtml::pixel_t(0);
    bool uniform = visible(T) && L.width == T.width && R.width == T.width && B.width == T.width &&
                   L.color == T.color && R.color == T.color && B.color == T.color && L.style == T.style &&
                   R.style == T.style && B.style == T.style;

    if(uniform && has_radius)
    {
        // Ring between outer and inner rounded rect (even-odd via hole winding).
        float w = T.width;
        auto  p = draw_pos;
        nvgBeginPath(m_vg);
        nvgRoundedRectVarying(m_vg, p.x, p.y, p.width, p.height, (r.top_left_x + r.top_left_y) / 2.0f,
                              (r.top_right_x + r.top_right_y) / 2.0f, (r.bottom_right_x + r.bottom_right_y) / 2.0f,
                              (r.bottom_left_x + r.bottom_left_y) / 2.0f);
        auto in = [w](float v) { return std::max(0.0f, v - w); };
        nvgRoundedRectVarying(m_vg, p.x + w, p.y + w, p.width - 2 * w, p.height - 2 * w,
                              in((r.top_left_x + r.top_left_y) / 2.0f), in((r.top_right_x + r.top_right_y) / 2.0f),
                              in((r.bottom_right_x + r.bottom_right_y) / 2.0f),
                              in((r.bottom_left_x + r.bottom_left_y) / 2.0f));
        nvgPathWinding(m_vg, NVG_HOLE);
        nvgFillColor(m_vg, to_nvg(T.color));
        nvgFill(m_vg);
        nvgRestore(m_vg);
        return;
    }

    float x0 = draw_pos.left(), y0 = draw_pos.top(), x1 = draw_pos.right(), y1 = draw_pos.bottom();
    float lw = visible(L) ? static_cast<float>(L.width) : 0.0f;
    float tw = visible(T) ? static_cast<float>(T.width) : 0.0f;
    float rw = visible(R) ? static_cast<float>(R.width) : 0.0f;
    float bw = visible(B) ? static_cast<float>(B.width) : 0.0f;

    // Each side is a trapezoid with mitred corners. (ax,ay)-(bx,by) is the outer edge,
    // (cx,cy)-(dx,dy) the inner one.
    auto side = [&](const litehtml::border& b, float w, float ax, float ay, float bx, float by, float cx, float cy,
                    float dx, float dy) {
        if(w <= 0.0f)
        {
            return;
        }
        auto col = b.color;
        if(b.style == litehtml::border_style_inset || b.style == litehtml::border_style_outset ||
           b.style == litehtml::border_style_groove || b.style == litehtml::border_style_ridge)
        {
            // Darken top/left for inset/groove, bottom/right for outset/ridge.
            bool tl   = (&b == &T || &b == &L);
            bool dark = (b.style == litehtml::border_style_inset || b.style == litehtml::border_style_groove) ? tl : !tl;
            if(dark)
            {
                col.red   = static_cast<litehtml::byte>(col.red * 0.6);
                col.green = static_cast<litehtml::byte>(col.green * 0.6);
                col.blue  = static_cast<litehtml::byte>(col.blue * 0.6);
            }
        }
        nvgFillColor(m_vg, to_nvg(col));

        if(b.style == litehtml::border_style_dotted || b.style == litehtml::border_style_dashed)
        {
            // Dashes along the centre line of the side.
            float mx0 = (ax + dx) / 2.0f, my0 = (ay + dy) / 2.0f;
            float mx1 = (bx + cx) / 2.0f, my1 = (by + cy) / 2.0f;
            float len = std::hypot(mx1 - mx0, my1 - my0);
            if(len <= 0.0f)
            {
                return;
            }
            float ux = (mx1 - mx0) / len, uy = (my1 - my0) / len;
            bool  dotted = b.style == litehtml::border_style_dotted;
            float dash   = dotted ? w : w * 3.0f;
            float period = dash * 2.0f;
            int   count  = std::max(1, static_cast<int>(std::round(len / period)));
            float step   = len / count;
            nvgBeginPath(m_vg);
            for(int i = 0; i < count; i++)
            {
                float s  = i * step + (step - dash) / 2.0f;
                float px = mx0 + ux * s, py = my0 + uy * s;
                if(dotted)
                {
                    nvgCircle(m_vg, px + ux * dash / 2.0f, py + uy * dash / 2.0f, w / 2.0f);
                } else
                {
                    float nx = -uy * w / 2.0f, ny = ux * w / 2.0f;
                    nvgMoveTo(m_vg, px + nx, py + ny);
                    nvgLineTo(m_vg, px + ux * dash + nx, py + uy * dash + ny);
                    nvgLineTo(m_vg, px + ux * dash - nx, py + uy * dash - ny);
                    nvgLineTo(m_vg, px - nx, py - ny);
                    nvgClosePath(m_vg);
                }
            }
            nvgFill(m_vg);
            return;
        }

        if(b.style == litehtml::border_style_double && w >= 3.0f)
        {
            // Two strips, each a third of the width.
            auto lerp = [](float a, float c, float t) { return a + (c - a) * t; };
            for(float t0 : {0.0f, 2.0f / 3.0f})
            {
                float t1 = t0 + 1.0f / 3.0f;
                nvgBeginPath(m_vg);
                nvgMoveTo(m_vg, lerp(ax, dx, t0), lerp(ay, dy, t0));
                nvgLineTo(m_vg, lerp(bx, cx, t0), lerp(by, cy, t0));
                nvgLineTo(m_vg, lerp(bx, cx, t1), lerp(by, cy, t1));
                nvgLineTo(m_vg, lerp(ax, dx, t1), lerp(ay, dy, t1));
                nvgClosePath(m_vg);
                nvgFill(m_vg);
            }
            return;
        }

        nvgBeginPath(m_vg);
        nvgMoveTo(m_vg, ax, ay);
        nvgLineTo(m_vg, bx, by);
        nvgLineTo(m_vg, cx, cy);
        nvgLineTo(m_vg, dx, dy);
        nvgClosePath(m_vg);
        nvgFill(m_vg);
    };

    side(T, tw, x0, y0, x1, y0, x1 - rw, y0 + tw, x0 + lw, y0 + tw);
    side(R, rw, x1, y0, x1, y1, x1 - rw, y1 - bw, x1 - rw, y0 + tw);
    side(B, bw, x1, y1, x0, y1, x0 + lw, y1 - bw, x1 - rw, y1 - bw);
    side(L, lw, x0, y1, x0, y0, x0 + lw, y0 + tw, x0 + lw, y1 - bw);

    nvgRestore(m_vg);
}

// ---------------------------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------------------------

void container_nanovg::set_caption(const char* caption)
{
    if(on_caption)
    {
        on_caption(caption ? caption : "");
    }
}

void container_nanovg::set_base_url(const char* base_url)
{
    if(base_url && *base_url)
    {
        m_base_dir = base_url;
    }
}

void container_nanovg::link(const std::shared_ptr<litehtml::document>& /*doc*/,
                            const litehtml::element::ptr& /*el*/)
{
}

void container_nanovg::on_anchor_click(const char* url, const litehtml::element::ptr& /*el*/)
{
    if(on_link)
    {
        on_link(url ? url : "");
    }
}

void container_nanovg::on_mouse_event(const litehtml::element::ptr& /*el*/, litehtml::mouse_event /*event*/)
{
}

void container_nanovg::set_cursor(const char* cursor)
{
    m_cursor = cursor ? cursor : "auto";
}

void container_nanovg::import_css(std::string& text, const std::string& url, std::string& baseurl)
{
    std::string   path = resolve_url(url.c_str(), baseurl.c_str());
    std::ifstream f(path, std::ios::binary);
    if(!f)
    {
        fprintf(stderr, "litehtml-nanovg: cannot load stylesheet '%s'\n", path.c_str());
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    text    = ss.str();
    baseurl = path;
}

void container_nanovg::get_viewport(litehtml::position& viewport) const
{
    viewport = litehtml::position(0, 0, m_width, m_height);
}

litehtml::element::ptr container_nanovg::create_element(const char* /*tag_name*/,
                                                        const litehtml::string_map& /*attributes*/,
                                                        const std::shared_ptr<litehtml::document>& /*doc*/)
{
    return nullptr;
}

void container_nanovg::get_media_features(litehtml::media_features& media) const
{
    media.type          = litehtml::media_type_screen;
    media.width         = m_width;
    media.height        = m_height;
    media.device_width  = m_width;
    media.device_height = m_height;
    media.color         = 8;
    media.color_index   = 0;
    media.monochrome    = 0;
    media.resolution    = 96;
}

void container_nanovg::get_language(std::string& language, std::string& culture) const
{
    language = "en";
    culture  = "";
}

std::string container_nanovg::resolve_color(const std::string& color) const
{
    // Named colors missing from litehtml's built-in table.
    if(to_lower(color) == "indigo")
    {
        return "#4b0082";
    }
    return {};
}
