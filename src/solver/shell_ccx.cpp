// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "core/paths.hpp"
#include "solver//shell_ccx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include "solver/calculix_bridge.hpp"

namespace fs = std::filesystem;

namespace fastcae {

namespace {
constexpr double kPi = 3.14159265358979323846;
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double norm(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
}  // namespace

ShellMesh makeShellPlate(double Lx, double Ly, double t, int nx, int ny) {
    nx = std::max(nx, 1);
    ny = std::max(ny, 1);
    ShellMesh m;
    m.thickness = t;
    m.nx = nx;
    m.ny = ny;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) {
            m.nodes.push_back({Lx * i / nx, Ly * j / ny, 0});
            m.normals.push_back({0, 0, 1});
            m.pressureDir.push_back({0, 0, -1});
        }
    auto id = [&](int i, int j) { return j * (nx + 1) + i; };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) m.quads.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1), id(i, j + 1)});
    return m;
}

ShellMesh makeShellCylinder(double R, double L, double t, int nc, int nl) {
    nc = std::max(nc, 8);
    nl = std::max(nl, 1);
    ShellMesh m;
    m.thickness = t;
    m.nx = nc;
    m.ny = nl;
    for (int j = 0; j <= nl; ++j)
        for (int i = 0; i < nc; ++i) {
            const double th = 2 * kPi * i / nc;
            m.nodes.push_back({R * std::cos(th), R * std::sin(th), L * j / nl});
            m.normals.push_back({std::cos(th), std::sin(th), 0});
            m.pressureDir.push_back({std::cos(th), std::sin(th), 0});
        }
    auto id = [&](int i, int j) { return j * nc + (i % nc); };
    for (int j = 0; j < nl; ++j)
        for (int i = 0; i < nc; ++i) m.quads.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1), id(i, j + 1)});
    return m;
}

std::vector<int> selectShellNodes(const ShellMesh& m, const std::string& name) {
    std::vector<int> out;
    if (m.nodes.empty()) return out;
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const auto& p : m.nodes) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-12});
    const double tol = 1e-6 * size;
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        const auto& p = m.nodes[i];
        bool in = false;
        if (name == "all") in = true;
        else if (name == "xmin") in = p.x - lo.x <= tol;
        else if (name == "xmax") in = hi.x - p.x <= tol;
        else if (name == "ymin") in = p.y - lo.y <= tol;
        else if (name == "ymax") in = hi.y - p.y <= tol;
        else if (name == "zmin") in = p.z - lo.z <= tol;
        else if (name == "zmax") in = hi.z - p.z <= tol;
        if (in) out.push_back(static_cast<int>(i));
    }
    return out;
}

std::vector<ShellLoad> shellPressureLoads(const ShellMesh& m, double p) {
    std::vector<double> area(m.nodes.size(), 0.0);
    for (const auto& q : m.quads) {
        // diện tích tứ giác = nửa độ dài tích có hướng hai đường chéo
        const Vec3 d1 = sub(m.nodes[q[2]], m.nodes[q[0]]), d2 = sub(m.nodes[q[3]], m.nodes[q[1]]);
        const double A = 0.5 * norm(cross(d1, d2));
        for (int k = 0; k < 4; ++k) area[q[k]] += A / 4;
    }
    std::vector<ShellLoad> out;
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        const Vec3& d = m.pressureDir[i];
        out.push_back({static_cast<int>(i), {p * area[i] * d.x, p * area[i] * d.y, p * area[i] * d.z}});
    }
    return out;
}

std::vector<ShellLoad> shellForceLoads(const std::vector<int>& nodes, const Vec3& total) {
    std::vector<ShellLoad> out;
    if (nodes.empty()) return out;
    const double s = 1.0 / static_cast<double>(nodes.size());
    for (int n : nodes) out.push_back({n, {total.x * s, total.y * s, total.z * s}});
    return out;
}

