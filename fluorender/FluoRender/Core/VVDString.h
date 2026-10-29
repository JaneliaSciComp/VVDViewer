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
#ifndef VVD_CORE_STRING_H
#define VVD_CORE_STRING_H

// vvd::String -- the toolkit-neutral string used by the engine and data layer.
//
// It IS a std::wstring (public inheritance, all constructors inherited), so
// every std::wstring API works and it can be passed wherever a std::wstring
// is expected. On top of that it offers the small subset of the wxString API
// the code base historically used (Mid, Find, MakeLower, Format, ...), which
// keeps the port mechanical and the call sites readable.
//
// GUI interop: when a GUI toolkit build defines VVD_WX_STRING_INTEROP the
// class gains an implicit constructor from wxString, so wx GUI code can pass
// wxString into engine APIs unchanged. Converting back (String -> wxString)
// needs nothing: wxString already has a constructor taking std::wstring.
// A Qt build gets the same courtesy with VVD_QT_STRING_INTEROP (QString).
//
// The engine itself must never define either macro; that is what keeps it
// free of toolkit headers.

#include <string>
#include <vector>
#include <cstdarg>
#include <type_traits>

#if defined(VVD_WX_STRING_INTEROP)
#include <wx/string.h>
#include <wx/arrstr.h>
#endif
#if defined(VVD_QT_STRING_INTEROP)
#include <QString>
#include <QStringList>
#endif

#include "../DLLExport.h"

namespace vvd
{
	EXPORT_API std::wstring Utf8ToWide(const std::string& utf8);
	EXPORT_API std::string  WideToUtf8(const std::wstring& wide);

	class EXPORT_API String : public std::wstring
	{
	public:
		using std::wstring::npos;

		String() = default;
		String(const String&) = default;
		String(String&&) noexcept = default;
		String& operator=(const String&) = default;
		String& operator=(String&&) noexcept = default;

		String(const std::wstring& s) : std::wstring(s) {}
		String(std::wstring&& s) noexcept : std::wstring(std::move(s)) {}
		String(const wchar_t* s) : std::wstring(s ? s : L"") {}
		String(const wchar_t* s, size_t n) : std::wstring(s, n) {}
		String(size_t n, wchar_t c) : std::wstring(n, c) {}
		String(wchar_t c) : std::wstring(1, c) {}
		// narrow strings are interpreted as UTF-8
		String(const std::string& utf8) : std::wstring(Utf8ToWide(utf8)) {}
		String(const char* utf8) : std::wstring(utf8 ? Utf8ToWide(utf8) : std::wstring()) {}

#if defined(VVD_WX_STRING_INTEROP)
		String(const wxString& s) : std::wstring(s.ToStdWstring()) {}
		String& operator=(const wxString& s) { std::wstring::operator=(s.ToStdWstring()); return *this; }
		String& operator+=(const wxString& s) { std::wstring::operator+=(s.ToStdWstring()); return *this; }
		wxString ToWx() const { return wxString(c_str()); }
#endif
#if defined(VVD_QT_STRING_INTEROP)
		String(const QString& s) : std::wstring(s.toStdWString()) {}
		QString ToQt() const { return QString::fromStdWString(*this); }
#endif

		String& operator=(const std::wstring& s) { std::wstring::operator=(s); return *this; }
		String& operator=(const wchar_t* s) { std::wstring::operator=(s ? s : L""); return *this; }
		String& operator=(const std::string& utf8) { std::wstring::operator=(Utf8ToWide(utf8)); return *this; }
		String& operator=(const char* utf8) { std::wstring::operator=(utf8 ? Utf8ToWide(utf8) : std::wstring()); return *this; }

		using std::wstring::operator+=;
		String& operator+=(const char* utf8) { if (utf8) std::wstring::operator+=(Utf8ToWide(utf8)); return *this; }
		String& operator+=(const std::string& utf8) { std::wstring::operator+=(Utf8ToWide(utf8)); return *this; }

