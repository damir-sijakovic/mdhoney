#include "markdown.h"

#include "md4c-html.h"

#include <cctype>
#include <map>

namespace
{
void append_output(const MD_CHAR* text, MD_SIZE size, void* user)
{
    static_cast<std::string*>(user)->append(text, size);
}

void replace_all(std::string& s, const std::string& from, const std::string& to)
{
    for(size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
    {
        s.replace(pos, from.size(), to);
    }
}

// Plain text of an HTML fragment: tags removed, the entities md4c emits decoded.
std::string html_text(const std::string& html)
{
    std::string out;
    bool        in_tag = false;
    for(char c : html)
    {
        if(c == '<')
        {
            in_tag = true;
        } else if(c == '>')
        {
            in_tag = false;
        } else if(!in_tag)
        {
            out += c;
        }
    }
    replace_all(out, "&lt;", "<");
    replace_all(out, "&gt;", ">");
    replace_all(out, "&quot;", "\"");
    replace_all(out, "&#39;", "'");
    replace_all(out, "&amp;", "&");
    return out;
}

// Adds id="slug" to <h1>..<h6>, de-duplicating like GitHub (intro, intro-1, ...).
std::string add_heading_ids(const std::string& html, std::string& title)
{
    std::string                out;
    std::map<std::string, int> used;
    size_t                     pos = 0;
    out.reserve(html.size() + html.size() / 16);

    while(true)
    {
        size_t h = html.find("<h", pos);
        if(h == std::string::npos || h + 3 >= html.size())
        {
            break;
        }
        char level = html[h + 2];
        if(level < '1' || level > '6' || html[h + 3] != '>')
        {
            out.append(html, pos, h + 2 - pos);
            pos = h + 2;
            continue;
        }
        std::string close = std::string("</h") + level + ">";
        size_t      end   = html.find(close, h);
        if(end == std::string::npos)
        {
            break;
        }
        std::string text = html_text(html.substr(h + 4, end - (h + 4)));
        if(title.empty() && level == '1')
        {
            title = text;
        }
        std::string slug  = slugify(text);
        int         count = used[slug]++;
        if(count > 0)
        {
            slug += "-" + std::to_string(count);
        }
        out.append(html, pos, h - pos);
        out += std::string("<h") + level + " id=\"" + slug + "\">";
        pos = h + 4;
    }
    out.append(html, pos, std::string::npos);
    return out;
}
} // namespace

std::string slugify(const std::string& text)
{
    std::string slug;
    for(unsigned char c : text)
    {
        if(c >= 0x80 || std::isalnum(c) || c == '-' || c == '_')
        {
            slug += static_cast<char>(c < 0x80 ? std::tolower(c) : c);
        } else if(c == ' ')
        {
            slug += '-';
        }
    }
    return slug;
}

md_result markdown_to_html(const std::string& markdown)
{
    md_result   result;
    std::string html;
    if(md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()), append_output, &html, MD_DIALECT_GITHUB,
               MD_HTML_FLAG_SKIP_UTF8_BOM) != 0)
    {
        result.html = "<p>Failed to parse Markdown.</p>";
        return result;
    }

    // U+2611 BALLOT BOX WITH CHECK, U+2610 BALLOT BOX
    replace_all(html, "<input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled checked>",
                "<span class=\"task-box done\">☑</span> ");
    replace_all(html, "<input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled>",
                "<span class=\"task-box\">☐</span> ");

    result.html = add_heading_ids(html, result.title);
    return result;
}
