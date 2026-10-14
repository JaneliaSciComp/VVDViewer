#include "VVDString.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <locale>
#include <vector>

namespace vvd
{
	// UTF-8 <-> wchar_t (UTF-16 on Windows, UTF-32 elsewhere). Hand-rolled so
	// we do not depend on the deprecated <codecvt> header.
	std::wstring Utf8ToWide(const std::string& s)
	{
		std::wstring out;
		out.reserve(s.size());
		size_t i = 0, n = s.size();
		while (i < n)
		{
			unsigned char c = (unsigned char)s[i];
			uint32_t cp; int extra;
			if (c < 0x80) { cp = c; extra = 0; }
			else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
			else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
			else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
			else { cp = 0xFFFD; extra = 0; }
			if (i + extra >= n + (extra ? 0 : 1) && extra) { cp = 0xFFFD; extra = 0; }
			for (int k = 1; k <= extra; ++k)
			{
				unsigned char cc = (unsigned char)s[i + k];
				if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; extra = k - 1; break; }
				cp = (cp << 6) | (cc & 0x3F);
			}
			i += 1 + extra;
			if (sizeof(wchar_t) == 2 && cp > 0xFFFF)
			{
				cp -= 0x10000;
				out.push_back((wchar_t)(0xD800 + (cp >> 10)));
				out.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
			}
			else
				out.push_back((wchar_t)cp);
		}
		return out;
	}

	std::string WideToUtf8(const std::wstring& w)
	{
		std::string out;
		out.reserve(w.size() * 2);
		for (size_t i = 0; i < w.size(); ++i)
		{
			uint32_t cp = (uint32_t)w[i];
			if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size())
			{
				uint32_t lo = (uint32_t)w[i + 1];
				if (lo >= 0xDC00 && lo <= 0xDFFF)
				{
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					++i;
				}
			}
			if (cp < 0x80) out.push_back((char)cp);
			else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
			else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
			else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
		}
		return out;
	}

	String String::Mid(size_t first, size_t count) const
	{
		if (first >= size()) return String();
		return String(substr(first, count));
	}

	String String::SubString(size_t from, size_t to) const
	{
		if (from >= size() || to < from) return String();
		return String(substr(from, to - from + 1));
	}

	String String::Left(size_t count) const { return String(substr(0, std::min(count, size()))); }

	String String::Right(size_t count) const
	{
		if (count >= size()) return *this;
		return String(substr(size() - count));
	}

	String String::BeforeFirst(wchar_t ch) const
	{
		size_t p = find(ch);
		return p == npos ? *this : String(substr(0, p));
	}

	String String::BeforeLast(wchar_t ch) const
	{
		size_t p = rfind(ch);
		return p == npos ? String() : String(substr(0, p));
	}

	String String::AfterFirst(wchar_t ch) const
	{
		size_t p = find(ch);
		return p == npos ? String() : String(substr(p + 1));
	}

	String String::AfterLast(wchar_t ch) const
	{
		size_t p = rfind(ch);
		return p == npos ? *this : String(substr(p + 1));
	}

	String String::BeforeLast(wchar_t ch, String* rest) const
	{
		size_t p = rfind(ch);
		if (p == npos) { if (rest) *rest = *this; return String(); }
		if (rest) *rest = String(substr(p + 1));
		return String(substr(0, p));
	}

	String& String::operator<<(double v)
	{
		std::wostringstream os; os << v; append(os.str()); return *this;
	}

	size_t String::Find(wchar_t ch, bool fromEnd) const { return fromEnd ? rfind(ch) : find(ch); }
	size_t String::Find(const std::wstring& sub) const { return find(sub); }

	bool String::StartsWith(const std::wstring& prefix) const
	{
		return size() >= prefix.size() && compare(0, prefix.size(), prefix) == 0;
	}

	bool String::EndsWith(const std::wstring& suffix) const
	{
		return size() >= suffix.size() && compare(size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	String& String::MakeLower()
	{
		std::transform(begin(), end(), begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
		return *this;
	}

	String& String::MakeUpper()
	{
		std::transform(begin(), end(), begin(), [](wchar_t c) { return (wchar_t)std::towupper(c); });
		return *this;
	}

	String& String::Trim(bool fromRight)
	{
		auto isspace_w = [](wchar_t c) { return std::iswspace(c) != 0; };
		if (fromRight)
		{
			while (!empty() && isspace_w(back())) pop_back();
		}
		else
		{
			size_t i = 0;
			while (i < size() && isspace_w((*this)[i])) ++i;
			erase(0, i);
		}
		return *this;
	}

	size_t String::Replace(const std::wstring& oldStr, const std::wstring& newStr, bool replaceAll)
	{
		if (oldStr.empty()) return 0;
		size_t count = 0, pos = 0;
		while ((pos = find(oldStr, pos)) != npos)
		{
			std::wstring::replace(pos, oldStr.size(), newStr);
			pos += newStr.size();
			++count;
			if (!replaceAll) break;
		}
		return count;
	}

	bool String::IsSameAs(const std::wstring& other, bool caseSensitive) const
	{
		if (caseSensitive) return *this == other;
		return CmpNoCase(other) == 0;
	}

	int String::CmpNoCase(const std::wstring& other) const
	{
		String a(*this); a.MakeLower();
		String b(other);  b.MakeLower();
		return a.compare(b);
	}

	bool String::ToDouble(double* val) const
	{
		if (empty()) return false;
		wchar_t* end = nullptr;
		errno = 0;
		double v = std::wcstod(c_str(), &end);
		if (end == c_str() || errno == ERANGE) return false;
		while (*end && std::iswspace(*end)) ++end;
		if (*end) return false;
		if (val) *val = v;
		return true;
	}

	bool String::ToCDouble(double* val) const
	{
		if (empty()) return false;
		std::string narrow = WideToUtf8(*this);
		std::istringstream is(narrow);
		is.imbue(std::locale::classic());
		double v;
		is >> v;
		if (is.fail()) return false;
		is >> std::ws;
		if (!is.eof()) return false;
		if (val) *val = v;
		return true;
	}

	bool String::ToLong(long* val, int base) const
	{
		if (empty()) return false;
		wchar_t* end = nullptr;
		errno = 0;
		long v = std::wcstol(c_str(), &end, base);
		if (end == c_str() || errno == ERANGE) return false;
		while (*end && std::iswspace(*end)) ++end;
		if (*end) return false;
		if (val) *val = v;
		return true;
	}

	bool String::ToULong(unsigned long* val, int base) const
	{
		if (empty()) return false;
		wchar_t* end = nullptr;
		errno = 0;
		unsigned long v = std::wcstoul(c_str(), &end, base);
		if (end == c_str() || errno == ERANGE) return false;
		while (*end && std::iswspace(*end)) ++end;
		if (*end) return false;
		if (val) *val = v;
		return true;
	}

	bool String::ToInt(int* val) const
	{
		long l;
		if (!ToLong(&l)) return false;
		if (val) *val = (int)l;
		return true;
	}

	String String::FormatV(const wchar_t* fmt, va_list ap)
	{
		// vswprintf has no "query the size" mode; grow the buffer until it fits.
		std::vector<wchar_t> buf(256);
		for (;;)
		{
			va_list ap2;
			va_copy(ap2, ap);
			int n = std::vswprintf(buf.data(), buf.size(), fmt, ap2);
			va_end(ap2);
			if (n >= 0 && (size_t)n < buf.size())
				return String(std::wstring(buf.data(), (size_t)n));
			if (buf.size() > (1u << 20)) return String();
			buf.resize(buf.size() * 2);
		}
	}

	String String::Format(const wchar_t* fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		String s = FormatV(fmt, ap);
		va_end(ap);
		return s;
	}

	String String::Format(const char* fmt, ...)
	{
		// Narrow format strings: format into a narrow buffer, then widen (UTF-8).
		std::vector<char> buf(256);
		for (;;)
		{
			va_list ap;
			va_start(ap, fmt);
			int n = std::vsnprintf(buf.data(), buf.size(), fmt, ap);
			va_end(ap);
			if (n < 0) return String();
			if ((size_t)n < buf.size())
				return String(std::string(buf.data(), (size_t)n));
			buf.resize((size_t)n + 1);
		}
	}

	std::vector<String> String::Split(wchar_t delim, bool keepEmpty) const
	{
		std::vector<String> out;
		std::wstring cur;
		for (wchar_t c : *this)
		{
			if (c == delim)
			{
				if (keepEmpty || !cur.empty()) out.push_back(String(cur));
				cur.clear();
			}
			else cur.push_back(c);
		}
		if (keepEmpty || !cur.empty()) out.push_back(String(cur));
		return out;
	}

	bool ReadTextLines(const String& path, std::vector<String>& lines)
	{
#ifdef _WIN32
		std::ifstream in(path.c_str(), std::ios::binary);
#else
		std::ifstream in(WideToUtf8(path), std::ios::binary);
#endif
		if (!in) return false;
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			lines.push_back(String(line));
		}
		return true;
	}

	bool WriteTextLines(const String& path, const std::vector<String>& lines)
	{
#ifdef _WIN32
		std::ofstream out(path.c_str(), std::ios::binary);
#else
		std::ofstream out(WideToUtf8(path), std::ios::binary);
#endif
		if (!out) return false;
		for (const String& l : lines)
			out << WideToUtf8(l) << '\n';
		return true;
	}
}