		// ---- conversions -------------------------------------------------
		const std::wstring& ToStdWstring() const { return *this; }
		std::string ToStdString() const { return WideToUtf8(*this); }   // UTF-8
		// implicit narrow (UTF-8) conversion: wxString offered "operator const
		// char*", and a fair amount of GUI code assigns names into std::string.
		operator std::string() const { return WideToUtf8(*this); }
		std::string ToUTF8() const { return WideToUtf8(*this); }
		const wchar_t* wc_str() const { return c_str(); }

		// ---- wxString-compatible helpers ----------------------------------
		bool IsEmpty() const { return empty(); }
		size_t Len() const { return length(); }
		size_t Length() const { return length(); }
		void Clear() { clear(); }

		// Mid(first[, count]) : substring, clamped like wxString
		String Mid(size_t first, size_t count = npos) const;
		// SubString(from, to) : inclusive range, like wxString
		String SubString(size_t from, size_t to) const;
		String Left(size_t count) const;
		String Right(size_t count) const;
		String BeforeFirst(wchar_t ch) const;
		String BeforeLast(wchar_t ch) const;
		String AfterFirst(wchar_t ch) const;
		String AfterLast(wchar_t ch) const;

		// Find: returns index or npos (wxNOT_FOUND == -1 in wx; use npos here).
		size_t Find(wchar_t ch, bool fromEnd = false) const;
		size_t Find(const std::wstring& sub) const;
		size_t Find(const char* utf8) const { return find(Utf8ToWide(utf8 ? utf8 : "")); }
		size_t Find(const std::string& utf8) const { return find(Utf8ToWide(utf8)); }
		bool Contains(const std::wstring& sub) const { return find(sub) != npos; }
		bool StartsWith(const std::wstring& prefix) const;
		bool EndsWith(const std::wstring& suffix) const;
		bool StartsWith(wchar_t c) const { return !empty() && front() == c; }
		bool EndsWith(wchar_t c) const { return !empty() && back() == c; }
		// wxString::BeforeLast(ch, &rest): rest receives the part after ch
		String BeforeLast(wchar_t ch, String* rest) const;
		String& Prepend(const std::wstring& s) { insert(0, s); return *this; }
		String& Prepend(wchar_t c) { insert(begin(), c); return *this; }
		String& Append(const std::wstring& s) { append(s); return *this; }
		String& Append(wchar_t c) { push_back(c); return *this; }
		wchar_t Last() const { return empty() ? L'\0' : back(); }
		wchar_t GetChar(size_t i) const { return (*this)[i]; }
		String& Truncate(size_t len) { if (len < size()) resize(len); return *this; }

		// std::wstring::compare with narrow (UTF-8) argument
		using std::wstring::compare;
		int compare(const char* utf8) const { return std::wstring::compare(Utf8ToWide(utf8 ? utf8 : "")); }
		int compare(const std::string& utf8) const { return std::wstring::compare(Utf8ToWide(utf8)); }
		int Cmp(const std::wstring& other) const { return std::wstring::compare(other); }

		// wxString-style stream append (str << x << y)
		String& operator<<(const std::wstring& s) { append(s); return *this; }
		String& operator<<(const wchar_t* s) { if (s) append(s); return *this; }
		String& operator<<(const std::string& utf8) { append(Utf8ToWide(utf8)); return *this; }
		String& operator<<(const char* utf8) { if (utf8) append(Utf8ToWide(utf8)); return *this; }
		String& operator<<(wchar_t c) { push_back(c); return *this; }
		String& operator<<(int v) { append(std::to_wstring(v)); return *this; }
		String& operator<<(unsigned int v) { append(std::to_wstring(v)); return *this; }
		String& operator<<(long v) { append(std::to_wstring(v)); return *this; }
		String& operator<<(unsigned long v) { append(std::to_wstring(v)); return *this; }
		String& operator<<(long long v) { append(std::to_wstring(v)); return *this; }
		String& operator<<(unsigned long long v) { append(std::to_wstring(v)); return *this; }
		String& operator<<(double v);

