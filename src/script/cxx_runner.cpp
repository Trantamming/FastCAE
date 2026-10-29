// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "script/cxx_runner.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "core/paths.hpp"
#include "solver/solver_registry.hpp"

namespace fs = std::filesystem;

#ifndef FASTCAE_DEV_INCLUDE
#define FASTCAE_DEV_INCLUDE ""
#endif
#ifndef FASTCAE_DEV_LIBDIR
#define FASTCAE_DEV_LIBDIR ""
#endif
#ifndef FASTCAE_LINK_EXTRA
#define FASTCAE_LINK_EXTRA ""
#endif

namespace fastcae {

static std::string q(const std::string& s) { return "\"" + s + "\""; }

static bool hasFile(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

static bool libIn(const fs::path& d, std::string* actual) {
    for (const char* rel : {"", "Release", "RelWithDebInfo"}) {
        const fs::path b = d / rel;
        if (hasFile(b / "libfastcae_core.a") || hasFile(b / "fastcae_core.lib")) { *actual = b.string(); return true; }
    }
    return false;
}

CxxToolchain findCxxToolchain(const std::string& exeDir, const std::string& compilerHint) {
    CxxToolchain t;
    std::vector<std::string> names;
    std::string hint = compilerHint;
    if (hint.empty()) if (const char* e = std::getenv("FASTCAE_CXX")) hint = e;
    if (!hint.empty() && hasFile(hint)) t.compiler = hint;
    else {
        t.compiler = findExecutable({"g++", "clang++", "c++", "cl"}, hint);
        if (t.compiler.empty() && !exeDir.empty()) {   // MinGW kèm theo bộ cài: <app>/mingw64/bin
            t.compiler = findExecutable({"g++"}, (fs::path(exeDir) / "mingw64" / "bin" / "g++.exe").string());
        }
    }
    if (t.compiler.empty()) {
        t.error = "Khong tim thay trinh bien dich C++ (g++/clang++/cl). Cai MinGW-w64 (WinLibs) hoac dat bien FASTCAE_CXX.";
        return t;
    }
    const std::string stem = fs::path(t.compiler).stem().string();
    t.msvc = (stem == "cl");

    // include/lib: SDK cài đặt cạnh chương trình -> biến FASTCAE_SDK -> cây build khi phát triển
    std::vector<fs::path> roots;
    if (const char* s = std::getenv("FASTCAE_SDK")) roots.emplace_back(s);
    if (!exeDir.empty()) {
        roots.emplace_back(fs::path(exeDir) / "sdk");
        roots.emplace_back(fs::path(exeDir).parent_path() / "sdk");
        roots.emplace_back(fs::path(exeDir).parent_path());
    }
    for (const auto& r : roots) {
        for (const char* inc : {"include/fastcae", "include"}) {
            std::string actual;
            if (hasFile(r / inc / "sdk" / "fastcae_all.hpp") && libIn(r / "lib", &actual)) {
                t.includeDir = (r / inc).string();
                t.libDir = actual;
                return t;
            }
        }
    }
    std::string actual;
    if (std::string(FASTCAE_DEV_INCLUDE).size() && hasFile(fs::path(FASTCAE_DEV_INCLUDE) / "sdk" / "fastcae_all.hpp") && libIn(FASTCAE_DEV_LIBDIR, &actual)) {
        t.includeDir = FASTCAE_DEV_INCLUDE;
        t.libDir = actual;
        return t;
    }
    t.error = "Khong tim thay FastCAE SDK (include + lib). Cai thanh phan sdk (cmake --install build --component sdk) hoac dat FASTCAE_SDK.";
    return t;
}

static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Bỏ chú thích // và /* */ (giữ chuỗi) để dò main() không bị đánh lừa bởi lời giải thích.
static std::string stripComments(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') {
            o += s[i++];
            while (i < s.size() && s[i] != '"') { if (s[i] == '\\') o += s[i++]; if (i < s.size()) o += s[i++]; }
            if (i < s.size()) o += s[i];
        } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
            o += '\n';
        } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
            ++i;
        } else o += s[i];
    }
    return o;
}

