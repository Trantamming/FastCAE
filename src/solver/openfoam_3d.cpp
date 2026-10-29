// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/openfoam_3d.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <map>
#include <fstream>
#include <sstream>

#include "solver/openfoam_results.hpp"

namespace fs = std::filesystem;

namespace fastcae {

namespace {

const char* kFaceName[6] = {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};

std::string header(const char* cls, const char* object) {
    std::ostringstream o;
    o << "/*--------------------------------*- C++ -*----------------------------------*\\\n"
         "  Tao boi FastCAE - Tac gia: Tran Minh Tam (MIT)\n"
         "\\*---------------------------------------------------------------------------*/\n"
         "FoamFile\n{\n    version     2.0;\n    format      ascii;\n    class       " << cls << ";\n    object      " << object << ";\n}\n\n";
    return o.str();
}

bool put(const fs::path& p, const std::string& text, std::string* err) {
    std::ofstream f(p, std::ios::binary);
    if (!f) { if (err) *err = "Khong ghi duoc " + p.string(); return false; }
    f << text;
    return true;
}

bool turbulent(const OpenFoam3D& p) { return p.model != "laminar"; }

bool validType(const std::string& t) { return t == "inlet" || t == "outlet" || t == "wall" || t == "symmetry" || t == "slip"; }

std::string vec(const std::array<double, 3>& v) {
    std::ostringstream o;
    o << "(" << v[0] << " " << v[1] << " " << v[2] << ")";
    return o.str();
}

double minSection(const OpenFoam3D& p) {
    // kích thước mặt cắt vuông góc với dòng chủ đạo (hướng vận tốc vào)
    const double ax = std::abs(p.inletU[0]), ay = std::abs(p.inletU[1]), az = std::abs(p.inletU[2]);
    if (ax >= ay && ax >= az) return std::min(p.ly, p.lz);
    if (ay >= az) return std::min(p.lx, p.lz);
    return std::min(p.lx, p.ly);
}

double speedIn(const OpenFoam3D& p) { return std::sqrt(p.inletU[0] * p.inletU[0] + p.inletU[1] * p.inletU[1] + p.inletU[2] * p.inletU[2]); }

}  // namespace

bool validateOpenFoam3D(const OpenFoam3D& p, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    for (double v : {p.lx, p.ly, p.lz}) if (!std::isfinite(v) || v <= 0) return fail("cfd3d: kich thuoc hop (geometry = box lx ly lz) phai la so duong huu han");
    if (!p.cadMesh) {
        for (int n : {p.nx, p.ny, p.nz}) if (n < 1) return fail("cfd3d: divisions phai >= 1");
        if (static_cast<double>(p.nx) * p.ny * p.nz > 5.0e6) return fail("cfd3d: luoi qua lon (> 5 trieu o); giam divisions");
    } else if (p.cadMesh->tets.empty() || p.cadMesh->tets.size() > 5000000) {
        return fail("cfd3d: luoi CAD rong hoac qua lon (> 5 trieu o)");
    }
    if (!(p.faceOther == "wall" || p.faceOther == "symmetry" || p.faceOther == "slip")) return fail("cfd3d: bc_other = wall | symmetry | slip");
    if (!std::isfinite(p.nu) || p.nu <= 0) return fail("cfd3d: nu phai la so duong huu han (m2/s)");
    for (double v : p.inletU) if (!std::isfinite(v)) return fail("cfd3d: inlet_velocity khong hop le");
    if (p.model != "laminar" && p.model != "kepsilon" && p.model != "komegasst") return fail("cfd3d: model = laminar | kepsilon | komegasst");
    bool in = false, out = false;
    for (int i = 0; i < 6; ++i) {
        if (!validType(p.face[i])) return fail(std::string("cfd3d: bc_") + kFaceName[i] + " = inlet | outlet | wall | symmetry | slip");
        in = in || p.face[i] == "inlet";
        out = out || p.face[i] == "outlet";
    }
    if (in && !(speedIn(p) > 0)) return fail("cfd3d: co mat 'inlet' nhung inlet_velocity = 0");
    if (in != out && !(in && !out)) { /* chỉ có cửa ra, không có cửa vào: vô nghĩa */ }
    if (out && !in) return fail("cfd3d: co mat 'outlet' nhung khong co mat 'inlet'");
    if (in && !out) return fail("cfd3d: co mat 'inlet' nhung khong co mat 'outlet' (khong bao toan khoi luong)");
    if (p.transient && !(p.endTime > 0 && p.deltaT > 0)) return fail("cfd3d: end_time va dt phai > 0");
    if (p.maxIter < 1) return fail("cfd3d: max_iter phai >= 1");
    if (!(p.turbIntensity > 0 && p.turbIntensity < 1)) return fail("cfd3d: turbulence_intensity phai trong (0,1)");
    return true;
}

std::string openFoam3DSolver(const OpenFoam3D& p) { return p.transient ? "pimpleFoam" : "simpleFoam"; }

std::string openFoam3DShellScript(const std::string& bashrc, const std::string& caseDir, const OpenFoam3D& p) {
    const std::string c = "\"" + caseDir + "\"";
    const std::string base = "\"" + caseDir + "/log.";
    const std::string solver = openFoam3DSolver(p);
    std::string s;
    if (!bashrc.empty()) s += "source \"" + bashrc + "\" >/dev/null 2>&1; ";
    if (p.cadMesh) s += "checkMesh -case " + c + " > " + base + "checkMesh\" 2>&1; ";   // chẩn đoán lưới CAD (không bắt buộc đạt)
    else s += "blockMesh -case " + c + " > " + base + "blockMesh\" 2>&1 && ";
    s += solver + " -case " + c + " > " + base + solver + "\" 2>&1";
    return s;
}


namespace {

struct PolyPatch {
    std::string name, role, type;
    int start = 0, n = 0;
};

struct PolyBuild {
    std::vector<std::array<int, 3>> faces;   // nội bộ rồi biên, đã sắp theo patch
    std::vector<int> owner, neighbour;
    int nInternal = 0;
    std::vector<PolyPatch> patches;
    std::vector<char> nodeWall, nodeInlet;   // theo nút lưới
    double inletArea = 0;
    std::vector<std::array<double, 3>> outletAreaVec;   // vec diện tích (ra ngoài) từng mặt outlet
    std::vector<int> outletCell;
};

const char* polyType(const std::string& role) { return role == "wall" ? "wall" : (role == "symmetry" ? "symmetryPlane" : "patch"); }

bool buildPoly(const Mesh& m, const OpenFoam3D& p, PolyBuild& out, std::string* err) {
    auto fail = [&](const std::string& s) { if (err) *err = s; return false; };
    Vec3 lo, hi;
    m.bounds(lo, hi);
    const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-300});
    const double tol = 1e-7 * size;
    struct Rec { int c0 = -1, c1 = -1; std::array<int, 3> v{}; };
    std::map<std::array<int, 3>, Rec> faces;
    for (std::size_t c = 0; c < m.tets.size(); ++c) {
        const auto& t = m.tets[c];
        Vec3 cen{0, 0, 0};
        for (int k = 0; k < 4; ++k) { cen.x += m.nodes[static_cast<std::size_t>(t[k])].x / 4; cen.y += m.nodes[static_cast<std::size_t>(t[k])].y / 4; cen.z += m.nodes[static_cast<std::size_t>(t[k])].z / 4; }
        for (int k = 0; k < 4; ++k) {
            std::array<int, 3> v{};
            int q = 0;
            for (int j = 0; j < 4; ++j) if (j != k) v[static_cast<std::size_t>(q++)] = t[j];
            const Vec3& a = m.nodes[static_cast<std::size_t>(v[0])];
            const Vec3& b = m.nodes[static_cast<std::size_t>(v[1])];
            const Vec3& d = m.nodes[static_cast<std::size_t>(v[2])];
            const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{d.x - a.x, d.y - a.y, d.z - a.z};
            const Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            if (n.x * (cen.x - a.x) + n.y * (cen.y - a.y) + n.z * (cen.z - a.z) > 0) std::swap(v[1], v[2]);   // hướng ra ngoài ô
            auto key = v;
            std::sort(key.begin(), key.end());
            auto& r = faces[key];
            if (r.c0 < 0) { r.c0 = static_cast<int>(c); r.v = v; }
            else if (r.c1 < 0) r.c1 = static_cast<int>(c);
            else return fail("cfd3d: luoi CAD khong hop le (mot mat chung cho hon 2 phan tu)");
        }
    }
    struct Int { int c0, c1; std::array<int, 3> v; };
    std::vector<Int> internal;
    // patch: 0..5 = xmin..zmax, 6 = còn lại
    std::vector<std::vector<std::pair<int, std::array<int, 3>>>> bnd(7);
    for (const auto& [key, r] : faces) {
        if (r.c1 >= 0) { internal.push_back({r.c0, r.c1, r.v}); continue; }
        int patch = 6;
        auto on = [&](double v, double target) { return std::abs(v - target) <= tol; };
        for (int pl = 0; pl < 6 && patch == 6; ++pl) {
            bool all = true;
            for (int k = 0; k < 3; ++k) {
                const Vec3& q = m.nodes[static_cast<std::size_t>(r.v[static_cast<std::size_t>(k)])];
                const double val = pl < 2 ? q.x : (pl < 4 ? q.y : q.z);
                const double target = pl == 0 ? lo.x : (pl == 1 ? hi.x : (pl == 2 ? lo.y : (pl == 3 ? hi.y : (pl == 4 ? lo.z : hi.z))));
                all = all && on(val, target);
            }
            if (all) patch = pl;
        }
        bnd[static_cast<std::size_t>(patch)].push_back({r.c0, r.v});
    }
    std::sort(internal.begin(), internal.end(), [](const Int& a, const Int& b) { return a.c0 != b.c0 ? a.c0 < b.c0 : a.c1 < b.c1; });
    out = PolyBuild{};
    for (const auto& f : internal) { out.faces.push_back(f.v); out.owner.push_back(f.c0); out.neighbour.push_back(f.c1); }
    out.nInternal = static_cast<int>(internal.size());
    out.nodeWall.assign(m.nodes.size(), 0);
    out.nodeInlet.assign(m.nodes.size(), 0);
    static const char* names[7] = {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax", "walls"};
    for (int pt = 0; pt < 7; ++pt) {
        auto& list = bnd[static_cast<std::size_t>(pt)];
        const std::string role = pt < 6 ? p.face[static_cast<std::size_t>(pt)] : p.faceOther;
        if (list.empty()) {
            if (pt < 6 && (role == "inlet" || role == "outlet")) return fail(std::string("cfd3d: khong co mat bien nao nam tren mat phang ") + names[pt] + " nhung bc_" + names[pt] + " = " + role + " (hinh khong co mat phang do; doi bc_* hoac huong hinh)");
            continue;
        }
        PolyPatch pp;
        pp.name = names[pt]; pp.role = role; pp.type = polyType(role); pp.start = static_cast<int>(out.faces.size()); pp.n = static_cast<int>(list.size());
        for (const auto& [cell, v] : list) {
            out.faces.push_back(v);
            out.owner.push_back(cell);
            for (int k = 0; k < 3; ++k) {
                if (role == "wall") out.nodeWall[static_cast<std::size_t>(v[static_cast<std::size_t>(k)])] = 1;
                if (role == "inlet") out.nodeInlet[static_cast<std::size_t>(v[static_cast<std::size_t>(k)])] = 1;
            }
            if (role == "inlet" || role == "outlet") {
                const Vec3& a = m.nodes[static_cast<std::size_t>(v[0])];
                const Vec3& b = m.nodes[static_cast<std::size_t>(v[1])];
                const Vec3& d = m.nodes[static_cast<std::size_t>(v[2])];
                const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{d.x - a.x, d.y - a.y, d.z - a.z};
                const std::array<double, 3> av{0.5 * (e1.y * e2.z - e1.z * e2.y), 0.5 * (e1.z * e2.x - e1.x * e2.z), 0.5 * (e1.x * e2.y - e1.y * e2.x)};
                if (role == "inlet") out.inletArea += std::sqrt(av[0] * av[0] + av[1] * av[1] + av[2] * av[2]);
                else { out.outletAreaVec.push_back(av); out.outletCell.push_back(cell); }
            }
        }
        out.patches.push_back(pp);
    }
    return true;
}

}  // namespace