		String& MakeLower();
		String& MakeUpper();
		String Lower() const { String s(*this); s.MakeLower(); return s; }
		String Upper() const { String s(*this); s.MakeUpper(); return s; }
		String& Trim(bool fromRight = true);
		size_t Replace(const std::wstring& oldStr, const std::wstring& newStr, bool replaceAll = true);
		bool IsSameAs(const std::wstring& other, bool caseSensitive = true) const;
		int CmpNoCase(const std::wstring& other) const;

		bool ToDouble(double* val) const;    // current locale (like wxString::ToDouble)
		bool ToCDouble(double* val) const;   // "C" locale   (like wxString::ToCDouble)
		bool ToLong(long* val, int base = 10) const;
		bool ToULong(unsigned long* val, int base = 10) const;
		bool ToInt(int* val) const;

		// printf-style formatting. Prefer the narrow-format overload: with a
		// wide format string, "%s" means char* on glibc but wchar_t* on MSVC
		// (use "%ls" for wchar_t* portably).
		static String Format(const wchar_t* fmt, ...);
		static String Format(const char* fmt, ...);
		static String FormatV(const wchar_t* fmt, va_list ap);

		// Split on a delimiter (like wxStringTokenizer with default mode)
		std::vector<String> Split(wchar_t delim, bool keepEmpty = false) const;
	};

	// Concatenation / comparison helpers with literals, mirroring wxString.
	// The pointer overloads are templates constrained to genuine char pointers
	// and arrays, so that a wxString (which converts implicitly to const char*
	// AND const wchar_t*) never matches them -- otherwise "wxString + String"
	// would be ambiguous in the wx GUI build.
	namespace detail
	{
		template<class T> struct is_narrow_cstr : std::false_type {};
		template<> struct is_narrow_cstr<const char*> : std::true_type {};
		template<> struct is_narrow_cstr<char*> : std::true_type {};
		template<size_t N> struct is_narrow_cstr<const char[N]> : std::true_type {};
		template<size_t N> struct is_narrow_cstr<char[N]> : std::true_type {};
		template<class T> struct is_wide_cstr : std::false_type {};
		template<> struct is_wide_cstr<const wchar_t*> : std::true_type {};
		template<> struct is_wide_cstr<wchar_t*> : std::true_type {};
		template<size_t N> struct is_wide_cstr<const wchar_t[N]> : std::true_type {};
		template<size_t N> struct is_wide_cstr<wchar_t[N]> : std::true_type {};
		template<class T> using narrow_t = typename std::enable_if<is_narrow_cstr<typename std::remove_reference<T>::type>::value, int>::type;
		template<class T> using wide_t = typename std::enable_if<is_wide_cstr<typename std::remove_reference<T>::type>::value, int>::type;
	}

	namespace detail
	{
		// exactly vvd::String / std::wstring / std::string (no implicit conversions from other classes)
		template<class T> struct is_str : std::false_type {};
		template<> struct is_str<String> : std::true_type {};
		template<> struct is_str<std::wstring> : std::true_type {};
		template<class T> struct is_nstr : std::false_type {};
		template<> struct is_nstr<std::string> : std::true_type {};
		template<class T> using bare = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
		template<class A, class B> using str_str_t = typename std::enable_if<
			is_str<bare<A>>::value && is_str<bare<B>>::value && (std::is_same<bare<A>, String>::value || std::is_same<bare<B>, String>::value), int>::type;
		template<class A, class B> using str_nstr_t = typename std::enable_if<std::is_same<bare<A>, String>::value && is_nstr<bare<B>>::value, int>::type;
		template<class A, class B> using nstr_str_t = typename std::enable_if<is_nstr<bare<A>>::value && std::is_same<bare<B>, String>::value, int>::type;
		template<class A> using str_t = typename std::enable_if<std::is_same<bare<A>, String>::value, int>::type;
	}

