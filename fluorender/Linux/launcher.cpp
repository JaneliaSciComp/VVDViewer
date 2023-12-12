#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <linux/limits.h>
#include <filesystem>
#include <sstream>
namespace fs = std::filesystem;

int main()
{
    char exepath[PATH_MAX];
	ssize_t count = readlink("/proc/self/exe", exepath, PATH_MAX);
	const char *exedir;
	if (count != -1) {
    	exedir = dirname(exepath);
	}

    fs::path dir (exedir);
    fs::path schema_dir ("data/schemas");
    fs::path cache_file ("data/loaders.cache");
    fs::path exe_file ("data/VVDViewer");
    fs::path ld_file ("data/lib/ld-linux-x86-64.so.2");
    fs::path cache_full_path = dir / cache_file;
    fs::path schemas_full_path = dir / schema_dir;
    fs::path exe_full_path = dir / exe_file;
    fs::path ld_full_path = dir / ld_file;

    std::stringstream ss;

    ss << "export GDK_PIXBUF_MODULE_FILE=" << cache_full_path << " && " << "export XDG_DATA_DIRS=\"" << schemas_full_path << "/usr/shre/gnome:/usr/local/share:/usr/share\" && " << ld_full_path << " " << exe_full_path;

    system(ss.str().c_str());
}