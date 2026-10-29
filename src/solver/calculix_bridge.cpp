// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/solver_registry.hpp"
#include "core/paths.hpp"
#include "solver//calculix_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace fastcae {

bool writeCalculixInp(const std::string& path, const Mesh& mesh, const Material& mat,
                      const std::vector<DisplacementBC>& bcs, const std::vector<NodalForce>& loads,
                      std::string* error, const std::vector<std::array<int, 10>>* tets10) {
    std::ofstream os(path);
    if (!os) {
        if (error) *error = "Cannot write " + path;
        return false;
    }
    os << std::setprecision(12);
    os << "** FastCAE -> CalculiX deck (linear static, C3D4)\n*HEADING\nFastCAE job\n";
    os << "*NODE, NSET=NALL\n";
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i)
        os << i + 1 << ", " << mesh.nodes[i].x << ", " << mesh.nodes[i].y << ", " << mesh.nodes[i].z << "\n";
    if (tets10) {
        os << "*ELEMENT, TYPE=C3D10, ELSET=EALL\n";
        for (std::size_t c = 0; c < tets10->size(); ++c) {
            os << c + 1;
            for (int a : (*tets10)[c]) os << ", " << a + 1;
            os << "\n";
        }
    } else {
        os << "*ELEMENT, TYPE=C3D4, ELSET=EALL\n";
        for (std::size_t c = 0; c < mesh.tets.size(); ++c) {
            const auto& t = mesh.tets[c];
            os << c + 1 << ", " << t[0] + 1 << ", " << t[1] + 1 << ", " << t[2] + 1 << ", " << t[3] + 1 << "\n";
        }
    }
    os << "*MATERIAL, NAME=MAT1\n*ELASTIC\n" << mat.E << ", " << mat.nu << "\n";
    os << "*DENSITY\n" << mat.rho << "\n";
    os << "*SOLID SECTION, ELSET=EALL, MATERIAL=MAT1\n";
    os << "*STEP\n*STATIC\n";
    if (!bcs.empty()) {
        os << "*BOUNDARY\n";
        for (const auto& b : bcs) os << b.node + 1 << ", " << b.component + 1 << ", " << b.component + 1 << ", " << b.value << "\n";
    }
    if (!loads.empty()) {
        os << "*CLOAD\n";
        for (const auto& l : loads) {
            if (l.f.x != 0) os << l.node + 1 << ", 1, " << l.f.x << "\n";
            if (l.f.y != 0) os << l.node + 1 << ", 2, " << l.f.y << "\n";
            if (l.f.z != 0) os << l.node + 1 << ", 3, " << l.f.z << "\n";
        }
    }
    os << "*NODE FILE\nU\n*EL FILE\nS\n*END STEP\n";
    return static_cast<bool>(os);
}

// ccx ASCII frd layout (fixed width):
//   " -4  DISP        4    1"            block header (name in cols 6-13)
//   " -5  D1          1    2    1    0"  component descriptors
//   " -1" + I10 node id + n * E12.5      node record (n values, 6 per line; continuation lines start " -2")
//   " -3"                                 end of block
bool readFrd(const std::string& path, FrdResults& out, std::string* error) {
    std::ifstream is(path);
    if (!is) {
        if (error) *error = "Cannot open " + path;
        return false;
    }
    out.blocks.clear();
    std::string line, current;
    int ncomp = 0;
    std::vector<double>* lastRec = nullptr;
    while (std::getline(is, line)) {
        if (line.size() < 3) continue;
        const std::string tag = line.substr(0, 3);
        if (tag == " -4") {
            std::istringstream ss(line.substr(3));
            ss >> current >> ncomp;   // e.g. "DISP 4 1": 4 = components incl. ALL
            current = current.substr(0, current.find_first_of(" \t"));
            lastRec = nullptr;
        } else if (tag == " -3") {
            current.clear();
            lastRec = nullptr;
        } else if (tag == " -1" && !current.empty()) {
            if (line.size() < 13) continue;
            const int id = std::atoi(line.substr(3, 10).c_str());
            auto& rec = out.blocks[current][id - 1];
            rec.clear();
            for (std::size_t pos = 13; pos + 12 <= line.size(); pos += 12)
                rec.push_back(std::atof(line.substr(pos, 12).c_str()));
            lastRec = &rec;
        } else if (tag == " -2" && lastRec) {
            for (std::size_t pos = 13; pos + 12 <= line.size(); pos += 12)
                lastRec->push_back(std::atof(line.substr(pos, 12).c_str()));
        }
    }
    (void)ncomp;
    if (out.blocks.empty()) {
        if (error) *error = "No result blocks found in " + path;
        return false;
    }
    return true;
}