	// Forwarding references: an rvalue std::wstring operand then binds exactly,
	// as it does to libc++'s operator+(basic_string&&, ...), so these win on the
	// String operand instead of tying (clang reported the tie as ambiguous).
	template<class A, class B, detail::str_str_t<A, B> = 0> String operator+(A&& a, B&& b) { return String(static_cast<const std::wstring&>(a) + static_cast<const std::wstring&>(b)); }
	template<class A, class B, detail::str_nstr_t<A, B> = 0> String operator+(A&& a, const B& b) { return String(static_cast<const std::wstring&>(a) + Utf8ToWide(b)); }
	template<class A, class B, detail::nstr_str_t<A, B> = 0> String operator+(const A& a, B&& b) { return String(Utf8ToWide(a) + static_cast<const std::wstring&>(b)); }
	template<class A, class C, detail::str_t<A> = 0, detail::narrow_t<C> = 0> String operator+(const A& a, const C& b) { String s(a); s += (const char*)b; return s; }
	template<class C, class B, detail::narrow_t<C> = 0, detail::str_t<B> = 0> String operator+(const C& a, const B& b) { String s((const char*)a); s += b; return s; }
	template<class A, class C, detail::str_t<A> = 0, detail::wide_t<C> = 0> String operator+(const A& a, const C& b) { return String(static_cast<const std::wstring&>(a) + (const wchar_t*)b); }
	template<class C, class B, detail::wide_t<C> = 0, detail::str_t<B> = 0> String operator+(const C& a, const B& b) { return String(std::wstring((const wchar_t*)a) + static_cast<const std::wstring&>(b)); }
	template<class A, detail::str_t<A> = 0> String operator+(const A& a, wchar_t b) { String s(a); s.push_back(b); return s; }
	template<class B, detail::str_t<B> = 0> String operator+(wchar_t a, const B& b) { String s(1, a); s += b; return s; }

	inline bool operator==(const String& a, const String& b) { return static_cast<const std::wstring&>(a) == static_cast<const std::wstring&>(b); }
	inline bool operator!=(const String& a, const String& b) { return !(a == b); }
	inline bool operator==(const String& a, const std::wstring& b) { return static_cast<const std::wstring&>(a) == b; }
	inline bool operator==(const std::wstring& a, const String& b) { return a == static_cast<const std::wstring&>(b); }
	inline bool operator!=(const String& a, const std::wstring& b) { return !(a == b); }
	inline bool operator!=(const std::wstring& a, const String& b) { return !(a == b); }
	inline bool operator<(const String& a, const String& b) { return static_cast<const std::wstring&>(a) < static_cast<const std::wstring&>(b); }
	template<class A, class C, detail::str_t<A> = 0, detail::wide_t<C> = 0> bool operator==(const A& a, const C& b) { return static_cast<const std::wstring&>(a) == (const wchar_t*)b; }
	template<class A, class C, detail::str_t<A> = 0, detail::wide_t<C> = 0> bool operator!=(const A& a, const C& b) { return !(a == b); }
	template<class A, class C, detail::str_t<A> = 0, detail::narrow_t<C> = 0> bool operator==(const A& a, const C& b) { return static_cast<const std::wstring&>(a) == Utf8ToWide((const char*)b); }
	template<class A, class C, detail::str_t<A> = 0, detail::narrow_t<C> = 0> bool operator!=(const A& a, const C& b) { return !(a == b); }
	template<class A, class B, detail::str_nstr_t<A, B> = 0> bool operator==(const A& a, const B& b) { return static_cast<const std::wstring&>(a) == Utf8ToWide(b); }
	template<class A, class B, detail::str_nstr_t<A, B> = 0> bool operator!=(const A& a, const B& b) { return !(a == b); }

#if defined(VVD_WX_STRING_INTEROP)
	// Exact-match overloads for mixed wxString / vvd::String expressions. Both
	// classes convert implicitly to the other, which would otherwise make the
	// plain operators ambiguous.
	template<class W> using wx_t = typename std::enable_if<std::is_same<typename std::remove_cv<typename std::remove_reference<W>::type>::type, wxString>::value, int>::type;
	template<class W, wx_t<W> = 0> String operator+(const String& a, const W& b) { return String(static_cast<const std::wstring&>(a) + b.ToStdWstring()); }
	template<class W, wx_t<W> = 0> String operator+(const W& a, const String& b) { return String(a.ToStdWstring() + static_cast<const std::wstring&>(b)); }
	template<class W, wx_t<W> = 0> bool operator==(const String& a, const W& b) { return static_cast<const std::wstring&>(a) == b.ToStdWstring(); }
	template<class W, wx_t<W> = 0> bool operator==(const W& a, const String& b) { return b == a; }
	template<class W, wx_t<W> = 0> bool operator!=(const String& a, const W& b) { return !(a == b); }
	template<class W, wx_t<W> = 0> bool operator!=(const W& a, const String& b) { return !(b == a); }
	// wxConfigBase::Write(key, T) calls wxToString(T) via ADL.
	inline wxString wxToString(const String& s) { return wxString(s.c_str()); }
	inline bool wxFromString(const wxString& s, String* out) { if (!out) return false; *out = s; return true; }
#endif
#if defined(VVD_QT_STRING_INTEROP)
	template<class Q> using qt_t = typename std::enable_if<std::is_same<typename std::remove_cv<typename std::remove_reference<Q>::type>::type, QString>::value, int>::type;
	template<class Q, qt_t<Q> = 0> String operator+(const String& a, const Q& b) { return String(static_cast<const std::wstring&>(a) + b.toStdWString()); }
	template<class Q, qt_t<Q> = 0> String operator+(const Q& a, const String& b) { return String(a.toStdWString() + static_cast<const std::wstring&>(b)); }
	template<class Q, qt_t<Q> = 0> bool operator==(const String& a, const Q& b) { return static_cast<const std::wstring&>(a) == b.toStdWString(); }
	template<class Q, qt_t<Q> = 0> bool operator==(const Q& a, const String& b) { return b == a; }
	template<class Q, qt_t<Q> = 0> bool operator!=(const String& a, const Q& b) { return !(a == b); }
	template<class Q, qt_t<Q> = 0> bool operator!=(const Q& a, const String& b) { return !(b == a); }
#endif