CxxRunResult buildAndRunCxx(const std::string& sourcePath, const std::string& exeDir, const std::vector<std::string>& args,
                            const OutputFn& onOutput, const std::atomic<bool>* cancel, const std::string& buildDirIn,
                            const std::string& compilerHint, bool runAfterBuild) {
    CxxRunResult R;
    const auto emit = [&](const std::string& s) { if (onOutput) onOutput(s); };
    const CxxToolchain tc = findCxxToolchain(exeDir, compilerHint);
    if (!tc.error.empty()) { R.message = tc.error; return R; }

    const std::string src = readFile(sourcePath);
    if (src.empty()) { R.message = "Khong doc duoc " + sourcePath; return R; }

    fs::path bdir = buildDirIn.empty() ? fs::path(userWorkRoot()) / "cxx" : fs::path(buildDirIn);
    std::error_code ec;
    fs::create_directories(bdir, ec);
    if (ec || !isWritableDir(bdir.string())) { R.message = "Khong ghi duoc thu muc bien dich " + bdir.string(); return R; }

    // Bọc mã nếu không có main()
    const bool hasMain = std::regex_search(stripComments(src), std::regex("\\b(int|auto)\\s+main\\s*\\("));
    const std::string stem = fs::path(sourcePath).stem().string();
    fs::path cpp = fs::path(sourcePath);
    if (!hasMain) {
        cpp = bdir / (stem + "_wrapped.cpp");
        std::ofstream f(cpp, std::ios::binary);
        // giữ #include của người dùng ở đầu file
        std::istringstream in(src);
        std::string line, inc, body;
        while (std::getline(in, line)) (line.find("#include") == 0 ? inc : body) += line + "\n";
        f << "#include \"sdk/fastcae_all.hpp\"\n" << inc << "int main(int argc, char** argv) {\n(void)argc; (void)argv;\ntry {\n"
          << "#line 1 \"" << fs::path(sourcePath).filename().string() << "\"\n" << body
          << "\n} catch (const std::exception& e) { std::cerr << \"Loi: \" << e.what() << \"\\n\"; return 1; }\nreturn 0;\n}\n";
    }

    R.exePath = (bdir / (stem +
#ifdef _WIN32
                         ".exe"
#else
                         ""
#endif
                         )).string();
    std::string cmd;
    if (tc.msvc) {
        cmd = q(tc.compiler) + " /nologo /std:c++20 /EHsc /O2 /utf-8 /openmp /I" + q(tc.includeDir) + " " + q(cpp.string()) + " /Fe:" + q(R.exePath) +
              " /Fo:" + q((bdir / (stem + ".obj")).string()) + " " + q((fs::path(tc.libDir) / "fastcae_core.lib").string()) + " " +
              q((fs::path(tc.libDir) / "fastcae_lua.lib").string());
    } else {
        // Windows: đường dẫn kết thúc bằng '\\' ngay trước dấu nháy đóng ( -L"D:\\x\\" ) làm dấu nháy bị coi là ký tự thường và nuốt
        // phần còn lại của dòng lệnh (-lfastcae_core...). Dùng '/' và bỏ dấu gạch cuối cho -I và -L (g++/MinGW chấp nhận '/').
        auto fwd = [](std::string d) {
            for (auto& c : d) if (c == '\\') c = '/';
            while (d.size() > 1 && d.back() == '/') d.pop_back();
            return d;
        };
        cmd = q(tc.compiler) + " -std=c++20 -O2 -fopenmp -I" + q(fwd(tc.includeDir)) + " " + q(cpp.string()) + " -o " + q(R.exePath) + " -L" + q(fwd(tc.libDir)) +
              " -lfastcae_core -lfastcae_lua " + FASTCAE_LINK_EXTRA;
    }
    if (const char* extra = std::getenv("FASTCAE_CXX_FLAGS")) cmd += std::string(" ") + extra;   // thư viện C++ khác: -I.. -L.. -lmfem
    emit("$ " + cmd + "\n");
    std::string cap;
    const int rc = runCommandLine(cmd, bdir.string(), onOutput, cancel, &cap);
    if (rc != 0) { R.message = "Bien dich that bai (ma " + std::to_string(rc) + ")"; return R; }
    if (!runAfterBuild) { R.ok = true; R.message = "Bien dich xong: " + R.exePath; return R; }

    std::string run = q(R.exePath);
    for (const auto& a : args) run += " " + q(a);
    emit("\n");
    const std::string cwd = fs::path(sourcePath).has_parent_path() ? fs::absolute(fs::path(sourcePath)).parent_path().string() : std::string();
    const int rr = runCommandLine(run, cwd, onOutput, cancel, nullptr);
    R.ok = rr == 0;
    R.message = rr == 0 ? "Hoan tat" : "Chuong trinh thoat voi ma " + std::to_string(rr);
    return R;
}

}  // namespace fastcae
