// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/openfoam_results.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace fastcae {

namespace {

bool slurp(const std::string& path, std::string& out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "Khong doc duoc " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Vị trí ngay sau `internalField`; trả về npos nếu không có.
std::size_t afterInternalField(const std::string& s) {
    const auto p = s.find("internalField");
    return p == std::string::npos ? p : p + 13;
}

void skipSpace(const std::string& s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool readNumber(const std::string& s, std::size_t& i, double& v) {
    skipSpace(s, i);
    const char* b = s.c_str() + i;
    char* e = nullptr;
    v = std::strtod(b, &e);
    if (e == b) return false;
    i += static_cast<std::size_t>(e - b);
    return true;
}

bool readToken(const std::string& s, std::size_t& i, std::string& tok) {
    skipSpace(s, i);
    const std::size_t b = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '(' && s[i] != ')' && s[i] != ';') ++i;
    tok = s.substr(b, i - b);
    return !tok.empty();
}

template <int N, typename Out>
bool parseField(const std::string& s, Out& out, std::string* err) {
    std::size_t i = afterInternalField(s);
    if (i == std::string::npos) {
        if (err) *err = "Khong thay internalField";
        return false;
    }
    std::string kind;
    if (!readToken(s, i, kind)) return false;
    if (kind == "uniform") {   // một giá trị cho mọi ô: trả về đúng một phần tử
        skipSpace(s, i);
        std::array<double, 3> a{0, 0, 0};
        if (N == 3) {
            if (s[i] != '(') return false;
            ++i;
            for (int k = 0; k < 3; ++k)
                if (!readNumber(s, i, a[k])) return false;
        } else if (!readNumber(s, i, a[0])) {
            return false;
        }
        if constexpr (N == 3) out.push_back(a);
        else out.push_back(a[0]);
        return true;
    }
    if (kind != "nonuniform") {
        if (err) *err = "internalField khong doc duoc: " + kind;
        return false;
    }
    std::string list;
    readToken(s, i, list);   // List<vector> hoặc List<scalar>
    double cnt = 0;
    if (!readNumber(s, i, cnt)) return false;
    const std::size_t n = static_cast<std::size_t>(cnt);
    skipSpace(s, i);
    if (i >= s.size() || s[i] != '(') {
        if (err) *err = "Dinh dang List khong hop le";
        return false;
    }
    ++i;
    out.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        if constexpr (N == 3) {
            skipSpace(s, i);
            if (i >= s.size() || s[i] != '(') return false;
            ++i;
            std::array<double, 3> a{};
            for (int c = 0; c < 3; ++c)
                if (!readNumber(s, i, a[c])) return false;
            skipSpace(s, i);
            if (i >= s.size() || s[i] != ')') return false;
            ++i;
            out.push_back(a);
        } else {
            double x = 0;
            if (!readNumber(s, i, x)) return false;
            out.push_back(x);
        }
    }
    return true;
}

}  // namespace

bool readFoamVectorField(const std::string& path, std::vector<std::array<double, 3>>& out, std::string* error) {
    std::string s;
    out.clear();
    if (!slurp(path, s, error)) return false;
    if (!parseField<3>(s, out, error)) {
        if (error && error->empty()) *error = "Khong doc duoc truong vector trong " + path;
        return false;
    }
    return true;
}

bool readFoamScalarField(const std::string& path, std::vector<double>& out, std::string* error) {
    std::string s;
    out.clear();
    if (!slurp(path, s, error)) return false;
    if (!parseField<1>(s, out, error)) {
        if (error && error->empty()) *error = "Khong doc duoc truong vo huong trong " + path;
        return false;
    }
    return true;
}

void cleanOpenFoamResults(const std::string& caseDir) {
    std::error_code ec;
    if (!fs::is_directory(caseDir, ec)) return;
    for (const auto& e : fs::directory_iterator(caseDir, ec)) {
        const std::string name = e.path().filename().string();
        if (e.is_directory(ec)) {
            char* end = nullptr;
            const double t = std::strtod(name.c_str(), &end);
            const bool numeric = !name.empty() && end && *end == '\0';
            if ((numeric && t != 0.0) || name == "VTK" || name == "postProcessing" || name.rfind("processor", 0) == 0) fs::remove_all(e.path(), ec);
        } else if (name.rfind("log.", 0) == 0) {
            fs::remove(e.path(), ec);
        }
    }
}

std::string latestFoamTime(const std::string& caseDir) {
    std::string best;
    double bestVal = -1;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(caseDir, ec)) {
        if (!e.is_directory()) continue;
        const std::string name = e.path().filename().string();
        char* end = nullptr;
        const double v = std::strtod(name.c_str(), &end);
        if (end == name.c_str() || *end != '\0') continue;
        if (v > bestVal) {
            bestVal = v;
            best = name;
        }
    }
    return best;
}

