// Registers the user's system fonts (via fontconfig) with the litehtml container.
#pragma once

class container_nanovg;

// Registers sans-serif, serif and monospace in regular/bold/italic/bold-italic, plus fallback
// fonts for symbols and CJK. Returns false if no usable font was found.
bool register_system_fonts(container_nanovg& container);

// Releases fontconfig's caches. Call once at exit, after the fonts are no longer needed.
void release_system_fonts();
