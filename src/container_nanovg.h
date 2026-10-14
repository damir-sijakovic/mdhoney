// litehtml document_container implemented on top of NanoVGX (NanoVG fork with multi-stop gradients).
// Works with any NanoVGX GL backend (GL2/GL3/GLES2/GLES3).
#pragma once

#include <litehtml.h>
#include "nanovgx.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

class container_nanovg : public litehtml::document_container
{
  public:
    explicit container_nanovg(NVGcontext* vg);
    ~container_nanovg() override;

    // Register a TTF/OTF file for a family (e.g. "sans-serif", "serif", "monospace", "Arial").
    // index selects the face inside a font collection (.ttc). Returns false if it can't be loaded.
    bool add_font_face(const std::string& family, bool bold, bool italic, const std::string& path, int index = 0);
    // Font used by every face for glyphs it lacks (symbols, CJK, ...). Tried in the order added.
    bool add_fallback_font(const std::string& path, int index = 0);
    void set_default_font(const std::string& family, float size_px);

    // Directory used to resolve relative URLs (images, stylesheets).
    void set_base_dir(const std::string& dir) { m_base_dir = dir; }

    // Call once per frame before document::draw() (after nvgBeginFrame).
    void begin_frame(int width, int height);
    void set_viewport(int width, int height);

    const std::string& cursor() const { return m_cursor; }
    std::function<void(const std::string&)> on_caption;
    std::function<void(const std::string&)> on_link;

    // document_container
    litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document* doc,
                                   litehtml::font_metrics* fm) override;
    void               delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t  text_width(const char* text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color,
                   const litehtml::position& pos) override;
    litehtml::pixel_t pt_to_px(float pt) const override;
    litehtml::pixel_t get_default_font_size() const override;
    const char*       get_default_font_name() const override;
    void              draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
    void              load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;
    void              get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url,
                    const std::string& base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                         const litehtml::web_color& color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                              const litehtml::background_layer::linear_gradient& gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                              const litehtml::background_layer::radial_gradient& gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                             const litehtml::background_layer::conic_gradient& gradient) override;
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos,
                      bool root) override;

    void set_caption(const char* caption) override;
    void set_base_url(const char* base_url) override;
    void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override;
    void on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
    void on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override;
    void set_cursor(const char* cursor) override;
    void transform_text(std::string& text, litehtml::text_transform tt) override;
    void import_css(std::string& text, const std::string& url, std::string& baseurl) override;
    void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
    void del_clip() override;
    void get_viewport(litehtml::position& viewport) const override;
    litehtml::element::ptr create_element(const char* tag_name, const litehtml::string_map& attributes,
                                          const std::shared_ptr<litehtml::document>& doc) override;
    void get_media_features(litehtml::media_features& media) const override;
    void get_language(std::string& language, std::string& culture) const override;
    std::string resolve_color(const std::string& color) const override;

  private:
    struct face_t
    {
        std::string family; // lower-case
        bool        bold;
        bool        italic;
        int         id;
    };
    struct font_t
    {
        int                 face;
        float               size;
        bool                fake_italic;
        int                 decoration;
        litehtml::web_color decoration_color;
        float               ascent;
        float               descent;
        float               x_height;
    };
    struct image_t
    {
        int id = -1;
        int w  = 0;
        int h  = 0;
    };

    std::string resolve_url(const char* src, const char* baseurl) const;
    image_t*    get_image(const std::string& path);
    int         find_face(const std::string& families, int weight, bool italic, bool& fake_italic) const;
    void        apply_clip();
    void        clip_layer(const litehtml::background_layer& layer);
    void        path_rounded_rect(const litehtml::position& pos, const litehtml::border_radiuses& r);
    void        fill_gradient(const litehtml::background_layer& layer, NVGpaint paint);

    NVGcontext*                    m_vg;
    std::vector<face_t>            m_faces;
    std::vector<int>               m_fallbacks;
    std::string                    m_default_family = "sans-serif";
    float                          m_default_size   = 16.0f;
    std::string                    m_base_dir       = ".";
    std::string                    m_cursor         = "auto";
    int                            m_width          = 800;
    int                            m_height         = 600;
    std::vector<litehtml::position> m_clips;
    std::map<std::string, image_t> m_images;
};