bool writePolyMeshFromTets(const std::string& caseDir, const Mesh& mesh, const OpenFoam3D& p, std::string* error,
                           std::vector<std::pair<std::string, std::string>>* patchRoles) {
    PolyBuild b;
    if (!buildPoly(mesh, p, b, error)) return false;
    if (patchRoles) { patchRoles->clear(); for (const auto& pp : b.patches) patchRoles->emplace_back(pp.name, pp.role); }
    std::error_code ec;
    const fs::path dir = fs::path(caseDir) / "constant" / "polyMesh";
    fs::create_directories(dir, ec);
    if (ec) { if (error) *error = "Khong tao duoc " + dir.string(); return false; }
    const std::string note = "nPoints:" + std::to_string(mesh.nodes.size()) + "  nCells:" + std::to_string(mesh.tets.size()) +
                             "  nFaces:" + std::to_string(b.faces.size()) + "  nInternalFaces:" + std::to_string(b.nInternal);
    auto head = [&](const char* cls, const char* obj, bool withNote) {
        std::ostringstream o;
        o << "FoamFile\n{\n    version     2.0;\n    format      ascii;\n    class       " << cls << ";\n";
        if (withNote) o << "    note        \"" << note << "\";\n";
        o << "    location    \"constant/polyMesh\";\n    object      " << obj << ";\n}\n\n";
        return o.str();
    };
    {
        std::ostringstream s;
        s << std::setprecision(15) << head("vectorField", "points", false) << mesh.nodes.size() << "\n(\n";
        for (const auto& q : mesh.nodes) s << "(" << q.x << " " << q.y << " " << q.z << ")\n";
        s << ")\n";
        if (!put(dir / "points", s.str(), error)) return false;
    }
    {
        std::ostringstream s;
        s << head("faceList", "faces", false) << b.faces.size() << "\n(\n";
        for (const auto& f : b.faces) s << "3(" << f[0] << " " << f[1] << " " << f[2] << ")\n";
        s << ")\n";
        if (!put(dir / "faces", s.str(), error)) return false;
    }
    {
        std::ostringstream s;
        s << head("labelList", "owner", true) << b.owner.size() << "\n(\n";
        for (int o : b.owner) s << o << "\n";
        s << ")\n";
        if (!put(dir / "owner", s.str(), error)) return false;
        std::ostringstream n;
        n << head("labelList", "neighbour", true) << b.neighbour.size() << "\n(\n";
        for (int o : b.neighbour) n << o << "\n";
        n << ")\n";
        if (!put(dir / "neighbour", n.str(), error)) return false;
    }
    {
        std::ostringstream s;
        s << head("polyBoundaryMesh", "boundary", false) << b.patches.size() << "\n(\n";
        for (const auto& pp : b.patches)
            s << "    " << pp.name << "\n    {\n        type " << pp.type << ";\n        nFaces " << pp.n << ";\n        startFace " << pp.start << ";\n    }\n";
        s << ")\n";
        if (!put(dir / "boundary", s.str(), error)) return false;
    }
    return true;
}

