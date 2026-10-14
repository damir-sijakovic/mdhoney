// mdhoney: a Markdown reader. md4c -> HTML -> litehtml layout -> NanoVGX -> OpenGL ES 3.
//
//   mdhoney [file.md[#heading]] [--dark] [--grayscale] [--serif] [--zoom F] [--find TEXT]
//           [--size WxH] [--scroll PX] [--png out.png]
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

#define GLFW_INCLUDE_ES3
#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>
#include "nanovgx.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovgx_gl.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "container_nanovg.h"
#include "fonts.h"
#include "markdown.h"
#include "textindex.h"
#include "theme.h"

extern char** environ;

namespace
{
const char* WELCOME_MD = R"(# Welcome to mdhoney

A small, fast **Markdown reader**: [md4c](https://github.com/mity/md4c) turns Markdown into HTML,
**litehtml** lays it out and **NanoVGX** draws it with OpenGL ES 3.

Drop a `.md` file on this window, or start it with `mdhoney file.md`.

## Keys

| Key | Action |
|---|---|
| <kbd>Space</kbd> <kbd>PgDn</kbd> / <kbd>Shift+Space</kbd> <kbd>PgUp</kbd> | Page down / up |
| <kbd>↓</kbd> <kbd>j</kbd> / <kbd>↑</kbd> <kbd>k</kbd> | Scroll |
| <kbd>Home</kbd> <kbd>g</kbd> / <kbd>End</kbd> <kbd>G</kbd> | Top / bottom |
| <kbd>Backspace</kbd> <kbd>Alt+←</kbd> / <kbd>Alt+→</kbd> | Back / forward |
| <kbd>Ctrl+F</kbd>, then <kbd>Enter</kbd> / <kbd>Shift+Enter</kbd> | Find in page, next / previous |
| <kbd>Ctrl+C</kbd> / <kbd>Ctrl+A</kbd> | Copy selection / select all |
| <kbd>Ctrl +</kbd> / <kbd>Ctrl −</kbd> / <kbd>Ctrl 0</kbd> | Zoom in / out / reset (or Ctrl+wheel) |
| Right click | Menu: light, dark, grayscale |
| <kbd>t</kbd> | Light / dark theme |
| <kbd>f</kbd> | Sans / serif body font |
| <kbd>r</kbd> | Reload (files also reload automatically when they change) |
| <kbd>Esc</kbd> | Close the menu or find bar, clear the selection, or quit |
| <kbd>q</kbd> | Quit |

Drag to select text, double click selects a word, triple click a paragraph.

Links to other Markdown files open in mdhoney, `#heading` links jump within the page and web
links open in your browser.

- [x] Tables, task lists, ~~strikethrough~~, autolinks (GitHub flavour)
- [x] Live reload while you edit
- [x] Text selection, copy and find in page
)";

struct history_entry
{
    std::string path;
    float       scroll;
};

// Popup menu entries.
enum menu_entry
{
    MENU_LIGHT,
    MENU_DARK,
    MENU_SEPARATOR,
    MENU_GRAYSCALE,
    MENU_COUNT
};

struct app_t
{
    GLFWwindow*             window    = nullptr;
    NVGcontext*             vg        = nullptr;
    container_nanovg*       container = nullptr;
    litehtml::document::ptr doc;
    text_index              index;

    std::string path;     // current file, empty for the welcome page
    std::string markdown; // its source
    md_result   page;     // converted HTML
    long long   mtime = 0;

    bool  dark      = false;
    bool  grayscale = false;
    bool  serif     = false;
    float zoom      = 1.0f;
    float ui_scale  = 1.0f; // monitor content scale

    int    win_w = 0, win_h = 0;
    int    layout_w = -1;
    float  scroll = 0, target = 0;
    double mouse_x = 0, mouse_y = 0;
    bool   dragging_bar = false;
    float  drag_offset  = 0;
    bool   hover_bar    = false;
    bool   dirty        = true;

    // Selection
    text_pos sel_anchor, sel_focus;
    bool     selecting  = false; // left button held in the page
    bool     drag_moved = false; // ... and moved, so it is a selection drag, not a click
    double   press_x = 0, press_y = 0;
    double   last_click_time = -1e9;
    int      click_count     = 0;

    // Find in page
    bool                    find_open = false;
    std::string             find_query;
    std::vector<text_range> matches;
    int                     current_match = -1;

    // Popup menu
    bool  menu_open = false;
    float menu_x = 0, menu_y = 0;
    int   menu_hover = -1;