std::string findCalculix(const std::string& hint) {
    // cùng bộ dò với bảng Quản lý solver: đường dẫn đặt tay (file hoặc thư mục) -> FASTCAE_CCX -> PATH
    return findExecutable({"ccx", "ccx_2.22", "ccx_2.21", "ccx_2.20", "ccx_2.19", "ccx_static"}, hint, "FASTCAE_CCX");
}

CalculixRun runCalculix(const Mesh& mesh, const Material& mat, const std::vector<DisplacementBC>& bcs,
                        const std::vector<NodalForce>& loads, const CalculixOptions& opt,
                        const std::vector<std::array<int, 10>>* tets10) {
    CalculixRun run;
    std::error_code ec;
    fs::create_directories(opt.workDir, ec);
    const fs::path dir = fs::absolute(fastcae::resolveWorkDir(opt.workDir, "", "ccx_run"));
    run.inpPath = (dir / (opt.jobName + ".inp")).string();
    run.frdPath = (dir / (opt.jobName + ".frd")).string();

    std::string err;
    if (!writeCalculixInp(run.inpPath, mesh, mat, bcs, loads, &err, tets10)) { run.message = err; return run; }

    const std::string exe = findCalculix(opt.executable);
    if (exe.empty()) {
        run.message = "Khong tim thay CalculiX (ccx). Da ghi file " + run.inpPath +
                      ". Cai ccx va dat bien moi truong FASTCAE_CCX hoac them vao PATH.";
        return run;
    }
    // ccx is invoked as: ccx <jobname> (without extension), cwd = work dir.
#ifdef _WIN32
    const std::string cmd = "cd /d \"" + dir.string() + "\" && \"" + exe + "\" " + opt.jobName + " > " + opt.jobName + ".log 2>&1";
#else
    const std::string cmd = "cd \"" + dir.string() + "\" && \"" + exe + "\" " + opt.jobName + " > " + opt.jobName + ".log 2>&1";
#endif
    const int rc = std::system(cmd.c_str());   // không đổi thư mục làm việc của cả tiến trình (an toàn với luồng nền)
    if (rc != 0 || !fs::exists(run.frdPath)) {
        run.message = "ccx that bai (ma " + std::to_string(rc) + "), xem " + (dir / (opt.jobName + ".log")).string();
        return run;
    }

    FrdResults frd;
    if (!readFrd(run.frdPath, frd, &err)) { run.message = err; return run; }

    const std::size_t n = mesh.numNodes();
    ElasticResult& r = run.result;
    r.u.assign(3 * n, 0.0);
    r.dispMagnitude.assign(n, 0.0);
    r.vonMisesNodal.assign(n, 0.0);
    if (auto it = frd.blocks.find("DISP"); it != frd.blocks.end())
        for (const auto& [node, v] : it->second)
            if (node >= 0 && node < static_cast<int>(n) && v.size() >= 3) {
                for (int k = 0; k < 3; ++k) r.u[3 * node + k] = v[k];
                r.dispMagnitude[node] = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                r.maxDisplacement = std::max(r.maxDisplacement, r.dispMagnitude[node]);
            }
    if (auto it = frd.blocks.find("STRESS"); it != frd.blocks.end())
        for (const auto& [node, s] : it->second)
            if (node >= 0 && node < static_cast<int>(n) && s.size() >= 6) {
                const double vm = std::sqrt(0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) +
                                                   (s[2] - s[0]) * (s[2] - s[0])) +
                                            3.0 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
                r.vonMisesNodal[node] = vm;
                r.maxVonMises = std::max(r.maxVonMises, vm);
            }
    r.linear.converged = true;
    run.ok = true;
    run.message = "CalculiX hoan tat";
    return run;
}

}  // namespace fastcae