bool writeShellInp(const std::string& path, const ShellMesh& m, const Material& mat, const std::vector<ShellFix>& fixes,
                   const std::vector<ShellLoad>& loads, std::string* error) {
    std::ofstream f(path);
    if (!f) {
        if (error) *error = "Khong ghi duoc " + path;
        return false;
    }
    f.precision(12);
    f << "** FastCAE shell deck - Tran Minh Tam (MIT)\n*NODE\n";
    for (std::size_t i = 0; i < m.nodes.size(); ++i) f << i + 1 << ", " << m.nodes[i].x << ", " << m.nodes[i].y << ", " << m.nodes[i].z << "\n";
    f << "*ELEMENT, TYPE=S4, ELSET=EALL\n";
    for (std::size_t e = 0; e < m.quads.size(); ++e)
        f << e + 1 << ", " << m.quads[e][0] + 1 << ", " << m.quads[e][1] + 1 << ", " << m.quads[e][2] + 1 << ", " << m.quads[e][3] + 1 << "\n";
    f << "*MATERIAL, NAME=MAT\n*ELASTIC\n" << mat.E << ", " << mat.nu << "\n*DENSITY\n" << mat.rho << "\n";
    f << "*SHELL SECTION, ELSET=EALL, MATERIAL=MAT\n" << m.thickness << "\n";
    f << "*BOUNDARY\n";
    for (const auto& fx : fixes)
        for (int n : fx.nodes)
            for (int d = 0; d < 6; ++d)
                if (fx.dof[d]) f << n + 1 << ", " << d + 1 << ", " << d + 1 << "\n";
    f << "*STEP\n*STATIC\n*CLOAD\n";
    for (const auto& l : loads) {
        if (l.f.x != 0) f << l.node + 1 << ", 1, " << l.f.x << "\n";
        if (l.f.y != 0) f << l.node + 1 << ", 2, " << l.f.y << "\n";
        if (l.f.z != 0) f << l.node + 1 << ", 3, " << l.f.z << "\n";
    }
    // OUTPUT=3D (mặc định của ccx) mở rộng vỏ thành các lớp nút để lấy ứng suất mặt trên/dưới
    f << "*NODE FILE\nU\n*EL FILE\nS\n*END STEP\n";
    return true;
}

namespace {

// Toạ độ nút trong .frd (khối "    2C"): id -> vị trí
bool readFrdCoordinates(const std::string& path, std::map<int, Vec3>& out) {
    std::ifstream is(path);
    if (!is) return false;
    std::string line;
    bool inBlock = false;
    while (std::getline(is, line)) {
        if (line.rfind("    2C", 0) == 0) {
            inBlock = true;
            continue;
        }
        if (inBlock && line.rfind(" -3", 0) == 0) break;
        if (inBlock && line.rfind(" -1", 0) == 0 && line.size() >= 49) {
            const int id = std::atoi(line.substr(3, 10).c_str());
            out[id] = {std::atof(line.substr(13, 12).c_str()), std::atof(line.substr(25, 12).c_str()), std::atof(line.substr(37, 12).c_str())};
        }
    }
    return !out.empty();
}

}  // namespace

ShellResult runCalculixShell(const ShellMesh& m, const Material& mat, const std::vector<ShellFix>& fixes,
                             const std::vector<ShellLoad>& loads, const ShellOptions& opt) {
    ShellResult res;
    std::error_code ec;
    fs::create_directories(opt.workDir, ec);
    const fs::path dir = fs::absolute(fastcae::resolveWorkDir(opt.workDir, "", "ccx_run"));
    res.inpPath = (dir / (opt.jobName + ".inp")).string();
    const std::string frdPath = (dir / (opt.jobName + ".frd")).string();
    std::string err;
    if (!writeShellInp(res.inpPath, m, mat, fixes, loads, &err)) { res.message = err; return res; }
    const std::string exe = findCalculix(opt.executable);
    if (exe.empty()) {
        res.message = "Khong tim thay CalculiX (ccx). Da ghi file " + res.inpPath + ". Cai ccx va dat FASTCAE_CCX hoac them vao PATH.";
        return res;
    }
    fs::remove(frdPath, ec);
#ifdef _WIN32
    const std::string cmd = "cd /d \"" + dir.string() + "\" && \"" + exe + "\" " + opt.jobName + " > " + opt.jobName + ".log 2>&1";
#else
    const std::string cmd = "cd \"" + dir.string() + "\" && \"" + exe + "\" " + opt.jobName + " > " + opt.jobName + ".log 2>&1";
#endif
    const int rc = std::system(cmd.c_str());
    if (rc != 0 || !fs::exists(frdPath)) {
        res.message = "ccx that bai (ma " + std::to_string(rc) + "), xem " + (dir / (opt.jobName + ".log")).string();
        return res;
    }
    FrdResults frd;
    if (!readFrd(frdPath, frd, &err)) { res.message = err; return res; }
    std::map<int, Vec3> coords;   // id (1-based) -> vị trí, theo khoá của readFrd là id-1
    if (!readFrdCoordinates(frdPath, coords)) { res.message = "Khong doc duoc toa do nut trong .frd"; return res; }

    // Ghép nút mở rộng của ccx về nút mặt trung bình gốc theo vị trí (cách tối đa t/2 theo pháp tuyến)
    const double reach = 0.5 * m.thickness * 1.001 + 1e-9;
    const double cell = std::max(m.thickness, 1e-9);
    auto key = [&](const Vec3& p) {
        return std::array<long long, 3>{static_cast<long long>(std::floor(p.x / cell)), static_cast<long long>(std::floor(p.y / cell)),
                                        static_cast<long long>(std::floor(p.z / cell))};
    };
    struct H {
        std::size_t operator()(const std::array<long long, 3>& k) const {
            return static_cast<std::size_t>(k[0] * 73856093LL ^ k[1] * 19349663LL ^ k[2] * 83492791LL);
        }
    };
    std::unordered_map<std::array<long long, 3>, std::vector<int>, H> grid;
    for (std::size_t i = 0; i < m.nodes.size(); ++i) grid[key(m.nodes[i])].push_back(static_cast<int>(i));
    const std::size_t n = m.nodes.size();
    res.u.assign(3 * n, 0.0);
    res.dispMagnitude.assign(n, 0.0);
    res.vonMises.assign(n, 0.0);
    std::vector<int> cnt(n, 0);
    const auto dispIt = frd.blocks.find("DISP");
    const auto strIt = frd.blocks.find("STRESS");
    if (dispIt == frd.blocks.end()) { res.message = "Khong co chuyen vi trong .frd"; return res; }
    for (const auto& [idx0, v] : dispIt->second) {
        auto cit = coords.find(idx0 + 1);
        if (cit == coords.end() || v.size() < 3) continue;
        const Vec3& p = cit->second;
        const auto k = key(p);
        int best = -1;
        double bd = 1e300;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    auto g = grid.find({k[0] + dx, k[1] + dy, k[2] + dz});
                    if (g == grid.end()) continue;
                    for (int i : g->second) {
                        const double d = norm(sub(m.nodes[i], p));
                        if (d < bd) { bd = d; best = i; }
                    }
                }
        if (best < 0 || bd > reach) continue;
        for (int c = 0; c < 3; ++c) res.u[3 * best + c] += v[c];
        ++cnt[best];
        if (strIt != frd.blocks.end()) {
            auto sit = strIt->second.find(idx0);
            if (sit != strIt->second.end() && sit->second.size() >= 6) {
                const auto& s = sit->second;
                const double vm = std::sqrt(0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) + (s[2] - s[0]) * (s[2] - s[0])) +
                                            3.0 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
                res.vonMises[best] = std::max(res.vonMises[best], vm);
            }
        }
    }
    int unmapped = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (cnt[i] == 0) { ++unmapped; continue; }
        for (int c = 0; c < 3; ++c) res.u[3 * i + c] /= cnt[i];
        res.dispMagnitude[i] = std::sqrt(res.u[3 * i] * res.u[3 * i] + res.u[3 * i + 1] * res.u[3 * i + 1] + res.u[3 * i + 2] * res.u[3 * i + 2]);
        res.maxDisplacement = std::max(res.maxDisplacement, res.dispMagnitude[i]);
        res.maxVonMises = std::max(res.maxVonMises, res.vonMises[i]);
    }
    if (unmapped > static_cast<int>(n) / 10) {
        res.message = "Khong ghep duoc ket qua ccx ve nut goc (" + std::to_string(unmapped) + "/" + std::to_string(n) + " nut)";
        return res;
    }
    res.ok = true;
    res.message = "CalculiX hoan tat (phan tu vo S4)";
    return res;
}

