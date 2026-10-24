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
#ifndef _BASE_WRITER_H_
#define _BASE_WRITER_H_

#include <string>
#include <cstdio>
#include <codecvt>
#include <locale>
#include <nrrd.h>
#include <FLIVR/TextureBrick.h>
#include "DLLExport.h"

using namespace std;

#ifdef STATIC_COMPILE
	#define nrrdWrap nrrdWrap_va
	#define nrrdAxisInfoSet nrrdAxisInfoSet_va
#endif

class EXPORT_API BaseWriter
{
public:
	//BaseWriter();
	virtual ~BaseWriter() {};

	virtual void SetData(const std::shared_ptr<FLIVR::VL_Nrrd> &data) = 0;
	virtual void SetSpacings(double spcx, double spcy, double spcz) = 0;
	virtual void SetCompression(bool value) = 0;
	virtual void Save(wstring filename, int mode) = 0;

	//nrrdSave from a wide name. teem opens files with a narrow fopen: on
	//Windows a name outside ASCII is opened here with _wfopen and written with
	//nrrdWrite (attached header: the writers set nrrdFormatNRRD); elsewhere
	//the narrow name is UTF-8. (A (char) cast of each UTF-16 unit wrote
	//nothing, or a stray file, for folders such as "Łukasz".)
	static int SaveNrrdFile(const wstring& filename, const Nrrd* nrrd, NrrdIoState* nio)
	{
		if (!nrrd || !nio)
			return 1;
#ifdef _WIN32
		bool ascii = true;
		for (wchar_t c : filename)
			if (c >= 0x80) { ascii = false; break; }
		if (!ascii)
		{
			FILE* fp = nullptr;
			if (_wfopen_s(&fp, filename.c_str(), L"wb") != 0 || !fp)
				return 1;
			if (nio->format == nrrdFormatUnknown)
				nio->format = nrrdFormatNRRD;
			const int ret = nrrdWrite(fp, nrrd, nio);
			fclose(fp);
			return ret;
		}
		return nrrdSave(string(filename.begin(), filename.end()).c_str(), nrrd, nio);
#else
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> conv;
		return nrrdSave(conv.to_bytes(filename).c_str(), nrrd, nio);
#endif
	}
};

#endif//_BASE_WRITER_H_