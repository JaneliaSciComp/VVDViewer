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

#ifndef _TEXTRENDERER_H_
#define _TEXTRENDERER_H_

//#include <glew.h>
#include <string>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <FLIVR/Color.h>
#include <TextureAtlas.h>
#include <Font.h>
#include <Vulkan2dRender.h>
#include "DLLExport.h"

using namespace std;
using namespace FLIVR;

class EXPORT_API TextRenderer
{
public:
	TextRenderer(const string &lib_name, std::shared_ptr<Vulkan2dRender> v2drender);
	~TextRenderer();

	void LoadNewFace(const string &lib_name, int size = 0);
	void SetSize(int size);
	unsigned int GetSize();

	void RenderText(const std::unique_ptr<vks::VFrameBuffer>& framebuf, 
		const wstring& text, Color &color, float x, float y);
	TextDimensions RenderTextDims(wstring& text);

private:
	static FT_Library m_ft;
	static bool m_init;

	bool m_valid;
	std::shared_ptr<FLFont> m_cur_font;
	std::string m_cur_libname;

	unsigned int m_size;

	std::shared_ptr<TextureAtlas> m_textureAtlas;
	std::map<std::string, std::shared_ptr<FLFont>> m_fonts;
	std::shared_ptr<Vulkan2dRender> m_v2drender;
};

#endif//_TEXTRENDERER_H_