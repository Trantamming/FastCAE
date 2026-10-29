// FastCAE - Tran Minh Tam (MIT)
#include "core/paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace fastcae {

static std::string env(const char* k) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string();
}

std::string userWorkRoot() {
    if (const std::string o = env("FASTCAE_WORK"); !o.empty()) return o;
    fs::path p;
#ifdef _WIN32
    if (const std::string d = env("LOCALAPPDATA"); !d.empty()) p = fs::path(d) / "FastCAE" / "work";
    else if (const std::string u = env("USERPROFILE"); !u.empty()) p = fs::path(u) / "AppData" / "Local" / "FastCAE" / "work";
#else
    if (const std::string x = env("XDG_DATA_HOME"); !x.empty()) p = fs::path(x) / "FastCAE" / "work";
    else if (const std::string h = env("HOME"); !h.empty()) p = fs::path(h) / ".local" / "share" / "FastCAE" / "work";
#endif
    if (p.empty() || !isWritableDir(p.string())) {
        std::error_code ec;
        p = fs::temp_directory_path(ec) / "FastCAE" / "work";
    }
    return p.generic_string();
}

bool isWritableDir(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) return false;
    const fs::path probe = fs::path(dir) / ".fastcae_write_test";
    {
        std::ofstream f(probe);
        if (!f) return false;
        f << "ok";
    }
    fs::remove(probe, ec);
    return true;
}

std::string resolveWorkDir(const std::string& requested, const std::string& baseDir, const std::string& sub) {
    std::string want = requested;
    if (want.empty()) want = baseDir.empty() ? std::string() : baseDir + "/" + sub;
    else if (!baseDir.empty() && fs::path(want).is_relative()) want = baseDir + "/" + want;
    if (want.empty()) want = sub;  // tuong doi so voi thu muc chay; neu khong ghi duoc (Program Files) se roi xuong du phong
    if (isWritableDir(want)) return want;
    // thu muc muon dung khong ghi duoc (vd C:\Program Files\FastCAE\bin\ccx_shell)
    const std::string fallback = userWorkRoot() + "/" + sub;
    std::error_code ec;
    fs::create_directories(fallback, ec);
    return fallback;
}

std::string userDocumentsDir() {
    fs::path home;
#ifdef _WIN32
    if (const std::string u = env("USERPROFILE"); !u.empty()) home = u;
#else
    if (const std::string h = env("HOME"); !h.empty()) home = h;
#endif
    if (home.empty()) return userWorkRoot();
    std::error_code ec;
    fs::path docs = home / "Documents";
    if (fs::is_directory(docs, ec)) return (docs / "FastCAE").generic_string();
    return home.generic_string();
}

}  // namespace fastcae
