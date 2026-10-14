# mdhoney 🍯

A small, fast Markdown reader for Linux.

Markdown is converted to HTML by [md4c](https://github.com/mity/md4c), laid out by
[litehtml](https://github.com/litehtml/litehtml), and drawn by
[NanoVGX](third_party/nanovgx) with OpenGL ES 3. NanoVGX is a fork of
[NanoVG](https://github.com/memononen/nanovg) with multi-stop gradients, a grayscale mode and
leak fixes.

## Build

You need CMake, a C++17 compiler, and the development packages for GLFW, OpenGL ES and
fontconfig. On Debian or Ubuntu:

```sh
sudo apt install cmake g++ libglfw3-dev libgles-dev libfontconfig-dev
```

Then:

```sh
cmake -S . -B build && cmake --build build -j
./build/mdhoney samples/sample.md
```

All other dependencies are included in `third_party/`, so a fresh clone builds as is.

## Usage

```
mdhoney [file.md[#heading]] [--dark] [--grayscale] [--serif] [--zoom F] [--find TEXT]
        [--size WxH] [--scroll PX] [--png out.png]
```

With no file, mdhoney shows a welcome page with the key list. You can also drop a file onto the
window. `file.md#heading` opens the file at that heading. `--png` renders one frame to an image
and exits, which is handy for scripts and tests.

| Key | Action |
|---|---|
| Space, PgDn / Shift+Space, PgUp | Page down / up |
| ↓ `j` / ↑ `k`, mouse wheel | Scroll (smooth) |
| Home `g` / End `G` | Top / bottom |
| Backspace, Alt+← / Alt+→, mouse back/forward buttons | History back / forward |
| Ctrl+F (or F3), then Enter / Shift+Enter | Find in page, next / previous match; Esc closes |
| Drag / double click / triple click / Shift+click | Select text / a word / a paragraph / extend |
| Ctrl+C / Ctrl+A | Copy selection / select all |
| Right click | Menu: Light, Dark, Grayscale |
| Ctrl + / Ctrl − / Ctrl 0, Ctrl+wheel | Zoom in / out / reset |
| `t` | Light / dark theme |
| `f` | Sans / serif body font |
| `r` | Reload |
| `q` | Quit |
| Esc | Close the menu or find bar, clear the selection, or quit |

You can also drag the scrollbar.

## Features

- **Markdown:** CommonMark plus GitHub extensions: tables (with column alignment), task lists,
  strikethrough and autolinks. Inline HTML such as `<kbd>` and `<mark>` works too.
- **Links:**
  - `#heading` links jump within the page. Headings get GitHub-style ids.
  - Relative links to `.md` files open in mdhoney, with back/forward history.
  - Web links and other files open with `xdg-open`.
- **Live reload:** the file reloads when it changes on disk, keeping your scroll position. You
  can keep it open next to your editor.
- **Themes:** "honey" light and dark themes, a centred reading column, and zoom that keeps
  your place.
- **Menu:** right-click opens a small menu with Light, Dark and a Grayscale checkbox.
  Grayscale is done in the NanoVGX shader, so images turn grey too.
- **Selection and copy:**
  - Drag to select. When you drag past the top or bottom edge, the page scrolls.
  - Double-click selects a word, triple-click a paragraph, and Shift+click extends the
    selection.
  - Copied text keeps the page structure: wrapped lines join with spaces, paragraphs and code
    lines are separate lines, and table cells are separated by tabs.
  - A plain click still follows links. A drag that starts on a link selects text instead.
- **Find in page:** case-insensitive, and the search updates as you type. All matches are
  highlighted, the current one in orange, with an "n / m" counter. Text you've selected becomes
  the starting search.
- **Fonts:** your system fonts through fontconfig, with automatic fallback fonts for symbols
  and CJK text. Monitor scaling is taken into account.
- **Images:** local images, with paths relative to the Markdown file.

## Project layout

```
src/main.cpp              window, input, navigation, history, live reload, scrollbar, menu, find bar
src/textindex.cpp         text runs from the litehtml layout: hit testing, selection, copy, search
src/container_nanovg.cpp  litehtml drawing container: fonts, text, boxes, borders, images, gradients
src/markdown.cpp          md4c -> HTML, heading ids, task list boxes
src/fonts.cpp             fontconfig font discovery
src/theme.h               light and dark stylesheets
samples/                  sample documents
third_party/md4c          md4c 0.5.2, unmodified
third_party/litehtml      litehtml, unmodified
third_party/nanovgx       NanoVGX, our NanoVG fork (see its README)
```

## How links are handled

1. **Click:** litehtml works out which link was clicked and calls the container's
   `on_anchor_click`, which passes the `href` to mdhoney.
2. **Click or drag:** a plain click follows the link. A drag that starts on a link selects text
   instead, and the click is cancelled.
3. **Deferred:** the link is stored and handled on the next pass of the main loop, not inside
   litehtml's click handler. Handling it immediately would replace the page while litehtml is
   still processing the click.
4. **By type** (`handle_link` in `src/main.cpp`):
   - `#heading` scrolls to the element with that id (or the heading whose anchor matches) and
     adds a history entry, so Backspace returns you.
   - `http://`, `https://` and `mailto:` open in your default app through `xdg-open`. It's
     started directly, not through a shell, so characters in a URL can't run commands.
   - Relative paths and `file://` links to Markdown files (`.md`, `.markdown`, `.mdown`, `.mkd`,
     `.txt`) open in mdhoney, relative to the current file, with back/forward history. A
     `#heading` part jumps to that heading, and `%20`-style escapes are decoded.
   - Any other local file opens with `xdg-open`.
5. **Files dropped** on the window go through the same path as a `file://` link.

**Caution:** the last rule means a Markdown file can link to a local script or `.desktop`
file. Clicking it hands that file to your desktop's default app, which on some desktops can run
it. Only one click is needed, so be careful with links in Markdown from untrusted sources.

## How selection works

litehtml lays out text but has no notion of selection. After each layout, mdhoney walks
litehtml's element tree in reading order. litehtml keeps one element per word and per space, so
each one is recorded with its position, font and enclosing block. The selection highlight, the
copied text and find-in-page all use that index. Character positions inside a word are measured
with the same font NanoVGX draws it with.

## Not supported yet

- **Syntax highlighting in code blocks.**
- **Remote images:** `http(s)` images aren't downloaded.
- **Colour emoji:** the font rasteriser (stb_truetype) can't draw colour glyphs.
- **Complex scripts:** no text shaping, so Arabic and Indic scripts don't render correctly.
- **Wide code lines** wrap instead of scrolling sideways.

## Licences

mdhoney includes these third-party libraries, each under its own licence:

| Library | Licence | File |
|---|---|---|
| md4c | MIT | `third_party/md4c/LICENSE.md` |
| litehtml | BSD 3-clause | `third_party/litehtml/LICENSE` |
| NanoVG / NanoVGX | zlib (NanoVGX is marked as an altered version) | `third_party/nanovgx/LICENSE.txt` |
| Roboto fonts (fallback only) | SIL Open Font License | `third_party/nanovgx/example/LICENSE_OFL.txt` |
