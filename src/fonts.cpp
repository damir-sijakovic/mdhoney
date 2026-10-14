#include "fonts.h"

#include "container_nanovg.h"

#include <fontconfig/fontconfig.h>

#include <cctype>
#include <cstdio>
#include <set>
#include <string>

namespace
{
struct font_file
{
    std::string path;
    int         index  = 0;
    bool        bold   = false;
    bool        italic = false;
};

// stb_truetype reads TrueType and CFF-based OpenType, not bitmap or web fonts.
bool supported(const std::string& path)
{
    auto dot = path.find_last_of('.');
    if(dot == std::string::npos)
    {
        return false;
    }
    std::string ext = path.substr(dot);
    for(auto& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
}

bool match(const std::string& pattern, font_file& out)
{
    FcPattern* pat = FcNameParse(reinterpret_cast<const FcChar8*>(pattern.c_str()));
    if(!pat)
    {
        return false;
    }
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult   res;
    FcPattern* m  = FcFontMatch(nullptr, pat, &res);
    bool       ok = false;
    if(m)
    {
        FcChar8* file   = nullptr;
        int      index  = 0;
        int      weight = FC_WEIGHT_REGULAR;
        int      slant  = FC_SLANT_ROMAN;
        if(FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch && supported(reinterpret_cast<char*>(file)))
        {
            FcPatternGetInteger(m, FC_INDEX, 0, &index);
            FcPatternGetInteger(m, FC_WEIGHT, 0, &weight);
            FcPatternGetInteger(m, FC_SLANT, 0, &slant);
            out.path   = reinterpret_cast<char*>(file);
            out.index  = index;
            out.bold   = weight >= FC_WEIGHT_DEMIBOLD;
            out.italic = slant != FC_SLANT_ROMAN;
            ok         = true;
        }
        FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    return ok;
}
} // namespace

bool register_system_fonts(container_nanovg& container)
{
    if(!FcInit())
    {
        fprintf(stderr, "mdhoney: fontconfig init failed\n");
        return false;
    }

    bool                  any = false;
    std::set<std::string> used;
    for(const char* family : {"sans-serif", "serif", "monospace"})
    {
        for(int style = 0; style < 4; style++)
        {
            bool        bold    = style & 1;
            bool        italic  = style & 2;
            std::string pattern = std::string(family) + (bold ? ":weight=bold" : "") + (italic ? ":slant=italic" : "");
            font_file   f;
            if(!match(pattern, f))
            {
                continue;
            }
            // Only register a style when the matched face really has it. Otherwise the container
            // picks the closest face and fakes the italic.
            if(f.bold != bold || f.italic != italic)
            {
                continue;
            }
            if(container.add_font_face(family, bold, italic, f.path, f.index))
            {
                used.insert(f.path);
                any = true;
            }
        }
    }

    // Fallbacks for glyphs the main fonts lack: symbols (DejaVu has a large set) and CJK.
    for(const char* pattern : {"DejaVu Sans", "sans-serif:lang=ja", "sans-serif:lang=zh-cn", "sans-serif:lang=ko",
                               "Noto Sans Symbols2", "Noto Sans Math"})
    {
        font_file f;
        if(match(pattern, f) && used.insert(f.path + "#" + std::to_string(f.index)).second &&
           used.find(f.path) == used.end())
        {
            container.add_fallback_font(f.path, f.index);
        }
    }

    // Last resort: the font bundled with nanovgx.
    if(!any)
    {
        any = container.add_font_face("sans-serif", false, false, NANOVGX_EXAMPLE_DIR "/Roboto-Regular.ttf") &&
              container.add_font_face("sans-serif", true, false, NANOVGX_EXAMPLE_DIR "/Roboto-Bold.ttf");
    }
    return any;
}

void release_system_fonts()
{
    FcFini();
}
