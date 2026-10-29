// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
// Dependency-free unit/validation tests for FastCAE.
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include "cad/cad_import.hpp"
#include "core/case_file.hpp"
#include "mesh/box_mesher.hpp"
#include <fstream>
#include "solver/calculix_bridge.hpp"
#include "solver/cavity_cfd.hpp"
#include "solver/quadratic.hpp"
#include "solver/elastic.hpp"
#include "solver/thermal.hpp"
#include "solver/openfoam_case.hpp"
#include "cad/shape_library.hpp"
#include "solver/frame.hpp"
#include "solver/shell_ccx.hpp"
#include "cad/frame_templates.hpp"
#include "script/script_engine.hpp"
#include <atomic>
#include <map>
#include "solver/sparse.hpp"
#include "solver/openfoam_results.hpp"
#include <fstream>
#include "solver/solver_registry.hpp"
#include "core/about.hpp"
#include "core/vtk_reader.hpp"
#include "app/case_runner.hpp"
#include "solver/ccx_analysis.hpp"
#include "solver/openfoam_3d.hpp"
#include <sstream>
#include <cstdio>
#include <filesystem>

using namespace fastcae;

static int failures = 0;
#define CHECK(cond, msg)                                                         \
    do {                                                                         \
        if (!(cond)) { std::cerr << "FAIL: " << (msg) << "  [" #cond "]\n"; ++failures; } \
        else std::cout << "ok:   " << (msg) << "\n";                               \
    } while (0)

static void testMesh() {
    Mesh m = makeBoxMesh(2.0, 1.0, 0.5, 4, 3, 2);
    CHECK(m.numCells() == 4 * 3 * 2 * 6, "box mesh has 6 tets per hexahedron");
    CHECK(std::abs(m.totalVolume() - 1.0) < 1e-12, "box mesh volume = 2*1*0.5");
    bool positive = true;
    for (std::size_t c = 0; c < m.numCells(); ++c) positive &= m.tetVolume(c) > 0;
    CHECK(positive, "all tets positively oriented");
    CHECK(selectNodes(m, "xmin").size() == 4 * 3, "xmin face node count");
}

static void testPatchTension() {
    // Displacement-driven uniaxial tension: linear tets reproduce this exactly.
    const double L = 2, W = 1, Hh = 1, E = 100e9, nu = 0.25, eps = 1e-3;
    Mesh m = makeBoxMesh(L, W, Hh, 4, 2, 2);
    std::vector<DisplacementBC> bcs;
    for (int n : selectNodes(m, "xmin")) bcs.push_back({n, 0, 0});
    for (int n : selectNodes(m, "xmax")) bcs.push_back({n, 0, eps * L});
    for (int n : selectNodes(m, "ymin")) bcs.push_back({n, 1, 0});
    for (int n : selectNodes(m, "zmin")) bcs.push_back({n, 2, 0});
    Material mat; mat.E = E; mat.nu = nu;
    auto r = solveElastic(m, mat, bcs, {});
    const double sigma = E * eps;
    CHECK(r.linear.converged, "patch test: CG converged");
    double minVm = 1e300;
    for (double v : r.vonMises) minVm = std::min(minVm, v);
    CHECK(std::abs(r.maxVonMises - sigma) / sigma < 1e-6 && std::abs(minVm - sigma) / sigma < 1e-6,
          "patch test: uniform von Mises = E*eps in every element");
    // Poisson contraction of the free lateral face: u_y(ymax) = -nu*eps*W.
    double uy = 0;
    for (int n : selectNodes(m, "ymax")) uy += r.u[3 * n + 1];
    uy /= selectNodes(m, "ymax").size();
    CHECK(std::abs(uy - (-nu * eps * W)) / (nu * eps * W) < 1e-6, "patch test: Poisson contraction correct");
}

static void testCantilever() {
    const double L = 1.0, b = 0.1, h = 0.1, E = 210e9, P = 1000;
    Mesh m = makeBoxMesh(L, b, h, 20, 3, 3);
    std::vector<DisplacementBC> bcs;
    for (int n : selectNodes(m, "xmin")) for (int c = 0; c < 3; ++c) bcs.push_back({n, c, 0});
    auto right = selectNodes(m, "xmax");
    std::vector<NodalForce> loads;
    for (int n : right) loads.push_back({n, {0, 0, -P / right.size()}});
    Material mat; mat.E = E; mat.nu = 0.3;
    auto r = solveElastic(m, mat, bcs, loads);
    const double beam = P * L * L * L / (3 * E * (b * h * h * h / 12.0));
    double tip = 0;
    for (int n : right) tip = std::max(tip, std::abs(r.u[3 * n + 2]));
    std::cout << "      cantilever FEM tip = " << tip << ", beam theory = " << beam
              << ", ratio = " << tip / beam << "\n";
    CHECK(r.linear.converged, "cantilever: CG converged");
    // 4-node tets are known to be over-stiff in bending: require a sane, bounded answer.
    CHECK(tip / beam > 0.35 && tip / beam < 1.15, "cantilever: tip deflection within expected range of beam theory");
}

static void testThermal() {
    Mesh m = makeBoxMesh(1, 0.2, 0.2, 10, 2, 2);
    std::vector<TemperatureBC> bcs;
    for (int n : selectNodes(m, "xmin")) bcs.push_back({n, 100});
    for (int n : selectNodes(m, "xmax")) bcs.push_back({n, 0});
    Material mat; mat.k = 45;
    auto r = solveThermal(m, mat, bcs, {});
    double err = 0;
    for (std::size_t i = 0; i < m.numNodes(); ++i)
        err = std::max(err, std::abs(r.T[i] - 100.0 * (1.0 - m.nodes[i].x)));
    CHECK(r.linear.converged && err < 1e-6, "thermal: linear temperature profile reproduced exactly");
}

static void testCavity() {
    CavityParams p;
    p.n = 41; p.Re = 100;
    auto r = solveCavity(p);
    const double uc = r.uAt(20, 20);
    std::cout << "      cavity steps=" << r.steps << " change=" << r.change << " u(0.5,0.5)=" << uc
              << " (Ghia 1982: -0.2058)\n";
    CHECK(r.converged, "cavity: reached steady state");
    CHECK(std::abs(uc - (-0.2058)) < 0.03, "cavity: centre u-velocity close to Ghia et al. benchmark");
}

static void testCaseFileAndCad() {
    CaseFile cf;
    cf.parse("type = Elastic # c\nfix = xmin\nfix = ymin y\nE = 2e11\n");
    CHECK(cf.getAll("fix").size() == 2, "case file: repeated keys");
    CHECK(cf.getDouble("e", 0) == 2e11, "case file: numbers, case-insensitive keys");
    CHECK(detectCadFormat("part.SLDPRT") == CadFormat::Native, "cad: SLDPRT detected as native format");
    CHECK(detectCadFormat("a.stp") == CadFormat::Step, "cad: STEP detected");
    auto r = makePrimitive("box", {1, 1, 1}, 0.25);
    CHECK(r.ok && r.mesh.numCells() > 0, "cad: built-in box primitive");
}

static void testCalculixBridge() {
    Mesh m = makeBoxMesh(1, 1, 1, 1, 1, 1);
    Material mat;
    std::vector<DisplacementBC> bcs{{0, 0, 0}, {0, 1, 0}, {0, 2, 0}};
    std::vector<NodalForce> loads{{7, {0, 0, -10}}};
    const std::string inp = "bridge_test.inp";
    CHECK(writeCalculixInp(inp, m, mat, bcs, loads), "calculix: deck written");
    std::ifstream is(inp);
    std::string all((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    CHECK(all.find("*ELEMENT, TYPE=C3D4") != std::string::npos && all.find("*CLOAD") != std::string::npos &&
          all.find("*BOUNDARY") != std::string::npos, "calculix: deck contains nodes, elements, BC, loads");

    // Synthetic .frd in ccx fixed-width layout.
    {
        std::ofstream f("bridge_test.frd");
        f << "    1C\n"
          << " -4  DISP        4    1\n -5  D1          1    2    1    0\n"
          << " -1         1 1.00000E-03-2.00000E-03 3.00000E-03\n"
          << " -1         2 0.00000E+00 0.00000E+00 0.00000E+00\n -3\n"
          << " -4  STRESS      6    1\n"
          << " -1         1 1.00000E+06 0.00000E+00 0.00000E+00 0.00000E+00 0.00000E+00 0.00000E+00\n -3\n9999\n";
    }
    FrdResults fr;
    CHECK(readFrd("bridge_test.frd", fr), "calculix: frd parsed");
    CHECK(fr.blocks["DISP"][0].size() == 3 && std::abs(fr.blocks["DISP"][0][1] + 2e-3) < 1e-12,
          "calculix: frd displacement values incl. negative numbers");
    CHECK(fr.blocks["STRESS"][0].size() == 6 && fr.blocks["STRESS"][0][0] == 1e6, "calculix: frd stress");
    std::remove("bridge_test.inp"); std::remove("bridge_test.frd");
}

static void testQuadratic() {
    // 1) Mesh construction
    Mesh lin = makeBoxMesh(2, 1, 1, 2, 1, 1);
    QuadMesh q = makeQuadratic(lin);
    // Euler-type check: every tet has 6 distinct mid nodes, shared across neighbours.
    CHECK(q.tets10.size() == lin.tets.size() && q.base.numNodes() > lin.numNodes(), "T10: mid-edge nodes added");
    bool mids = true;
    for (std::size_t c = 0; c < q.tets10.size(); ++c) {
        const auto& t = q.tets10[c];
        const int ed[6][2] = {{0, 1}, {1, 2}, {0, 2}, {0, 3}, {1, 3}, {2, 3}};
        for (int k = 0; k < 6; ++k) {
            const Vec3 &a = q.base.nodes[t[ed[k][0]]], &b = q.base.nodes[t[ed[k][1]]], &m = q.base.nodes[t[4 + k]];
            mids &= std::abs(m.x - (a.x + b.x) / 2) + std::abs(m.y - (a.y + b.y) / 2) + std::abs(m.z - (a.z + b.z) / 2) < 1e-12;
        }
    }
    CHECK(mids, "T10: mid nodes are at edge midpoints, in ccx C3D10 order");

    // 2) Patch test: displacement-driven uniaxial tension must be exact.
    {
        const double L = 2, W = 1, Hh = 1, E = 100e9, nu = 0.25, eps = 1e-3;
        QuadMesh qm = makeQuadratic(makeBoxMesh(L, W, Hh, 2, 1, 1));
        std::vector<DisplacementBC> bcs;
        for (int n : selectNodes(qm.base, "xmin")) bcs.push_back({n, 0, 0});
        for (int n : selectNodes(qm.base, "xmax")) bcs.push_back({n, 0, eps * L});
        for (int n : selectNodes(qm.base, "ymin")) bcs.push_back({n, 1, 0});
        for (int n : selectNodes(qm.base, "zmin")) bcs.push_back({n, 2, 0});
        Material mat; mat.E = E; mat.nu = nu;
        auto r = solveElasticT10(qm, mat, bcs, {});
        const double sigma = E * eps;
        CHECK(r.linear.converged, "T10 patch test: CG converged");
        CHECK(std::abs(r.maxVonMises - sigma) / sigma < 1e-6, "T10 patch test: uniform stress E*eps");
    }

    // 3) Cantilever: consistent end load. T10 must be far closer to beam theory than T4.
    {
        const double L = 1.0, b = 0.1, h = 0.1, E = 210e9, P = 1000;
        const double beam = P * L * L * L / (3 * E * (b * h * h * h / 12.0));
        Material mat; mat.E = E; mat.nu = 0.3;
        Mesh lm = makeBoxMesh(L, b, h, 10, 2, 2);

        auto setup = [&](const Mesh& m, const QuadMesh* quad, std::vector<DisplacementBC>& bcs, std::vector<NodalForce>& loads) {
            for (int n : selectNodes(m, "xmin")) for (int c = 0; c < 3; ++c) bcs.push_back({n, c, 0});
            loads = distributeForce(m, quad, selectNodes(m, "xmax"), {0, 0, -P});
        };
        double fsum = 0;
        {
            std::vector<DisplacementBC> bcs; std::vector<NodalForce> loads;
            setup(lm, nullptr, bcs, loads);
            for (auto& l : loads) fsum += l.f.z;
            auto r4 = solveElastic(lm, mat, bcs, loads);
            double tip4 = 0;
            for (int n : selectNodes(lm, "xmax")) tip4 = std::max(tip4, std::abs(r4.u[3 * n + 2]));
            QuadMesh qm = makeQuadratic(lm);
            std::vector<DisplacementBC> bcs2; std::vector<NodalForce> loads2;
            setup(qm.base, &qm, bcs2, loads2);
            double f2 = 0, cornerLoad = 0;
            for (auto& l : loads2) { f2 += l.f.z; if (std::size_t(l.node) < qm.numCornerNodes) cornerLoad += std::abs(l.f.z); }
            auto r10 = solveElasticT10(qm, mat, bcs2, loads2);
            double tip10 = 0;
            for (int n : selectNodes(qm.base, "xmax")) tip10 = std::max(tip10, std::abs(r10.u[3 * n + 2]));
            std::cout << "      cantilever (10x2x2): T4 tip/beam = " << tip4 / beam << ", T10 tip/beam = " << tip10 / beam
                      << ", T10 max von Mises = " << r10.maxVonMises << " (beam root stress 6.0e6), iterations T10 = "
                      << r10.linear.iterations << "\n";
            CHECK(std::abs(fsum + P) < 1e-9 && std::abs(f2 + P) < 1e-9, "consistent load: total force preserved (T4 and T10)");
            CHECK(cornerLoad < 1e-12, "T10 consistent face load: corner nodes get zero, mid-edge nodes carry it");
            CHECK(r10.linear.converged, "T10 cantilever: CG converged");
            CHECK(tip10 / beam > 0.93 && tip10 / beam < 1.07, "T10 cantilever: tip deflection within 7% of beam theory");
            CHECK(tip10 > tip4 * 1.3, "T10 is much less stiff in bending than T4");
        }
    }

    // 4) Thermal T10 reproduces the linear profile.
    {
        QuadMesh qm = makeQuadratic(makeBoxMesh(1, 0.2, 0.2, 4, 1, 1));
        std::vector<TemperatureBC> bcs;
        for (int n : selectNodes(qm.base, "xmin")) bcs.push_back({n, 100});
        for (int n : selectNodes(qm.base, "xmax")) bcs.push_back({n, 0});
        Material mat; mat.k = 45;
        auto r = solveThermalT10(qm, mat, bcs, {});
        double err = 0;
        for (const auto& t : qm.tets10) for (int a : t) err = std::max(err, std::abs(r.T[a] - 100.0 * (1.0 - qm.base.nodes[a].x)));
        CHECK(r.linear.converged && err < 1e-6, "thermal T10: linear profile exact");
    }
}

static void testMshAndCad() {
    // Minimal MSH 2.2 and 4.1 files holding one tetrahedron (+ a triangle that must be skipped).
    const char* v2 = "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n$Nodes\n4\n10 0 0 0\n20 1 0 0\n30 0 1 0\n40 0 0 1\n$EndNodes\n"
                     "$Elements\n2\n1 2 2 0 1 10 20 30\n2 4 2 0 1 10 20 30 40\n$EndElements\n";
    const char* v4 = "$MeshFormat\n4.1 0 8\n$EndMeshFormat\n$Nodes\n1 4 1 4\n3 1 0 4\n1\n2\n3\n4\n0 0 0\n1 0 0\n0 1 0\n0 0 1\n$EndNodes\n"
                     "$Elements\n2 2 1 2\n2 1 2 1\n1 1 2 3\n3 1 4 1\n2 1 2 3 4\n$EndElements\n";
    for (int ver = 0; ver < 2; ++ver) {
        const std::string f = ver ? "t4.msh" : "t2.msh";
        { std::ofstream o(f); o << (ver ? v4 : v2); }
        Mesh m; std::string err;
        const bool ok = readGmshMsh(f, m, &err);
        CHECK(ok && m.numNodes() == 4 && m.numCells() == 1 && std::abs(m.totalVolume() - 1.0 / 6.0) < 1e-12,
              ver ? "msh 4.1 reader: one tetrahedron, volume 1/6" : "msh 2.2 reader: one tetrahedron, volume 1/6");
        std::remove(f.c_str());
    }
    CHECK(detectCadFormat("a.msh") == CadFormat::Msh, "cad: .msh detected");
    if (cadKernelAvailable()) {
        const double pi = 3.14159265358979323846;
        auto cyl = makePrimitive("cylinder", {0.5, 2.0}, 0.08);
        CHECK(cyl.ok && std::abs(cyl.mesh.totalVolume() - pi * 0.25 * 2.0) / (pi * 0.5) < 0.03, "gmsh: cylinder volume within 3%");
        auto sph = makePrimitive("sphere", {1.0}, 0.15);
        CHECK(sph.ok && std::abs(sph.mesh.totalVolume() - 4.0 / 3.0 * pi) / (4.0 / 3.0 * pi) < 0.05, "gmsh: sphere volume within 5%");
    }
}

static void testRegistryAndAuthor() {
    const auto& reg = allSolvers();
    CHECK(reg.size() >= 15, "registry: lists the open-source solvers");
    for (const char* id : {"calculix", "openfoam", "openradioss", "code_aster", "elmer", "su2", "code_saturne", "precice", "chrono", "petsc", "mfem"})
        CHECK(findSolverInfo(id) != nullptr, std::string("registry: has ") + id);
    CHECK(findSolverInfo("gmsh") && findSolverInfo("gmsh")->copyleft == Copyleft::Strong, "registry: Gmsh is GPL (combined work when linked)");
    CHECK(findSolverInfo("lua") && findSolverInfo("lua")->copyleft == Copyleft::Permissive, "registry: Lua is MIT");
    CHECK(findSolverInfo("kratos") && findSolverInfo("kratos")->licenseChecked && findSolverInfo("kratos")->license.find("BSD-4") != std::string::npos, "registry: Kratos BSD-4 verified");
    CHECK(findSolverInfo("elmer")->license.find("GPL-2.0+") == 0 && findSolverInfo("elmer")->license.find("LGPL-2.1+") != std::string::npos,
          "registry: Elmer is GPL-2.0+ with an LGPL-2.1+ ElmerSolver library");
    CHECK(findSolverInfo("su2")->copyleft == Copyleft::Weak, "registry: SU2 is LGPL (weak copyleft)");
    CHECK(findSolverInfo("openradioss")->copyleft == Copyleft::Network, "registry: OpenRadioss is AGPL");
    CHECK(findSolverInfo("petsc")->copyleft == Copyleft::Permissive, "registry: PETSc is BSD");
    CHECK(findSolverInfo("code_aster")->license == "GPL-3.0+" && findSolverInfo("code_aster")->licenseChecked, "registry: Code_Aster is GPL-3.0+ (checked in the upstream headers)");
    CHECK(findSolverInfo("code_saturne")->license == "GPL-2.0+" && findSolverInfo("code_saturne")->licenseChecked, "registry: Code_Saturne is GPL-2.0+ (checked in the upstream headers)");
    CHECK(findSolverInfo("dealii")->copyleft == Copyleft::Weak && findSolverInfo("dealii")->license.find("LGPL-2.1+") == 0, "registry: deal.II is LGPL-2.1+ (Apache dual licence only for part of the code)");
    CHECK(findSolverInfo("opencourant") && findSolverInfo("opencourant")->licenseChecked && findSolverInfo("opencourant")->copyleft == Copyleft::Network, "registry: OpenCourant AGPL checked in its repository");
    CHECK(findSolverInfo("openradioss") && !findSolverInfo("openradioss")->licenseChecked, "registry: OpenRadioss upstream is gone, so it is not marked as checked");
    for (const char* id : {"qt6", "occt", "gomp", "mingw_rt"})
        CHECK(findSolverInfo(id) != nullptr && findSolverInfo(id)->copyleft == Copyleft::Weak && findSolverInfo(id)->licenseChecked, std::string("registry: shipped component ") + id);
    CHECK(findSolverInfo("qt6")->license.find("LGPL-3.0") == 0, "registry: Qt is LGPL-3.0 first");
    CHECK(findSolverInfo("petsc")->licenseNote.find("Open MPI") != std::string::npos, "registry: PETSc note covers Open MPI");
    {
        std::size_t unchecked = 0;
        for (const auto& s : reg) unchecked += s.licenseChecked ? 0 : 1;
        CHECK(unverifiedLicenseCount() == unchecked, "registry: unverified count matches the flags");
        const std::string rep = licenseReport();
        bool all = true;
        for (const auto& s : reg)
            if (s.id != "builtin" && rep.find(s.id + " | ") == std::string::npos) all = false;
        CHECK(all && rep.find("CHUA doi chieu") != std::string::npos, "registry: licence report lists every component");
    }
    const std::string a = expandArgs("-i {input} -np 1", "/tmp/m/model_0000.rad");
    CHECK(a == "-i \"/tmp/m/model_0000.rad\" -np 1", "registry: {input} expansion");
    CHECK(expandArgs("{base0001}", "/tmp/m/model_0000.rad") == "\"/tmp/m/model_0001.rad\"", "registry: {base0001} expansion");
    CHECK(expandArgs("{base}", "/tmp/m/beam.inp") == "beam", "registry: {base} expansion");
    CHECK(std::string(about::kAuthor) == "Trần Minh Tâm", "about: author name");
}

// THIRD_PARTY_NOTICES.txt phải nhắc mọi thành phần trong registry kèm đúng giấy phép (một dòng chứa cả tên và giấy phép),
// và thêm thành phần mới vào registry mà quên cập nhật thông báo sẽ làm test này thất bại.
static void testNoticesMatchRegistry() {
    std::ifstream in(std::string(FASTCAE_SOURCE_DIR) + "/THIRD_PARTY_NOTICES.txt");
    CHECK(in.good(), "notices: THIRD_PARTY_NOTICES.txt readable");
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    const std::pair<const char*, const char*> keyword[] = {
        {"calculix", "CalculiX"}, {"openfoam", "OpenFOAM"}, {"openradioss", "OpenRadioss"}, {"opencourant", "OpenCourant"},
        {"code_aster", "Code_Aster"}, {"elmer", "Elmer"}, {"su2", "SU2"}, {"code_saturne", "Code_Saturne"}, {"precice", "preCICE"},
        {"chrono", "Chrono"}, {"fenicsx", "FEniCSx"}, {"kratos", "Kratos"}, {"mfem", "MFEM"}, {"dealii", "deal.II"}, {"gmsh", "Gmsh"},
        {"lua", "Lua"}, {"petsc", "PETSc"}, {"qt6", "Qt 6"}, {"occt", "OpenCASCADE"}, {"gomp", "libgomp"}, {"mingw_rt", "libstdc++"}};
    for (const auto& s : allSolvers()) {
        if (s.id == "builtin" || s.integration == Integration::Plugin) continue;   // plugin do người dùng khai báo: không thuộc THIRD_PARTY_NOTICES
        const char* kw = nullptr;
        for (const auto& k : keyword)
            if (s.id == k.first) kw = k.second;
        if (!kw) {
            CHECK(false, "notices: no keyword mapped for registry entry " + s.id);
            continue;
        }
        const std::string token = s.license.substr(0, s.license.find(' '));
        bool found = false;
        for (const auto& l : lines)
            if (l.find(kw) != std::string::npos && l.find(token) != std::string::npos) found = true;
        CHECK(found, "notices: " + s.id + " listed with licence " + token);
    }
}

static void testPlaneSelection() {
    Mesh m = makeBoxMesh(1.0, 0.2, 0.1, 5, 2, 2);
    PlaneSel p;
    p.n = {1, 0, 0};
    p.d = 0;
    p.tol = 1e-9;
    auto a = selectPlaneNodes(m, p);
    auto b = selectNodes(m, "xmin");
    CHECK(!a.empty() && a == b, "plane selection x=0 equals named set xmin");
    p.n = {-1, 0, 0};
    p.d = -1.0;
    CHECK(selectPlaneNodes(m, p) == selectNodes(m, "xmax"), "plane selection with flipped normal equals xmax");
    p.n = {1, 0, 0};
    p.d = 0.4;
    CHECK(selectPlaneNodes(m, p).empty(), "plane through the interior selects no nodes");
}

static void writeFoamFile(const std::filesystem::path& p, const std::string& cls, const std::string& body) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p);
    f << "FoamFile\n{\n    version 2.0;\n    format ascii;\n    class " << cls << ";\n    object x;\n}\n\ndimensions [0 0 0 0 0 0 0];\n\n" << body;
}

static void testOpenFoamResults() {
    const auto dir = std::filesystem::temp_directory_path() / "fastcae_of_results";
    { std::error_code ec_; std::filesystem::remove_all(dir, ec_); }
    writeFoamFile(dir / "0" / "U", "volVectorField", "internalField uniform (0 0 0);\n");
    writeFoamFile(dir / "0.5" / "U", "volVectorField", "internalField uniform (9 9 9);\n");
    // 2x2 ô, thứ tự x chạy nhanh: (0,0) (1,0) (0,1) (1,1)
    writeFoamFile(dir / "1" / "U", "volVectorField",
                  "internalField nonuniform List<vector>\n4\n(\n(1 0 0)\n(2 0 0)\n(3 0.5 0)\n(4 -0.5 0)\n)\n;\n");
    writeFoamFile(dir / "1" / "p", "volScalarField", "internalField nonuniform List<scalar>\n4\n(\n10\n20\n30\n40\n)\n;\n");
    CHECK(latestFoamTime(dir.string()) == "1", "openfoam: latest time directory is the numerically largest");
    OpenFoamCavityResult r;
    std::string err;
    CHECK(readOpenFoamCavity(dir.string(), 0.1, 2.0, r, &err), "openfoam: cavity result read");
    CHECK(r.n == 2 && r.uCell(1, 0) == 2 && r.uCell(0, 1) == 3 && r.vCell(1, 1) == -0.5 && r.p[3] == 40, "openfoam: cell ordering j*n+i");
    std::vector<double> nu, nv, np;
    cavityNodalFields(r, nu, nv, np);
    CHECK(nu.size() == 9 && nu[2 * 3 + 0] == 2.0 && nu[2 * 3 + 2] == 2.0 && nu[0] == 0.0, "openfoam: lid nodes take the lid speed, walls are at rest");
    CHECK(np[1 * 3 + 1] == 25.0, "openfoam: interior node averages its four neighbouring cells");
    std::vector<std::array<double, 3>> vec;
    CHECK(readFoamVectorField((dir / "0.5" / "U").string(), vec) && vec.size() == 1 && vec[0][0] == 9, "openfoam: uniform field");
    CHECK(!readFoamVectorField((dir / "nope").string(), vec, &err) && !err.empty(), "openfoam: missing file reports an error");
    const std::string script = openFoamShellScript("/opt/of/etc/bashrc", "/tmp/my case", true);
    CHECK(script.find("source \"/opt/of/etc/bashrc\"") == 0 && script.find("blockMesh -case \"/tmp/my case\"") != std::string::npos &&
              script.find("icoFoam") != std::string::npos && script.find("foamToVTK") != std::string::npos,
          "openfoam: shell script sources the environment and quotes paths");
    CHECK(openFoamShellScript("", "/c", false).find("source") == std::string::npos, "openfoam: no environment -> uses PATH");
    CHECK(findOpenFoamBashrc("/definitely/not/here").find("definitely") == std::string::npos, "openfoam: invalid bashrc hint is ignored");
    { std::error_code ec_; std::filesystem::remove_all(dir, ec_); }
}

// Lưới kín và hợp lệ: mỗi mặt tam giác thuộc 1 (biên) hoặc 2 (trong) tứ diện, không bao giờ 3 trở lên.
static bool meshConforming(const Mesh& m) {
    std::map<std::array<int, 3>, int> cnt;
    static const int f[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    for (const auto& t : m.tets)
        for (const auto& q : f) {
            std::array<int, 3> k = {t[q[0]], t[q[1]], t[q[2]]};
            std::sort(k.begin(), k.end());
            if (++cnt[k] > 2) return false;
        }
    for (std::size_t i = 0; i < m.tets.size(); ++i)
        if (m.tetVolume(i) <= 0) return false;
    return true;
}

static FrameSection rectSec(double b, double h) {
    FrameSection s;
    std::string err;
    makeSection("S", "rect", {b, h}, s, err);
    return s;
}

static void testFrameSolver() {
    const double E = 210e9, L = 2.0, b = 0.05, h = 0.1;
    const FrameSection sec = rectSec(b, h);
    const double EI = E * sec.Iy;
    {   // dầm công xôn, lực đầu tự do: PL^3/(3EI) và mômen ngàm PL (1 phần tử là đủ chính xác)
        FrameModel m;
        m.E = E;
        m.sections = {sec};
        m.nodes = {{0, 0, 0}, {L, 0, 0}};
        m.members = {{0, 1, 0, MemberKind::Beam, 0}};
        FrameSupport s;
        s.node = 0;
        s.fixed.fill(true);
        m.supports = {s};
        m.nodalLoads = {{1, {0, 0, -1000, 0, 0, 0}}};
        const auto r = solveFrame(m);
        CHECK(r.ok, "frame: cantilever solved");
        CHECK(std::abs(r.disp[1][2] + 1000 * L * L * L / (3 * EI)) < 1e-9 * std::abs(r.disp[1][2]) + 1e-15, "frame: cantilever tip deflection = PL^3/3EI");
        CHECK(std::abs(r.reactions[0][2] - 1000) < 1e-6 && std::abs(std::abs(r.reactions[0][4]) - 1000 * L) < 1e-6, "frame: cantilever reactions P and PL");
        const double sigma = 1000 * L * 0.5 * h / sec.Iy;
        CHECK(std::abs(r.maxStress - sigma) < 1e-6 * sigma, "frame: max bending stress = M c / I");
    }
    {   // dầm đơn giản chịu tải phân bố, 4 phần tử: võng giữa nhịp = 5qL^4/(384EI)
        FrameModel m;
        m.E = E;
        m.sections = {sec};
        for (int i = 0; i <= 4; ++i) m.nodes.push_back({L * i / 4, 0, 0});
        for (int i = 0; i < 4; ++i) {
            m.members.push_back({i, i + 1, 0, MemberKind::Beam, 0});
            m.memberLoads.push_back({i, {0, 0, -5000}});
        }
        FrameSupport a, c;
        a.node = 0; a.fixed = {true, true, true, true, false, false};   // ngăn xoắn và trượt
        c.node = 4; c.fixed = {false, true, true, false, false, false};
        m.supports = {a, c};
        const auto r = solveFrame(m);
        const double exact = 5.0 * 5000 * std::pow(L, 4) / (384 * EI);
        CHECK(r.ok && std::abs(r.disp[2][2] + exact) < 1e-6 * exact, "frame: simply supported UDL midspan = 5qL^4/384EI");
        CHECK(std::abs(r.reactions[0][2] - 5000 * L / 2) < 1e-6 && std::abs(r.reactions[4][2] - 5000 * L / 2) < 1e-6, "frame: UDL reactions qL/2");
        const double Mmax = 5000 * L * L / 8;
        CHECK(std::abs(r.maxMoment - Mmax) < 1e-6 * Mmax, "frame: midspan moment qL^2/8 from member forces");
    }
    {   // dàn tam giác: P = 10 kN xuống tại đỉnh, A(0,0) gối cố định, B(4,0) gối con lăn
        FrameModel m;
        m.E = E;
        FrameSection bar;
        std::string err;
        makeSection("bar", "circle", {0.02}, bar, err);
        m.sections = {bar};
        m.nodes = {{0, 0, 0}, {4, 0, 0}, {2, 0, 3}};
        m.members = {{0, 2, 0, MemberKind::Truss, 0}, {1, 2, 0, MemberKind::Truss, 0}, {0, 1, 0, MemberKind::Truss, 0}};
        FrameSupport a, c;
        a.node = 0; a.fixed = {true, true, true, false, false, false};
        c.node = 1; c.fixed = {false, true, true, false, false, false};
        m.supports = {a, c};
        m.nodalLoads = {{2, {0, 0, -10000, 0, 0, 0}}};
        const auto r = solveFrame(m);
        const double Lc = std::sqrt(13.0), Fac = 5000 * Lc / 3;   // nén
        CHECK(r.ok && std::abs(r.reactions[0][2] - 5000) < 1e-6 && std::abs(r.reactions[1][2] - 5000) < 1e-6, "truss: vertical reactions P/2");
        CHECK(std::abs(r.members[0].f2[0] - (-Fac)) < 1e-6 * Fac && std::abs(r.members[1].f2[0] - (-Fac)) < 1e-6 * Fac, "truss: inclined bars in compression 6009 N");
        CHECK(std::abs(r.members[2].f2[0] - Fac * 2 / Lc) < 1e-6 * Fac, "truss: bottom chord in tension 3333 N");
    }
    {   // xoắn: T L / (G J)
        FrameModel m;
        m.E = E;
        m.sections = {sec};
        m.nodes = {{0, 0, 0}, {L, 0, 0}};
        m.members = {{0, 1, 0, MemberKind::Beam, 0}};
        FrameSupport s;
        s.node = 0;
        s.fixed.fill(true);
        m.supports = {s};
        m.nodalLoads = {{1, {0, 0, 0, 500, 0, 0}}};
        const auto r = solveFrame(m);
        const double G = E / (2 * (1 + 0.3));
        CHECK(r.ok && std::abs(r.disp[1][3] - 500 * L / (G * sec.J)) < 1e-9 * r.disp[1][3], "frame: torsion angle = TL/GJ");
    }
    {   // lò xo nối tiếp gối: u = F/k
        FrameModel m;
        m.E = E;
        m.sections = {sec};
        m.nodes = {{0, 0, 0}, {1, 0, 0}};
        m.members = {{0, 1, 0, MemberKind::Spring, 2000.0}};
        FrameSupport s;
        s.node = 0;
        s.fixed.fill(true);
        m.supports = {s};
        m.nodalLoads = {{1, {100, 0, 0, 0, 0, 0}}};
        const auto r = solveFrame(m);
        CHECK(r.ok && std::abs(r.disp[1][0] - 0.05) < 1e-12, "spring: u = F/k");
    }
    {   // cáp chỉ chịu kéo: khung chữ nhật 4x3 m, 4 thanh dàn + 2 cáp chéo, lực ngang H = 1000 N tại D.
        // Cáp AC chịu kéo T = H*5/4 = 1250 N, cáp BD chùng (không chịu nén).
        FrameModel m;
        m.E = E;
        FrameSection bs, cs;
        std::string err;
        makeSection("bar", "circle", {0.05}, bs, err);
        makeSection("cab", "circle", {0.01}, cs, err);
        m.sections = {bs, cs};
        m.nodes = {{0, 0, 0}, {4, 0, 0}, {4, 0, 3}, {0, 0, 3}};   // A B C D
        m.members = {{0, 1, 0, MemberKind::Truss, 0}, {1, 2, 0, MemberKind::Truss, 0}, {2, 3, 0, MemberKind::Truss, 0},
                     {3, 0, 0, MemberKind::Truss, 0}, {0, 2, 1, MemberKind::Cable, 0}, {1, 3, 1, MemberKind::Cable, 0}};
        FrameSupport a, c;
        a.node = 0; a.fixed = {true, true, true, false, false, false};
        c.node = 1; c.fixed = {false, true, true, false, false, false};
        m.supports = {a, c};
        m.nodalLoads = {{3, {1000, 0, 0, 0, 0, 0}}};
        const auto r = solveFrame(m);
        CHECK(r.ok, "cable: braced panel solved");
        if (r.ok) {
            CHECK(r.members[4].active && !r.members[5].active, "cable: tension diagonal active, compressed diagonal slack");
            CHECK(std::abs(r.members[4].f2[0] - 1250) < 1e-6 * 1250, "cable: tension in the active diagonal = 1250 N");
            CHECK(r.cableIterations >= 2, "cable: needed an iteration to drop the slack cable");
        }
    }
    {   // khớp bản lề: dầm ngàm trái, đầu phải được ngàm xoay nhưng có bản lề (pin2) -> dầm công xôn có gối con lăn.
        // Lý thuyết: R_B = 3qL/8, M_A = qL^2/8 (nếu bản lề không hoạt động sẽ là ngàm hai đầu: qL/2 và qL^2/12).
        FrameModel m;
        m.E = E;
        m.sections = {sec};
        m.nodes = {{0, 0, 0}, {L, 0, 0}};
        FrameMember mb{0, 1, 0, MemberKind::Beam, 0};
        mb.pin2 = true;
        m.members = {mb};
        m.memberLoads = {{0, {0, 0, -5000}}};
        FrameSupport a, c;
        a.node = 0; a.fixed.fill(true);
        c.node = 1; c.fixed.fill(true);
        m.supports = {a, c};
        const auto r = solveFrame(m);
        CHECK(r.ok, "hinge: solved");
        if (r.ok) {
            CHECK(std::abs(r.reactions[1][2] - 3 * 5000 * L / 8) < 1e-6 * 3 * 5000 * L / 8, "hinge: propped cantilever R_B = 3qL/8");
            CHECK(std::abs(std::abs(r.reactions[0][4]) - 5000 * L * L / 8) < 1e-6 * 5000 * L * L / 8, "hinge: fixed-end moment qL^2/8");
            CHECK(std::abs(r.members[0].f2[4]) < 1e-6 && std::abs(r.members[0].f2[5]) < 1e-6, "hinge: no moment at the released end");
        }
    }
    {   // khung thiếu liên kết bị từ chối
        FrameModel m;
        m.sections = {sec};
        m.nodes = {{0, 0, 0}, {1, 0, 0}};
        m.members = {{0, 1, 0, MemberKind::Beam, 0}};
        CHECK(!solveFrame(m).ok, "frame: model without supports is rejected");
    }
}

static FrameModel beamLine(int n, double L, const FrameSection& sec, double E) {
    FrameModel m;
    m.E = E;
    m.rho = 7850;
    m.sections = {sec};
    for (int i = 0; i <= n; ++i) m.nodes.push_back({L * i / n, 0, 0});
    for (int i = 0; i < n; ++i) m.members.push_back({i, i + 1, 0, MemberKind::Beam, 0});
    return m;
}

static void testScripting() {
    std::string out;
    auto mk = [&](ScriptEngine& e) { e.setPrinter([&](const std::string& s) { out += s; }); };
    {
        ScriptEngine e;
        mk(e);
        out.clear();
        const auto r = e.runString("print(1 + 1, 'x')");
        CHECK(r.ok && out == "2\tx\n", "script: print is captured");
    }
    {   // dầm công xôn giải trong Lua phải trùng PL^3/3EI (độ chính xác máy)
        ScriptEngine e;
        mk(e);
        out.clear();
        const auto r = e.runString(R"(
            local s = fastcae.section('rect', 0.05, 0.1)
            local res = fastcae.solve(fastcae.case{ type='frame', E=210e9, nu=0.3, rho=7850, section='S rect 0.05 0.1',
                node={'1 0 0 0','2 2 0 0'}, member='1 2 S beam', support='1 fixed', nload='2 0 0 -1000' })
            local exact = 1000 * 2^3 / (3 * 210e9 * s.Iy)
            print(string.format('%.6e %.6e %s', res.values.max_displacement, exact, tostring(res.ok)))
        )");
        double fe = 0, ex = 0;
        char okc[8] = {0};
        std::sscanf(out.c_str(), "%le %le %7s", &fe, &ex, okc);
        CHECK(r.ok && ex > 0 && std::abs(fe - ex) < 1e-6 * ex && std::string(okc) == "true", "script: cantilever solved from Lua equals PL^3/3EI");
    }
    {
        ScriptEngine e;
        mk(e);
        out.clear();
        const auto r = e.runString(R"(
            local res = fastcae.run(fastcae.frame_template('warren_truss', {12, 2, 6, 10000}))
            print(res.ok, #res.series.member_n_max, res.type, res:field_stats('displacement_magnitude') ~= nil)
            local bad = fastcae.run('type = frame')
            print(bad.ok)
        )");
        CHECK(r.ok && out.find("true\t") == 0 && out.find("frame") != std::string::npos && out.find("false") != std::string::npos,
              "script: run returns result tables (ok flag, series, fields), failures do not raise");
    }
    {
        ScriptEngine e;
        mk(e);
        const auto r = e.runString("fastcae.solve('type = frame')");
        CHECK(!r.ok && r.error.find("giai that bai") != std::string::npos, "script: solve raises on failure with a message");
    }
    {
        ScriptEngine e;
        mk(e);
        CHECK(!e.runString("os.execute('echo x')").ok, "script sandbox: os.execute is disabled");
        CHECK(!e.runString("io.popen('echo x')").ok, "script sandbox: io.popen is disabled");
        CHECK(!e.runString("os.exit(0)").ok, "script sandbox: os.exit is disabled");
        CHECK(!e.runString("dofile('x.lua')").ok, "script sandbox: dofile is disabled");
        const auto r = e.runString("local x = nil\nlocal y = x.field");
        CHECK(!r.ok && r.error.find("script:2") != std::string::npos, "script: errors report the line number");
    }
    {
        ScriptEngine e;
        mk(e);
        std::atomic<bool> stop(true);
        e.setCancelFlag(&stop);
        const auto r = e.runString("while true do end");
        CHECK(!r.ok && r.error.find("dung") != std::string::npos, "script: cancel flag stops an infinite loop");
    }
    {
        ScriptEngine e;
        mk(e);
        out.clear();
        const auto r = e.runString(R"(
            local t = fastcae.case{ zeta = 'last', type = 'frame', node = {'1 0 0 0', '2 1 0 0'}, alpha = 3 }
            print(t:match('^[^\n]*\n([^\n]*)'))
            print(#fastcae.templates() >= 21, fastcae.materials.steel.E)
        )");
        CHECK(r.ok && out.find("type = frame") != std::string::npos && out.find("true") != std::string::npos, "script: case() puts type first; templates() lists the library");
    }
}

static void testFrameEigen() {
    const double E = 210e9, L = 2.0, rho = 7850;
    const FrameSection sec = rectSec(0.05, 0.1);
    const double pi = 3.14159265358979323846;
    const double w0 = std::sqrt(E * sec.Iz / (rho * sec.A * std::pow(L, 4)));   // trục yếu (Iz) trước
    auto rel = [](double a, double b) { return std::abs(a - b) / std::abs(b); };
    {   // dầm công xôn 10 phần tử: (beta L)^2 sqrt(EI/rho A L^4), beta L = 1.8751, 4.6941
        FrameModel m = beamLine(10, L, sec, E);
        FrameSupport s;
        s.node = 0;
        s.fixed.fill(true);
        m.supports = {s};
        const auto r = solveFrameEigen(m, 4, false);
        CHECK(r.ok && r.values.size() >= 4, "modal: cantilever solved");
        if (r.ok && r.values.size() >= 4) {
            const double f1 = std::sqrt(r.values[0]), f2 = std::sqrt(r.values[1]), f3 = std::sqrt(r.values[2]);
            CHECK(rel(f1, 1.875104069 * 1.875104069 * w0) < 2e-3, "modal: cantilever 1st mode (weak axis) = 1.8751^2 sqrt(EI/rhoAL^4)");
            CHECK(rel(f2, 2.0 * f1) < 2e-3, "modal: strong-axis mode is sqrt(Iy/Iz) = 2 times the weak-axis mode");
            CHECK(rel(f3, 4.694091133 * 4.694091133 * w0) < 3e-3, "modal: cantilever 2nd weak-axis mode = 4.6941^2 sqrt(EI/rhoAL^4)");
            CHECK(r.shapes.size() >= 4 && std::abs(std::abs(r.shapes[0][10][1]) - 1.0) < 1e-9, "modal: mode shape normalised to unit tip displacement");
        }
    }
    {   // dầm đơn giản: omega_n = (n pi)^2 sqrt(EI/rho A L^4)
        FrameModel m = beamLine(10, L, sec, E);
        FrameSupport a, c;
        a.node = 0; a.fixed = {true, true, true, true, false, false};
        c.node = 10; c.fixed = {false, true, true, false, false, false};
        m.supports = {a, c};
        const auto r = solveFrameEigen(m, 3, false);
        CHECK(r.ok && r.values.size() >= 3, "modal: simply supported solved");
        if (r.ok && r.values.size() >= 3) {
            CHECK(rel(std::sqrt(r.values[0]), pi * pi * w0) < 1e-3, "modal: simply supported 1st mode = pi^2 sqrt(EI/rhoAL^4)");
            CHECK(rel(std::sqrt(r.values[2]), 4 * pi * pi * w0) < 3e-3, "modal: simply supported 3rd mode = (2pi)^2 sqrt(EI/rhoAL^4)");
        }
    }
    {   // hệ một bậc tự do: lò xo k, khối lượng m -> omega^2 = k/m
        FrameModel m;
        m.sections = {sec};
        m.nodes = {{0, 0, 0}, {1, 0, 0}};
        m.members = {{0, 1, 0, MemberKind::Spring, 4000.0}};
        FrameSupport a, b;
        a.node = 0; a.fixed.fill(true);
        b.node = 1; b.fixed = {false, true, true, false, false, false};
        m.supports = {a, b};
        m.masses = {{1, 10.0}};
        const auto r = solveFrameEigen(m, 1, false);
        CHECK(r.ok && !r.values.empty() && rel(r.values[0], 400.0) < 1e-9, "modal: mass-spring omega^2 = k/m");
    }
    {   // cột Euler công xôn: Pcr = pi^2 E I / (4 L^2) với I yếu = Iz
        FrameModel m = beamLine(8, L, sec, E);
        FrameSupport s;
        s.node = 0;
        s.fixed.fill(true);
        m.supports = {s};
        m.nodalLoads = {{8, {-1000, 0, 0, 0, 0, 0}}};
        const auto r = solveFrameEigen(m, 2, true);
        CHECK(r.ok && !r.values.empty(), "buckling: cantilever column solved");
        if (r.ok && !r.values.empty()) {
            const double pcr = pi * pi * E * sec.Iz / (4 * L * L);
            CHECK(rel(r.values[0] * 1000.0, pcr) < 2e-3, "buckling: cantilever Euler load pi^2 EI / 4L^2");
        }
    }
    {   // cột hai đầu bản lề: Pcr = pi^2 E I / L^2
        FrameModel m = beamLine(8, L, sec, E);
        FrameSupport a, c;
        a.node = 0; a.fixed = {true, true, true, true, false, false};
        c.node = 8; c.fixed = {false, true, true, false, false, false};
        m.supports = {a, c};
        m.nodalLoads = {{8, {-1000, 0, 0, 0, 0, 0}}};
        const auto r = solveFrameEigen(m, 1, true);
        CHECK(r.ok && !r.values.empty(), "buckling: pinned-pinned column solved");
        if (r.ok && !r.values.empty()) {
            const double pcr = pi * pi * E * sec.Iz / (L * L);
            CHECK(rel(r.values[0] * 1000.0, pcr) < 2e-3, "buckling: pinned-pinned Euler load pi^2 EI / L^2");
        }
    }
    {   // chỉ chịu kéo: không có mất ổn định
        FrameModel m = beamLine(4, L, sec, E);
        FrameSupport s;
        s.node = 0;
        s.fixed.fill(true);
        m.supports = {s};
        m.nodalLoads = {{4, {1000, 0, 0, 0, 0, 0}}};
        CHECK(!solveFrameEigen(m, 1, true).ok, "buckling: pure tension is rejected");
    }
}

static void testShellAndTemplates() {
    const double pi = 3.14159265358979323846;
    const ShellMesh pl = makeShellPlate(2.0, 0.5, 0.01, 8, 4);
    CHECK(pl.nodes.size() == 45 && pl.quads.size() == 32, "shell plate: node and quad counts");
    double sum = 0;
    for (const auto& l : shellPressureLoads(pl, 1000.0)) sum += l.f.z;
    CHECK(std::abs(sum + 1000.0 * 2.0 * 0.5) < 1e-9, "shell plate: pressure loads sum to p*A (downward)");
    CHECK(selectShellNodes(pl, "xmin").size() == 5 && selectShellNodes(pl, "ymax").size() == 9, "shell plate: edge node sets");
    const ShellMesh cy = makeShellCylinder(1.0, 3.0, 0.01, 24, 12);
    CHECK(cy.nodes.size() == 24 * 13 && cy.quads.size() == 24 * 12, "shell cylinder: node and quad counts");
    double fr = 0;
    for (const auto& l : shellPressureLoads(cy, 100.0)) fr += std::hypot(l.f.x, l.f.y);
    // áp suất trong nhân diện tích hình trụ nội tiếp (đa giác 24 cạnh): tổng độ lớn lực nút = p * diện tích
    const double areaPoly = 24 * 2 * std::sin(pi / 24) * 3.0;
    CHECK(std::abs(fr - 100.0 * areaPoly) < 1e-6 * 100.0 * areaPoly, "shell cylinder: pressure resultant magnitude");
    CHECK(selectShellNodes(cy, "zmin").size() == 24, "shell cylinder: base ring node set");
    // mô hình hiển thị: hai lớp nút, thể tích dương
    ShellResult empty;
    Mesh rm;
    VtkFields rf;
    shellRender(pl, empty, rm, rf, 0.05);
    CHECK(rm.nodes.size() == 90 && rm.tets.size() == 32 * 6 && std::abs(rm.totalVolume() - 2.0 * 0.5 * 0.05) < 1e-9, "shell render: slab volume = A * t_render");
    // mẫu: mọi mẫu khung/dàn/tấm sinh được file case hợp lệ, đọc lại và giải được (khung) hoặc phân tích được (tấm/vỏ)
    for (const auto& s : frameTemplateCatalog()) {
        std::vector<double> p;
        for (const auto& q : s.params) p.push_back(q.def);
        std::string err;
        const std::string text = frameTemplateText(s.name, p, err);
        CHECK(!text.empty(), std::string("template ") + s.name + ": generated");
        CaseFile cf;
        cf.parse(text);
        FrameModel fm;
        CHECK(readFrameModel(cf, fm, err), std::string("template ") + s.name + ": model parses " + err);
        const auto r = solveFrame(fm);
        CHECK(r.ok, std::string("template ") + s.name + ": solves");
        double fz = 0, loads = 0;
        for (const auto& rr : r.reactions) fz += rr[2];
        for (const auto& nl : fm.nodalLoads) loads -= nl.f[2];
        for (const auto& ml : fm.memberLoads) {
            const Vec3 d{fm.nodes[fm.members[ml.member].n2].x - fm.nodes[fm.members[ml.member].n1].x, fm.nodes[fm.members[ml.member].n2].y - fm.nodes[fm.members[ml.member].n1].y,
                         fm.nodes[fm.members[ml.member].n2].z - fm.nodes[fm.members[ml.member].n1].z};
            loads -= ml.q.z * std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        }
        CHECK(std::abs(fz - loads) < 1e-6 * std::max(1.0, std::abs(loads)), std::string("template ") + s.name + ": vertical equilibrium (reactions = loads)");
    }
    for (const auto& s : shellTemplateCatalog()) {
        std::vector<double> p;
        for (const auto& q : s.params) p.push_back(q.def);
        std::string err;
        CHECK(!shellTemplateText(s.name, p, err).empty(), std::string("template ") + s.name + ": generated");
    }
    std::string err;
    CHECK(frameTemplateText("simple_beam", {-1, 10000, 8}, err).empty() && !err.empty(), "template: non-positive parameter rejected");
}

static void testShapeLibrary() {
    struct Case { const char* name; std::vector<double> p; double vol; };
    const std::vector<Case> cases = {
        {"ibeam", {2, 0.1, 0.2, 0.01, 0.006}, 2 * (2 * 0.1 * 0.01 + 0.18 * 0.006)},
        {"channel", {2, 0.08, 0.2, 0.01, 0.006}, 2 * (2 * 0.08 * 0.01 + 0.18 * 0.006)},
        {"angle", {2, 0.1, 0.1, 0.01}, 2 * (0.1 * 0.01 + 0.09 * 0.01)},
        {"tee", {2, 0.1, 0.15, 0.01, 0.006}, 2 * (0.1 * 0.01 + 0.14 * 0.006)},
        {"rect_tube", {2, 0.1, 0.15, 0.005}, 2 * (0.1 * 0.15 - 0.09 * 0.14)},
        {"plate", {1, 0.5, 0.02}, 1 * 0.5 * 0.02},
        {"plate_hole", {1, 0.5, 0.02, 0.1}, (1 * 0.5 - 0.01) * 0.02},
        {"wall", {3, 3, 0.2}, 3 * 3 * 0.2},
        {"pad_footing", {1.5, 1.5, 0.5, 0.4, 0.6}, 1.5 * 1.5 * 0.5 + 0.4 * 0.4 * 0.6},
        {"retaining_wall", {2, 3, 0.3, 2, 0.4, 0.5}, 2 * (2 * 0.4 + 0.3 * 2.6)},
    };
    for (const auto& c : cases) {
        Mesh m;
        std::string err;
        const bool ok = buildShapeMesh(c.name, c.p, 0, m, err);
        CHECK(ok, std::string("shape ") + c.name + ": mesh built " + err);
        if (!ok) continue;
        CHECK(std::abs(m.totalVolume() - c.vol) < 1e-9 * std::max(1.0, c.vol) + 1e-12, std::string("shape ") + c.name + ": volume matches formula");
        CHECK(meshConforming(m), std::string("shape ") + c.name + ": conforming positive tets");
    }
    Mesh m;
    std::string err;
    CHECK(!buildShapeMesh("ibeam", {1, 0.1, 0.2, 0.2, 0.006}, 0, m, err), "shape: flanges thicker than the section are rejected");
    CHECK(!buildShapeMesh("khong-co", {1}, 0, m, err), "shape: unknown name is rejected");
    CHECK(shapeCaseText("ibeam", {2, 0.1, 0.2, 0.01, 0.006}).find("geometry = ibeam 2 0.1 0.2 0.01 0.006") != std::string::npos, "shape: case text has the geometry line");
}

static void testLinearSolverBackends() {
    CHECK(setLinearSolver("cg") && setLinearSolver("direct") && setLinearSolver("auto"), "linear solver: auto/direct/cg are always available");
    {   // Poisson 2D 30x30: giải trực tiếp phải trùng CG
        const int m = 30, n = m * m;
        std::vector<Triplet> tr;
        std::vector<double> b(n, 1.0);
        for (int j = 0; j < m; ++j)
            for (int i = 0; i < m; ++i) {
                const int k = j * m + i;
                tr.push_back({k, k, 4.0});
                if (i > 0) tr.push_back({k, k - 1, -1.0});
                if (i + 1 < m) tr.push_back({k, k + 1, -1.0});
                if (j > 0) tr.push_back({k, k - m, -1.0});
                if (j + 1 < m) tr.push_back({k, k + m, -1.0});
            }
        const CsrMatrix A = buildCsr(n, tr);
        std::vector<double> xd, xc;
        setLinearSolver("direct");
        const auto rd = solveCg(A, b, xd, 1e-12, 5000);
        setLinearSolver("cg");
        const auto rc = solveCg(A, b, xc, 1e-12, 5000);
        double d = 0, mx = 0;
        for (int i = 0; i < n; ++i) { d = std::max(d, std::abs(xd[i] - xc[i])); mx = std::max(mx, std::abs(xd[i])); }
        CHECK(rd.converged && rd.iterations == 0 && rc.converged && d < 1e-8 * mx, "direct banded Cholesky matches CG");
    }
    CHECK(!setLinearSolver("khong-co"), "linear solver: unknown name is rejected");
    CHECK(petscAvailable() == setLinearSolver("petsc"), "linear solver: petsc accepted only when built in");
    setLinearSolver("cg");
#ifdef FASTCAE_HAVE_PETSC
    // 1D Poisson (tam giác 2,-1): so PETSc với CG tự viết
    const int n = 200;
    std::vector<Triplet> tr;
    std::vector<double> b(n, 1.0);
    for (int i = 0; i < n; ++i) {
        tr.push_back({i, i, 2.0});
        if (i > 0) tr.push_back({i, i - 1, -1.0});
        if (i + 1 < n) tr.push_back({i, i + 1, -1.0});
    }
    const CsrMatrix A = buildCsr(n, tr);
    std::vector<double> x1, x2;
    setLinearSolver("cg");
    const auto r1 = solveCg(A, b, x1, 1e-12, 5000);
    CHECK(setLinearSolver("petsc"), "petsc: backend selected");
    const auto r2 = solveCg(A, b, x2, 1e-12, 5000);
    setLinearSolver("cg");
    double dmax = 0;
    for (int i = 0; i < n; ++i) dmax = std::max(dmax, std::abs(x1[i] - x2[i]));
    CHECK(r1.converged && r2.converged && dmax < 1e-6 * *std::max_element(x1.begin(), x1.end()), "petsc: same solution as built-in CG");
#endif
}

static void testOpenFoamCase() {
    const auto dir = std::filesystem::temp_directory_path() / "fastcae_of_test";
    { std::error_code ec_; std::filesystem::remove_all(dir, ec_); }
    OpenFoamCavity p;
    p.cells = 10;
    p.reynolds = 100;
    std::string err;
    CHECK(writeOpenFoamCavity(dir.string(), p, &err), "openfoam: case written");
    for (const char* f : {"system/blockMeshDict", "system/controlDict", "system/fvSchemes", "system/fvSolution",
                          "constant/transportProperties", "0/U", "0/p"})
        CHECK(std::filesystem::exists(dir / f), std::string("openfoam: ") + f);
    std::string all;
    {
        std::ifstream tp(dir / "constant/transportProperties");
        all.assign((std::istreambuf_iterator<char>(tp)), std::istreambuf_iterator<char>());
    }   // đóng file trước khi xóa thư mục (Windows không xóa được file đang mở)
    CHECK(all.find("0.001") != std::string::npos, "openfoam: nu = U*L/Re = 0.001 for Re=100");
    { std::error_code ec_; std::filesystem::remove_all(dir, ec_); }
}

static RunReport runText(const std::string& text) {
    CaseFile cf;
    cf.parse(text);
    std::ostringstream os;
    return runCase(cf, os, "", false);
}

// Hồi quy cho các lỗi tìm được bằng fuzz/ASan: đầu vào xấu phải báo lỗi nhanh, không treo/sập/cấp phát khổng lồ.
static void testHardening() {
    auto r = runText("type = elastic\ngeometry = box 1 0.1 0.1\ndivisions = 20 999999999 3\nfix = xmin\nload = xmax 0 0 -1\n");
    CHECK(!r.ok && r.message.find("min") != std::string::npos, "hardening: oversized divisions rejected");
    r = runText("type = elastic\ngeometry = box 1 0.1 0.1\ndivisions = nan inf 1e308\nfix = xmin\nload = xmax 0 0 -1\n");
    CHECK(r.ok || !r.message.empty(), "hardening: nan/inf divisions do not crash");
    r = runText("type = cavity\nn = 999999999\nRe = 100\n");
    CHECK(!r.ok, "hardening: cavity n out of range rejected");
    r = runText("type = cavity\nn = 20\nRe = nan\n");
    CHECK(!r.ok && r.iterations == 0, "hardening: cavity Re = nan rejected immediately");
    r = runText("type = frame\nnode = a 0 0 0\nnode = b 1 0 0\nmember = a b\nudl = 1e308 0 0 -1\n");
    CHECK(!r.ok, "hardening: huge udl member id rejected");
    // Script lỗi lặp lại nhiều lần: không rò rỉ (ASan sẽ bắt) và không làm hỏng trạng thái.
    ScriptEngine e;
    int bad = 0;
    for (int i = 0; i < 50; ++i) if (!e.runString("fastcae.solve('type = frame')").ok) ++bad;
    CHECK(bad == 50, "hardening: failing fastcae.solve raises every time (no leak under ASan)");
}

static void testCcxFacesPressureContact() {
    Mesh m = makeBoxMesh(1, 1, 1, 2, 2, 2);
    const auto top = selectNodes(m, "zmax");
    const auto faces = boundaryFacesOnNodes(m, top);
    CHECK(faces.size() == 8, "ccx faces: 2x2 grid on top boundary gives 8 triangular faces");
    bool ok = true;
    for (const auto& f : faces) ok = ok && f.face >= 1 && f.face <= 4 && f.elem >= 0 && f.elem < static_cast<int>(m.tets.size());
    CHECK(ok, "ccx faces: valid element/face ids");
    CHECK(boundaryFacesOnNodes(m, {}).empty(), "ccx faces: empty set -> no faces");
    // mặt trong (giữa hai khối) không phải mặt biên: chọn mặt phẳng z = 0.5 không cho mặt biên nào
    CHECK(boundaryFacesOnNodes(m, selectNodes(m, "all")).size() == 6u * 8u, "ccx faces: whole boundary of a 2x2x2 box = 6 sides x 8 triangles");
    CcxSpec s;
    s.analysis = "contact";
    s.bcs.push_back({0, 0, 0});
    CcxPressure pr; pr.faces = faces; pr.value = 2.5e6;
    s.pressures.push_back(pr);
    CcxContact ct; ct.slave = {{0, 1}}; ct.master = {{1, 3}}; ct.adjust = 0.002;
    s.contacts.push_back(ct);
    std::string err;
    const std::string d = ccxDeck(m, nullptr, s, &err);
    CHECK(d.find("*DLOAD\n") != std::string::npos && d.find(", P") != std::string::npos && d.find("2500000") != std::string::npos, "ccx deck: face pressure written as *DLOAD Pn");
    CHECK(d.find("*CONTACT PAIR, INTERACTION=IC1, TYPE=SURFACE TO SURFACE, ADJUST=0.002") != std::string::npos && d.find("*SURFACE, NAME=SLAVE1") != std::string::npos, "ccx deck: contact pair with ADJUST and surfaces");
    s.contacts[0].master.clear();
    CHECK(ccxDeck(m, nullptr, s, &err).empty() && err.find("contact") != std::string::npos, "ccx deck: contact with empty surface rejected");
    auto r = runText("type = elastic\nsolver = calculix\ngeometry = box 1 1 1\ndivisions = 2 2 2\nfix = xmin\ncontact = xmin xmax friction 0.3\n");
    CHECK(!r.ok && r.message.find("ma sat") != std::string::npos, "contact: friction rejected (not supported)");
    r = runText("type = elastic\nsolver = calculix\ngeometry = box 1 1 1\ndivisions = 2 2 2\nfix = xmin\npressure = nonexistent 100\n");
    CHECK(!r.ok, "pressure: unknown node set rejected");
}

static void testCcxDeck() {
    Mesh m = makeBoxMesh(1, 0.1, 0.1, 4, 1, 1);
    CcxSpec s;
    s.bcs.push_back({0, 0, 0});
    s.loads.push_back({1, {0, 0, -5}});
    std::string err;
    s.analysis = "buckle";
    auto d = ccxDeck(m, nullptr, s, &err);
    const auto bpos = d.find("*BUCKLE");
    CHECK(bpos != std::string::npos && d.find("*CLOAD", bpos) != std::string::npos, "ccx deck: buckle step repeats the reference load");
    s.analysis = "plastic";
    d = ccxDeck(m, nullptr, s, &err);
    CHECK(d.empty() && err.find("yield") != std::string::npos, "ccx deck: plastic without yield rejected");
    s.yield = 250e6; s.hardening = 1e9;
    d = ccxDeck(m, nullptr, s, &err);
    CHECK(d.find("*PLASTIC\n250000000, 0.") != std::string::npos && d.find("PEEQ") != std::string::npos, "ccx deck: plastic table + PEEQ output");
    s.analysis = "heat_transient"; s.totalTime = 10; s.tempBCs.push_back({0, 100.0});
    d = ccxDeck(m, nullptr, s, &err);
    CHECK(d.find("DC3D4") != std::string::npos && d.find("*HEAT TRANSFER\n") != std::string::npos && d.find(", 11, 11, 100") != std::string::npos, "ccx deck: transient heat uses DC3D4 + dof 11");
    s.analysis = "dynamic";
    d = ccxDeck(m, nullptr, s, &err);
    CHECK(d.find("*DYNAMIC, DIRECT") != std::string::npos && d.find("AMPLITUDE=AST") != std::string::npos, "ccx deck: dynamic direct + step amplitude");
    s.analysis = "bogus";
    CHECK(ccxDeck(m, nullptr, s, &err).empty() && !err.empty(), "ccx deck: unknown analysis rejected");
    // đọc frd nhiều gia số
    {
        std::ofstream f("ccx_inc_test.frd");
        f << "    1C$\n  100CL  101 1.00000E-01           2                     0    1           1\n -4  DISP        4    1\n -1         1 1.00000E+00 2.00000E+00 3.00000E+00\n -3\n"
             "  100CL  102 2.00000E-01           2                     0    1           1\n -4  STRESS      6    1\n -1         1 1.00000E+00 0.00000E+00 0.00000E+00 0.00000E+00 0.00000E+00 0.00000E+00\n -3\n";
    }
    std::vector<FrdIncrement> inc;
    CHECK(readFrdIncrements("ccx_inc_test.frd", inc) && inc.size() == 2 && inc[0].blocks.count("DISP") && inc[1].blocks.count("STRESS") &&
          std::abs(inc[1].value - 0.2) < 1e-12 && std::abs(inc[0].blocks["DISP"][0][2] - 3.0) < 1e-12, "ccx frd: increments parsed separately");
    std::remove("ccx_inc_test.frd");
}

static void testOpenFoamPolyMesh() {
    Mesh m = makeBoxMesh(2, 1, 1, 2, 2, 2);
    OpenFoam3D p;
    p.cadMesh = &m;
    p.nu = 0.1;
    p.face = {"inlet", "outlet", "wall", "wall", "symmetry", "symmetry"};
    std::string err;
    CHECK(validateOpenFoam3D(p, &err), "polyMesh: CAD mesh accepted without divisions");
    const auto dir = (std::filesystem::temp_directory_path() / "fastcae_poly_test").string();
    std::filesystem::remove_all(dir);
    CHECK(writeOpenFoam3D(dir, p, &err), "polyMesh: case written from tetrahedral mesh");
    auto slurp = [&](const std::string& f) { std::ifstream in(dir + "/" + f); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); };
    const std::string owner = slurp("constant/polyMesh/owner"), bnd = slurp("constant/polyMesh/boundary"), U = slurp("0/U");
    // 2x2x2 hộp = 48 tứ diện; biên = 6 mặt x 8 tam giác = 48; trong = (4*48 - 48)/2 = 72
    CHECK(owner.find("nCells:48") != std::string::npos && owner.find("nInternalFaces:72") != std::string::npos && owner.find("nFaces:120") != std::string::npos, "polyMesh: face/cell counts consistent");
    CHECK(bnd.find("xmin") != std::string::npos && bnd.find("symmetryPlane") != std::string::npos && bnd.find("walls") == std::string::npos, "polyMesh: six planar patches, symmetry type, no 'walls' patch for a pure box");
    CHECK(U.find("zmin") != std::string::npos && !std::filesystem::exists(dir + "/system/blockMeshDict"), "polyMesh: fields follow patches, no blockMeshDict");
    CHECK(openFoam3DShellScript("", dir, p).find("blockMesh") == std::string::npos && openFoam3DShellScript("", dir, p).find("checkMesh") != std::string::npos, "polyMesh: shell script skips blockMesh, runs checkMesh");
    std::filesystem::remove_all(dir);
    // mặt phẳng không có mặt biên nào nhưng được gán inlet/outlet -> báo lỗi rõ ràng
    Mesh one;
    one.nodes = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    one.tets = {{0, 1, 2, 3}};
    OpenFoam3D q;
    q.cadMesh = &one;
    q.nu = 0.1;
    CHECK(!writePolyMeshFromTets(dir, one, q, &err) && err.find("xmax") != std::string::npos, "polyMesh: inlet/outlet plane without faces rejected");
    std::filesystem::remove_all(dir);
}

static void testOpenFoam3D() {
    OpenFoam3D p;
    p.lx = 10; p.ly = 1; p.lz = 0.5; p.nx = 20; p.ny = 10; p.nz = 2; p.nu = 0.1;
    p.face = {"inlet", "outlet", "wall", "wall", "symmetry", "symmetry"};
    std::string err;
    CHECK(validateOpenFoam3D(p, &err), "cfd3d: valid channel accepted");
    OpenFoam3D bad = p; bad.face[1] = "wall";
    CHECK(!validateOpenFoam3D(bad, &err) && err.find("outlet") != std::string::npos, "cfd3d: inlet without outlet rejected");
    bad = p; bad.nu = -1;
    CHECK(!validateOpenFoam3D(bad, &err), "cfd3d: negative viscosity rejected");
    bad = p; bad.model = "les";
    CHECK(!validateOpenFoam3D(bad, &err), "cfd3d: unknown turbulence model rejected");
    bad = p; bad.nx = 100000; bad.ny = 100000;
    CHECK(!validateOpenFoam3D(bad, &err), "cfd3d: oversized mesh rejected");
    const auto dir = (std::filesystem::temp_directory_path() / "fastcae_foam3d_test").string();
    std::filesystem::remove_all(dir);
    p.model = "kepsilon";
    CHECK(writeOpenFoam3D(dir, p, &err), "cfd3d: case written");
    auto slurp = [&](const std::string& f) { std::ifstream in(dir + "/" + f); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); };
    const std::string bm = slurp("system/blockMeshDict"), U = slurp("0/U"), pf = slurp("0/p"), eps = slurp("0/epsilon");
    CHECK(bm.find("symmetryPlane") != std::string::npos && bm.find("(20 10 2)") != std::string::npos, "cfd3d: blockMesh has symmetry patches and cell counts");
    CHECK(U.find("noSlip") != std::string::npos && pf.find("fixedValue") != std::string::npos, "cfd3d: no-slip walls and fixed outlet pressure");
    CHECK(eps.find("epsilonWallFunction") != std::string::npos && slurp("constant/turbulenceProperties").find("kEpsilon") != std::string::npos, "cfd3d: k-epsilon wall functions");
    CHECK(openFoam3DSolver(p) == "simpleFoam" && (p.transient = true, openFoam3DSolver(p) == "pimpleFoam"), "cfd3d: steady -> simpleFoam, transient -> pimpleFoam");
    std::filesystem::remove_all(dir);
    auto r = runText("type = cfd3d\nsolver = builtin\ngeometry = box 1 1 1\ndivisions = 4 4 4\n");
    CHECK(!r.ok && r.message.find("openfoam") != std::string::npos, "cfd3d: requires solver = openfoam");
}

static void testVtkReader() {
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "fastcae_reader_test.vtk";
    {
        std::ofstream o(p);
        o << "# vtk DataFile Version 3.0\nt\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS 8 double\n"
             "0 0 0\n1 0 0\n1 1 0\n0 1 0\n0 0 1\n1 0 1\n1 1 1\n0 1 1\n"
             "CELLS 2 11\n8 0 1 2 3 4 5 6 7\n2 0 1\nCELL_TYPES 2\n12\n3\n"
             "POINT_DATA 8\nSCALARS T double 1\nLOOKUP_TABLE default\n0 1 2 3 4 5 6 7\nVECTORS U float\n";
        for (int i = 0; i < 8; ++i) o << (i == 7 ? 3 : 0) << " " << (i == 7 ? 4 : 0) << " 0\n";
        o << "CELL_DATA 2\nSCALARS p double 1\nLOOKUP_TABLE default\n10 20\n";
    }
    Mesh m;
    VtkFields f;
    VtkReadInfo info;
    std::string err;
    CHECK(readVtk(p.string(), m, f, &info, &err), "vtk reader: reads hexahedron file");
    CHECK(info.tetsProduced == 6 && info.cellsSkipped == 1, "vtk reader: hex -> 6 tets, line cell skipped");
    CHECK(std::abs(m.totalVolume() - 1.0) < 1e-12, "vtk reader: unit cube volume preserved");
    CHECK(f.pointScalars.count("T") && f.pointScalars.count("U_magnitude"), "vtk reader: scalar + vector magnitude");
    CHECK(std::abs(f.pointScalars["U_magnitude"][7] - 5.0) < 1e-12, "vtk reader: |U| = 5");
    CHECK(f.pointScalars.count("p"), "vtk reader: cell data averaged to nodes");
    // round trip with the FastCAE writer
    const fs::path q = fs::temp_directory_path() / "fastcae_reader_rt.vtk";
    Mesh m2;
    VtkFields f2;
    CHECK(writeVtk(q.string(), m, f, &err) && readVtk(q.string(), m2, f2, nullptr, &err), "vtk reader: writer round trip");
    CHECK(m2.numCells() == m.numCells() && f2.pointScalars.count("T"), "vtk reader: round trip keeps cells and fields");
    { std::ofstream o(p); o << "# vtk DataFile Version 3.0\nt\nBINARY\nDATASET UNSTRUCTURED_GRID\n"; }
    CHECK(!readVtk(p.string(), m2, f2, nullptr, &err) && err.find("ASCII") != std::string::npos, "vtk reader: binary file rejected with hint");
    fs::remove(p);
    fs::remove(q);
}


// ---- Nhap nhieu dinh dang CAD / luoi: phan tu khoi khac tu dien, INP Abaqus, nhan dang duoi file, bo chuyen doi ngoai
static std::string writeTmp(const std::string& name, const std::string& text) {
    std::ofstream o(name);
    o << text;
    return name;
}

// So mat tam giac chi xuat hien mot lan (mat bien). Luoi lien tuc: mat chung cua hai phan tu xuat hien hai lan.
static std::size_t boundaryFaces(const Mesh& m) {
    std::map<std::array<int, 3>, int> cnt;
    static const int f[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    for (const auto& t : m.tets)
        for (const auto& fi : f) {
            std::array<int, 3> k = {t[static_cast<std::size_t>(fi[0])], t[static_cast<std::size_t>(fi[1])], t[static_cast<std::size_t>(fi[2])]};
            std::sort(k.begin(), k.end());
            ++cnt[k];
        }
    std::size_t b = 0;
    for (const auto& kv : cnt) b += kv.second == 1;
    return b;
}

static void testCadFormats() {
    CHECK(detectCadFormat("a.xao") == CadFormat::Xao, "formats: .xao");
    CHECK(detectCadFormat("a.STL") == CadFormat::Stl, "formats: .stl");
    for (const char* f : {"a.unv", "a.inp", "a.bdf", "a.nas", "a.med", "a.mesh"})
        CHECK(detectCadFormat(f) == CadFormat::ForeignMesh, std::string("formats: foreign mesh ") + f);
    CHECK(detectCadFormat("a.FCStd") == CadFormat::FreeCad, "formats: FCStd");
    CHECK(detectCadFormat("a.catpart") == CadFormat::Native, "formats: CATPart is closed");
    CHECK(canImportFormat("a.step") && canImportFormat("a.stl") && canImportFormat("a.inp") && canImportFormat("a.FCStd"), "formats: importable");
#ifndef _WIN32
    unsetenv("FASTCAE_CAD_CONVERTER");
    CHECK(!canImportFormat("a.sldprt"), "formats: SLDPRT not importable without a converter");
    setenv("FASTCAE_CAD_CONVERTER", "cp \"{in}\" \"{out}\"", 1);
    CHECK(canImportFormat("a.sldprt"), "formats: SLDPRT importable once a converter is configured");
    unsetenv("FASTCAE_CAD_CONVERTER");
#endif
    CHECK(cadImportExtensions().size() >= 15, "formats: extension list for the open dialog");

    const std::string nodes2 =
        "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n$Nodes\n12\n"
        "1 0 0 0\n2 1 0 0\n3 1 1 0\n4 0 1 0\n5 0 0 1\n6 1 0 1\n7 1 1 1\n8 0 1 1\n9 2 0 0\n10 2 1 0\n11 2 0 1\n12 2 1 1\n$EndNodes\n";
    // hai luc dien chung mat + mot tam giac (bi bo qua)
    {
        Mesh m; std::string err;
        const std::string f = writeTmp("hex2.msh", nodes2 + "$Elements\n3\n1 5 2 0 1 1 2 3 4 5 6 7 8\n2 5 2 0 1 2 9 10 3 6 11 12 7\n3 2 2 0 1 1 2 3\n$EndElements\n");
        CHECK(readGmshMsh(f, m, &err), "hex: msh reads");
        CHECK(std::abs(m.totalVolume() - 2.0) < 1e-12, "hex: two hexahedra keep total volume 2");
        CHECK(m.numCells() == 24 && m.numNodes() == 14, "hex: 12 tets each + 1 centre node each");
        CHECK(boundaryFaces(m) == 20, "hex: shared face is conforming (only the 10 outer quads = 20 triangles are boundary)");
        std::remove(f.c_str());
    }
    {
        Mesh m;
        const std::string f = writeTmp("prism.msh", "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n$Nodes\n6\n1 0 0 0\n2 1 0 0\n3 0 1 0\n4 0 0 1\n5 1 0 1\n6 0 1 1\n$EndNodes\n$Elements\n1\n1 6 0 1 2 3 4 5 6\n$EndElements\n");
        CHECK(readGmshMsh(f, m) && std::abs(m.totalVolume() - 0.5) < 1e-12 && boundaryFaces(m) == 2 * 1 + 3 * 2, "prism: volume 1/2, 2 tri + 3 quad faces");
        std::remove(f.c_str());
    }
    {
        Mesh m;
        const std::string f = writeTmp("pyr.msh", "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n$Nodes\n5\n1 0 0 0\n2 1 0 0\n3 1 1 0\n4 0 1 0\n5 0.5 0.5 1\n$EndNodes\n$Elements\n1\n1 7 0 1 2 3 4 5\n$EndElements\n");
        CHECK(readGmshMsh(f, m) && std::abs(m.totalVolume() - 1.0 / 3.0) < 1e-12, "pyramid: volume 1/3");
        std::remove(f.c_str());
    }
    {   // tu dien bac 2: nut giua canh khong dung phai bi bo
        Mesh m;
        const std::string f = writeTmp("t10.msh", "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n$Nodes\n10\n1 0 0 0\n2 1 0 0\n3 0 1 0\n4 0 0 1\n5 .5 0 0\n6 .5 .5 0\n7 0 .5 0\n8 0 0 .5\n9 .5 0 .5\n10 0 .5 .5\n$EndNodes\n$Elements\n1\n1 11 0 1 2 3 4 5 6 7 8 9 10\n$EndElements\n");
        CHECK(readGmshMsh(f, m) && m.numNodes() == 4 && std::abs(m.totalVolume() - 1.0 / 6.0) < 1e-12, "tet10: corner tet kept, unused mid-edge nodes dropped");
        std::remove(f.c_str());
    }
    {   // luoi chi co be mat -> bao loi ro rang
        Mesh m; std::string err;
        const std::string f = writeTmp("surf.msh", "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n$Nodes\n3\n1 0 0 0\n2 1 0 0\n3 0 1 0\n$EndNodes\n$Elements\n1\n1 2 0 1 2 3\n$EndElements\n");
        CHECK(!readGmshMsh(f, m, &err) && err.find("be mat") != std::string::npos, "surface-only msh: clear error");
        std::remove(f.c_str());
    }
    {   // Abaqus/CalculiX: C3D8 + C3D10 (nhieu dong) + vo S4 bi bo qua, nut danh so khong lien tuc
        Mesh m; std::string err;
        const std::string f = writeTmp("m.inp",
            "*HEADING\nx\n** comment\n*NODE, NSET=ALL\n10, 0,0,0\n20, 1,0,0\n30, 1,1,0\n40, 0,1,0\n50, 0,0,1\n60, 1,0,1\n70, 1,1,1\n80, 0,1,1\n"
            "*ELEMENT, TYPE=C3D8R, ELSET=A\n1, 10,20,30,40,50,60,70,80\n*ELEMENT, type = S4\n9, 10,20,30,40\n");
        CHECK(readAbaqusInp(f, m, &err), "inp: reads C3D8");
        CHECK(std::abs(m.totalVolume() - 1.0) < 1e-12 && m.numCells() == 12, "inp: hexahedron -> 12 tets, volume 1");
        std::remove(f.c_str());
        const std::string g = writeTmp("m10.inp",
            "*NODE\n1,0,0,0\n2,1,0,0\n3,0,1,0\n4,0,0,1\n5,.5,0,0\n6,.5,.5,0\n7,0,.5,0\n8,0,0,.5\n9,.5,0,.5\n10,0,.5,.5\n"
            "*ELEMENT, TYPE=C3D10\n1, 1,2,3,4,5,6,7,\n8,9,10\n");
        Mesh m2;
        CHECK(readAbaqusInp(g, m2, &err) && m2.numCells() == 1 && m2.numNodes() == 4 && std::abs(m2.totalVolume() - 1.0 / 6.0) < 1e-12,
              "inp: C3D10 written over two lines");
        std::remove(g.c_str());
        const std::string h = writeTmp("shell.inp", "*NODE\n1,0,0,0\n2,1,0,0\n3,0,1,0\n*ELEMENT, TYPE=S3\n1,1,2,3\n");
        Mesh m3;
        CHECK(!readAbaqusInp(h, m3, &err) && err.find("khoi") != std::string::npos, "inp: shell-only file gives a clear error");
        std::remove(h.c_str());
        auto r = importCad(f, {});
        CHECK(!r.ok, "importCad: missing file fails cleanly");
    }
#ifndef _WIN32
    {   // bo chuyen doi ngoai (gia lap): .sldprt la file luoi .msh doi ten -> sau chuyen doi se doc nhu .step? dung .msh tru thuc
        const std::string src = writeTmp("part.sldprt", "not a real cad file");
        setenv("FASTCAE_CAD_CONVERTER", "false {in} {out}", 1);
        auto r = importCad(src, {});
        CHECK(!r.ok && r.message.find("Chuyen doi") != std::string::npos, "converter: failing converter reports an error");
        setenv("FASTCAE_CAD_CONVERTER", "mytool --in x", 1);
        r = importCad(src, {});
        CHECK(!r.ok && r.message.find("{in}") != std::string::npos, "converter: template without {in}/{out} rejected");
        unsetenv("FASTCAE_CAD_CONVERTER");
        r = importCad(src, {});
        CHECK(!r.ok && r.message.find("STEP") != std::string::npos, "converter: no converter -> advice to export STEP");
        std::remove(src.c_str());
    }
#endif
}

int main() {
    testVtkReader();
    testRegistryAndAuthor();
    testHardening();
    testCcxDeck();
    testCcxFacesPressureContact();
    testOpenFoam3D();
    testOpenFoamPolyMesh();
    testNoticesMatchRegistry();
    testPlaneSelection();
    testFrameSolver();
    testScripting();
    testFrameEigen();
    testShellAndTemplates();
    testShapeLibrary();
    testLinearSolverBackends();
    testOpenFoamCase();
    testOpenFoamResults();
    testMesh();
    testPatchTension();
    testCantilever();
    testThermal();
    testCavity();
    testCaseFileAndCad();
    testCalculixBridge();
    testQuadratic();
    testMshAndCad();
    testCadFormats();
    if (failures) { std::cerr << failures << " test(s) FAILED\n"; return 1; }
    std::cout << "All tests passed\n";
    return 0;
}