bool writeOpenFoam3D(const std::string& dir, const OpenFoam3D& p, std::string* error) {
    if (!validateOpenFoam3D(p, error)) return false;
    std::error_code ec;
    const fs::path root(dir);
    for (const char* sub : {"system", "constant", "0"}) fs::create_directories(root / sub, ec);
    if (ec) { if (error) *error = "Khong tao duoc thu muc " + dir; return false; }
    cleanOpenFoamResults(dir);
    const bool turb = turbulent(p);
    const bool kEps = p.model == "kepsilon", kOm = p.model == "komegasst";

    // ---- lưới: blockMesh (hộp) hoặc polyMesh ghi trực tiếp từ lưới tứ diện CAD
    std::vector<std::pair<std::string, std::string>> patchRoles;
    if (p.cadMesh) {
        if (!writePolyMeshFromTets(dir, *p.cadMesh, p, error, &patchRoles)) return false;
    } else {
        for (int i = 0; i < 6; ++i) patchRoles.emplace_back(kFaceName[i], p.face[static_cast<std::size_t>(i)]);
        {
            std::ostringstream s;
            s << header("dictionary", "blockMeshDict") << "convertToMeters 1;\n\nvertices\n(\n"
              << "    (0 0 0)\n    (" << p.lx << " 0 0)\n    (" << p.lx << " " << p.ly << " 0)\n    (0 " << p.ly << " 0)\n"
              << "    (0 0 " << p.lz << ")\n    (" << p.lx << " 0 " << p.lz << ")\n    (" << p.lx << " " << p.ly << " " << p.lz << ")\n    (0 " << p.ly << " " << p.lz << ")\n);\n\n"
              << "blocks\n(\n    hex (0 1 2 3 4 5 6 7) (" << p.nx << " " << p.ny << " " << p.nz << ") simpleGrading (1 1 1)\n);\n\nedges\n(\n);\n\nboundary\n(\n";
            static const char* faces[6] = {"(0 4 7 3)", "(1 2 6 5)", "(0 1 5 4)", "(3 7 6 2)", "(0 3 2 1)", "(4 5 6 7)"};
            for (int i = 0; i < 6; ++i) {
                const std::string& t = p.face[i];
                const char* type = t == "wall" ? "wall" : (t == "symmetry" ? "symmetryPlane" : "patch");
                s << "    " << kFaceName[i] << "\n    {\n        type " << type << ";\n        faces (" << faces[i] << ");\n    }\n";
            }
            s << ");\n\nmergePatchPairs\n(\n);\n";
            if (!put(root / "system/blockMeshDict", s.str(), error)) return false;
        }
    }
    // ---- controlDict
    {
        std::ostringstream s;
        s << header("dictionary", "controlDict") << "application " << openFoam3DSolver(p) << ";\nstartFrom startTime;\nstartTime 0;\nstopAt endTime;\n";
        if (p.transient) {
            s << "endTime " << p.endTime << ";\ndeltaT " << p.deltaT << ";\nwriteControl adjustableRunTime;\nwriteInterval " << p.endTime / 10 << ";\n"
              << "adjustTimeStep yes;\nmaxCo 0.9;\nmaxDeltaT " << p.endTime / 20 << ";\n";
        } else {
            s << "endTime " << p.maxIter << ";\ndeltaT 1;\nwriteControl timeStep;\nwriteInterval " << p.maxIter << ";\n";
        }
        s << "purgeWrite 0;\nwriteFormat ascii;\nwritePrecision 8;\nwriteCompression off;\ntimeFormat general;\ntimePrecision 8;\nrunTimeModifiable true;\n";
        if (!put(root / "system/controlDict", s.str(), error)) return false;
    }
    // ---- fvSchemes
    {
        std::ostringstream s;
        s << header("dictionary", "fvSchemes") << "ddtSchemes\n{\n    default " << (p.transient ? "Euler" : "steadyState") << ";\n}\n"
          << "gradSchemes\n{\n    default " << (!p.gradScheme.empty() ? p.gradScheme : (p.cadMesh ? "pointCellsLeastSquares" : "Gauss linear")) << ";\n}\n"
          << "divSchemes\n{\n    default none;\n    div(phi,U) " << (p.transient ? "Gauss linearUpwind grad(U)" : "bounded Gauss linearUpwind grad(U)") << ";\n"
          << "    div(phi,k) " << (p.transient ? "Gauss limitedLinear 1" : "bounded Gauss limitedLinear 1") << ";\n"
          << "    div(phi,epsilon) " << (p.transient ? "Gauss limitedLinear 1" : "bounded Gauss limitedLinear 1") << ";\n"
          << "    div(phi,omega) " << (p.transient ? "Gauss limitedLinear 1" : "bounded Gauss limitedLinear 1") << ";\n"
          << "    div((nuEff*dev2(T(grad(U))))) Gauss linear;\n    div((nu*dev2(T(grad(U))))) Gauss linear;\n}\n"
          << "laplacianSchemes\n{\n    default Gauss linear corrected;\n}\n"
          << "interpolationSchemes\n{\n    default linear;\n}\n"
          << "snGradSchemes\n{\n    default corrected;\n}\n"
          << "wallDist\n{\n    method meshWave;\n}\n";
        if (!put(root / "system/fvSchemes", s.str(), error)) return false;
    }
    // ---- fvSolution
    {
        std::ostringstream s;
        s << header("dictionary", "fvSolution") << "solvers\n{\n"
          << "    p\n    {\n        solver GAMG;\n        smoother GaussSeidel;\n        tolerance 1e-9;\n        relTol " << (p.transient ? "0.01" : "0.05") << ";\n    }\n"
          << "    pFinal\n    {\n        $p;\n        relTol 0;\n    }\n"
          << "    \"(U|k|epsilon|omega)\"\n    {\n        solver smoothSolver;\n        smoother symGaussSeidel;\n        tolerance 1e-9;\n        relTol 0.1;\n    }\n"
          << "    \"(U|k|epsilon|omega)Final\"\n    {\n        $U;\n        relTol 0;\n    }\n}\n";
        if (p.transient) {
            s << "PIMPLE\n{\n    nOuterCorrectors 1;\n    nCorrectors 2;\n    nNonOrthogonalCorrectors " << (p.cadMesh ? 2 : 0) << ";\n    pRefCell 0;\n    pRefValue 0;\n}\n";
        } else {
            s << "SIMPLE\n{\n    nNonOrthogonalCorrectors " << (p.cadMesh ? 2 : 0) << ";\n    consistent yes;\n    pRefCell 0;\n    pRefValue 0;\n"
              << "    residualControl\n    {\n        p 1e-6;\n        U 1e-7;\n        \"(k|epsilon|omega)\" 1e-6;\n    }\n}\n"
              << "relaxationFactors\n{\n    equations\n    {\n        U 0.9;\n        \".*\" 0.9;\n    }\n}\n";
        }
        if (!put(root / "system/fvSolution", s.str(), error)) return false;
    }
    // ---- constant: tính chất + mô hình rối (ghi cả kiểu cũ và kiểu mới của OpenFOAM)
    {
        std::ostringstream tp;
        tp << header("dictionary", "transportProperties") << "transportModel Newtonian;\nnu [0 2 -1 0 0 0 0] " << p.nu << ";\n";
        if (!put(root / "constant/transportProperties", tp.str(), error)) return false;
        std::ostringstream pp;
        pp << header("dictionary", "physicalProperties") << "viscosityModel constant;\nnu [0 2 -1 0 0 0 0] " << p.nu << ";\n";
        if (!put(root / "constant/physicalProperties", pp.str(), error)) return false;
        const char* rasName = kEps ? "kEpsilon" : (kOm ? "kOmegaSST" : "");
        std::ostringstream tb;
        tb << header("dictionary", "turbulenceProperties");
        if (turb) tb << "simulationType RAS;\nRAS\n{\n    RASModel " << rasName << ";\n    turbulence on;\n    printCoeffs on;\n}\n";
        else tb << "simulationType laminar;\n";
        if (!put(root / "constant/turbulenceProperties", tb.str(), error)) return false;
        std::ostringstream mt;
        mt << header("dictionary", "momentumTransport");
        if (turb) mt << "simulationType RAS;\nRAS\n{\n    model " << rasName << ";\n    turbulence on;\n    printCoeffs on;\n}\n";
        else mt << "simulationType laminar;\n";
        if (!put(root / "constant/momentumTransport", mt.str(), error)) return false;
    }
    // ---- trường ban đầu / điều kiện biên
    const double Uin = speedIn(p);
    const double I = p.turbIntensity;
    const double kIn = 1.5 * (I * Uin) * (I * Uin);
    const double lscale = p.lengthScale > 0 ? p.lengthScale : 0.07 * minSection(p);
    const double Cmu = 0.09;
    const double epsIn = std::pow(Cmu, 0.75) * std::pow(kIn, 1.5) / lscale;
    const double omIn = std::sqrt(kIn) / (std::pow(Cmu, 0.25) * lscale);
    const double nutIn = kEps ? Cmu * kIn * kIn / epsIn : (kOm ? kIn / omIn : 0.0);
    bool anyInlet = false;
    for (const auto& f : patchRoles) anyInlet = anyInlet || f.second == "inlet";
    const std::array<double, 3> U0 = anyInlet ? p.inletU : std::array<double, 3>{0, 0, 0};

    auto patchBlock = [&](const std::string& field, auto&& perType) {
        std::ostringstream b;
        b << "boundaryField\n{\n";
        for (const auto& pr : patchRoles) b << "    " << pr.first << "\n    {\n" << perType(pr.second) << "    }\n";
        b << "}\n";
        return b.str();
    };
    {
        std::ostringstream s;
        s << header("volVectorField", "U") << "dimensions [0 1 -1 0 0 0 0];\ninternalField uniform " << vec(U0) << ";\n"
          << patchBlock("U", [&](const std::string& t) -> std::string {
                if (t == "inlet") return "        type fixedValue;\n        value uniform " + vec(p.inletU) + ";\n";
                if (t == "outlet") return "        type zeroGradient;\n";
                if (t == "wall") return "        type noSlip;\n";
                if (t == "symmetry") return "        type symmetryPlane;\n";
                return "        type slip;\n";
            });
        if (!put(root / "0/U", s.str(), error)) return false;
    }
    {
        std::ostringstream s;
        s << header("volScalarField", "p") << "dimensions [0 2 -2 0 0 0 0];\ninternalField uniform 0;\n"
          << patchBlock("p", [&](const std::string& t) -> std::string {
                if (t == "outlet") return "        type fixedValue;\n        value uniform 0;\n";
                if (t == "symmetry") return "        type symmetryPlane;\n";
                return "        type zeroGradient;\n";
            });
        if (!put(root / "0/p", s.str(), error)) return false;
    }
    if (turb) {
        auto scalar = [&](const char* name, const char* dims, double internal, double inlet, const char* wallType, bool nutField) {
            std::ostringstream s;
            s << header("volScalarField", name) << "dimensions " << dims << ";\ninternalField uniform " << internal << ";\n"
              << patchBlock(name, [&](const std::string& t) -> std::string {
                    std::ostringstream o;
                    if (nutField) {
                        if (t == "wall") o << "        type nutkWallFunction;\n        value uniform 0;\n";
                        else if (t == "symmetry") o << "        type symmetryPlane;\n";
                        else o << "        type calculated;\n        value uniform 0;\n";
                    } else if (t == "inlet") o << "        type fixedValue;\n        value uniform " << inlet << ";\n";
                    else if (t == "wall") o << "        type " << wallType << ";\n        value uniform " << internal << ";\n";
                    else if (t == "symmetry") o << "        type symmetryPlane;\n";
                    else o << "        type zeroGradient;\n";
                    return o.str();
                });
            return put(root / "0" / name, s.str(), error);
        };
        if (!scalar("k", "[0 2 -2 0 0 0 0]", kIn, kIn, "kqRWallFunction", false)) return false;
        if (!scalar("nut", "[0 2 -1 0 0 0 0]", nutIn, nutIn, "", true)) return false;
        if (kEps && !scalar("epsilon", "[0 2 -3 0 0 0 0]", epsIn, epsIn, "epsilonWallFunction", false)) return false;
        if (kOm && !scalar("omega", "[0 0 -1 0 0 0 0]", omIn, omIn, "omegaWallFunction", false)) return false;
    }
    return true;
}

