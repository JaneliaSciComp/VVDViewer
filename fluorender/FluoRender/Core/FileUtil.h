#ifndef VVD_CORE_FILEUTIL_H
#define VVD_CORE_FILEUTIL_H

// Toolkit-neutral file, path, download and archive helpers that replace the
// wxFileExists / wxStandardPaths / wxURL / wxZipInputStream / wxFileConfig
// usages in the engine and data layer.

#include <map>
#include <vector>

#include "VVDString.h"

namespace vvd
{
	// ---- basic file system ---------------------------------------------
	EXPORT_API bool FileExists(const String& path);       // regular file
	EXPORT_API bool DirExists(const String& path);
	EXPORT_API bool Mkdir(const String& path);            // single level, ok if exists
	EXPORT_API bool MkdirRecursive(const String& path);
	EXPORT_API bool RemoveFile(const String& path);
	EXPORT_API bool CopyFile(const String& src, const String& dst, bool overwrite = true);
	EXPORT_API long long FileSize(const String& path);    // -1 if missing
	EXPORT_API wchar_t PathSeparator();
	EXPORT_API String JoinPath(const String& a, const String& b);
	EXPORT_API String GetFileName(const String& path);    // "dir/a.txt" -> "a.txt"
	EXPORT_API String GetFileStem(const String& path);    // "dir/a.txt" -> "a"
	EXPORT_API String GetFileExt(const String& path);     // "dir/a.txt" -> "txt" (no dot)
	EXPORT_API String GetParentDir(const String& path);   // "dir/a.txt" -> "dir"
	EXPORT_API String ToAbsolutePath(const String& path);
	EXPORT_API bool IsAbsolutePath(const String& path);

	// ---- standard locations (wxStandardPaths replacement) ---------------
	// The application name is used to build the per-user data directory
	// (e.g. ~/Library/Application Support/<app> on macOS). The GUI sets it once
	// at start-up; the default is "VVDViewer".
	EXPORT_API void SetAppName(const String& name);
	EXPORT_API String GetAppName();
	EXPORT_API String GetExecutablePath();
	EXPORT_API String GetExecutableDir();
	EXPORT_API String GetResourcesDir();     // <bundle>/Contents/Resources on macOS, exe dir elsewhere
	EXPORT_API String GetUserDataDir();
	EXPORT_API String GetUserConfigDir();
	EXPORT_API String GetTempDir();
	EXPORT_API String GetUserCacheDir();     // ~/Library/Caches/VVDViewer on macOS, GetUserDataDir() elsewhere

	// ---- settings files (fluorender.set, default_*.dft, plugin .dft) -------
	// One per-user directory shared by the wx app (VVDViewer) and the Qt app
	// (VVDViewerQt); the name is fixed ("VVDViewer"), independent of
	// SetAppName():
	//   macOS   ~/Library/Application Support/VVDViewer
	//   Windows %APPDATA%\VVDViewer
	//   Linux   $XDG_CONFIG_HOME/VVDViewer (default ~/.config/VVDViewer)
	// The environment variable VVD_SETTINGS_DIR overrides it (tests).
	// The install copies (macOS <bundle>/Contents/Resources, elsewhere the exe
	// dir) are read-only factory defaults: settings are never written there.
	EXPORT_API String GetUserSettingsDir();
	EXPORT_API String GetFactorySettingsDir();
	// The file to read: <user dir>/name if it exists, else the factory copy,
	// else (Windows / Linux) the legacy GetUserConfigDir()/name the old code
	// wrote when the exe dir was read-only; else <user dir>/name (missing).
	EXPORT_API String SettingsReadPath(const String& name);
	// The file to write: <user dir>/name (the directory is created).
	EXPORT_API String SettingsWritePath(const String& name);
	EXPORT_API String GetDocumentsDir();
	// A new, unique file name in the temp directory (the file is NOT created).
	EXPORT_API String CreateTempFileName(const String& prefix = String(L"vvd"));

	// ---- network ---------------------------------------------------------
	// Downloads url into dest (overwritten). Returns false on any error.
	EXPORT_API bool DownloadToFile(const String& url, const String& dest);
	EXPORT_API bool IsURL(const String& s);   // http://, https://, ftp://

	// ---- zip archives (miniz) --------------------------------------------
	// Extracts every regular file in the archive into destDir (created if
	// needed). Hidden entries (a name component starting with '.') are skipped
	// when skipHidden is set. Full paths of the extracted files are appended
	// to 'extracted'. Returns false on error.
	// If 'extensions' is given (e.g. {".nrrd", ".tif"}), only entries with one
	// of those extensions (case-insensitive) are extracted.
	EXPORT_API bool UnzipToDir(const String& zipPath, const String& destDir,
		std::vector<String>& extracted, bool skipHidden = true,
		const std::vector<String>* extensions = nullptr);
	EXPORT_API bool ListZipEntries(const String& zipPath, std::vector<String>& names);
	// Extracts only the first regular file of the archive into destDir and
	// returns its path in outPath (wxZipInputStream::GetNextEntry idiom).
	EXPORT_API bool UnzipFirstFile(const String& zipPath, const String& destDir, String& outPath);

