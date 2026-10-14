// Markdown to HTML conversion (md4c, GitHub dialect) with reader-friendly post-processing.
#pragma once

#include <string>

struct md_result
{
    std::string html;  // HTML fragment for <body>
    std::string title; // text of the first <h1>, empty if there is none
};

// Converts Markdown to HTML. Headings get GitHub-style ids (for #anchor links) and task list
// checkboxes are replaced with ballot box glyphs, since litehtml does not draw form controls.
md_result markdown_to_html(const std::string& markdown);

// GitHub-style heading anchor: "Hello, World!" -> "hello-world".
std::string slugify(const std::string& text);