void shellRender(const ShellMesh& m, const ShellResult& r, Mesh& mesh, VtkFields& fields, double renderT) {
    mesh = {};
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const auto& p : m.nodes) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-9});
    const double t = renderT > 0 ? renderT : std::max(m.thickness, 0.01 * size);   // phóng to độ dày để nhìn thấy
    const std::size_t N = m.nodes.size();
    for (int layer = 0; layer < 2; ++layer)
        for (std::size_t i = 0; i < N; ++i) {
            const double s = (layer == 0 ? -0.5 : 0.5) * t;
            mesh.nodes.push_back({m.nodes[i].x + m.normals[i].x * s, m.nodes[i].y + m.normals[i].y * s, m.nodes[i].z + m.normals[i].z * s});
        }
    static const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (const auto& q : m.quads) {
        auto id = [&](int i, int j, int k) {
            const int corner = (j == 0) ? i : (i == 0 ? 3 : 2);   // (0,0)=q0 (1,0)=q1 (1,1)=q2 (0,1)=q3
            return q[corner] + (k ? static_cast<int>(N) : 0);
        };
        for (const auto& p : perms) {
            std::array<int, 3> o{0, 0, 0};
            Tet tt;
            tt[0] = id(0, 0, 0);
            for (int s = 0; s < 3; ++s) {
                o[p[s]] = 1;
                tt[s + 1] = id(o[0], o[1], o[2]);
            }
            mesh.tets.push_back(tt);
        }
    }
    mesh.fixOrientation();
    if (!r.ok) return;
    std::vector<double> vec(6 * N), mag(2 * N), vm(2 * N);
    for (int layer = 0; layer < 2; ++layer)
        for (std::size_t i = 0; i < N; ++i) {
            const std::size_t k = i + layer * N;
            for (int c = 0; c < 3; ++c) vec[3 * k + c] = r.u[3 * i + c];
            mag[k] = r.dispMagnitude[i];
            vm[k] = r.vonMises[i];
        }
    fields.pointVectors["displacement"] = vec;
    fields.pointScalars["displacement_magnitude"] = mag;
    fields.pointScalars["von_mises_nodal"] = vm;
}

}  // namespace fastcae