	// wxArrayString replacement: a std::vector<String> with the handful of
	// wx-style accessors the code base uses.
	class EXPORT_API StringArray : public std::vector<String>
	{
	public:
		using std::vector<String>::vector;
		StringArray() = default;
		StringArray(const std::vector<String>& v) : std::vector<String>(v) {}
		StringArray(const std::vector<std::wstring>& v) : std::vector<String>(v.begin(), v.end()) {}
#if defined(VVD_WX_STRING_INTEROP)
		StringArray(const wxArrayString& a) { reserve(a.size()); for (size_t i = 0; i < a.size(); ++i) push_back(String(a[i])); }
		wxArrayString ToWx() const { wxArrayString a; for (const String& s : *this) a.Add(wxString(s.c_str())); return a; }
		operator wxArrayString() const { return ToWx(); }
#endif
#if defined(VVD_QT_STRING_INTEROP)
		StringArray(const QStringList& a) { reserve(a.size()); for (const QString& s : a) push_back(String(s)); }
		QStringList ToQt() const { QStringList a; for (const String& s : *this) a << s.ToQt(); return a; }
#endif
		void Add(const String& s) { push_back(s); }
		void Insert(const String& s, size_t pos) { insert(begin() + pos, s); }
		void RemoveAt(size_t pos) { erase(begin() + pos); }
		String& Item(size_t i) { return (*this)[i]; }
		const String& Item(size_t i) const { return (*this)[i]; }
		String& Last() { return back(); }
		size_t GetCount() const { return size(); }
		size_t Count() const { return size(); }
		bool IsEmpty() const { return empty(); }
		void Clear() { clear(); }
		int Index(const String& s) const
		{
			for (size_t i = 0; i < size(); ++i) if ((*this)[i] == s) return (int)i;
			return -1;
		}
	};

	// Text file helpers (UTF-8 on disk).
	EXPORT_API bool ReadTextLines(const String& path, std::vector<String>& lines);
	EXPORT_API bool WriteTextLines(const String& path, const std::vector<String>& lines);
}

// Convenient, unambiguous aliases used throughout the engine.
typedef vvd::String VVDString;

#endif // VVD_CORE_STRING_H