// ---------------------------------------------------------------- đọc kết quả
bool readOpenFoam3D(const std::string& caseDir, const OpenFoam3D& p, OpenFoam3DResult& out, std::string* error) {
    out = OpenFoam3DResult{};
    out.time = latestFoamTime(caseDir);
    if (out.time.empty() || out.time == "0") { if (error) *error = "Khong co thu muc thoi gian ket qua trong " + caseDir; return false; }
    const std::string dir = caseDir + "/" + out.time + "/";
    std::vector<std::array<double, 3>> U;
    std::vector<double> pc;
    if (!readFoamVectorField(dir + "U", U, error)) return false;
    if (!readFoamScalarField(dir + "p", pc, error)) return false;
    const std::size_t nc = p.cadMesh ? p.cadMesh->tets.size() : static_cast<std::size_t>(p.nx) * p.ny * p.nz;
    auto expand = [&](auto& v) { if (v.size() == 1 && nc > 1) v.assign(nc, v[0]); };
    expand(U);
    expand(pc);
    if (U.size() != nc || pc.size() != nc) { if (error) *error = "So o doc duoc khong khop luoi (" + std::to_string(U.size()) + " / " + std::to_string(nc) + ")"; return false; }
    std::vector<double> kc, nutc;
    if (p.model != "laminar") {
        std::string e2;
        if (readFoamScalarField(dir + "k", kc, &e2)) expand(kc);
        if (readFoamScalarField(dir + "nut", nutc, &e2)) expand(nutc);
        if (kc.size() != nc) kc.clear();
        if (nutc.size() != nc) nutc.clear();
    }
    if (p.cadMesh) {   // lưới tứ diện: nút = trung bình các ô chứa nút; nút trên tường u = 0; cửa vào lấy giá trị vào
        const Mesh& m = *p.cadMesh;
        PolyBuild pb;
        if (!buildPoly(m, p, pb, error)) return false;
        const std::size_t nn = m.nodes.size();
        out.ux.assign(nn, 0); out.uy.assign(nn, 0); out.uz.assign(nn, 0); out.p.assign(nn, 0); out.speed.assign(nn, 0);
        if (!kc.empty()) out.k.assign(nn, 0);
        if (!nutc.empty()) out.nut.assign(nn, 0);
        std::vector<int> cnt(nn, 0);
        for (std::size_t c = 0; c < nc; ++c)
            for (int a = 0; a < 4; ++a) {
                const std::size_t id = static_cast<std::size_t>(m.tets[c][static_cast<std::size_t>(a)]);
                out.ux[id] += U[c][0]; out.uy[id] += U[c][1]; out.uz[id] += U[c][2]; out.p[id] += pc[c];
                if (!kc.empty()) out.k[id] += kc[c];
                if (!nutc.empty()) out.nut[id] += nutc[c];
                ++cnt[id];
            }
        for (std::size_t i = 0; i < nn; ++i) {
            const double w = cnt[i] > 0 ? 1.0 / cnt[i] : 0.0;
            out.ux[i] *= w; out.uy[i] *= w; out.uz[i] *= w; out.p[i] *= w;
            if (!kc.empty()) out.k[i] *= w;
            if (!nutc.empty()) out.nut[i] *= w;
            if (pb.nodeWall[i]) { out.ux[i] = out.uy[i] = out.uz[i] = 0; }
            else if (pb.nodeInlet[i]) { out.ux[i] = p.inletU[0]; out.uy[i] = p.inletU[1]; out.uz[i] = p.inletU[2]; }
            out.speed[i] = std::sqrt(out.ux[i] * out.ux[i] + out.uy[i] * out.uy[i] + out.uz[i] * out.uz[i]);
            out.maxSpeed = std::max(out.maxSpeed, out.speed[i]);
        }
        out.pMin = *std::min_element(out.p.begin(), out.p.end());
        out.pMax = *std::max_element(out.p.begin(), out.p.end());
        // cân bằng khối lượng xấp xỉ: lưu lượng vào chính xác (U·n·A) so với lưu lượng ra theo giá trị ô sát biên
        double qin = 0, qout = 0;
        for (std::size_t i = 0; i < pb.outletCell.size(); ++i) {
            const auto& av = pb.outletAreaVec[i];
            const auto& u = U[static_cast<std::size_t>(pb.outletCell[i])];
            qout += u[0] * av[0] + u[1] * av[1] + u[2] * av[2];
        }
        qin = std::sqrt(p.inletU[0] * p.inletU[0] + p.inletU[1] * p.inletU[1] + p.inletU[2] * p.inletU[2]) * pb.inletArea;
        if (qin > 0) out.massImbalance = std::abs(qout - qin) / qin;
        return true;
    }
    // ô -> nút: trung bình các ô kề (chỉ số ô: x nhanh nhất, như blockMesh); nút trên tường lấy u = 0; cửa vào lấy giá trị vào
    const int NX = p.nx + 1, NY = p.ny + 1, NZ = p.nz + 1;
    const std::size_t nn = static_cast<std::size_t>(NX) * NY * NZ;
    out.ux.assign(nn, 0); out.uy.assign(nn, 0); out.uz.assign(nn, 0); out.p.assign(nn, 0); out.speed.assign(nn, 0);
    if (!kc.empty()) out.k.assign(nn, 0);
    if (!nutc.empty()) out.nut.assign(nn, 0);
    auto cell = [&](int i, int j, int k) { return (static_cast<std::size_t>(k) * p.ny + j) * p.nx + i; };
    for (int k = 0; k < NZ; ++k)
        for (int j = 0; j < NY; ++j)
            for (int i = 0; i < NX; ++i) {
                double sux = 0, suy = 0, suz = 0, sp = 0, sk = 0, snut = 0;
                int cnt = 0;
                for (int dk = -1; dk <= 0; ++dk)
                    for (int dj = -1; dj <= 0; ++dj)
                        for (int di = -1; di <= 0; ++di) {
                            const int ci = i + di, cj = j + dj, ck = k + dk;
                            if (ci < 0 || cj < 0 || ck < 0 || ci >= p.nx || cj >= p.ny || ck >= p.nz) continue;
                            const std::size_t c = cell(ci, cj, ck);
                            sux += U[c][0]; suy += U[c][1]; suz += U[c][2]; sp += pc[c];
                            if (!kc.empty()) sk += kc[c];
                            if (!nutc.empty()) snut += nutc[c];
                            ++cnt;
                        }
                const std::size_t id = (static_cast<std::size_t>(k) * NY + j) * NX + i;
                double ux = sux / cnt, uy = suy / cnt, uz = suz / cnt;
                // điều kiện biên theo mặt mà nút nằm trên
                bool wall = false;
                bool inlet = false;
                const bool onFace[6] = {i == 0, i == p.nx, j == 0, j == p.ny, k == 0, k == p.nz};
                for (int f = 0; f < 6; ++f) {
                    if (!onFace[f]) continue;
                    if (p.face[f] == "wall") wall = true;
                    if (p.face[f] == "inlet") inlet = true;
                }
                if (wall) { ux = uy = uz = 0; }
                else if (inlet) { ux = p.inletU[0]; uy = p.inletU[1]; uz = p.inletU[2]; }
                out.ux[id] = ux; out.uy[id] = uy; out.uz[id] = uz;
                out.p[id] = sp / cnt;
                if (!kc.empty()) out.k[id] = sk / cnt;
                if (!nutc.empty()) out.nut[id] = snut / cnt;
                out.speed[id] = std::sqrt(ux * ux + uy * uy + uz * uz);
                out.maxSpeed = std::max(out.maxSpeed, out.speed[id]);
            }
    out.pMin = *std::min_element(out.p.begin(), out.p.end());
    out.pMax = *std::max_element(out.p.begin(), out.p.end());
    // cân bằng khối lượng (theo ô sát mặt vào/ra thuộc trục x): chỉ khi vào/ra là xmin/xmax
    if (p.face[0] == "inlet" && p.face[1] == "outlet") {
        const double dA = (p.ly / p.ny) * (p.lz / p.nz);
        double qin = p.inletU[0] * p.ly * p.lz, qout = 0;   // vào: giá trị cho trước (chính xác); ra: cầu phương điểm giữa theo ô cuối
        for (int k = 0; k < p.nz; ++k)
            for (int j = 0; j < p.ny; ++j) qout += U[cell(p.nx - 1, j, k)][0] * dA;
        if (std::abs(qin) > 0) out.massImbalance = std::abs(qout - qin) / std::abs(qin);
    }
    return true;
}

}  // namespace fastcae
