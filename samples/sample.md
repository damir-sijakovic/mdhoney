# The mdhoney sample

A tour of everything **mdhoney** renders. Jump to [Tables](#tables), [Code](#code) or the
[second page](second.md), and come back with <kbd>Backspace</kbd>.

## Text

Plain text with **bold**, *italic*, ***bold italic***, ~~strikethrough~~, `inline code` and a
[link to md4c](https://github.com/mity/md4c). Autolinks work too: https://www.commonmark.org.

> Honey never spoils. Archaeologists have found pots of honey in ancient Egyptian tombs
> that are over 3,000 years old and still perfectly edible.
>
> — a well-known fact

Symbols and other scripts come from fallback fonts: → ⇒ ≈ ≠ ∑ ∞ ★ ♥ ✓ — 日本語のテキスト — 中文 — 한국어.

## Lists

1. Gather the frames
2. Uncap the comb
   - with a hot knife
   - or an uncapping fork
3. Spin, strain and jar

- [x] Bees fed
- [x] Hive inspected
- [ ] Honey harvested

## Tables

| Variety    | Colour        | Flavour              | Crystallises |
|------------|---------------|----------------------|:------------:|
| Acacia     | Very light    | Mild, vanilla        | Slowly       |
| Clover     | Light amber   | Sweet, floral        | Medium       |
| Buckwheat  | Dark          | Malty, molasses-like | Fast         |
| Manuka     | Dark cream    | Earthy, herbal       | Medium       |

## Code

```cpp
// Render Markdown with md4c, then lay it out with litehtml.
md_result page = markdown_to_html(markdown);
auto doc = litehtml::document::createFromString(html, &container);
doc->render(window_width);
doc->draw(0, 0, -scroll, &clip);
```

```sh
cmake -S . -B build && cmake --build build -j
./build/mdhoney samples/sample.md
```

## Images

![A jellyfish](images/jellyfish.jpg)

---

### Headings with the same name

### Headings with the same name

Duplicate headings get unique anchors (`#headings-with-the-same-name-1`), like on GitHub.
