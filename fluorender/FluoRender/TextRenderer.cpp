/*
For more information, please see: http://software.sci.utah.edu

The MIT License

Copyright (c) 2014 Scientific Computing and Imaging Institute,
University of Utah.


Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "TextRenderer.h"
#include <FLIVR/ShaderProgram.h>
#include <sstream>

bool TextRenderer::m_init = false;
FT_Library TextRenderer::m_ft;

TextRenderer::TextRenderer(const string &lib_name, std::shared_ptr<Vulkan2dRender> v2drender)
	: m_valid(false),
	m_size(14)
{
	m_v2drender = v2drender;

	m_textureAtlas = std::make_shared<TextureAtlas>();
	m_textureAtlas->Initialize(m_v2drender->m_vulkan->vulkanDevice, 1024, 1024);

	FT_Error err;
	if (!m_init)
	{
		err = FT_Init_FreeType(&m_ft);
		if (!err)
			m_init = true;
	}

	if (!m_init) return;

	FT_Face face;
	err = FT_New_Face(m_ft, lib_name.c_str(), 0, &face);
	if (!err)
		m_valid = true;

	if (m_valid)
	{
		err = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
		err = FT_Set_Pixel_Sizes(face, 0, m_size);
		m_cur_font = std::make_shared<FLFont>(face, m_textureAtlas);
		std::stringstream ss;
		ss << lib_name.c_str() << "?" << m_size;
		m_fonts[ss.str()] = m_cur_font;
		m_cur_libname = lib_name.c_str();
	}
}

TextRenderer::~TextRenderer()
{

}

void TextRenderer::LoadNewFace(const string &lib_name, int size)
{
	FT_Error err;
	if (!m_init)
	{
		err = FT_Init_FreeType(&m_ft);
		if (!err)
			m_init = true;
	}

	if (!m_init) return;

	if (size > 0)
		m_size = size;

	std::stringstream ss;
	ss << lib_name.c_str() << "?" << m_size;

	auto foundIt = m_fonts.find(ss.str());
	if (foundIt != m_fonts.end()) {
		m_cur_font = foundIt->second;
		m_cur_libname = lib_name.c_str();
		return;
	}

	FT_Face face;
	err = FT_New_Face(m_ft, lib_name.c_str(), 0, &face);
	if (!err)
		m_valid = true;

	if (m_valid)
	{
		err = FT_Select_Charmap(face, FT_ENCODING_UNICODE); 
		err = FT_Set_Pixel_Sizes(face, 0, m_size);
		m_cur_font = std::make_shared<FLFont>(face, m_textureAtlas);
		m_fonts[ss.str()] = m_cur_font;
		m_cur_libname = lib_name.c_str();
	}
}

void TextRenderer::SetSize(int size)
{
	if (!m_valid || size <= 0)
		return;

	m_size = size;
	LoadNewFace(m_cur_libname);
}

unsigned int TextRenderer::GetSize()
{
	if (!m_valid)
		return 0;
	else
		return m_size;
}

void TextRenderer::RenderText(const std::unique_ptr<vks::VFrameBuffer>& framebuf,
	const wstring& text, Color& color, float x, float y)
{
	if (!m_valid)
		return;
    if (text.empty())
        return;
	
	vks::VulkanDevice* prim_dev = m_v2drender->m_vulkan->vulkanDevice;
	TextDimensions dims = m_cur_font->Measure(text);

	int w = framebuf->w;
	int h = framebuf->h;

	std::vector<Vulkan2dRender::V2DRenderParams> v2drender_params;
	Vulkan2dRender::V2dPipeline pl =
		m_v2drender->preparePipeline(
			IMG_SHDR_TEXT,
			V2DRENDER_BLEND_OVER_UI,
			framebuf->attachments[0]->format,
			framebuf->attachments.size(),
			0,
			framebuf->attachments[0]->is_swapchain_images);

	float pos = x;
	for (auto c : text) {
		auto glyph = m_cur_font->GetGlyph(c);
		auto texture = glyph->GetTexture();
		if (texture) {

			float gw = (float)glyph->GetWidth() * 2.0f / w;
			float gh = (float)glyph->GetHeight() * 2.0f / h;
			float x0 = pos + (float)glyph->GetLeft() * 2.0f / w;
			float y0 = y - (float)glyph->GetTop() * 2.0f / h;

			TextureWindow tw = texture->GetTextureWindow();

			Vulkan2dRender::V2DRenderParams param;
			param.pipeline = pl;
			param.clear = false;
			param.tex[0] = m_textureAtlas->m_tex.get();
			param.loc[0] = glm::vec4(gw, gh, tw.x1-tw.x0, tw.y1-tw.y0);
			param.loc[1] = glm::vec4(x0, y0, tw.x0, tw.y0);
			param.loc[2] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), 1.0f);
			v2drender_params.push_back(param);
		}
		pos += (float)glyph->GetAdvance() * 2.0f / w;
	}

	vks::VulkanSemaphoreSettings sem = prim_dev->GetNextRenderSemaphoreSettings();
	v2drender_params[0].waitSemaphores = sem.waitSemaphores;
	v2drender_params[0].waitSemaphoreCount = sem.waitSemaphoreCount;
	v2drender_params[0].signalSemaphores = sem.signalSemaphores;
	v2drender_params[0].signalSemaphoreCount = sem.signalSemaphoreCount;

	if (!framebuf->renderPass)
		framebuf->replaceRenderPass(pl.pass);

	m_v2drender->seq_render(framebuf, v2drender_params.data(), v2drender_params.size());
}

TextDimensions TextRenderer::RenderTextDims(wstring& text)
{
	return m_cur_font->Measure(text);
}
