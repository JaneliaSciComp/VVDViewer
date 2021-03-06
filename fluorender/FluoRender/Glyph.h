//
// Created by snorri on 10.10.2018.
//

#ifndef VULKAN_SPRITES_GLYPH_H
#define VULKAN_SPRITES_GLYPH_H

#include <ft2build.h>
#include FT_FREETYPE_H

#include <memory>

class AtlasTexture;
class TextureAtlas;

class Glyph {
public:
    Glyph(FT_Face face, FT_UInt ix, std::shared_ptr<TextureAtlas> ta);

    int GetLeft();
    int GetTop();
    int GetWidth();
    int GetHeight();
    int GetAdvance();

    std::shared_ptr<AtlasTexture> GetTexture();

protected:
    FT_Face m_face;
    FT_UInt m_glyphIndex;
    int m_left;
    int m_top;
    int m_width;
    int m_height;
    int m_advance;
    std::shared_ptr<AtlasTexture> m_texture;

    void CreateTextureFromBitmap(std::shared_ptr<TextureAtlas> &ta);
};


#endif //VULKAN_SPRITES_GLYPH_H
