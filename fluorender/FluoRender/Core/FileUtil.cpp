#include "FileUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <functional>
#include <thread>
#include <fstream>
#include <sstream>
#include <locale>
#include <cwctype>
#include <cwchar>

#include <curl/curl.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz/miniz.h"

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <pwd.h>
#include <unistd.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace vvd
{
	static fs::path ToPath(const String& s) { return fs::path(std::wstring(s)); }
	static String FromPath(const fs::path& p) { return String(p.wstring()); }

	bool FileExists(const String& path)
	{
		std::error_code ec;
		return !path.empty() && fs::is_regular_file(ToPath(path), ec);
	}

	bool DirExists(const String& path)
	{
		std::error_code ec;
		return !path.empty() && fs::is_directory(ToPath(path), ec);
	}

	bool Mkdir(const String& path)
	{
		std::error_code ec;
		if (fs::is_directory(ToPath(path), ec)) return true;
		return fs::create_directory(ToPath(path), ec);
	}

	bool MkdirRecursive(const String& path)
	{
		std::error_code ec;
		if (fs::is_directory(ToPath(path), ec)) return true;
		return fs::create_directories(ToPath(path), ec);
	}

	bool RemoveFile(const String& path)
	{
		std::error_code ec;
		return fs::remove(ToPath(path), ec);
	}

	bool CopyFile(const String& src, const String& dst, bool overwrite)
	{
		std::error_code ec;
		fs::copy_file(ToPath(src), ToPath(dst),
			overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
		return !ec;
	}

	long long FileSize(const String& path)
	{
		std::error_code ec;
		auto sz = fs::file_size(ToPath(path), ec);
		return ec ? -1 : (long long)sz;
	}

	wchar_t PathSeparator()
	{
#ifdef _WIN32
		return L'\\';
#else
		return L'/';
#endif
	}

	String JoinPath(const String& a, const String& b)
	{
		if (a.empty()) return b;
		if (b.empty()) return a;
		wchar_t last = a.back();
		if (last == L'/' || last == L'\\') return a + b;
		return a + PathSeparator() + b;
	}

	String GetFileName(const String& path) { return FromPath(ToPath(path).filename()); }
	String GetFileStem(const String& path) { return FromPath(ToPath(path).stem()); }
	String GetFileExt(const String& path)
	{
		String e = FromPath(ToPath(path).extension());
		if (!e.empty() && e[0] == L'.') e.erase(0, 1);
		return e;
	}
	String GetParentDir(const String& path) { return FromPath(ToPath(path).parent_path()); }
	String ToAbsolutePath(const String& path)
	{
		std::error_code ec;
		fs::path p = fs::absolute(ToPath(path), ec);
		return ec ? path : FromPath(p);
	}
	bool IsAbsolutePath(const String& path) { return ToPath(path).is_absolute(); }

	// ---- standard locations --------------------------------------------

	static String& AppNameStorage()
	{
		static String name(L"VVDViewer");
		return name;
	}
	void SetAppName(const String& name) { AppNameStorage() = name; }
	String GetAppName() { return AppNameStorage(); }

	String GetExecutablePath()
	{
#if defined(_WIN32)
		wchar_t buf[MAX_PATH * 4];
		DWORD n = GetModuleFileNameW(NULL, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
		return String(std::wstring(buf, n));
#elif defined(__APPLE__)
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::string buf(size + 1, '\0');
		if (_NSGetExecutablePath(buf.data(), &size) != 0) return String();
		buf.resize(std::strlen(buf.c_str()));
		std::error_code ec;
		fs::path p = fs::canonical(fs::path(buf), ec);
		return ec ? String(buf) : FromPath(p);
#else
		std::error_code ec;
		fs::path p = fs::read_symlink("/proc/self/exe", ec);
		return ec ? String() : FromPath(p);
#endif
	}

	String GetExecutableDir() { return GetParentDir(GetExecutablePath()); }

	String GetResourcesDir()
	{
#if defined(__APPLE__)
		// <App>.app/Contents/MacOS/<exe> -> <App>.app/Contents/Resources
		fs::path exe = ToPath(GetExecutablePath());
		fs::path contents = exe.parent_path().parent_path();
		if (contents.filename() == "Contents")
		{
			fs::path res = contents / "Resources";
			std::error_code ec;
			if (fs::is_directory(res, ec)) return FromPath(res);
		}
		return GetExecutableDir();
#else
		return GetExecutableDir();
#endif
	}

	static String HomeDir()
	{
#if defined(_WIN32)
		wchar_t* p = nullptr; size_t n = 0;
		if (_wdupenv_s(&p, &n, L"USERPROFILE") == 0 && p) { String s(p); free(p); return s; }
		return String();
#else
		const char* h = std::getenv("HOME");
		if (h && *h) return String(std::string(h));
		struct passwd* pw = getpwuid(getuid());
		return pw ? String(std::string(pw->pw_dir)) : String();
#endif
	}

	String GetUserDataDir()
	{
#if defined(_WIN32)
		wchar_t buf[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, buf)))
			return JoinPath(String(buf), GetAppName());
		return JoinPath(HomeDir(), GetAppName());
#elif defined(__APPLE__)
		return JoinPath(JoinPath(JoinPath(HomeDir(), L"Library"), L"Application Support"), GetAppName());
#else
		return JoinPath(HomeDir(), String(L".") + GetAppName());
#endif
	}

	String GetUserConfigDir()
	{
		// Same locations as wxStandardPaths::GetUserConfigDir(): the wx GUI
		// still writes default_volume_settings.dft there.
#if defined(_WIN32)
		wchar_t buf[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, buf))) return String(buf);
		return HomeDir();
