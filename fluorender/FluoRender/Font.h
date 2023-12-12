//
// Created by snorri on 10.10.2018.
//

#ifndef VULKAN_SPRITES_FONT_H
#define VULKAN_SPRITES_FONT_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include "Glyph.h"

#include <memory>
#include <map>
#include <string>

class TextureAtlas;

struct TextDimensions {
    int width;
    int height;
};

class Renderer;
class FLFont {
public:
    FLFont(const FT_Face& face, std::shared_ptr<TextureAtlas> ta);

    int GetNumGlyphs();
    std::shared_ptr<Glyph> GetGlyph(wchar_t c);

    TextDimensions Measure(const std::wstring& text);

protected:
    FT_Face m_face;
    std::shared_ptr<TextureAtlas> m_textureAtlas;
    std::map<wchar_t, std::shared_ptr<Glyph>> m_glyphs;
};


#endif //VULKAN_SPRITES_FONT_H
