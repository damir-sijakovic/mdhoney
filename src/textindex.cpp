#include "textindex.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool is_block_display(litehtml::style_display d)
{
    switch(d)
    {
    case litehtml::display_block:
    case litehtml::display_list_item:
    case litehtml::display_table:
    case litehtml::display_table_caption:
    case litehtml::display_table_cell:
    case litehtml::display_table_row:
    case litehtml::display_flex:
        return true;
    default:
        return false;
    }
}

std::string lower_ascii(std::string s)
{
    for(auto& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}
} // namespace

void text_index::clear()
{
    m_runs.clear();
    m_plain.clear();
}

void text_index::build(const litehtml::document::ptr& doc, litehtml::document_container* container)
{
    clear();
    m_container = container;
    m_block     = 0;
    m_row       = -1;
    m_counter   = 0;
    if(doc && doc->root())
    {
        walk(doc->root(), false);
    }

    // Plain text with separators: same line -> as is, wrapped line -> space, new block -> newline,
    // next table cell -> tab.
    for(size_t i = 0; i < m_runs.size(); i++)
    {
        auto& r = m_runs[i];
        if(i > 0)
        {
            const auto& p = m_runs[i - 1];
            if(r.block != p.block)
            {
                m_plain += (r.row >= 0 && r.row == p.row) ? "\t" : "\n";
            } else if(std::fabs(r.y - p.y) > std::min(r.h, p.h) / 2 && !p.text.empty() && !r.text.empty() &&
                      !is_space(p.text.back()) && !is_space(r.text.front()))
            {
                m_plain += " ";
            }
        }
        r.plain_start = static_cast<int>(m_plain.size());
        m_plain += r.text;
    }
}

void text_index::walk(const litehtml::element::ptr& el, bool pre)
{
    if(el->css().get_display() == litehtml::display_none)
    {
        return;
    }
    if(el->is_text())
    {
        std::string text;
        el->get_text(text);
        auto parent = el->parent();
        auto p      = el->get_placement();
        bool line_break = pre && text == "\n"; // litehtml gives these a zero-size box
        if(text.empty() || !parent ||
           (!line_break && static_cast<float>(p.width) <= 0.0f && static_cast<float>(p.height) <= 0.0f))
        {
            return; // collapsed white space or not rendered
        }
        if(el->is_white_space() && !(pre && text == "\n"))
        {
            text = " "; // a soft line break or tab in normal text reads as a space
        }
        add_run(text, parent->css().get_font(), p);
        return;
    }
    if(el->is_break())
    {
        add_run("\n", 0, el->get_placement());
        return;
    }

    // 'pre' is the parent's white-space mode; text elements use it as is.
    auto ws = el->css().get_white_space();
    pre     = ws == litehtml::white_space_pre || ws == litehtml::white_space_pre_wrap ||
          ws == litehtml::white_space_pre_line;

    bool block   = is_block_display(el->css().get_display());
    bool row     = el->css().get_display() == litehtml::display_table_row;
    int  old_row = m_row;
    if(row)
    {
        m_row = ++m_counter;
    }
    if(block)
    {
        m_block = ++m_counter;
    }
    for(const auto& child : el->children())
    {
        walk(child, pre);
    }
    if(block)
    {
        m_block = ++m_counter; // text after a nested block starts a new line
    }
    if(row)
    {
        m_row = old_row;
    }
}

void text_index::add_run(std::string text, litehtml::uint_ptr font, const litehtml::position& p)
{
    run_t r;
    r.text        = std::move(text);
    r.font        = font;
    r.x           = p.x;
    r.y           = p.y;
    r.w           = p.width;
    r.h           = p.height;
    r.block       = m_block;
    r.row         = m_row;
    r.plain_start = 0;
    m_runs.push_back(std::move(r));
}

void text_index::measure(const run_t& r) const
{
    if(!r.bounds.empty())
    {
        return;
    }
    for(int i = 0; i <= static_cast<int>(r.text.size()); i++)
    {
        // UTF-8 continuation bytes are not character boundaries.
        if(i < static_cast<int>(r.text.size()) && (static_cast<unsigned char>(r.text[i]) & 0xC0) == 0x80)
        {
            continue;
        }
        r.bounds.push_back(i);
        if(i == 0)
        {
            r.stops.push_back(0.0f);
        } else if(i == static_cast<int>(r.text.size()))
        {
            r.stops.push_back(r.w);
        } else
        {
            float w = r.font && m_container ? static_cast<float>(m_container->text_width(r.text.substr(0, i).c_str(), r.font))
                                            : r.w * i / r.text.size();
            r.stops.push_back(std::min(w, r.w));
        }
    }
}

float text_index::x_at(const run_t& r, int byte) const
{
    measure(r);
    auto it = std::lower_bound(r.bounds.begin(), r.bounds.end(), byte);
    if(it == r.bounds.end())
    {
        return r.w;
    }
    return r.stops[it - r.bounds.begin()];
}