bool readOpenFoamCavity(const std::string& caseDir, double length, double lidSpeed, OpenFoamCavityResult& out, std::string* error) {
    out = {};
    out.length = length;
    out.lidSpeed = lidSpeed;
    out.time = latestFoamTime(caseDir);
    if (out.time.empty() || out.time == "0") {
        if (error) *error = "OpenFOAM chua ghi ket qua nao trong " + caseDir + " (xem log.icoFoam)";
        return false;
    }
    std::vector<std::array<double, 3>> U;
    if (!readFoamVectorField((fs::path(caseDir) / out.time / "U").string(), U, error)) return false;
    const int n = static_cast<int>(std::lround(std::sqrt(static_cast<double>(U.size()))));
    if (n < 2 || static_cast<std::size_t>(n) * n != U.size()) {
        if (error) *error = "So o khong phai n x n (" + std::to_string(U.size()) + ")";
        return false;
    }
    out.n = n;
    out.u.resize(U.size());
    out.v.resize(U.size());
    for (std::size_t k = 0; k < U.size(); ++k) {
        out.u[k] = U[k][0];
        out.v[k] = U[k][1];
    }
    std::vector<double> P;
    if (readFoamScalarField((fs::path(caseDir) / out.time / "p").string(), P, nullptr) && P.size() == U.size()) out.p = P;
    else out.p.assign(U.size(), 0.0);
    return true;
}

void cavityNodalFields(const OpenFoamCavityResult& r, std::vector<double>& u, std::vector<double>& v, std::vector<double>& p) {
    const int n = r.n, P = n + 1;
    u.assign(static_cast<std::size_t>(P) * P, 0.0);
    v = u;
    p = u;
    for (int j = 0; j < P; ++j)
        for (int i = 0; i < P; ++i) {
            double su = 0, sv = 0, sp = 0;
            int c = 0;
            for (int dj = -1; dj <= 0; ++dj)
                for (int di = -1; di <= 0; ++di) {
                    const int ci = i + di, cj = j + dj;
                    if (ci < 0 || cj < 0 || ci >= n || cj >= n) continue;
                    const std::size_t k = static_cast<std::size_t>(cj) * n + ci;
                    su += r.u[k];
                    sv += r.v[k];
                    sp += r.p[k];
                    ++c;
                }
            const std::size_t q = static_cast<std::size_t>(j) * P + i;
            u[q] = su / c;
            v[q] = sv / c;
            p[q] = sp / c;
            if (i == 0 || i == n || j == 0 || j == n) {   // điều kiện biên: tường đứng yên, nắp trượt
                u[q] = (j == n) ? r.lidSpeed : 0.0;
                v[q] = 0.0;
            }
        }
    // hai nút góc trên thuộc cả tường lẫn nắp: OpenFOAM coi nắp (movingWall) là mặt trên
}

std::string findOpenFoamBashrc(const std::string& hint) {
    auto isFile = [](const fs::path& p) {
        std::error_code ec;
        return fs::is_regular_file(p, ec);
    };
    if (!hint.empty()) {
        if (isFile(hint) && fs::path(hint).filename() == "bashrc") return hint;
        if (isFile(fs::path(hint) / "etc" / "bashrc")) return (fs::path(hint) / "etc" / "bashrc").string();
    }
    if (const char* e = std::getenv("FASTCAE_OPENFOAM_BASHRC"))
        if (*e && isFile(e)) return e;
    if (const char* wm = std::getenv("WM_PROJECT_DIR"))
        if (*wm && isFile(fs::path(wm) / "etc" / "bashrc")) return (fs::path(wm) / "etc" / "bashrc").string();
    std::vector<std::string> cands = {"/usr/share/openfoam/etc/bashrc", "/usr/lib/openfoam/etc/bashrc"};
    for (const char* root : {"/opt", "/usr/lib/openfoam", "/usr/local"}) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& e : fs::directory_iterator(root, ec)) {
            const std::string name = e.path().filename().string();
            if (name.rfind("openfoam", 0) == 0 || name.rfind("OpenFOAM", 0) == 0) cands.push_back((e.path() / "etc" / "bashrc").string());
        }
    }
    for (const auto& c : cands)
        if (isFile(c)) return c;
    return {};
}

std::string openFoamShellScript(const std::string& bashrc, const std::string& caseDir, bool vtk) {
    const std::string c = "\"" + caseDir + "\"";
    std::string s;
    if (!bashrc.empty()) s += "source \"" + bashrc + "\" >/dev/null 2>&1; ";
    s += "blockMesh -case " + c + " > " + c.substr(0, c.size() - 1) + "/log.blockMesh\" 2>&1 && ";
    s += "icoFoam -case " + c + " > " + c.substr(0, c.size() - 1) + "/log.icoFoam\" 2>&1";
    if (vtk) s += "; foamToVTK -case " + c + " -latestTime > " + c.substr(0, c.size() - 1) + "/log.foamToVTK\" 2>&1 || true";
    return s;
}

}  // namespace fastcae