	// ---- INI-style config (wxFileConfig reader replacement) --------------
	// Understands the wxFileConfig text format: "[group/subgroup]" sections
	// and "key=value" lines; '#' and ';' start comments. Keys are looked up as
	// "/group/subgroup/key" or ("group/subgroup", "key").
	// Group/key names and values use wxFileConfig's escaping on disk
	// ("[gm\ calculation]", "mem\ swap=1", "C:\\dir"), so files written by
	// Save() are read back identically by wxFileConfig and vice versa.
	class EXPORT_API IniFile
	{
	public:
		IniFile() = default;
		explicit IniFile(const String& path) { Load(path); }
		bool Load(const String& path);
		bool Save(const String& path) const;
		bool Has(const String& fullKey) const;
		bool Read(const String& fullKey, String& value) const;
		bool Read(const String& fullKey, double& value) const;
		bool Read(const String& fullKey, long& value) const;
		bool Read(const String& fullKey, int& value) const;
		bool Read(const String& fullKey, bool& value) const;
		// pointer forms (wxConfigBase::Read idiom)
		bool Read(const String& fullKey, String* value) const { return value && Read(fullKey, *value); }
		bool Read(const String& fullKey, double* value) const { return value && Read(fullKey, *value); }
		bool Read(const String& fullKey, long* value) const { return value && Read(fullKey, *value); }
		bool Read(const String& fullKey, int* value) const { return value && Read(fullKey, *value); }
		bool Read(const String& fullKey, bool* value) const { return value && Read(fullKey, *value); }
		String ReadString(const String& fullKey, const String& def = String()) const;
		double ReadDouble(const String& fullKey, double def = 0.0) const;
		long ReadLong(const String& fullKey, long def = 0) const;
		bool ReadBool(const String& fullKey, bool def = false) const;
		void Write(const String& fullKey, const String& value);
		void Write(const String& fullKey, double value);
		void Write(const String& fullKey, long value);
		void Write(const String& fullKey, int value) { Write(fullKey, (long)value); }
		void Write(const String& fullKey, bool value);
		std::vector<String> Groups(const String& parent = String()) const;
		std::vector<String> Keys(const String& group) const;
		void Clear() { m_values.clear(); m_order.clear(); }
	private:
		static String Normalize(const String& key);
		void Set(const String& normKey, const String& value);
		std::map<String, String> m_values;   // "/group/sub/key" -> value
		std::vector<String> m_order;         // keys in insertion order (Save keeps it, like wxFileConfig)
	};
}

namespace vvd
{
	// ---- line-oriented text files (wxTextInputStream / wxTextOutputStream) --
	class EXPORT_API TextFileReader
	{
	public:
		explicit TextFileReader(const String& path);
		bool IsOk() const { return m_ok; }
		bool Ok() const { return m_ok; }
		bool Eof() const { return m_next >= m_lines.size(); }
		String ReadLine();
		const std::vector<String>& Lines() const { return m_lines; }
	private:
		bool m_ok = false;
		size_t m_next = 0;
		std::vector<String> m_lines;
	};

	class EXPORT_API TextFileWriter
	{
	public:
		explicit TextFileWriter(const String& path);
		~TextFileWriter();
		bool IsOk() const { return m_ok; }
		bool Ok() const { return m_ok; }
		void Close();
		TextFileWriter& operator<<(const String& s) { Write(s); return *this; }
		TextFileWriter& operator<<(const std::wstring& s) { Write(s); return *this; }
		TextFileWriter& operator<<(const wchar_t* s) { Write(std::wstring(s ? s : L"")); return *this; }
		TextFileWriter& operator<<(const std::string& s) { WriteUtf8(s); return *this; }
		TextFileWriter& operator<<(const char* s) { WriteUtf8(s ? s : ""); return *this; }
		TextFileWriter& operator<<(char c) { WriteUtf8(std::string(1, c)); return *this; }
		TextFileWriter& operator<<(wchar_t c) { Write(std::wstring(1, c)); return *this; }
		TextFileWriter& operator<<(bool v) { WriteUtf8(v ? "1" : "0"); return *this; }
		TextFileWriter& operator<<(int v);
		TextFileWriter& operator<<(unsigned int v);
		TextFileWriter& operator<<(long v);
		TextFileWriter& operator<<(unsigned long v);
		TextFileWriter& operator<<(long long v);
		TextFileWriter& operator<<(unsigned long long v);
		TextFileWriter& operator<<(double v);
		TextFileWriter& operator<<(float v) { return *this << (double)v; }
	private:
		void Write(const std::wstring& s) { WriteUtf8(WideToUtf8(s)); }
		void WriteUtf8(const std::string& s);
		void* m_file = nullptr;   // FILE*
		bool m_ok = false;
	};
}

#endif // VVD_CORE_FILEUTIL_H
