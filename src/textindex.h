// Index of the text litehtml laid out, for text selection, copy and find in page.
//
// litehtml has no selection support, so this walks the element tree after layout and records
// every text piece (litehtml keeps one element per word and per space) with its position in
// document coordinates, its font and the block it belongs to.
#pragma once

#include <litehtml.h>

#include <string>
#include <utility>
#include <vector>

// A caret position: before byte 'byte' of run 'run'.
struct text_pos
{
    int run  = -1;
    int byte = 0;

    bool valid() const { return run >= 0; }
    bool operator==(const text_pos& o) const { return run == o.run && byte == o.byte; }
    bool operator!=(const text_pos& o) const { return !(*this == o); }
    bool operator<(const text_pos& o) const { return run < o.run || (run == o.run && byte < o.byte); }
};

using text_range = std::pair<text_pos, text_pos>;

class text_index
{
  public:
    void build(const litehtml::document::ptr& doc, litehtml::document_container* container);
    void clear();
    bool empty() const { return m_runs.empty(); }
    int  run_count() const { return static_cast<int>(m_runs.size()); }

    // Nearest caret position to a point in document coordinates.
    text_pos hit(float x, float y) const;
    // True if the point is over text (for the I-beam cursor).
    bool over_text(float x, float y) const;

    text_range word_at(const text_pos& p) const;  // the run (word) at p
    text_range block_at(const text_pos& p) const; // the paragraph/block at p
    text_range all() const;

    // Text between two positions (in any order), with line breaks between blocks.
    std::string text(text_pos a, text_pos b) const;
    // Highlight rectangles in document coordinates.
    void rects(text_pos a, text_pos b, std::vector<litehtml::position>& out) const;
    // All case-insensitive matches of query.
    std::vector<text_range> find(const std::string& query) const;

  private:
    struct run_t
    {
        std::string        text;
        litehtml::uint_ptr font;
        float              x, y, w, h;
        int                block;
        int                row; // table row id, -1 outside tables
        int                plain_start;
        // x offset of every UTF-8 character boundary, computed on first use
        mutable std::vector<int>   bounds;
        mutable std::vector<float> stops;
    };

    void  walk(const litehtml::element::ptr& el, bool pre);
    void  add_run(std::string text, litehtml::uint_ptr font, const litehtml::position& p);
    void  measure(const run_t& r) const;
    float x_at(const run_t& r, int byte) const;
    int   plain_offset(const text_pos& p) const;
    text_pos from_plain(int offset) const;

    litehtml::document_container* m_container = nullptr;
    std::vector<run_t>            m_runs;
    std::string                   m_plain; // whole document as text, for copy and find
    int                           m_block   = 0;
    int                           m_row     = -1;
    int                           m_counter = 0;
};
