#include <utility>

//
// Created by snorri on 10.10.2018.
//

#include "Font.h"
#include <algorithm>

FLFont::FLFont(const FT_Face& face, std::shared_ptr<TextureAtlas> ta) : m_face(face), m_textureAtlas(std::move(ta)) {

}

int FLFont::GetNumGlyphs() {
    return static_cast<int>(m_face->num_glyphs);
}

std::shared_ptr<Glyph> FLFont::GetGlyph(wchar_t c) {
    auto foundIt = m_glyphs.find(c);
    if(foundIt != m_glyphs.end()) {
        return foundIt->second;
    }

    auto glyphIndex = FT_Get_Char_Index(m_face, c);
    auto glyph = std::make_shared<Glyph>(m_face, glyphIndex, m_textureAtlas);

    m_glyphs[c] = glyph;
    return glyph;
}

TextDimensions FLFont::Measure(const std::wstring& text) {
    int width = 0;
    int height = 0;

    for(auto c: text) {
        auto glyph = GetGlyph(c);
        width += glyph->GetAdvance();
        height = std::max(height, glyph->GetTop() + glyph->GetHeight());
    }

    return TextDimensions{width, height};
}