text_pos text_index::hit(float x, float y) const
{
    if(m_runs.empty())
    {
        return {};
    }
    // Prefer runs on the line under the point; otherwise the closest line.
    int   best      = -1;
    float best_dy   = 1e30f;
    float best_dx   = 1e30f;
    for(int i = 0; i < static_cast<int>(m_runs.size()); i++)
    {
        const auto& r = m_runs[i];
        if(r.w <= 0.0f && r.h <= 0.0f)
        {
            continue; // line breaks have no box
        }
        float dy = y < r.y ? r.y - y : (y > r.y + r.h ? y - (r.y + r.h) : 0.0f);
        float dx = x < r.x ? r.x - x : (x > r.x + r.w ? x - (r.x + r.w) : 0.0f);
        if(dy < best_dy - 0.5f || (std::fabs(dy - best_dy) <= 0.5f && dx < best_dx))
        {
            best    = i;
            best_dy = dy;
            best_dx = dx;
        }
    }
    if(best < 0)
    {
        return {};
    }
    const auto& r = m_runs[best];
    if(x <= r.x)
    {
        return {best, 0};
    }
    if(x >= r.x + r.w)
    {
        return {best, static_cast<int>(r.text.size())};
    }
    measure(r);
    float local = x - r.x;
    for(size_t i = 1; i < r.stops.size(); i++)
    {
        if(local < (r.stops[i - 1] + r.stops[i]) / 2)
        {
            return {best, r.bounds[i - 1]};
        }
    }
    return {best, static_cast<int>(r.text.size())};
}

bool text_index::over_text(float x, float y) const
{
    for(const auto& r : m_runs)
    {
        if(x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h && !is_space(r.text.front()))
        {
            return true;
        }
    }
    return false;
}

text_range text_index::word_at(const text_pos& p) const
{
    if(!p.valid())
    {
        return {};
    }
    int i = p.run;
    // At the end of a word the caret belongs to that word, not to the following space.
    if(p.byte == 0 && i > 0 && is_space(m_runs[i].text.front()) && !is_space(m_runs[i - 1].text.back()))
    {
        i--;
    }
    return {{i, 0}, {i, static_cast<int>(m_runs[i].text.size())}};
}

text_range text_index::block_at(const text_pos& p) const
{
    if(!p.valid())
    {
        return {};
    }
    int a = p.run, b = p.run;
    while(a > 0 && m_runs[a - 1].block == m_runs[p.run].block)
    {
        a--;
    }
    while(b + 1 < static_cast<int>(m_runs.size()) && m_runs[b + 1].block == m_runs[p.run].block)
    {
        b++;
    }
    return {{a, 0}, {b, static_cast<int>(m_runs[b].text.size())}};
}

text_range text_index::all() const
{
    if(m_runs.empty())
    {
        return {};
    }
    int last = static_cast<int>(m_runs.size()) - 1;
    return {{0, 0}, {last, static_cast<int>(m_runs[last].text.size())}};
}

int text_index::plain_offset(const text_pos& p) const
{
    const auto& r = m_runs[p.run];
    return r.plain_start + std::clamp(p.byte, 0, static_cast<int>(r.text.size()));
}

text_pos text_index::from_plain(int offset) const
{
    auto it = std::upper_bound(m_runs.begin(), m_runs.end(), offset,
                               [](int off, const run_t& r) { return off < r.plain_start; });
    if(it == m_runs.begin())
    {
        return {0, 0};
    }
    int i = static_cast<int>(it - m_runs.begin()) - 1;
    return {i, std::min(offset - m_runs[i].plain_start, static_cast<int>(m_runs[i].text.size()))};
}

std::string text_index::text(text_pos a, text_pos b) const
{
    if(!a.valid() || !b.valid() || a.run >= run_count() || b.run >= run_count())
    {
        return {};
    }
    if(b < a)
    {
        std::swap(a, b);
    }
    int s = plain_offset(a), e = plain_offset(b);
    return m_plain.substr(s, e - s);
}

void text_index::rects(text_pos a, text_pos b, std::vector<litehtml::position>& out) const
{
    if(!a.valid() || !b.valid() || a.run >= run_count() || b.run >= run_count())
    {
        return;
    }
    if(b < a)
    {
        std::swap(a, b);
    }
    for(int i = a.run; i <= b.run; i++)
    {
        const auto& r  = m_runs[i];
        float       x0 = i == a.run ? x_at(r, a.byte) : 0.0f;
        float       x1 = i == b.run ? x_at(r, b.byte) : r.w;
        if(x1 > x0)
        {
            out.emplace_back(r.x + x0, r.y, x1 - x0, r.h);
        }
    }
}

std::vector<text_range> text_index::find(const std::string& query) const
{
    std::vector<text_range> out;
    if(query.empty())
    {
        return out;
    }
    std::string hay = lower_ascii(m_plain);
    std::string q   = lower_ascii(query);
    for(size_t pos = hay.find(q); pos != std::string::npos; pos = hay.find(q, pos + q.size()))
    {
        out.emplace_back(from_plain(static_cast<int>(pos)), from_plain(static_cast<int>(pos + q.size())));
    }
    return out;
}