#elif defined(__APPLE__)
		return JoinPath(JoinPath(HomeDir(), L"Library"), L"Preferences");
#else
		return HomeDir();
#endif
	}

	String GetTempDir()
	{
		std::error_code ec;
		fs::path p = fs::temp_directory_path(ec);
		if (ec) return String(L"/tmp");
		String s = FromPath(p);
		while (!s.empty() && (s.back() == L'/' || s.back() == L'\\')) s.pop_back();
		return s;
	}

	String GetDocumentsDir()
	{
#if defined(_WIN32)
		wchar_t buf[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, buf))) return String(buf);
		return HomeDir();
#else
		return JoinPath(HomeDir(), L"Documents");
#endif
	}

	String CreateTempFileName(const String& prefix)
	{
		static std::atomic<unsigned> counter{ 0 };
		String dir = GetTempDir();
		for (int attempt = 0; attempt < 10000; ++attempt)
		{
			unsigned n = counter++;
			String name = prefix + String::Format("_%u_%u", (unsigned)std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFF, n);
			String path = JoinPath(dir, name);
			std::error_code ec;
			if (!fs::exists(ToPath(path), ec)) return path;
		}
		return JoinPath(dir, prefix);
	}

	// ---- network ---------------------------------------------------------

	bool IsURL(const String& s)
	{
		String l = s.Lower();
		return l.StartsWith(L"http://") || l.StartsWith(L"https://") || l.StartsWith(L"ftp://");
	}

	static size_t CurlWriteToFile(void* ptr, size_t size, size_t nmemb, void* stream)
	{
		return std::fwrite(ptr, size, nmemb, (FILE*)stream);
	}

	bool DownloadToFile(const String& url, const String& dest)
	{
		CURL* curl = curl_easy_init();
		if (!curl) return false;
#ifdef _WIN32
		FILE* fp = _wfopen(dest.c_str(), L"wb");
#else
		FILE* fp = std::fopen(dest.ToStdString().c_str(), "wb");
#endif
		if (!fp) { curl_easy_cleanup(curl); return false; }
		std::string u = url.ToStdString();
		curl_easy_setopt(curl, CURLOPT_URL, u.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToFile);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		// wxURL used a 10 s socket timeout; do the same for connecting and
		// abort a transfer that stalls (< 1 byte/s for 30 s) instead of a hard
		// total limit, so large downloads still work.
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
		CURLcode rc = curl_easy_perform(curl);
		std::fclose(fp);
		curl_easy_cleanup(curl);
		if (rc != CURLE_OK) { RemoveFile(dest); return false; }
		return true;
	}

	// ---- zip -------------------------------------------------------------

	struct ZipHandle
	{
		mz_zip_archive zip;
		FILE* fp = nullptr;   // Windows: opened by us so that wide paths work
	};

	static bool OpenZip(const String& zipPath, ZipHandle& h)
	{
		std::memset(&h.zip, 0, sizeof(h.zip));
		h.fp = nullptr;
#ifdef _WIN32
		h.fp = _wfopen(zipPath.c_str(), L"rb");
		if (!h.fp) return false;
		if (!mz_zip_reader_init_cfile(&h.zip, h.fp, 0, 0)) { std::fclose(h.fp); h.fp = nullptr; return false; }
		return true;
#else
		return mz_zip_reader_init_file(&h.zip, zipPath.ToStdString().c_str(), 0) != 0;
#endif
	}

	static void CloseZip(ZipHandle& h)
	{
		mz_zip_reader_end(&h.zip);   // does not close a caller-supplied FILE*
		if (h.fp) { std::fclose(h.fp); h.fp = nullptr; }
	}

	// wxZipInputStream loop idiom: skip entries whose *file name* starts with '.'
	static bool IsHiddenEntry(const std::string& name)
	{
		size_t slash = name.find_last_of("/\\");
		size_t start = slash == std::string::npos ? 0 : slash + 1;
		return start < name.size() && name[start] == '.';
	}

	static bool HasAllowedExtension(const std::string& name, const std::vector<String>* extensions)
	{
		if (!extensions) return true;
		size_t dot = name.find_last_of('.');
		String ext = dot == std::string::npos ? String() : String(name.substr(dot)).Lower();
		for (const String& e : *extensions)
			if (ext == e.Lower()) return true;
		return false;
	}

	bool ListZipEntries(const String& zipPath, std::vector<String>& names)
	{
		ZipHandle h;
		if (!OpenZip(zipPath, h)) return false;
		mz_zip_archive& zip = h.zip;
		mz_uint n = mz_zip_reader_get_num_files(&zip);
		for (mz_uint i = 0; i < n; ++i)
		{
			mz_zip_archive_file_stat st;
			if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
			names.push_back(String(std::string(st.m_filename)));
		}
		CloseZip(h);
		return true;
	}

	bool UnzipToDir(const String& zipPath, const String& destDir, std::vector<String>& extracted,
		bool skipHidden, const std::vector<String>* extensions)
	{
		ZipHandle h;
		if (!OpenZip(zipPath, h)) return false;
		mz_zip_archive& zip = h.zip;
		if (!MkdirRecursive(destDir)) { CloseZip(h); return false; }

		bool ok = true;
		mz_uint n = mz_zip_reader_get_num_files(&zip);
		for (mz_uint i = 0; i < n && ok; ++i)
		{
			mz_zip_archive_file_stat st;
			if (!mz_zip_reader_file_stat(&zip, i, &st)) { ok = false; break; }
			if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
			std::string name(st.m_filename);
			if (skipHidden && IsHiddenEntry(name)) continue;
			if (!HasAllowedExtension(name, extensions)) continue;

			String out = JoinPath(destDir, String(name));
			MkdirRecursive(GetParentDir(out));
#ifdef _WIN32
			FILE* fp = _wfopen(out.c_str(), L"wb");
			if (!fp) { ok = false; break; }
			ok = mz_zip_reader_extract_to_cfile(&zip, i, fp, 0) != 0;
			std::fclose(fp);
#else
			ok = mz_zip_reader_extract_to_file(&zip, i, out.ToStdString().c_str(), 0) != 0;
#endif
			if (ok) extracted.push_back(out);
		}
		CloseZip(h);
		return ok;
	}

	bool UnzipFirstFile(const String& zipPath, const String& destDir, String& outPath)
	{
		ZipHandle h;
		if (!OpenZip(zipPath, h)) return false;
		mz_zip_archive& zip = h.zip;
		if (!MkdirRecursive(destDir)) { CloseZip(h); return false; }
		bool ok = false;
		mz_uint n = mz_zip_reader_get_num_files(&zip);
		for (mz_uint i = 0; i < n; ++i)
		{
			mz_zip_archive_file_stat st;
			if (!mz_zip_reader_file_stat(&zip, i, &st)) break;
			if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
			String out = JoinPath(destDir, GetFileName(String(std::string(st.m_filename))));
#ifdef _WIN32
			FILE* fp = _wfopen(out.c_str(), L"wb");
			if (!fp) break;
			ok = mz_zip_reader_extract_to_cfile(&zip, i, fp, 0) != 0;
			std::fclose(fp);
#else
			ok = mz_zip_reader_extract_to_file(&zip, i, out.ToStdString().c_str(), 0) != 0;
#endif
			if (ok) outPath = out;
			break;
		}
		CloseZip(h);
		return ok;
	}

	// ---- text files ---------------------------------------------------------

	TextFileReader::TextFileReader(const String& path)
	{
		m_ok = ReadTextLines(path, m_lines);
	}

	String TextFileReader::ReadLine()
	{
		if (m_next >= m_lines.size()) return String();
		return m_lines[m_next++];
	}

	TextFileWriter::TextFileWriter(const String& path)
	{
#ifdef _WIN32
		FILE* fp = _wfopen(path.c_str(), L"wb");
#else
		FILE* fp = std::fopen(path.ToStdString().c_str(), "wb");
#endif
		m_file = fp;
		m_ok = fp != nullptr;
	}

	TextFileWriter::~TextFileWriter() { Close(); }

	void TextFileWriter::Close()
	{
		if (m_file) { std::fclose((FILE*)m_file); m_file = nullptr; }
	}

	void TextFileWriter::WriteUtf8(const std::string& s)
	{
		if (m_file && !s.empty()) std::fwrite(s.data(), 1, s.size(), (FILE*)m_file);
	}

	TextFileWriter& TextFileWriter::operator<<(int v) { WriteUtf8(std::to_string(v)); return *this; }
	TextFileWriter& TextFileWriter::operator<<(unsigned int v) { WriteUtf8(std::to_string(v)); return *this; }
	TextFileWriter& TextFileWriter::operator<<(long v) { WriteUtf8(std::to_string(v)); return *this; }
	TextFileWriter& TextFileWriter::operator<<(unsigned long v) { WriteUtf8(std::to_string(v)); return *this; }
	TextFileWriter& TextFileWriter::operator<<(long long v) { WriteUtf8(std::to_string(v)); return *this; }
	TextFileWriter& TextFileWriter::operator<<(unsigned long long v) { WriteUtf8(std::to_string(v)); return *this; }
	TextFileWriter& TextFileWriter::operator<<(double v)
	{
		// wxTextOutputStream::WriteDouble formats with "%f" in the C locale
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%f", v);
		WriteUtf8(buf); return *this;
	}

	// ---- IniFile -----------------------------------------------------------

	String IniFile::Normalize(const String& key)
	{
		String k = key;
		for (auto& c : k) if (c == L'\\') c = L'/';
		if (k.empty() || k[0] != L'/') k = String(L"/") + k;
		return k;
	}

	// wxFileConfig name/value escaping (FilterIn/OutEntryName, FilterIn/OutValue
	// in wx/src/common/fileconf.cpp).
	static String IniUnescapeName(const String& s)
	{
		String out;
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == L'\\' && i + 1 < s.size()) ++i;
			out += s[i];
		}
		return out;
	}

	static String IniEscapeName(const String& s)
	{
		static const wchar_t* safe = L"@_/-!.*%()";
		String out;
		for (wchar_t c : s)
		{
			if (!std::iswalnum(c) && !std::wcschr(safe, c)) out += L'\\';
			out += c;
		}
		return out;
	}

	static String IniUnescapeValue(const String& s)
	{
		String out;
		bool quoted = !s.empty() && s[0] == L'"';
		for (size_t n = quoted ? 1 : 0; n < s.size(); ++n)
		{
			wchar_t c = s[n];
			if (c == L'\\' && n + 1 < s.size())
			{
				wchar_t e = s[++n];
				switch (e)
				{
				case L'n': out += L'\n'; break;
				case L'r': out += L'\r'; break;
				case L't': out += L'\t'; break;
				case L'\\': out += L'\\'; break;
				case L'"': out += L'"'; break;
				default: out += L'\\'; out += e; break;   // lenient: keep unknown escapes
				}
			}
			else if (c != L'"' || !quoted)
				out += c;
			// else: closing quote of a quoted value
		}
		return out;
	}

	static String IniEscapeValue(const String& s)
	{
		if (s.empty()) return s;
		bool quote = std::iswspace(s[0]) || s[0] == L'"';
		String out;
		if (quote) out += L'"';
		for (wchar_t c : s)
		{
			wchar_t e = 0;
			switch (c)
			{
			case L'\n': e = L'n'; break;
			case L'\r': e = L'r'; break;
			case L'\t': e = L't'; break;
			case L'\\': e = L'\\'; break;
			case L'"': if (quote) e = L'"'; break;
			default: break;
			}
			if (e) { out += L'\\'; out += e; }
			else out += c;
		}
		if (quote) out += L'"';
		return out;
	}

	void IniFile::Set(const String& normKey, const String& value)
	{
		auto it = m_values.find(normKey);
		if (it == m_values.end())
		{
			m_values.emplace(normKey, value);
			m_order.push_back(normKey);
		}
		else
			it->second = value;
	}

	bool IniFile::Load(const String& path)
	{
		std::vector<String> lines;
		if (!ReadTextLines(path, lines)) return false;
		String group(L"/");
		for (String line : lines)
		{
			line.Trim(true); line.Trim(false);
			if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
			if (line[0] == L'[')
			{
				// closing ']' is the first unescaped one
				size_t end = String::npos;
				for (size_t i = 1; i < line.size(); ++i)
				{
					if (line[i] == L'\\') { ++i; continue; }
					if (line[i] == L']') { end = i; break; }
				}
				if (end == String::npos) continue;
				String g = IniUnescapeName(line.Mid(1, end - 1));
				while (!g.empty() && g[0] == L'/') g = g.Mid(1);
				group = String(L"/") + g;
				if (group.back() != L'/') group += L'/';
				continue;
			}
			// key ends at the first unescaped '='
			size_t eq = String::npos;
			for (size_t i = 0; i < line.size(); ++i)
			{
				if (line[i] == L'\\') { ++i; continue; }
				if (line[i] == L'=') { eq = i; break; }
			}
			if (eq == String::npos) continue;
			String key = line.Left(eq); key.Trim(true);
			key = IniUnescapeName(key);
			String val = line.Mid(eq + 1); val.Trim(false);
			Set(group + key, IniUnescapeValue(val));
		}
		return true;
	}

	bool IniFile::Save(const String& path) const
	{
		// wxFileConfig layout: root entries first, then one "[group]" block per
		// group in creation order, no blank lines.
		std::vector<String> groups;                       // "" = root
		std::map<String, std::vector<String>> entries;    // group -> lines
		for (const String& full : m_order)
		{
			auto it = m_values.find(full);
			if (it == m_values.end()) continue;
			size_t slash = full.rfind(L'/');
			String group = slash == String::npos ? String() : full.Left(slash);
			if (!group.empty() && group[0] == L'/') group = group.Mid(1);
			String key = full.Mid(slash + 1);
			if (!entries.count(group)) groups.push_back(group);
			entries[group].push_back(IniEscapeName(key) + L"=" + IniEscapeValue(it->second));
		}
		std::vector<String> lines;
		auto root = entries.find(String());
		if (root != entries.end())
			lines.insert(lines.end(), root->second.begin(), root->second.end());
		for (const String& g : groups)
		{
			if (g.empty()) continue;
			lines.push_back(String(L"[") + IniEscapeName(g) + L"]");
			const auto& e = entries[g];
			lines.insert(lines.end(), e.begin(), e.end());
		}
		return WriteTextLines(path, lines);
	}

	bool IniFile::Has(const String& fullKey) const { return m_values.count(Normalize(fullKey)) != 0; }

	bool IniFile::Read(const String& fullKey, String& value) const
	{
		auto it = m_values.find(Normalize(fullKey));
		if (it == m_values.end()) return false;
		value = it->second;
		return true;
	}
	bool IniFile::Read(const String& fullKey, double& value) const
	{
		String s; if (!Read(fullKey, s)) return false;
		return s.ToCDouble(&value);   // wxFileConfig writes numbers in the C locale
	}
	bool IniFile::Read(const String& fullKey, long& value) const
	{
		String s; if (!Read(fullKey, s)) return false;
		return s.ToLong(&value);
	}
	bool IniFile::Read(const String& fullKey, int& value) const
	{
		long l; if (!Read(fullKey, l)) return false;
		value = (int)l; return true;
	}
	bool IniFile::Read(const String& fullKey, bool& value) const
	{
		String s; if (!Read(fullKey, s)) return false;
		String l = s.Lower();
		if (l == L"1" || l == L"true" || l == L"yes" || l == L"on") { value = true; return true; }
		if (l == L"0" || l == L"false" || l == L"no" || l == L"off") { value = false; return true; }
		// wxConfigBase::Read(bool): any other integer, value != 0
		long n; if (Read(fullKey, n)) { value = n != 0; return true; }
		return false;
	}
	String IniFile::ReadString(const String& k, const String& def) const { String v; return Read(k, v) ? v : def; }
	double IniFile::ReadDouble(const String& k, double def) const { double v; return Read(k, v) ? v : def; }
	long IniFile::ReadLong(const String& k, long def) const { long v; return Read(k, v) ? v : def; }
	bool IniFile::ReadBool(const String& k, bool def) const { bool v; return Read(k, v) ? v : def; }

	void IniFile::Write(const String& fullKey, const String& value) { Set(Normalize(fullKey), value); }
	void IniFile::Write(const String& fullKey, double value)
	{
		std::wostringstream os; os.imbue(std::locale::classic()); os.precision(17); os << value;
		Write(fullKey, String(os.str()));
	}
	void IniFile::Write(const String& fullKey, long value) { Write(fullKey, String(std::to_wstring(value))); }
	void IniFile::Write(const String& fullKey, bool value) { Write(fullKey, String(value ? L"1" : L"0")); }

	std::vector<String> IniFile::Groups(const String& parent) const
	{
		String p = Normalize(parent);
		if (p.back() != L'/') p += L'/';
		std::vector<String> out;
		for (const auto& kv : m_values)
		{
			if (!kv.first.StartsWith(p)) continue;
			String rest = kv.first.Mid(p.size());
			size_t slash = rest.find(L'/');
			if (slash == String::npos) continue;
			String g = rest.Left(slash);
			if (std::find(out.begin(), out.end(), g) == out.end()) out.push_back(g);
		}
		return out;
	}

	std::vector<String> IniFile::Keys(const String& group) const
	{
		String p = Normalize(group);
		if (p.back() != L'/') p += L'/';
		std::vector<String> out;
		for (const auto& kv : m_values)
		{
			if (!kv.first.StartsWith(p)) continue;
			String rest = kv.first.Mid(p.size());
			if (rest.find(L'/') == String::npos) out.push_back(rest);
		}
		return out;
	}
}