    std::string                pending_link;
    std::vector<history_entry> back, forward;
    GLFWcursor*                hand  = nullptr;
    GLFWcursor*                ibeam = nullptr;
};

const theme_t& theme(const app_t& app)
{
    return app.dark ? dark_theme() : light_theme();
}

NVGcolor hex_color(const char* hex, unsigned char alpha = 255)
{
    unsigned v = 0;
    sscanf(hex + 1, "%x", &v);
    return nvgRGBA((v >> 16) & 255, (v >> 8) & 255, v & 255, alpha);
}

long long file_mtime(const std::string& path)
{
    struct stat st;
    if(path.empty() || stat(path.c_str(), &st) != 0)
    {
        return 0;
    }
    return static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
}

bool read_file(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
    {
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string dir_of(const std::string& path)
{
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
}

std::string base_name(const std::string& path)
{
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string percent_decode(const std::string& s)
{
    std::string out;
    for(size_t i = 0; i < s.size(); i++)
    {
        if(s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
           std::isxdigit(static_cast<unsigned char>(s[i + 2])))
        {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else
        {
            out += s[i];
        }
    }
    return out;
}

bool is_markdown(const std::string& path)
{
    auto dot = path.find_last_of('.');
    if(dot == std::string::npos)
    {
        return false;
    }
    std::string ext = path.substr(dot + 1);
    for(auto& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == "md" || ext == "markdown" || ext == "mdown" || ext == "mkd" || ext == "txt";
}

// Opens a URL or file with the desktop's default application, without a shell.
void open_external(const std::string& target)
{
    const char* argv[] = {"xdg-open", target.c_str(), nullptr};
    pid_t       pid;
    if(posix_spawnp(&pid, "xdg-open", nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0)
    {
        fprintf(stderr, "mdhoney: cannot run xdg-open for %s\n", target.c_str());
    }
}

float font_px(const app_t& app)
{
    return 17.0f * app.zoom * app.ui_scale;
}

float scroll_px(const app_t& app)
{
    return std::round(app.scroll);
}

// --- document ----------------------------------------------------------------------------------

// Rebuilds the litehtml document from the converted page (theme, zoom or font changes).
void build_document(app_t& app)
{
    std::string html = "<!DOCTYPE html><html><head><style>" + theme_css(theme(app), font_px(app), app.serif) +
                       "</style></head><body><article>" + app.page.html + "</article></body></html>";
    app.container->set_base_dir(app.path.empty() ? "." : dir_of(app.path));
    app.index.clear();
    app.doc      = litehtml::document::createFromString(html, app.container);
    app.layout_w = -1;
    app.dirty    = true;
}

void update_title(app_t& app)
{
    std::string name = !app.page.title.empty() ? app.page.title : (app.path.empty() ? "Welcome" : base_name(app.path));
    glfwSetWindowTitle(app.window, (name + " \xE2\x80\x94 mdhoney").c_str());
}

void clear_selection(app_t& app)
{
    app.sel_anchor = app.sel_focus = text_pos{};
    app.dirty                      = true;
}

bool has_selection(const app_t& app)
{
    return app.sel_anchor.valid() && app.sel_focus.valid() && app.sel_anchor != app.sel_focus;
}

// Loads a file (or the welcome page for an empty path) and converts it.
void load(app_t& app, const std::string& path)
{
    app.path = path;
    if(path.empty())
    {
        app.markdown = WELCOME_MD;
    } else if(!read_file(path, app.markdown))
    {
        app.markdown = "# Can't open file\n\n`" + path + "`\n\nPress <kbd>Backspace</kbd> to go back.";
    }
    app.mtime = file_mtime(path);
    app.page  = markdown_to_html(app.markdown);
    clear_selection(app); // the text changed, old positions mean nothing
    app.current_match = -1;
    build_document(app);
    update_title(app);
}

float doc_height(const app_t& app)
{
    return app.doc ? static_cast<float>(app.doc->height()) : 0.0f;
}

float max_scroll(const app_t& app)
{
    return std::max(0.0f, doc_height(app) - app.win_h);
}

void scroll_to(app_t& app, float y, bool smooth = true)
{
    app.target = std::clamp(y, 0.0f, max_scroll(app));
    if(!smooth)
    {
        app.scroll = app.target;
    }
    app.dirty = true;
}

// Scrolls so that a range is comfortably inside the window.
void reveal(app_t& app, const text_range& range, bool smooth)
{
    std::vector<litehtml::position> rects;
    app.index.rects(range.first, range.second, rects);
    if(rects.empty())
    {
        return;
    }
    float top = rects.front().y, bottom = rects.back().y + rects.back().height;
    float margin = 60.0f * app.ui_scale;
    if(top < app.target + margin || bottom > app.target + app.win_h - margin)
    {
        scroll_to(app, top - app.win_h / 3.0f, smooth);
    }
}

void run_find(app_t& app, bool pick_visible)
{
    app.matches = app.index.find(app.find_query);
    if(app.matches.empty())
    {
        app.current_match = -1;
    } else if(pick_visible || app.current_match < 0 || app.current_match >= static_cast<int>(app.matches.size()))
    {
        // The first match at or below the top of the window.
        app.current_match = 0;
        for(int i = 0; i < static_cast<int>(app.matches.size()); i++)
        {
            std::vector<litehtml::position> r;
            app.index.rects(app.matches[i].first, app.matches[i].second, r);
            if(!r.empty() && static_cast<float>(r.front().y) >= app.target)
            {
                app.current_match = i;
                break;
            }
        }
    }
    app.dirty = true;
}

void ensure_layout(app_t& app)
{
    if(app.doc && app.layout_w != app.win_w && app.win_w > 0)
    {
        app.container->set_viewport(app.win_w, app.win_h);
        app.doc->media_changed();
        app.doc->render(app.win_w);
        app.layout_w = app.win_w;
        app.dirty    = true;
        // Positions changed: rebuild the text index. Run order is stable for the same page, so a
        // selection survives theme, zoom and window size changes.
        app.index.build(app.doc, app.container);
        if(app.sel_anchor.run >= app.index.run_count() || app.sel_focus.run >= app.index.run_count())
        {
            clear_selection(app);
        }
        if(app.find_open && !app.find_query.empty())
        {
            run_find(app, false);
        }
    }
}

litehtml::element::ptr find_id(const litehtml::element::ptr& el, const std::string& id)
{
    const char* v = el->get_attr("id");
    if(!v)
    {
        v = el->get_attr("name");
    }
    if(v && id == v)
    {
        return el;
    }
    for(const auto& child : el->children())
    {
        if(auto found = find_id(child, id))
        {
            return found;
        }
    }
    return nullptr;
}

bool scroll_to_anchor(app_t& app, const std::string& anchor, bool smooth)
{
    ensure_layout(app);
    if(!app.doc || anchor.empty())
    {
        return false;
    }
    auto el = find_id(app.doc->root(), percent_decode(anchor));
    if(!el)
    {
        el = find_id(app.doc->root(), slugify(percent_decode(anchor)));
    }
    if(!el)
    {
        return false;
    }
    scroll_to(app, static_cast<float>(el->get_placement().y) - 12.0f * app.ui_scale, smooth);
    return true;
}

// Rebuilds the page and keeps the reading position (as a fraction of the page height).
void rebuild_keep_position(app_t& app)
{
    float ratio = doc_height(app) > 0 ? app.scroll / doc_height(app) : 0.0f;
    build_document(app);
    ensure_layout(app);
    scroll_to(app, ratio * doc_height(app), false);
}

void navigate(app_t& app, const std::string& path, const std::string& anchor)
{
    app.back.push_back({app.path, app.scroll});
    app.forward.clear();
    load(app, path);
    ensure_layout(app);
    if(!scroll_to_anchor(app, anchor, false))
    {
        scroll_to(app, 0, false);
    }
}

void go_history(app_t& app, bool backward)
{
    auto& from = backward ? app.back : app.forward;
    auto& to   = backward ? app.forward : app.back;
    if(from.empty())
    {
        return;
    }
    history_entry e = from.back();
    from.pop_back();
    to.push_back({app.path, app.scroll});
    if(e.path != app.path)
    {
        load(app, e.path);
    }
    ensure_layout(app);
    scroll_to(app, e.scroll, false);
}

void handle_link(app_t& app, const std::string& url)
{
    if(url.empty())
    {
        return;
    }
    if(url[0] == '#')
    {
        app.back.push_back({app.path, app.scroll});
        app.forward.clear();
        scroll_to_anchor(app, url.substr(1), true);
        return;
    }
    if(url.find("://") != std::string::npos || url.rfind("mailto:", 0) == 0)
    {
        if(url.rfind("file://", 0) != 0)
        {
            open_external(url);
            return;
        }
    }

    std::string target = url.rfind("file://", 0) == 0 ? url.substr(7) : url;
    std::string anchor;
    auto        hash = target.find('#');
    if(hash != std::string::npos)
    {
        anchor = target.substr(hash + 1);
        target = target.substr(0, hash);
    }
    target = percent_decode(target);
    if(!target.empty() && target[0] != '/')
    {
        target = (app.path.empty() ? std::string(".") : dir_of(app.path)) + "/" + target;
    }
    if(is_markdown(target))
    {
        navigate(app, target, anchor);
    } else
    {
        open_external(target);
    }
}

void set_zoom(app_t& app, float zoom)
{
    zoom = std::clamp(zoom, 0.5f, 3.0f);
    if(std::fabs(zoom - app.zoom) > 0.001f)
    {
        app.zoom = zoom;
        rebuild_keep_position(app);
    }
}

void set_dark(app_t& app, bool dark)
{
    if(app.dark != dark)
    {
        app.dark = dark;
        rebuild_keep_position(app);
    }
}

void set_grayscale(app_t& app, bool on)
{
    app.grayscale = on;
    nvglSetGrayscale(app.vg, on);
    app.dirty = true;
}

// --- selection and copy ------------------------------------------------------------------------

void copy_selection(app_t& app)
{
    if(has_selection(app))
    {
        glfwSetClipboardString(app.window, app.index.text(app.sel_anchor, app.sel_focus).c_str());
    }
}

text_pos hit_document(const app_t& app, double x, double y)
{
    return app.index.hit(static_cast<float>(x), static_cast<float>(y) + scroll_px(app));
}

// --- find in page ------------------------------------------------------------------------------

void open_find(app_t& app)
{
    app.find_open = true;
    // Like browsers: a short single-line selection becomes the query.
    if(has_selection(app))
    {
        std::string s = app.index.text(app.sel_anchor, app.sel_focus);
        if(!s.empty() && s.size() < 100 && s.find('\n') == std::string::npos)
        {
            app.find_query = s;
        }
    }
    run_find(app, true);
    if(app.current_match >= 0)
    {
        reveal(app, app.matches[app.current_match], true);
    }
}

void close_find(app_t& app)
{
    app.find_open = false;
    app.matches.clear();
    app.current_match = -1;
    app.dirty         = true;
}

void find_step(app_t& app, int dir)
{
    if(app.matches.empty())
    {
        return;
    }
    int n             = static_cast<int>(app.matches.size());
    app.current_match = ((app.current_match < 0 ? 0 : app.current_match + dir) % n + n) % n;
    reveal(app, app.matches[app.current_match], true);
    app.dirty = true;
}

void find_query_changed(app_t& app)
{
    run_find(app, true);
    if(app.current_match >= 0)
    {
        reveal(app, app.matches[app.current_match], true);
    }
}

void append_utf8(std::string& s, unsigned cp)
{
    if(cp < 0x80)
    {
        s += static_cast<char>(cp);
    } else if(cp < 0x800)
    {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if(cp < 0x10000)
    {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else
    {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void pop_utf8(std::string& s)
{
    while(!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)
    {
        s.pop_back();
    }
    if(!s.empty())
    {
        s.pop_back();
    }
}

struct rect_t
{
    float x, y, w, h;
    bool  contains(double px, double py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

rect_t find_bar_rect(const app_t& app)
{
    float s = app.ui_scale;
    float w = std::min(360.0f * s, app.win_w - 32.0f * s);
    return {app.win_w - w - 20.0f * s, 12.0f * s, w, 38.0f * s};
}

// --- popup menu --------------------------------------------------------------------------------

rect_t menu_rect(const app_t& app)
{
    float s = app.ui_scale;
    float w = 176.0f * s;
    float h = 6.0f * s * 2 + 30.0f * s * 3 + 9.0f * s;
    float x = std::clamp(app.menu_x, 4.0f * s, std::max(4.0f * s, app.win_w - w - 4.0f * s));
    float y = std::clamp(app.menu_y, 4.0f * s, std::max(4.0f * s, app.win_h - h - 4.0f * s));
    return {x, y, w, h};
}

rect_t menu_item_rect(const app_t& app, int item)
{
    float  s = app.ui_scale;
    rect_t m = menu_rect(app);
    float  y = m.y + 6.0f * s;
    for(int i = 0; i < item; i++)
    {
        y += i == MENU_SEPARATOR ? 9.0f * s : 30.0f * s;
    }
    return {m.x + 6.0f * s, y, m.w - 12.0f * s, item == MENU_SEPARATOR ? 9.0f * s : 30.0f * s};
}

int menu_item_at(const app_t& app, double x, double y)
{
    for(int i = 0; i < MENU_COUNT; i++)
    {
        if(i != MENU_SEPARATOR && menu_item_rect(app, i).contains(x, y))
        {
            return i;
        }
    }
    return -1;
}

void open_menu(app_t& app, float x, float y)
{
    app.menu_open  = true;
    app.menu_x     = x;
    app.menu_y     = y;
    app.menu_hover = menu_item_at(app, x, y);
    app.dirty      = true;
}

void close_menu(app_t& app)
{
    app.menu_open  = false;
    app.menu_hover = -1;
    app.dirty      = true;
}

void activate_menu(app_t& app, int item)
{
    close_menu(app);
    switch(item)
    {
    case MENU_LIGHT:
        set_dark(app, false);
        break;
    case MENU_DARK:
        set_dark(app, true);
        break;
    case MENU_GRAYSCALE:
        set_grayscale(app, !app.grayscale);
        break;
    default:
        break;
    }
}

// --- drawing -----------------------------------------------------------------------------------

struct ui_colors
{
    NVGcolor panel, border, text, muted, accent, hover, selection, match, current;
};

ui_colors colors(const app_t& app)
{
    const auto& t = theme(app);
    ui_colors   c;
    c.panel     = app.dark ? nvgRGBA(44, 40, 35, 250) : nvgRGBA(255, 253, 248, 250);
    c.border    = app.dark ? nvgRGBA(255, 255, 255, 30) : nvgRGBA(60, 40, 10, 40);
    c.text      = hex_color(t.text);
    c.muted     = hex_color(t.muted);
    c.accent    = hex_color(t.link);
    c.hover     = hex_color(t.link, 40);
    c.selection = app.dark ? nvgRGBA(240, 180, 80, 80) : nvgRGBA(240, 160, 30, 85);
    c.match     = app.dark ? nvgRGBA(255, 220, 80, 55) : nvgRGBA(255, 214, 0, 95);
    c.current   = app.dark ? nvgRGBA(255, 150, 40, 130) : nvgRGBA(255, 140, 0, 130);
    return c;
}

void fill_ranges(app_t& app, const text_pos& a, const text_pos& b, NVGcolor color)
{
    std::vector<litehtml::position> rects;
    app.index.rects(a, b, rects);
    if(rects.empty())
    {
        return;
    }
    float top = scroll_px(app);
    nvgBeginPath(app.vg);
    for(const auto& r : rects)
    {
        float x = r.x, y = static_cast<float>(r.y) - top, w = r.width, h = r.height;
        if(y > app.win_h || y + h < 0)
        {
            continue;
        }
        nvgRect(app.vg, x, y, w, h);
    }
    nvgFillColor(app.vg, color);
    nvgFill(app.vg);
}

void draw_highlights(app_t& app)
{
    auto c = colors(app);
    if(app.find_open)
    {
        for(int i = 0; i < static_cast<int>(app.matches.size()); i++)
        {
            fill_ranges(app, app.matches[i].first, app.matches[i].second, i == app.current_match ? c.current : c.match);
        }
    }
    if(has_selection(app))
    {
        fill_ranges(app, app.sel_anchor, app.sel_focus, c.selection);
    }
}

struct bar_geom
{
    bool  visible;
    float x, w, thumb_y, thumb_h;
};

bar_geom scrollbar(const app_t& app)
{
    bar_geom g{};
    float    h = doc_height(app);
    if(h <= app.win_h + 1)
    {
        return g;
    }
    g.visible = true;
    g.w       = 6.0f * app.ui_scale;
    g.x       = app.win_w - g.w - 4.0f * app.ui_scale;
    g.thumb_h = std::max(36.0f * app.ui_scale, app.win_h * app.win_h / h);
    g.thumb_y = max_scroll(app) > 0 ? app.scroll / max_scroll(app) * (app.win_h - g.thumb_h) : 0.0f;
    return g;
}

void draw_scrollbar(app_t& app)
{
    auto g = scrollbar(app);
    if(!g.visible)
    {
        return;
    }
    const auto& c     = theme(app).scrollbar;
    int         alpha = (app.dragging_bar || app.hover_bar) ? 170 : 80;
    nvgBeginPath(app.vg);
    nvgRoundedRect(app.vg, g.x, g.thumb_y + 2, g.w, g.thumb_h - 4, g.w / 2);
    nvgFillColor(app.vg, nvgRGBA(c[0], c[1], c[2], static_cast<unsigned char>(alpha)));
    nvgFill(app.vg);
}

void draw_panel(app_t& app, const rect_t& r, float radius)
{
    auto  c = colors(app);
    float s = app.ui_scale;
    // Soft shadow
    nvgBeginPath(app.vg);
    nvgRect(app.vg, r.x - 20 * s, r.y - 20 * s, r.w + 40 * s, r.h + 40 * s);
    nvgFillPaint(app.vg, nvgBoxGradient(app.vg, r.x, r.y + 3 * s, r.w, r.h, radius, 14 * s,
                                        nvgRGBA(0, 0, 0, app.dark ? 120 : 50), nvgRGBA(0, 0, 0, 0)));
    nvgFill(app.vg);
    nvgBeginPath(app.vg);
    nvgRoundedRect(app.vg, r.x, r.y, r.w, r.h, radius);
    nvgFillColor(app.vg, c.panel);
    nvgFill(app.vg);
    nvgStrokeColor(app.vg, c.border);
    nvgStrokeWidth(app.vg, 1.0f);
    nvgStroke(app.vg);
}

void draw_menu(app_t& app)
{
    if(!app.menu_open)
    {
        return;
    }
    auto   c = colors(app);
    float  s = app.ui_scale;
    rect_t m = menu_rect(app);
    draw_panel(app, m, 8 * s);

    const char* labels[MENU_COUNT] = {"Light", "Dark", nullptr, "Grayscale"};
    nvgFontFace(app.vg, "sans-serif");
    nvgFontSize(app.vg, 14.5f * s);
    nvgTextAlign(app.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    for(int i = 0; i < MENU_COUNT; i++)
    {
        rect_t r = menu_item_rect(app, i);
        if(i == MENU_SEPARATOR)
        {
            nvgBeginPath(app.vg);
            nvgRect(app.vg, r.x + 4 * s, r.y + r.h / 2, r.w - 8 * s, 1);
            nvgFillColor(app.vg, c.border);
            nvgFill(app.vg);
            continue;
        }
        if(i == app.menu_hover)
        {
            nvgBeginPath(app.vg);
            nvgRoundedRect(app.vg, r.x, r.y, r.w, r.h, 5 * s);
            nvgFillColor(app.vg, c.hover);
            nvgFill(app.vg);
        }
        float cx = r.x + 16 * s, cy = r.y + r.h / 2;
        if(i == MENU_GRAYSCALE)
        {
            // Checkbox
            float b = 13 * s;
            nvgBeginPath(app.vg);
            nvgRoundedRect(app.vg, cx - b / 2, cy - b / 2, b, b, 3 * s);
            if(app.grayscale)
            {
                nvgFillColor(app.vg, c.accent);
                nvgFill(app.vg);
                nvgBeginPath(app.vg);
                nvgMoveTo(app.vg, cx - 3.5f * s, cy + 0.2f * s);
                nvgLineTo(app.vg, cx - 1.0f * s, cy + 2.8f * s);
                nvgLineTo(app.vg, cx + 3.8f * s, cy - 2.6f * s);
                nvgStrokeColor(app.vg, c.panel);
                nvgStrokeWidth(app.vg, 1.8f * s);
                nvgLineCap(app.vg, NVG_ROUND);
                nvgLineJoin(app.vg, NVG_ROUND);
                nvgStroke(app.vg);
            } else
            {
                nvgStrokeColor(app.vg, c.muted);
                nvgStrokeWidth(app.vg, 1.3f * s);
                nvgStroke(app.vg);
            }
        } else
        {
            // Radio button
            bool on = (i == MENU_DARK) == app.dark;
            nvgBeginPath(app.vg);
            nvgCircle(app.vg, cx, cy, 6.5f * s);
            nvgStrokeColor(app.vg, on ? c.accent : c.muted);
            nvgStrokeWidth(app.vg, 1.3f * s);
            nvgStroke(app.vg);
            if(on)
            {
                nvgBeginPath(app.vg);
                nvgCircle(app.vg, cx, cy, 3.5f * s);
                nvgFillColor(app.vg, c.accent);
                nvgFill(app.vg);
            }
        }
        nvgFillColor(app.vg, c.text);
        nvgText(app.vg, r.x + 32 * s, cy, labels[i], nullptr);
    }
}

void draw_find_bar(app_t& app)
{
    if(!app.find_open)
    {
        return;
    }
    auto   c = colors(app);
    float  s = app.ui_scale;
    rect_t r = find_bar_rect(app);
    draw_panel(app, r, 8 * s);

    // Magnifier icon
    float ix = r.x + 18 * s, iy = r.y + r.h / 2;
    nvgBeginPath(app.vg);
    nvgCircle(app.vg, ix - 1.5f * s, iy - 1.5f * s, 5 * s);
    nvgMoveTo(app.vg, ix + 2 * s, iy + 2 * s);
    nvgLineTo(app.vg, ix + 6 * s, iy + 6 * s);
    nvgStrokeColor(app.vg, c.muted);
    nvgStrokeWidth(app.vg, 1.6f * s);
    nvgLineCap(app.vg, NVG_ROUND);
    nvgStroke(app.vg);

    nvgFontFace(app.vg, "sans-serif");
    nvgFontSize(app.vg, 14.5f * s);

    // Match counter on the right
    std::string count;
    if(!app.find_query.empty())
    {
        count = app.matches.empty() ? "No matches"
                                    : std::to_string(app.current_match + 1) + " / " + std::to_string(app.matches.size());
    }
    nvgTextAlign(app.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(app.vg, app.matches.empty() && !app.find_query.empty() ? nvgRGBA(214, 60, 50, 255) : c.muted);
    float count_w = count.empty() ? 0.0f : nvgTextBounds(app.vg, 0, 0, count.c_str(), nullptr, nullptr);
    nvgText(app.vg, r.x + r.w - 14 * s, iy, count.c_str(), nullptr);

    // Query text; scrolls left when it is longer than the field
    float tx = r.x + 34 * s, tw = r.w - 34 * s - 24 * s - count_w;
    nvgTextAlign(app.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    float qw = nvgTextBounds(app.vg, 0, 0, app.find_query.c_str(), nullptr, nullptr);
    float x  = qw > tw ? tx + tw - qw : tx;
    nvgSave(app.vg);
    nvgIntersectScissor(app.vg, tx - 2 * s, r.y, tw + 4 * s, r.h);
    if(app.find_query.empty())
    {
        nvgFillColor(app.vg, c.muted);
        nvgText(app.vg, tx, iy, "Find in page", nullptr);
        x = tx - qw;
    } else
    {
        nvgFillColor(app.vg, c.text);
        nvgText(app.vg, x, iy, app.find_query.c_str(), nullptr);
    }
    // Caret
    nvgBeginPath(app.vg);
    nvgRect(app.vg, x + qw + 1 * s, iy - 9 * s, 1.5f * s, 18 * s);
    nvgFillColor(app.vg, c.accent);
    nvgFill(app.vg);
    nvgRestore(app.vg);
}

void draw_frame(app_t& app, int fb_w, int fb_h)
{
    const auto& t   = theme(app);
    float       r   = t.clear[0], g = t.clear[1], b = t.clear[2];
    if(app.grayscale)
    {
        r = g = b = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(app.vg, app.win_w, app.win_h, static_cast<float>(fb_w) / app.win_w);
    app.container->begin_frame(app.win_w, app.win_h);
    if(app.doc)
    {
        litehtml::position clip(0, 0, app.win_w, app.win_h);
        app.doc->draw(0, 0, -scroll_px(app), &clip);
    }
    draw_highlights(app);
    draw_scrollbar(app);
    draw_find_bar(app);
    draw_menu(app);
    nvgEndFrame(app.vg);
}

bool save_png(const std::string& path, int w, int h)
{
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    // GL rows are bottom-up
    return stbi_write_png(path.c_str(), w, h, 4, px.data() + static_cast<size_t>(h - 1) * w * 4, -w * 4) != 0;
}

// --- input -------------------------------------------------------------------------------------

app_t* get_app(GLFWwindow* w)
{
    return static_cast<app_t*>(glfwGetWindowUserPointer(w));
}

void update_hover(app_t& app)
{
    if(!app.doc)
    {
        return;
    }
    float x = static_cast<float>(app.mouse_x), y = static_cast<float>(app.mouse_y);
    auto  noop = [](const litehtml::position&) {};
    if(app.menu_open)
    {
        int h = menu_item_at(app, x, y);
        if(h != app.menu_hover)
        {
            app.menu_hover = h;
            app.dirty      = true;
        }
        glfwSetCursor(app.window, nullptr);
        return;
    }
    if(app.doc->on_mouse_over(x, y + scroll_px(app), x, y, noop))
    {
        app.dirty = true;
    }
    auto g        = scrollbar(app);
    bool over_bar = g.visible && x >= g.x - 8 * app.ui_scale;
    if(over_bar != app.hover_bar)
    {
        app.hover_bar = over_bar;
        app.dirty     = true;
    }

    GLFWcursor* cursor = nullptr;
    if(app.find_open && find_bar_rect(app).contains(x, y))
    {
        cursor = app.ibeam;
    } else if(!over_bar && app.container->cursor() == "pointer" && !app.drag_moved)
    {
        cursor = app.hand;
    } else if(!over_bar && (app.drag_moved || app.index.over_text(x, y + scroll_px(app))))
    {
        cursor = app.ibeam;
    }
    glfwSetCursor(app.window, cursor);
}

void on_mouse_move(GLFWwindow* w, double x, double y)
{
    auto& app   = *get_app(w);
    app.mouse_x = x;
    app.mouse_y = y;
    if(app.dragging_bar)
    {
        auto  g     = scrollbar(app);
        float range = app.win_h - g.thumb_h;
        if(range > 0)
        {
            scroll_to(app, (static_cast<float>(y) - app.drag_offset) / range * max_scroll(app), false);
        }
        return;
    }
    if(app.selecting)
    {
        if(!app.drag_moved && std::hypot(x - app.press_x, y - app.press_y) > 3.0 * app.ui_scale)
        {
            app.drag_moved = true;
        }
        if(app.drag_moved && app.click_count == 1)
        {
            app.sel_focus = hit_document(app, x, y);
            app.dirty     = true;
        }
    }
    update_hover(app);
}

void on_mouse_button(GLFWwindow* w, int button, int action, int mods)
{
    auto&  app  = *get_app(w);
    double x    = app.mouse_x, y = app.mouse_y;
    float  fx   = static_cast<float>(x), fy = static_cast<float>(y);
    auto   noop = [](const litehtml::position&) {};

    // The menu takes every click while it is open.
    if(app.menu_open)
    {
        if(action == GLFW_PRESS)
        {
            int item = menu_item_at(app, x, y);
            if(item >= 0 && button == GLFW_MOUSE_BUTTON_LEFT)
            {
                activate_menu(app, item);
            } else if(button == GLFW_MOUSE_BUTTON_RIGHT)
            {
                open_menu(app, fx, fy);
            } else
            {
                close_menu(app);
            }
        }
        return;
    }
    if(button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        open_menu(app, fx, fy);
        return;
    }
    if(button == GLFW_MOUSE_BUTTON_4 && action == GLFW_PRESS)
    {
        go_history(app, true);
        return;
    }
    if(button == GLFW_MOUSE_BUTTON_5 && action == GLFW_PRESS)
    {
        go_history(app, false);
        return;
    }
    if(button != GLFW_MOUSE_BUTTON_LEFT || !app.doc)
    {
        return;
    }
    if(app.find_open && find_bar_rect(app).contains(x, y))
    {
        return;
    }

    auto g = scrollbar(app);
    if(action == GLFW_PRESS && g.visible && fx >= g.x - 8 * app.ui_scale)
    {
        app.dragging_bar = true;
        // Clicking outside the thumb centers it under the mouse.
        app.drag_offset = (fy >= g.thumb_y && fy <= g.thumb_y + g.thumb_h) ? fy - g.thumb_y : g.thumb_h / 2;
        on_mouse_move(w, x, y);
        return;
    }
    if(action == GLFW_RELEASE && app.dragging_bar)
    {
        app.dragging_bar = false;
        app.dirty        = true;
        return;
    }

    if(action == GLFW_PRESS)
    {
        double now = glfwGetTime();
        bool   near = std::hypot(x - app.press_x, y - app.press_y) < 4.0 * app.ui_scale;
        app.click_count     = (now - app.last_click_time < 0.4 && near) ? app.click_count % 3 + 1 : 1;
        app.last_click_time = now;
        app.press_x         = x;
        app.press_y         = y;
        app.selecting       = true;
        app.drag_moved      = false;

        text_pos p = hit_document(app, x, y);
        if(app.click_count == 2)
        {
            auto r         = app.index.word_at(p);
            app.sel_anchor = r.first;
            app.sel_focus  = r.second;
        } else if(app.click_count == 3)
        {
            auto r         = app.index.block_at(p);
            app.sel_anchor = r.first;
            app.sel_focus  = r.second;
        } else if((mods & GLFW_MOD_SHIFT) && app.sel_anchor.valid())
        {
            app.sel_focus  = p;
            app.drag_moved = true; // extending a selection is never a link click
        } else
        {
            app.sel_anchor = app.sel_focus = p;
        }
        app.dirty |= app.doc->on_lbutton_down(fx, fy + scroll_px(app), fx, fy, noop);
        app.dirty = true;
        return;
    }

    // Release: a drag selects text, a plain click follows links.
    app.selecting = false;
    if(app.drag_moved || app.click_count > 1)
    {
        app.doc->on_button_cancel(noop);
    } else
    {
        app.dirty |= app.doc->on_lbutton_up(fx, fy + scroll_px(app), fx, fy, noop);
    }
    app.drag_moved = false;
    app.dirty      = true;
    update_hover(app);
}

void on_scroll(GLFWwindow* w, double /*dx*/, double dy)
{
    auto& app = *get_app(w);
    if(app.menu_open)
    {
        close_menu(app);
    }
    if(glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
    {
        set_zoom(app, app.zoom * (dy > 0 ? 1.1f : 1.0f / 1.1f));
        return;
    }
    scroll_to(app, app.target - static_cast<float>(dy) * 3.0f * font_px(app));
}

void on_char(GLFWwindow* w, unsigned codepoint)
{
    auto& app = *get_app(w);
    if(app.find_open && codepoint >= 0x20)
    {
        append_utf8(app.find_query, codepoint);
        find_query_changed(app);
    }
}

void on_key(GLFWwindow* w, int key, int /*scancode*/, int action, int mods)
{
    if(action == GLFW_RELEASE)
    {
        return;
    }
    auto& app   = *get_app(w);
    bool  ctrl  = mods & GLFW_MOD_CONTROL;
    bool  alt   = mods & GLFW_MOD_ALT;
    bool  shift = mods & GLFW_MOD_SHIFT;
    float line  = 3.0f * font_px(app);
    float page  = app.win_h * 0.9f;

    if(app.menu_open)
    {
        if(key == GLFW_KEY_ESCAPE)
        {
            close_menu(app);
        }
        return;
    }

    // Shortcuts that work everywhere.
    if(ctrl && key == GLFW_KEY_C)
    {
        copy_selection(app);
        return;
    }
    if(ctrl && key == GLFW_KEY_A)
    {
        auto r         = app.index.all();
        app.sel_anchor = r.first;
        app.sel_focus  = r.second;
        app.dirty      = true;
        return;
    }
    if(ctrl && key == GLFW_KEY_F)
    {
        open_find(app);
        return;
    }
    if(key == GLFW_KEY_F3)
    {
        if(!app.find_open)
        {
            open_find(app);
        } else
        {
            find_step(app, shift ? -1 : 1);
        }
        return;
    }

    // While the find bar is open, typing goes to it (see on_char).
    if(app.find_open)
    {
        switch(key)
        {
        case GLFW_KEY_ESCAPE:
            close_find(app);
            return;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            find_step(app, shift ? -1 : 1);
            return;
        case GLFW_KEY_BACKSPACE:
            pop_utf8(app.find_query);
            find_query_changed(app);
            return;
        case GLFW_KEY_V:
            if(ctrl)
            {
                if(const char* clip = glfwGetClipboardString(w))
                {
                    std::string text = clip;
                    text             = text.substr(0, text.find('\n'));
                    app.find_query += text;
                    find_query_changed(app);
                }
            }
            return;
        case GLFW_KEY_PAGE_DOWN:
        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_DOWN:
        case GLFW_KEY_UP:
            break; // still scroll
        default:
            return;
        }
    }

    switch(key)
    {
    case GLFW_KEY_ESCAPE:
        if(has_selection(app))
        {
            clear_selection(app);
        } else
        {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
        break;
    case GLFW_KEY_Q:
        glfwSetWindowShouldClose(w, GLFW_TRUE);
        break;
    case GLFW_KEY_SPACE:
        scroll_to(app, app.target + (shift ? -page : page));
        break;
    case GLFW_KEY_PAGE_DOWN:
        scroll_to(app, app.target + page);
        break;
    case GLFW_KEY_PAGE_UP:
        scroll_to(app, app.target - page);
        break;
    case GLFW_KEY_DOWN:
    case GLFW_KEY_J:
        scroll_to(app, app.target + line);
        break;
    case GLFW_KEY_UP:
    case GLFW_KEY_K:
        scroll_to(app, app.target - line);
        break;
    case GLFW_KEY_HOME:
        scroll_to(app, 0);
        break;
    case GLFW_KEY_END:
        scroll_to(app, max_scroll(app));
        break;
    case GLFW_KEY_G:
        scroll_to(app, shift ? max_scroll(app) : 0);
        break;
    case GLFW_KEY_BACKSPACE:
        go_history(app, true);
        break;
    case GLFW_KEY_LEFT:
        if(alt)
        {
            go_history(app, true);
        }
        break;
    case GLFW_KEY_RIGHT:
        if(alt)
        {
            go_history(app, false);
        }
        break;
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:
        if(ctrl || key == GLFW_KEY_KP_ADD)
        {
            set_zoom(app, app.zoom * 1.1f);
        }
        break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
        if(ctrl || key == GLFW_KEY_KP_SUBTRACT)
        {
            set_zoom(app, app.zoom / 1.1f);
        }
        break;
    case GLFW_KEY_0:
        if(ctrl)
        {
            set_zoom(app, 1.0f);
        }
        break;
    case GLFW_KEY_T:
        set_dark(app, !app.dark);
        break;
    case GLFW_KEY_F:
        app.serif = !app.serif;
        rebuild_keep_position(app);
        break;
    case GLFW_KEY_R:
    {
        float keep = app.scroll;
        load(app, app.path);
        ensure_layout(app);
        scroll_to(app, keep, false);
        break;
    }
    default:
        break;
    }
}

void on_drop(GLFWwindow* w, int count, const char** paths)
{
    if(count > 0)
    {
        // Deferred like links, see the main loop.
        get_app(w)->pending_link = std::string("file://") + paths[0];
    }
}

// While dragging a selection near the top or bottom edge, scroll and keep extending it.
bool autoscroll_selection(app_t& app)
{
    if(!app.selecting || !app.drag_moved || app.click_count != 1)
    {
        return false;
    }
    float edge = 28.0f * app.ui_scale;
    float y    = static_cast<float>(app.mouse_y);
    float dy   = 0;
    if(y < edge)
    {
        dy = -(edge - y) * 0.6f - 2.0f;
    } else if(y > app.win_h - edge)
    {
        dy = (y - (app.win_h - edge)) * 0.6f + 2.0f;
    }
    if(dy == 0 || (dy < 0 && app.target <= 0) || (dy > 0 && app.target >= max_scroll(app)))
    {
        return false;
    }
    scroll_to(app, app.target + dy, false);
    app.sel_focus = hit_document(app, app.mouse_x, app.mouse_y);
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    app_t       app;
    std::string file, png, find;
    int         width = 900, height = 1000;
    float       start_scroll = 0;

    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if(a == "--png" && i + 1 < argc)
        {
            png = argv[++i];
        } else if(a == "--dark")
        {
            app.dark = true;
        } else if(a == "--grayscale")
        {
            app.grayscale = true;
        } else if(a == "--serif")
        {
            app.serif = true;
        } else if(a == "--zoom" && i + 1 < argc)
        {
            app.zoom = std::clamp(std::stof(argv[++i]), 0.5f, 3.0f);
        } else if(a == "--find" && i + 1 < argc)
        {
            find = argv[++i];
        } else if(a == "--size" && i + 1 < argc)
        {
            sscanf(argv[++i], "%dx%d", &width, &height);
        } else if(a == "--scroll" && i + 1 < argc)
        {
            start_scroll = std::stof(argv[++i]);
        } else if(a == "-h" || a == "--help")
        {
            printf("usage: mdhoney [file.md[#heading]] [--dark] [--grayscale] [--serif] [--zoom F] [--find TEXT]\n"
                   "               [--size WxH] [--scroll PX] [--png out.png]\n");
            return 0;
        } else
        {
            file = a;
        }
    }

    signal(SIGCHLD, SIG_IGN); // don't leave zombie xdg-open processes

    if(!glfwInit())
    {
        fprintf(stderr, "mdhoney: glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    if(!png.empty())
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    app.window = glfwCreateWindow(width, height, "mdhoney", nullptr, nullptr);
    if(!app.window)
    {
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
        app.window = glfwCreateWindow(width, height, "mdhoney", nullptr, nullptr);
    }
    if(!app.window)
    {
        fprintf(stderr, "mdhoney: cannot create an OpenGL ES 3.0 window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(app.window);
    glfwSwapInterval(1);

    app.vg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if(!app.vg)
    {
        fprintf(stderr, "mdhoney: nvgCreateGLES3 failed\n");
        return 1;
    }
    nvglSetGrayscale(app.vg, app.grayscale);

    float sx = 1, sy = 1;
    glfwGetWindowContentScale(app.window, &sx, &sy);
    app.ui_scale = std::max(1.0f, sx);

    // Owned by a pointer so it can be destroyed before the NanoVG context it uses.
    auto              container_owner = std::make_unique<container_nanovg>(app.vg);
    container_nanovg& container       = *container_owner;
    if(!register_system_fonts(container))
    {
        fprintf(stderr, "mdhoney: no usable fonts found\n");
    }
    container.on_link = [&app](const std::string& url) { app.pending_link = url; };
    app.container     = &container;
    app.hand          = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    app.ibeam         = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);

    glfwSetWindowUserPointer(app.window, &app);
    glfwSetCursorPosCallback(app.window, on_mouse_move);
    glfwSetMouseButtonCallback(app.window, on_mouse_button);
    glfwSetScrollCallback(app.window, on_scroll);
    glfwSetKeyCallback(app.window, on_key);
    glfwSetCharCallback(app.window, on_char);
    glfwSetDropCallback(app.window, on_drop);
    glfwSetWindowRefreshCallback(app.window, [](GLFWwindow* w) { get_app(w)->dirty = true; });

    // "file.md#heading" opens at that heading (only if the file name itself has no '#').
    std::string anchor;
    auto        hash = file.find('#');
    if(hash != std::string::npos && file_mtime(file) == 0)
    {
        anchor = file.substr(hash + 1);
        file   = file.substr(0, hash);
    }

    glfwGetWindowSize(app.window, &app.win_w, &app.win_h);
    load(app, file);
    ensure_layout(app);
    if(anchor.empty() || !scroll_to_anchor(app, anchor, false))
    {
        scroll_to(app, start_scroll, false);
    }
    if(!find.empty())
    {
        app.find_query = find;
        open_find(app);
        app.scroll = app.target;
    }

    double last_check = glfwGetTime();
    while(!glfwWindowShouldClose(app.window))
    {
        int fb_w, fb_h;
        glfwGetWindowSize(app.window, &app.win_w, &app.win_h);
        glfwGetFramebufferSize(app.window, &fb_w, &fb_h);
        if(app.win_w <= 0 || app.win_h <= 0)
        {
            glfwWaitEvents();
            continue;
        }

        // Links are handled here, outside litehtml's event handlers, because they can replace the document.
        if(!app.pending_link.empty())
        {
            std::string link = app.pending_link;
            app.pending_link.clear();
            close_menu(app);
            handle_link(app, link);
        }

        // Live reload when the file changes on disk.
        if(glfwGetTime() - last_check > 0.5)
        {
            last_check = glfwGetTime();
            long long m = file_mtime(app.path);
            if(m != 0 && m != app.mtime)
            {
                float keep = app.scroll;
                load(app, app.path);
                ensure_layout(app);
                scroll_to(app, keep, false);
            }
        }

        ensure_layout(app);
        app.target = std::clamp(app.target, 0.0f, max_scroll(app));

        bool autoscrolling = autoscroll_selection(app);

        // Smooth scrolling toward the target.
        bool animating = autoscrolling || std::fabs(app.target - app.scroll) > 0.5f;
        if(std::fabs(app.target - app.scroll) > 0.5f)
        {
            app.scroll += (app.target - app.scroll) * 0.3f;
            app.dirty = true;
        } else if(app.scroll != app.target)
        {
            app.scroll = app.target;
            app.dirty  = true;
            update_hover(app);
        }

        if(app.dirty)
        {
            app.dirty = false;
            draw_frame(app, fb_w, fb_h);
            if(!png.empty())
            {
                bool ok = save_png(png, fb_w, fb_h);
                printf("%s %s (%dx%d)\n", ok ? "saved" : "failed to save", png.c_str(), fb_w, fb_h);
                break;
            }
            glfwSwapBuffers(app.window);
        }

        if(animating)
        {
            glfwPollEvents();
        } else
        {
            glfwWaitEventsTimeout(0.5);
        }
    }

    app.doc.reset();         // fonts are owned by the container, drop the document first
    container_owner.reset(); // frees its images through NanoVG, so before nvgDeleteGLES3
    glfwDestroyCursor(app.hand);
    glfwDestroyCursor(app.ibeam);
    nvgDeleteGLES3(app.vg);
    release_system_fonts();
    glfwTerminate();
    return 0;
}
