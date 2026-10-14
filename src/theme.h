// Reader stylesheets. Sizes are in em, so the whole page scales with the base font size.
#pragma once

#include <string>

struct theme_t
{
    const char* name;
    const char* bg;
    const char* text;
    const char* muted;
    const char* heading;
    const char* link;
    const char* link_hover;
    const char* rule;
    const char* code_bg;
    const char* code_text;
    const char* quote_bar;
    const char* quote_text;
    const char* table_head;
    const char* table_stripe;
    const char* mark;
    float       clear[3]; // GL clear color, matches bg
    unsigned char scrollbar[3];
};

inline const theme_t& light_theme()
{
    static const theme_t t = {"honey", "#fffcf4", "#2d2620", "#7a6e60", "#1f1a15", "#b45f06", "#d9822b",
                              "#eadfca", "#f6eedd", "#5c3d10", "#e8a93a", "#5f564b", "#f4ead6", "#fbf6ea",
                              "#ffe9a8", {1.0f, 0.988f, 0.957f}, {120, 100, 70}};
    return t;
}

inline const theme_t& dark_theme()
{
    static const theme_t t = {"honey-dark", "#1b1916", "#e6dfd2", "#a39a8c", "#f7f1e6", "#f2b45a", "#ffd08a",
                              "#3a352d", "#2a2620", "#f0c987", "#d39a34", "#bdb3a4", "#2c2822", "#211e1a",
                              "#5c4a1c", {0.106f, 0.098f, 0.086f}, {200, 180, 150}};
    return t;
}

inline std::string theme_css(const theme_t& t, float font_px, bool serif)
{
    std::string css = R"(
html { background: @bg; }
body {
  margin: 0; padding: 0;
  background: @bg; color: @text;
  font-family: @body_font; font-size: @sizepx; line-height: 1.6;
}
article { display: block; max-width: 46em; margin: 0 auto; padding: 2.2em 2em 4em 2em; }
h1, h2, h3, h4, h5, h6 {
  font-family: sans-serif; color: @heading; line-height: 1.25;
  margin: 1.5em 0 0.6em 0; font-weight: bold;
}
h1 { font-size: 2.1em; padding-bottom: 0.25em; border-bottom: 1px solid @rule; margin-top: 0.4em; }
h2 { font-size: 1.55em; padding-bottom: 0.2em; border-bottom: 1px solid @rule; }
h3 { font-size: 1.25em; }
h4 { font-size: 1.05em; }
h5 { font-size: 0.95em; }
h6 { font-size: 0.9em; color: @muted; }
p, ul, ol, blockquote, table, pre { margin: 0 0 1em 0; }
a { color: @link; text-decoration: none; }
a:hover { color: @link_hover; text-decoration: underline; cursor: pointer; }
strong, b { font-weight: bold; color: @heading; }
del, s { color: @muted; }
hr { border: 0; border-top: 2px solid @rule; margin: 2em 0; height: 0; }
ul, ol { padding-left: 1.8em; }
li { margin: 0.25em 0; }
li > p { margin: 0.25em 0; }
li.task-list-item { list-style-type: none; }
.task-box { font-family: "DejaVu Sans", sans-serif; color: @muted; }
.task-box.done { color: @link; }
blockquote {
  margin-left: 0; padding: 0.2em 1em; color: @quote_text;
  border-left: 4px solid @quote_bar; background: @code_bg; border-radius: 0 6px 6px 0;
}
blockquote p { margin: 0.5em 0; }
code {
  font-family: monospace; font-size: 0.88em; color: @code_text;
  background: @code_bg; padding: 0.1em 0.35em; border-radius: 4px;
}
pre {
  font-family: monospace; font-size: 0.88em; line-height: 1.45;
  background: @code_bg; padding: 0.9em 1.1em; border-radius: 8px;
  border: 1px solid @rule; white-space: pre-wrap;
}
pre code { font-size: 1em; background: transparent; padding: 0; border-radius: 0; }
table { border-collapse: collapse; }
th, td { border: 1px solid @rule; padding: 0.4em 0.8em; }
th { background: @table_head; font-weight: bold; }
tr:nth-child(even) td { background: @table_stripe; }
img { max-width: 100%; border-radius: 6px; }
mark { background: @mark; color: @text; padding: 0 0.15em; }
kbd {
  font-family: monospace; font-size: 0.85em; padding: 0.05em 0.4em;
  border: 1px solid @rule; border-bottom-width: 2px; border-radius: 4px; background: @code_bg;
}
.footer { color: @muted; font-size: 0.85em; margin-top: 3em; }
)";
    auto set = [&css](const std::string& key, const std::string& value) {
        for(size_t pos = css.find(key); pos != std::string::npos; pos = css.find(key, pos + value.size()))
        {
            css.replace(pos, key.size(), value);
        }
    };
    // Longer keys first, so "@link_hover" is not eaten by "@link".
    set("@link_hover", t.link_hover);
    set("@link", t.link);
    set("@table_stripe", t.table_stripe);
    set("@table_head", t.table_head);
    set("@quote_text", t.quote_text);
    set("@quote_bar", t.quote_bar);
    set("@code_text", t.code_text);
    set("@code_bg", t.code_bg);
    set("@body_font", serif ? "serif" : "sans-serif");
    set("@heading", t.heading);
    set("@muted", t.muted);
    set("@text", t.text);
    set("@rule", t.rule);
    set("@mark", t.mark);
    set("@bg", t.bg);
    set("@size", std::to_string(static_cast<int>(font_px + 0.5f)));
    return css;
}
