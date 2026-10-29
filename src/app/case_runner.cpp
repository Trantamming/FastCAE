// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include <cstdlib>
#include "app/case_runner.hpp"
#include <map>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <cmath>
#include <new>
#include <ostream>
#include <stdexcept>

#include "core/paths.hpp"
#include "core/result_store.hpp"
#include "core/vtk_reader.hpp"
#include "solver/external_run.hpp"
#include <filesystem>
#include "cad/cad_import.hpp"
#include "cad/param_cad.hpp"
#include "mesh/box_mesher.hpp"
#include "solver/calculix_bridge.hpp"
#include "solver/ccx_analysis.hpp"
#include "solver/openfoam_3d.hpp"
#include "solver/frame.hpp"
#include "solver/shell_ccx.hpp"
#include "solver/openfoam_case.hpp"
#include "solver/openfoam_results.hpp"
#include "solver/solver_registry.hpp"
#include "solver/cavity_cfd.hpp"
#include "solver/quadratic.hpp"
#include "solver/sparse.hpp"
#include "solver/elastic.hpp"
#include "solver/thermal.hpp"

namespace fastcae {

namespace {

// Đổi số thực sang int an toàn: NaN/vô cực/ngoài phạm vi KHÔNG gây hành vi không xác định, mà bị kẹp vào [lo, hi].
int safeInt(double v, int lo, int hi) {
    if (!(v == v)) return lo;
    if (v <= lo) return lo;
    if (v >= hi) return hi;
    return static_cast<int>(v);
}

// Tham số CFD khoang kín dùng chung cho solver tích hợp và OpenFOAM: số hữu hạn, trong phạm vi hợp lý.
bool checkCavityParams(double n, double re, double lid, std::string& err) {
    if (!std::isfinite(n) || n < 5 || n > 1000) { err = "cavity: n (so diem luoi moi canh) phai tu 5 den 1000"; return false; }
    if (!std::isfinite(re) || re <= 0 || re > 1e6) { err = "cavity: Re phai la so duong huu han (toi da 1e6)"; return false; }
    if (!std::isfinite(lid) || lid == 0) { err = "cavity: lid_speed phai la so huu han khac 0"; return false; }
    return true;
}

std::vector<double> toDoubles(const std::vector<std::string>& t, std::size_t from) {
    std::vector<double> v;
    for (std::size_t i = from; i < t.size(); ++i) {
        try { v.push_back(std::stod(t[i])); } catch (...) { break; }
    }
    return v;
}

bool buildGeometry(const CaseFile& cf, std::ostream& log, const std::string& baseDir, Mesh& mesh,
                   std::string& err) {
    const auto g = CaseFile::tokens(cf.get("geometry"));
    if (g.empty()) { err = "Thieu khai bao 'geometry'"; return false; }
    const double h = cf.getDouble("mesh_size", 0);
    CadImportResult r;
    if (g[0] == "file") {
        if (g.size() < 2) { err = "geometry = file <duong_dan>"; return false; }
        std::string path = g[1];
        if (!baseDir.empty() && path.find(':') == std::string::npos && path[0] != '/') path = baseDir + "/" + path;
        CadImportOptions opt;
        opt.meshSize = h;
        opt.scaleToMeters = cf.getDouble("unit_scale", 1.0);
        log << "Nhap CAD: " << path << "\n";
        r = importCad(path, opt);
    } else if (g[0] == "param") {
        // geometry = param <file.fcad> ; param_set = L=0.3 (lặp được) ; cây tính năng tham số -> gmsh OCC -> lưới
        if (g.size() < 2) { err = "geometry = param <file.fcad>   (kèm param_set = ten=gia_tri)"; return false; }
        std::string path = g[1];
        if (!baseDir.empty() && path.find(':') == std::string::npos && path[0] != '/') path = baseDir + "/" + path;
        std::ifstream pf(path);
        if (!pf) { err = "Khong mo duoc file mo hinh tham so: " + path; return false; }
        std::stringstream buf; buf << pf.rdbuf();
        std::map<std::string, double> ov;
        for (const auto& kv : cf.getAll("param_set")) {
            const auto eq = kv.find('=');
            if (eq == std::string::npos) { err = "param_set = ten=gia_tri"; return false; }
            std::string n = kv.substr(0, eq), ex = kv.substr(eq + 1);
            while (!n.empty() && n.back() == ' ') n.pop_back();
            double v; std::string e2;
            if (!evalExpression(ex, ov, v, e2)) { err = "param_set " + n + ": " + e2; return false; }
            ov[n] = v;
        }
        log << "Dung mo hinh tham so: " << path << "\n";
        const ParamMeshResult pm = meshParamModel(buf.str(), ov, h, 1);
        for (const auto& w : pm.warnings) log << "CANH BAO: " << w << "\n";
        r.ok = pm.ok; r.message = pm.message; r.mesh = pm.mesh;
    } else {
        int dx = 0, dy = 0, dz = 0;
        const auto d = toDoubles(CaseFile::tokens(cf.get("divisions")), 0);
        if (d.size() >= 3) { dx = safeInt(d[0], 0, 1 << 20); dy = safeInt(d[1], 0, 1 << 20); dz = safeInt(d[2], 0, 1 << 20); }
        r = makePrimitive(g[0], toDoubles(g, 1), h, dx, dy, dz);
    }
    if (!r.ok) { err = r.message; return false; }
    mesh = std::move(r.mesh);
    if (cf.has("body")) mesh.bodyNodeStart = {0, static_cast<int>(mesh.nodes.size())};
    // Vật thể bổ sung: body = <hinh> <tham so...> at ox oy oz [div nx ny nz]  (các vật thể không chung nút; dùng tập nút theo mặt phẳng để chọn)
    for (const auto& line : cf.getAll("body")) {
        const auto t = CaseFile::tokens(line);
        std::size_t at = 0;
        while (at < t.size() && t[at] != "at") ++at;
        if (t.empty() || at == 0 || at + 3 >= t.size() + 0 || t.size() < at + 4) { err = "body = <hinh> <tham so...> at ox oy oz [div nx ny nz]"; return false; }
        const auto par = toDoubles(std::vector<std::string>(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(at)), 1);
        const auto off = toDoubles(std::vector<std::string>(t.begin() + static_cast<std::ptrdiff_t>(at) + 1, t.begin() + static_cast<std::ptrdiff_t>(at) + 4), 0);
        if (off.size() < 3) { err = "body: thieu toa do goc sau 'at'"; return false; }
        int bx = 0, by = 0, bz = 0;
        if (t.size() > at + 4 && t[at + 4] == "div") {
            const auto dv = toDoubles(std::vector<std::string>(t.begin() + static_cast<std::ptrdiff_t>(at) + 5, t.end()), 0);
            if (dv.size() < 3) { err = "body: div nx ny nz"; return false; }
            bx = safeInt(dv[0], 0, 1 << 20); by = safeInt(dv[1], 0, 1 << 20); bz = safeInt(dv[2], 0, 1 << 20);
        }
        CadImportResult br = makePrimitive(t[0], par, h, bx, by, bz);
        if (!br.ok) { err = "body: " + br.message; return false; }
        const int base = static_cast<int>(mesh.nodes.size());
        for (auto n : br.mesh.nodes) mesh.nodes.push_back({n.x + off[0], n.y + off[1], n.z + off[2]});
        for (auto tt : br.mesh.tets) mesh.tets.push_back({tt[0] + base, tt[1] + base, tt[2] + base, tt[3] + base});
        mesh.bodyNodeStart.push_back(static_cast<int>(mesh.nodes.size()));
    }
    log << "Luoi: " << mesh.numNodes() << " nut, " << mesh.numCells() << " phan tu tu dien\n";
    return true;
}

// ---- CalculiX mở rộng (solver = calculix + analysis = ...) ------------------------------------------------------
static CcxSpec readCcxSpec(const CaseFile& cf, const Material& mat, const std::string& analysis) {
    CcxSpec s;
    s.analysis = analysis;
    s.mat = mat;
    s.alpha = cf.getDouble("alpha", 0);
    s.cp = cf.getDouble("cp", 500);
    s.yield = cf.getDouble("yield", 0);
    s.hardening = cf.getDouble("hardening", 0);
    {
        const std::string nl = cf.get("nlgeom", "off");
        s.nlgeom = (nl == "on" || nl == "true" || nl == "1" || nl == "yes");
    }
    s.totalTime = cf.getDouble("time", analysis == "dynamic" || analysis == "heat_transient" ? 1.0 : 1.0);
    s.increments = safeInt(cf.getDouble("increments", 10), 1, 100000);
    s.modes = safeInt(cf.getDouble("modes", 6), 1, 100);
    s.initialTemp = cf.getDouble("t0", 0);
    const auto g = toDoubles(CaseFile::tokens(cf.get("gravity")), 0);
    if (g.size() >= 3) s.gravity = {g[0], g[1], g[2]};
    return s;
}

static void reportCcx(const CcxAnalysisResult& R, const CcxSpec& spec, std::ostream& log, RunReport& rep) {
    rep.ok = R.ok;
    rep.message = R.message;
    if (!R.ok) return;
    log << "Deck: " << R.inpPath << "\nKet qua: " << R.frdPath << "\n";
    const auto& e = R.elastic;
    rep.iterations = static_cast<int>(R.times.size());
    rep.maxValue = e.maxDisplacement;
    if (spec.analysis == "dynamic" && !R.historyMaxDisp.empty())   // động lực: giá trị đặc trưng = đỉnh theo thời gian, không phải gia số cuối
        rep.maxValue = *std::max_element(R.historyMaxDisp.begin(), R.historyMaxDisp.end());
    rep.secondary = e.maxVonMises;
    if (!e.u.empty()) {
        rep.fields.pointScalars["displacement_magnitude"] = e.dispMagnitude;
        rep.fields.pointVectors["displacement"] = e.u;
    }
    if (!e.vonMisesNodal.empty()) rep.fields.pointScalars["von_mises_nodal"] = e.vonMisesNodal;
    if (!R.temperature.empty() && (spec.analysis == "heat" || spec.analysis == "heat_transient" || spec.analysis == "thermal_stress"))
        rep.fields.pointScalars["temperature"] = R.temperature;
    if (!R.peeq.empty()) rep.fields.pointScalars["plastic_strain"] = R.peeq;
    for (const auto& f : R.frames) {   // chuỗi: mỗi dạng riêng / mốc thời gian là một khung cùng lưới
        RunFrame rf;
        rf.label = f.label;
        if (!f.u.empty()) { rf.fields.pointVectors["displacement"] = f.u; rf.fields.pointScalars["displacement_magnitude"] = f.dispMagnitude; }
        if (!f.vonMises.empty()) rf.fields.pointScalars["von_mises_nodal"] = f.vonMises;
        if (!f.temperature.empty()) rf.fields.pointScalars["temperature"] = f.temperature;
        rep.frames.push_back(std::move(rf));
    }
    rep.currentFrame = rep.frames.empty() ? 0 : static_cast<int>(rep.frames.size()) - 1;
    if (!R.contactPressure.empty()) {
        rep.fields.pointScalars["contact_pressure"] = R.contactPressure;
        rep.fields.pointScalars["contact_shear"] = R.contactShear;
        rep.values["max_contact_pressure"] = *std::max_element(R.contactPressure.begin(), R.contactPressure.end());
    }
    for (std::size_t k = 0; k < R.freqHz.size(); ++k) rep.values["frequency_" + std::to_string(k + 1) + "_hz"] = R.freqHz[k];
    for (std::size_t k = 0; k < R.factors.size(); ++k) rep.values["buckling_factor_" + std::to_string(k + 1)] = R.factors[k];
    if (!R.freqHz.empty()) rep.series["frequencies_hz"] = R.freqHz;
    if (!R.factors.empty()) rep.series["buckling_factors"] = R.factors;
    if (!R.historyMaxDisp.empty()) { rep.series["max_displacement_history"] = R.historyMaxDisp; rep.values["max_displacement"] = *std::max_element(R.historyMaxDisp.begin(), R.historyMaxDisp.end()); }
    if (!R.historyMaxVonMises.empty()) {   // động lực: đại lượng đặc trưng = đỉnh theo thời gian
        rep.series["max_von_mises_history"] = R.historyMaxVonMises;
        const double peak = *std::max_element(R.historyMaxVonMises.begin(), R.historyMaxVonMises.end());
        rep.values["max_von_mises"] = peak;
        if (spec.analysis == "dynamic") rep.secondary = peak;
    }
    if (!R.historyMaxTemp.empty()) { rep.series["max_temperature_history"] = R.historyMaxTemp; rep.values["t_final_max"] = R.historyMaxTemp.back(); }
    if (!R.times.empty()) rep.series["times"] = R.times;
    if (!e.vonMisesNodal.empty() && R.historyMaxVonMises.empty()) rep.values["max_von_mises"] = e.maxVonMises;
    if (!R.peeq.empty()) rep.values["max_plastic_strain"] = *std::max_element(R.peeq.begin(), R.peeq.end());
    std::ostringstream sm;
    sm << "CalculiX (" << spec.analysis << "): " << R.times.size() << " gia so";
    if (!R.freqHz.empty()) { sm << "; f = "; for (std::size_t k = 0; k < R.freqHz.size() && k < 6; ++k) sm << (k ? ", " : "") << R.freqHz[k]; sm << " Hz"; }
    if (!R.factors.empty()) { sm << "; he so mat on dinh = "; for (std::size_t k = 0; k < R.factors.size() && k < 6; ++k) sm << (k ? ", " : "") << R.factors[k]; }
    rep.summary = sm.str();
}

Material readMaterial(const CaseFile& cf) {
    Material m;
    m.E = cf.getDouble("e", m.E);
    m.nu = cf.getDouble("nu", m.nu);
    m.k = cf.getDouble("k", m.k);
    m.rho = cf.getDouble("rho", m.rho);
    return m;
}

}  // namespace

static std::string resolveOutput(const std::string& out, const std::string& baseDir) {
    if (out.empty() || baseDir.empty()) return out;
    const bool absolute = out[0] == '/' || out[0] == '\\' || out.find(':') != std::string::npos;
    return absolute ? out : baseDir + "/" + out;
}

// Tập nút: tên chuẩn (xmin, xmax, ..., all) hoặc tập mặt phẳng do người dùng chọn bằng chuột:
//   nodeset = <tên> plane nx ny nz d tol        (mặt phẳng n·x = d, dung sai tuyệt đối tol)
// Với phần tử bậc 2, các nút giữa cạnh nằm trên mặt đã chọn cũng được thêm vào.
static std::vector<int> selectSet(const Mesh& m, const QuadMesh* quad, const CaseFile& cf, const std::string& name) {
    for (const auto& line : cf.getAll("nodeset")) {
        const auto t = CaseFile::tokens(line);
        if (t.size() < 7 || t[0] != name || t[1] != "plane") continue;
        int bodyIdx = 0;   // nodeset = <ten> plane nx ny nz d tol [body k]  (k = 1, 2, ... theo thu tu geometry, body, body)
        if (t.size() >= 9 && t[7] == "body") { try { bodyIdx = std::stoi(t[8]); } catch (...) { return {}; } }
        PlaneSel p;
        try {
            p.n = {std::stod(t[2]), std::stod(t[3]), std::stod(t[4])};
            p.d = std::stod(t[5]);
            p.tol = std::stod(t[6]);
        } catch (...) {
            return {};
        }
        std::vector<int> nodes = selectPlaneNodes(m, p);
        if (bodyIdx > 0) {
            if (m.bodyNodeStart.size() < static_cast<std::size_t>(bodyIdx) + 1) return {};
            const int lo = m.bodyNodeStart[static_cast<std::size_t>(bodyIdx) - 1], hi = m.bodyNodeStart[static_cast<std::size_t>(bodyIdx)];
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](int n) { return n < lo || n >= hi; }), nodes.end());
        }
        if (quad) {
            std::vector<char> in(m.nodes.size(), 0);
            for (int n : nodes) in[n] = 1;
            static const int edge[6][2] = {{0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}};
            for (const auto& t10 : quad->tets10)
                for (int e = 0; e < 6; ++e)
                    if (in[t10[edge[e][0]]] && in[t10[edge[e][1]]] && !in[t10[4 + e]]) {
                        in[t10[4 + e]] = 1;
                        nodes.push_back(t10[4 + e]);
                    }
        }
        return nodes;
    }
    return selectNodes(m, name);
}

// ---------------------------------------------------------------- OpenFOAM (tiến trình ngoài)
static std::string shellQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) o += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    return o + "'";
}

// ---- CFD 3D bằng OpenFOAM (type = cfd3d, solver = openfoam) -----------------------------------------------------
static RunReport runOpenFoam3D(const CaseFile& cf, std::ostream& log, const std::string& baseDir) {
    RunReport rep;
    rep.type = "cfd3d";
    if (cf.get("solver", "builtin") != "openfoam") { rep.message = "type = cfd3d chi chay qua OpenFOAM: dat solver = openfoam"; return rep; }
    if (cf.has("body")) { rep.message = "cfd3d: chua ho tro nhieu vat the (body)"; return rep; }
    OpenFoam3D p;
    const auto g = CaseFile::tokens(cf.get("geometry"));
    const auto gd = toDoubles(g, 1);
    Mesh cad;   // lưới tứ diện từ CAD (geometry = file ...): ghi polyMesh trực tiếp, không dùng blockMesh
    const bool fromCad = !g.empty() && g[0] == "file";
    if (fromCad) {
        std::string gerr;
        if (!buildGeometry(cf, log, baseDir, cad, gerr)) { rep.message = gerr; return rep; }
        Vec3 lo, hi;
        cad.bounds(lo, hi);
        p.lx = hi.x - lo.x; p.ly = hi.y - lo.y; p.lz = hi.z - lo.z;
        p.cadMesh = &cad;
        p.faceOther = cf.get("bc_other", "wall");
    } else {
        if (g.empty() || g[0] != "box" || gd.size() < 3) { rep.message = "cfd3d: geometry = box lx ly lz (m) hoac geometry = file <cad>"; return rep; }
        p.lx = gd[0]; p.ly = gd[1]; p.lz = gd[2];
        const auto d = toDoubles(CaseFile::tokens(cf.get("divisions")), 0);
        if (d.size() < 3) { rep.message = "cfd3d: divisions = nx ny nz"; return rep; }
        p.nx = safeInt(d[0], 0, 1 << 20); p.ny = safeInt(d[1], 0, 1 << 20); p.nz = safeInt(d[2], 0, 1 << 20);
    }
    p.nu = cf.getDouble("nu_kin", 1e-5);
    const auto iv = toDoubles(CaseFile::tokens(cf.get("inlet_velocity", "1 0 0")), 0);
    if (iv.size() < 3) { rep.message = "cfd3d: inlet_velocity = ux uy uz"; return rep; }
    p.inletU = {iv[0], iv[1], iv[2]};
    static const char* names[6] = {"bc_xmin", "bc_xmax", "bc_ymin", "bc_ymax", "bc_zmin", "bc_zmax"};
    for (int i = 0; i < 6; ++i) p.face[i] = cf.get(names[i], p.face[i]);
    p.model = cf.get("model", "laminar");
    p.gradScheme = cf.get("grad_scheme", "");   // rỗng = tự chọn theo loại lưới (xem openfoam_3d.hpp)
    {
        const std::string tr = cf.get("transient", "off");
        p.transient = (tr == "on" || tr == "true" || tr == "1" || tr == "yes");
    }
    p.endTime = cf.getDouble("end_time", 1.0);
    p.deltaT = cf.getDouble("dt", 1e-3);
    p.maxIter = safeInt(cf.getDouble("max_iter", 1000), 1, 10000000);
    p.turbIntensity = cf.getDouble("turbulence_intensity", 0.05);
    p.lengthScale = cf.getDouble("turbulence_length", 0.0);
    std::string err;
    if (!validateOpenFoam3D(p, &err)) { rep.message = err; return rep; }

    const std::string work = fastcae::resolveWorkDir(cf.get("work_dir", ""), baseDir, "foam_run");
    const std::string caseDir = work + "/" + cf.get("job_name", "cfd3d");
    if (!writeOpenFoam3D(caseDir, p, &err)) { rep.message = err; return rep; }
    log << "OpenFOAM 3D: da ghi case " << caseDir << " (" << (fromCad ? std::to_string(cad.tets.size()) + " o tu dien tu CAD" : std::to_string(p.nx) + "x" + std::to_string(p.ny) + "x" + std::to_string(p.nz) + " o") << ", " << openFoam3DSolver(p) << ", " << p.model << ", nu=" << p.nu << ")\n";
#ifdef _WIN32
    if (findExecutable({"blockMesh"}).empty() || findExecutable({openFoam3DSolver(p)}).empty()) {
        rep.message = "Khong tim thay blockMesh/" + openFoam3DSolver(p) + " trong PATH. Case da ghi tai " + caseDir;
        return rep;
    }
    const std::string cmd = openFoam3DShellScript("", caseDir, p);
#else
    const std::string bashrc = findOpenFoamBashrc(cf.get("openfoam_bashrc", ""));
    if (bashrc.empty() && (findExecutable({"blockMesh"}).empty() || findExecutable({openFoam3DSolver(p)}).empty())) {
        rep.message = "Khong tim thay OpenFOAM (etc/bashrc hoac blockMesh/" + openFoam3DSolver(p) + " trong PATH). Case da ghi tai " + caseDir +
                      ". Dat openfoam_bashrc = <duong dan etc/bashrc> hoac bien FASTCAE_OPENFOAM_BASHRC.";
        return rep;
    }
    const std::string cmd = "bash -c " + shellQuote(openFoam3DShellScript(bashrc, caseDir, p));
#endif
    log << "OpenFOAM 3D: dang chay blockMesh + " << openFoam3DSolver(p) << "...\n";
    const int rc = std::system(cmd.c_str());
    if (rc != 0) { rep.message = "OpenFOAM that bai (ma " + std::to_string(rc) + "), xem " + caseDir + "/log.blockMesh va log." + openFoam3DSolver(p); return rep; }

    OpenFoam3DResult r;
    if (!readOpenFoam3D(caseDir, p, r, &err)) { rep.message = err; return rep; }
    log << "OpenFOAM 3D: doc ket qua t = " << r.time << (p.transient ? " s" : " (vong lap)") << "\n";
    rep.mesh = fromCad ? cad : makeBoxMesh(p.lx, p.ly, p.lz, p.nx, p.ny, p.nz);
    std::vector<double> vel(3 * r.ux.size());
    for (std::size_t i = 0; i < r.ux.size(); ++i) { vel[3 * i] = r.ux[i]; vel[3 * i + 1] = r.uy[i]; vel[3 * i + 2] = r.uz[i]; }
    rep.fields.pointScalars["speed"] = r.speed;
    rep.fields.pointScalars["pressure"] = r.p;
    if (!r.k.empty()) rep.fields.pointScalars["k"] = r.k;
    if (!r.nut.empty()) rep.fields.pointScalars["nut"] = r.nut;
    rep.fields.pointVectors["velocity"] = vel;
    rep.maxValue = r.maxSpeed;
    rep.secondary = r.pMax - r.pMin;
    rep.values["max_speed"] = r.maxSpeed;
    rep.values["p_min"] = r.pMin;
    rep.values["p_max"] = r.pMax;
    rep.values["mass_imbalance"] = r.massImbalance;
    rep.iterations = static_cast<int>(std::atof(r.time.c_str()));
    rep.ok = true;
    rep.message = "Hoi tu / ket thuc (OpenFOAM " + openFoam3DSolver(p) + ", t = " + r.time + ")";
    rep.summary = "OpenFOAM 3D (" + openFoam3DSolver(p) + ", " + p.model + "): max |U| = " + std::to_string(r.maxSpeed) + " m/s, dp = " + std::to_string(r.pMax - r.pMin) + " (m2/s2)";
    return rep;
}

// Khoang kín nắp trượt bằng OpenFOAM (icoFoam): ghi case -> chạy blockMesh + icoFoam -> đọc U, p về cùng khung hiển thị.
static RunReport runOpenFoamCavity(const CaseFile& cf, std::ostream& log, const std::string& baseDir) {
    RunReport rep;
    rep.type = "cavity";
    OpenFoamCavity p;
    {
        std::string perr;
        if (!checkCavityParams(cf.getDouble("n", 20), cf.getDouble("re", 100), cf.getDouble("lid_speed", 1.0), perr)) { rep.message = perr; return rep; }
    }
    p.cells = safeInt(cf.getDouble("n", 20), 5, 1000);
    p.reynolds = cf.getDouble("re", 100);
    p.lidSpeed = cf.getDouble("lid_speed", 1.0);
    p.endTime = cf.getDouble("end_time", 5.0);
    const std::string work = fastcae::resolveWorkDir(cf.get("work_dir", ""), baseDir, "foam_run");
    const std::string caseDir = work + "/" + cf.get("job_name", "cavity");
    std::string err;
    if (!writeOpenFoamCavity(caseDir, p, &err)) { rep.message = err; return rep; }
    log << "OpenFOAM: da ghi case " << caseDir << " (" << p.cells << "x" << p.cells << " o, Re=" << p.reynolds << ")\n";

#ifdef _WIN32
    const std::string bashrc;
    if (findExecutable({"blockMesh"}).empty() || findExecutable({"icoFoam"}).empty()) {
        rep.message = "Khong tim thay blockMesh/icoFoam trong PATH. Case da ghi tai " + caseDir + "; chay tay: blockMesh && icoFoam";
        return rep;
    }
    const std::string cmd = "blockMesh -case \"" + caseDir + "\" > \"" + caseDir + "/log.blockMesh\" 2>&1 && icoFoam -case \"" + caseDir +
                            "\" > \"" + caseDir + "/log.icoFoam\" 2>&1";
#else
    const std::string bashrc = findOpenFoamBashrc(cf.get("openfoam_bashrc", ""));
    if (bashrc.empty() && (findExecutable({"blockMesh"}).empty() || findExecutable({"icoFoam"}).empty())) {
        rep.message = "Khong tim thay OpenFOAM (etc/bashrc hoac blockMesh/icoFoam trong PATH). Case da ghi tai " + caseDir +
                      ". Dat openfoam_bashrc = <duong dan etc/bashrc> hoac bien FASTCAE_OPENFOAM_BASHRC.";
        return rep;
    }
    const std::string cmd = "bash -c " + shellQuote(openFoamShellScript(bashrc, caseDir, /*vtk=*/true));
#endif
    log << "OpenFOAM: dang chay blockMesh + icoFoam" << (bashrc.empty() ? "" : " (moi truong: " + bashrc + ")") << "...\n";
    const int rc = std::system(cmd.c_str());
    if (rc != 0) { rep.message = "OpenFOAM that bai (ma " + std::to_string(rc) + "), xem " + caseDir + "/log.blockMesh va log.icoFoam"; return rep; }

    OpenFoamCavityResult r;
    if (!readOpenFoamCavity(caseDir, 0.1, p.lidSpeed, r, &err)) { rep.message = err; return rep; }
    log << "OpenFOAM: doc ket qua thoi gian t = " << r.time << " s\n";

    std::vector<double> nu, nv, np;
    cavityNodalFields(r, nu, nv, np);
    const int P = r.n + 1;
    const std::size_t N = static_cast<std::size_t>(P) * P;
    const double dx = r.length / r.n;
    rep.mesh = makeBoxMesh(1.0, 1.0, 1.0 / r.n, r.n, r.n, 1);
    std::vector<double> speed(2 * N), pres(2 * N), om(2 * N), vel(6 * N, 0.0);
    double wmax = 0;
    for (std::size_t k = 0; k < 2 * N; ++k) {
        const std::size_t q = k % N;
        const int i = static_cast<int>(q % P), j = static_cast<int>(q / P);
        const int il = std::max(i - 1, 0), ir = std::min(i + 1, P - 1), jl = std::max(j - 1, 0), jr = std::min(j + 1, P - 1);
        const double dvdx = (nv[j * P + ir] - nv[j * P + il]) / ((ir - il) * dx);
        const double dudy = (nu[jr * P + i] - nu[jl * P + i]) / ((jr - jl) * dx);
        const double w = dvdx - dudy;
        wmax = std::max(wmax, std::abs(w));
        speed[k] = std::hypot(nu[q], nv[q]);
        pres[k] = np[q];
        om[k] = w;
        vel[3 * k] = nu[q];
        vel[3 * k + 1] = nv[q];
    }
    rep.fields.pointScalars["speed"] = speed;
    rep.fields.pointScalars["pressure"] = pres;
    rep.fields.pointScalars["vorticity"] = om;
    rep.fields.pointVectors["velocity"] = vel;
    rep.maxValue = wmax;
    // u tại tâm khoang (x = y = 0.5): ô giữa nếu n lẻ, trung bình 4 ô quanh tâm nếu n chẵn
    const int m = r.n / 2;
    rep.secondary = (r.n % 2) ? r.uCell(m, m) : 0.25 * (r.uCell(m - 1, m - 1) + r.uCell(m, m - 1) + r.uCell(m - 1, m) + r.uCell(m, m));
    rep.iterations = 0;
    rep.ok = true;
    rep.message = "Hoi tu (OpenFOAM icoFoam, t = " + r.time + " s)";
    log << "OpenFOAM: u tai tam khoang = " << rep.secondary << " m/s";
    if (std::abs(p.reynolds - 100.0) < 1e-9 && std::abs(p.lidSpeed - 1.0) < 1e-9) log << "  (Ghia 1982, Re=100: -0.20581)";
    log << "\n";
    return rep;
}

// ---------------------------------------------------------------- khung / dàn 1D
static RunReport runFrameCase(const CaseFile& cf, std::ostream& log, const std::string& baseDir, bool writeOutput) {
    RunReport rep;
    rep.type = "frame";
    FrameModel fm;
    std::string err;
    if (!readFrameModel(cf, fm, err)) { rep.message = err; return rep; }
    log << "Khung/dan 3D: " << fm.nodes.size() << " nut, " << fm.members.size() << " thanh (E=" << fm.E << ", nu=" << fm.nu << ")\n";
    std::string analysis = cf.get("analysis", "static");
    for (char& ch : analysis) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (analysis == "modal" || analysis == "buckling") {
        const bool buck = analysis == "buckling";
        const int nModes = std::clamp(safeInt(cf.getDouble("modes", buck ? 3 : 6), 1, 40), 1, 40);
        const FrameEigenResult er = solveFrameEigen(fm, nModes, buck);
        if (!er.ok) { rep.message = er.message; return rep; }
        const int show = std::clamp(static_cast<int>(cf.getDouble("mode", 1)), 1, static_cast<int>(er.shapes.size()));
        std::ostringstream sum;
        const double twoPi = 6.283185307179586;
        if (buck) {
            log << "Mat on dinh tuyen tinh (he so tai toi han lambda, tai toi han = lambda x tai da dat):\n";
            sum << "Mất ổn định tuyến tính — hệ số tải tới hạn λ (tải tới hạn = λ × tải đã đặt):\n";
            for (std::size_t i = 0; i < er.values.size(); ++i) {
                log << "  dang " << i + 1 << ": lambda = " << er.values[i] << "\n";
                sum << "dạng " << i + 1 << ": λ = " << er.values[i] << (static_cast<int>(i) + 1 == show ? "   ◄ đang hiển thị" : "") << "\n";
            }
        } else {
            log << "Dao dong rieng (tan so Hz):\n";
            sum << "Dao động riêng — tần số (Hz):\n";
            for (std::size_t i = 0; i < er.values.size(); ++i) {
                const double f = std::sqrt(std::max(er.values[i], 0.0)) / twoPi;
                log << "  dang " << i + 1 << ": f = " << f << " Hz  (T = " << (f > 0 ? 1.0 / f : 0.0) << " s)\n";
                sum << "dạng " << i + 1 << ": " << f << " Hz" << (static_cast<int>(i) + 1 == show ? "   ◄ đang hiển thị" : "") << "\n";
            }
        }
        FrameResult shape;
        shape.ok = true;
        shape.disp = er.shapes[show - 1];
        shape.members.assign(fm.members.size(), FrameMemberResult{});
        FrameRender mr = buildFrameRender(fm, shape);
        mr.fields.pointScalars["von_mises_nodal"] = mr.fields.pointScalars["displacement_magnitude"];
        rep.mesh = std::move(mr.mesh);
        rep.fields = std::move(mr.fields);
        for (std::size_t i = 0; i < er.values.size(); ++i) {
            const double f = std::sqrt(std::max(er.values[i], 0.0)) / twoPi;
            if (buck) {
                rep.series["load_factors"].push_back(er.values[i]);
                rep.values["load_factor_" + std::to_string(i + 1)] = er.values[i];
            } else {
                rep.series["frequencies_hz"].push_back(f);
                rep.values["frequency_" + std::to_string(i + 1) + "_hz"] = f;
            }
        }
        rep.values["mode_count"] = static_cast<double>(er.values.size());
        rep.maxValue = 1.0;   // dạng riêng chuẩn hoá: chuyển vị tịnh tiến lớn nhất = 1
        rep.secondary = buck ? er.values[show - 1] : std::sqrt(std::max(er.values[show - 1], 0.0)) / twoPi;
        rep.ok = true;
        rep.message = std::string("Hoi tu (") + (buck ? "mat on dinh" : "dao dong rieng") + ", hien thi dang " + std::to_string(show) + ")";
        sum << "(đổi dạng hiển thị: mode = k trong file case)";
        rep.summary = sum.str();
        return rep;
    }
    const FrameResult fr = solveFrame(fm);
    if (!fr.ok) { rep.message = fr.message; return rep; }
    FrameRender rr = buildFrameRender(fm, fr);
    rep.mesh = std::move(rr.mesh);
    rep.fields = std::move(rr.fields);
    rep.maxValue = fr.maxDisp;
    rep.secondary = fr.maxStress;
    rep.ok = true;
    rep.message = "Hoi tu";
    double sumFz = 0, sumFx = 0, sumFy = 0;
    for (const auto& r : fr.reactions) { sumFx += r[0]; sumFy += r[1]; sumFz += r[2]; }
    log << "  |u|max = " << fr.maxDisp << " m,  ung suat phap max = " << fr.maxStress << " Pa,  momen max = " << fr.maxMoment
        << " N.m,  luc doc max = " << fr.maxAxial << " N\n";
    log << "  Tong phan luc: Rx=" << sumFx << " Ry=" << sumFy << " Rz=" << sumFz << " N\n";
    if (fm.yield > 0) log << "  He so su dung max (sigma/fy) = " << fr.maxUtilization << (fr.maxUtilization > 1 ? "  (QUA GIOI HAN CHAY)" : "") << "\n";
    int slack = 0;
    for (const auto& m : fr.members) slack += m.active ? 0 : 1;
    if (slack) log << "  Cap chung (khong chiu nen): " << slack << " thanh\n";
    rep.values["max_displacement"] = fr.maxDisp;
    rep.values["max_stress"] = fr.maxStress;
    rep.values["max_moment"] = fr.maxMoment;
    rep.values["max_axial"] = fr.maxAxial;
    rep.values["max_utilization"] = fr.maxUtilization;
    rep.values["reaction_fx"] = sumFx;
    rep.values["reaction_fy"] = sumFy;
    rep.values["reaction_fz"] = sumFz;
    rep.values["n_nodes"] = static_cast<double>(fm.nodes.size());
    rep.values["n_members"] = static_cast<double>(fm.members.size());
    for (int d = 0; d < 6; ++d) {
        static const char* nm[6] = {"ux", "uy", "uz", "rx", "ry", "rz"};
        std::vector<double> u, r;
        for (std::size_t i = 0; i < fr.disp.size(); ++i) {
            u.push_back(fr.disp[i][d]);
            r.push_back(fr.reactions[i][d]);
        }
        rep.series[std::string("node_") + nm[d]] = u;
        rep.series[std::string("reaction_") + nm[d]] = r;
    }
    for (const auto& m : fr.members) {
        rep.series["member_length"].push_back(m.length);
        rep.series["member_n_max"].push_back(m.maxN);
        rep.series["member_m_max"].push_back(m.maxM);
        rep.series["member_v_max"].push_back(m.maxV);
        rep.series["member_stress_max"].push_back(m.maxStress);
        rep.series["member_n_start"].push_back(-m.f1[0]);
        rep.series["member_n_end"].push_back(m.f2[0]);
    }
    std::ostringstream sum;
    sum << "Khung/dàn 3D: " << fm.nodes.size() << " nút, " << fm.members.size() << " thanh\n"
        << "Chuyển vị lớn nhất: " << fr.maxDisp << " m\n"
        << "Ứng suất pháp lớn nhất: " << fr.maxStress << " Pa\n"
        << "Mômen lớn nhất: " << fr.maxMoment << " N·m\n"
        << "Lực dọc lớn nhất: " << fr.maxAxial << " N";
    if (fm.yield > 0) sum << "\nHệ số sử dụng σ/fy: " << fr.maxUtilization << (fr.maxUtilization > 1 ? " (VƯỢT GIỚI HẠN CHẢY)" : "");
    if (slack) sum << "\nCáp chùng: " << slack;
    rep.summary = sum.str();

    // bảng nội lực CSV (tuỳ chọn: report = file.csv)
    const std::string rpt = cf.get("report", "");
    if (writeOutput && !rpt.empty()) {
        std::string path = rpt;
        if (!baseDir.empty() && rpt.find(':') == std::string::npos && rpt[0] != '/') path = baseDir + "/" + rpt;
        std::ofstream f(path);
        if (f) {
            f << "member,node1,node2,length,N1,N2,Vy1,Vz1,T1,My1,My2,Mz1,Mz2,max_moment,max_stress\n";
            for (std::size_t i = 0; i < fr.members.size(); ++i) {
                const auto& m = fr.members[i];
                f << i + 1 << ',' << fm.members[i].n1 + 1 << ',' << fm.members[i].n2 + 1 << ',' << m.length << ',' << -m.f1[0] << ',' << m.f2[0]
                  << ',' << m.f1[1] << ',' << m.f1[2] << ',' << m.f1[3] << ',' << m.f1[4] << ',' << m.f2[4] << ',' << m.f1[5] << ',' << m.f2[5]
                  << ',' << m.maxM << ',' << m.maxStress << "\n";
            }
            log << "Da ghi bang noi luc: " << path << "\n";
        }
    }
    const std::string out = cf.get("output", "");
    if (writeOutput && !out.empty()) {
        std::string path = out;
        if (!baseDir.empty() && out.find(':') == std::string::npos && out[0] != '/') path = baseDir + "/" + out;
        if (writeVtk(path, rep.mesh, rep.fields, &err)) log << "Da ghi: " << path << "\n";
        else log << "Khong ghi duoc VTK: " << err << "\n";
    }
    return rep;
}

// ---------------------------------------------------------------- tấm / vỏ (CalculiX, phần tử vỏ S4)
static RunReport runShellCase(const CaseFile& cf, std::ostream& log, const std::string& baseDir, bool writeOutput) {
    RunReport rep;
    rep.type = "shell";
    const auto g = CaseFile::tokens(cf.get("geometry", ""));
    auto num = [&](std::size_t i, double def) { return i < g.size() ? std::atof(g[i].c_str()) : def; };
    ShellMesh sm;
    if (!g.empty() && g[0] == "shell_plate" && g.size() >= 6) sm = makeShellPlate(num(1, 1), num(2, 1), num(3, 0.01), safeInt(num(4, 10), 1, 2000), safeInt(num(5, 10), 1, 2000));
    else if (!g.empty() && g[0] == "shell_cylinder" && g.size() >= 6) sm = makeShellCylinder(num(1, 1), num(2, 1), num(3, 0.01), safeInt(num(4, 24), 3, 2000), safeInt(num(5, 10), 1, 2000));
    else {
        rep.message = "geometry = shell_plate Lx Ly t nx ny | shell_cylinder R L t nc nl";
        return rep;
    }
    if (!(sm.thickness > 0)) { rep.message = "Chieu day phai duong"; return rep; }
    Material mat;
    mat.E = cf.getDouble("e", mat.E);
    mat.nu = cf.getDouble("nu", mat.nu);
    mat.rho = cf.getDouble("rho", mat.rho);
    std::vector<ShellFix> fixes;
    for (const auto& line : cf.getAll("fix")) {
        const auto t = CaseFile::tokens(line);
        if (t.empty()) continue;
        ShellFix f;
        f.nodes = selectShellNodes(sm, t[0]);
        if (f.nodes.empty()) { rep.message = "Tap nut khong hop le: " + t[0]; return rep; }
        if (t.size() == 1) f.dof.fill(true);
        for (std::size_t i = 1; i < t.size(); ++i) {
            static const char* names[6] = {"x", "y", "z", "rx", "ry", "rz"};
            bool found = false;
            for (int d = 0; d < 6; ++d)
                if (t[i] == names[d]) { f.dof[d] = true; found = true; }
            if (!found) { rep.message = "fix: bac tu do khong hop le: " + t[i] + " (x y z rx ry rz)"; return rep; }
        }
        fixes.push_back(f);
    }
    if (fixes.empty()) { rep.message = "Thieu lien ket: them dong fix = xmin (hoac fix = xmin x y z de chi khoa chuyen vi)"; return rep; }
    std::vector<ShellLoad> loads;
    const double p = cf.getDouble("pressure", 0.0);
    if (p != 0.0) {
        auto l = shellPressureLoads(sm, p);
        loads.insert(loads.end(), l.begin(), l.end());
    }
    for (const auto& line : cf.getAll("load")) {
        const auto t = CaseFile::tokens(line);
        if (t.size() < 4) { rep.message = "load = <tap nut> Fx Fy Fz"; return rep; }
        const auto nodes = selectShellNodes(sm, t[0]);
        if (nodes.empty()) { rep.message = "Tap nut khong hop le: " + t[0]; return rep; }
        auto l = shellForceLoads(nodes, {std::atof(t[1].c_str()), std::atof(t[2].c_str()), std::atof(t[3].c_str())});
        loads.insert(loads.end(), l.begin(), l.end());
    }
    if (loads.empty()) { rep.message = "Thieu tai: them pressure = <Pa> hoac load = <tap nut> Fx Fy Fz"; return rep; }
    ShellOptions opt;
    opt.jobName = cf.get("job_name", "fastcae_shell");
    opt.workDir = fastcae::resolveWorkDir(cf.get("work_dir", ""), baseDir, "ccx_shell");
    opt.executable = cf.get("ccx_path", "");
    log << "Tam/vo (CalculiX S4): " << sm.nodes.size() << " nut, " << sm.quads.size() << " o, t = " << sm.thickness << " m\n";
    ShellResult sr = runCalculixShell(sm, mat, fixes, loads, opt);
    if (!sr.ok) { rep.message = sr.message; return rep; }
    shellRender(sm, sr, rep.mesh, rep.fields);
    rep.ok = true;
    rep.message = "Hoi tu (CalculiX, vo S4)";
    rep.maxValue = sr.maxDisplacement;
    rep.secondary = sr.maxVonMises;
    rep.values["max_displacement"] = sr.maxDisplacement;
    rep.values["max_von_mises"] = sr.maxVonMises;
    rep.values["n_nodes"] = static_cast<double>(sm.nodes.size());
    rep.values["n_elements"] = static_cast<double>(sm.quads.size());
    rep.series["node_displacement"] = sr.dispMagnitude;
    rep.series["node_von_mises"] = sr.vonMises;
    log << "  |u|max = " << sr.maxDisplacement << " m,  von Mises max (mat tren/duoi) = " << sr.maxVonMises << " Pa\n";
    std::ostringstream sum;
    sum << "Tấm/vỏ CalculiX (S4): " << sm.nodes.size() << " nút, " << sm.quads.size() << " ô\n"
        << "Chuyển vị lớn nhất: " << sr.maxDisplacement << " m\n"
        << "von Mises lớn nhất (mặt trên/dưới): " << sr.maxVonMises << " Pa";
    rep.summary = sum.str();
    const std::string out = cf.get("output", "");
    if (writeOutput && !out.empty()) {
        std::string path = out, err;
        if (!baseDir.empty() && out.find(':') == std::string::npos && out[0] != '/') path = baseDir + "/" + out;
        if (writeVtk(path, rep.mesh, rep.fields, &err)) log << "Da ghi: " << path << "\n";
        else log << "Khong ghi duoc VTK: " << err << "\n";
    }
    return rep;
}


// ---------------------------------------------------------------- Mô hình trung lập (.fcmodel.json) và solver theo hợp đồng mô hình
// FastCAE làm phần dựng mô hình (hình học -> lưới, tập nút, vật liệu, liên kết, tải); thuật toán giải do bên ngoài đảm nhiệm.
namespace {

std::string jnum(double v) {
    if (!std::isfinite(v)) return "0";
    char b[40];
    std::snprintf(b, sizeof b, "%.17g", v);
    return b;
}

std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += static_cast<char>(c);
    }
    return o + "\"";
}

std::string jints(const std::vector<int>& v) {
    std::string o = "[";
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) o += ","; o += std::to_string(v[i]); }
    return o + "]";
}

// type = elastic | thermal. `mesh`/`quad` là lưới cuối cùng (đã bậc 2 nếu order = 2).
bool buildModelJson(const CaseFile& cf, const Mesh& mesh, const QuadMesh* quad, const Material& mat, const std::string& type,
                    std::string& json, std::string& err) {
    std::ostringstream o;
    o << "{\n\"format\":\"fastcae-model\",\"version\":1,\"units\":\"SI\",\n";
    o << "\"analysis\":" << jstr(type) << ",\"order\":" << (quad ? 2 : 1) << ",\n";
    o << "\"material\":{\"E\":" << jnum(mat.E) << ",\"nu\":" << jnum(mat.nu) << ",\"k\":" << jnum(mat.k) << ",\"rho\":" << jnum(mat.rho) << "},\n";
    o << "\"mesh\":{\"num_nodes\":" << mesh.numNodes() << ",\"nodes\":[";
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i)
        o << (i ? "," : "") << jnum(mesh.nodes[i].x) << "," << jnum(mesh.nodes[i].y) << "," << jnum(mesh.nodes[i].z);
    o << "],\n\"tet4\":[";
    for (std::size_t c = 0; c < mesh.tets.size(); ++c)
        o << (c ? "," : "") << mesh.tets[c][0] << "," << mesh.tets[c][1] << "," << mesh.tets[c][2] << "," << mesh.tets[c][3];
    o << "]";
    if (quad) {
        // Thứ tự nút T10 (giống CalculiX C3D10 / Abaqus C3D10): 0-3 là đỉnh, 4=0-1, 5=1-2, 6=2-0, 7=0-3, 8=1-3, 9=2-3
        o << ",\n\"tet10\":[";
        for (std::size_t c = 0; c < quad->tets10.size(); ++c)
            for (int k = 0; k < 10; ++k) o << ((c || k) ? "," : "") << quad->tets10[c][static_cast<std::size_t>(k)];
        o << "]";
    }
    o << ",\n\"body_node_start\":" << jints(mesh.bodyNodeStart) << "},\n";

    std::map<std::string, std::vector<int>> sets;   // chỉ các tập được dùng
    const auto getSet = [&](const std::string& name) -> const std::vector<int>* {
        auto it = sets.find(name);
        if (it == sets.end()) it = sets.emplace(name, selectSet(mesh, quad, cf, name)).first;
        return it->second.empty() ? nullptr : &it->second;
    };

    o << "\"dirichlet\":[";
    bool first = true;
    if (type == "elastic") {
        for (const auto& line : cf.getAll("fix")) {
            const auto t = CaseFile::tokens(line);
            if (t.empty()) continue;
            const auto* ns = getSet(t[0]);
            if (!ns) { err = "Tap nut khong hop le: " + t[0]; return false; }
            std::vector<int> dofs;
            if (t.size() == 1) dofs = {0, 1, 2};
            for (std::size_t i = 1; i < t.size(); ++i) {
                if (t[i] == "x") dofs.push_back(0);
                if (t[i] == "y") dofs.push_back(1);
                if (t[i] == "z") dofs.push_back(2);
            }
            o << (first ? "" : ",") << "\n {\"set\":" << jstr(t[0]) << ",\"dofs\":" << jints(dofs) << ",\"value\":0,\"nodes\":" << jints(*ns) << "}";
            first = false;
        }
        if (first) { err = "Chua co lien ket (fix) - bai toan khong xac dinh"; return false; }
    } else {
        for (const auto& line : cf.getAll("temp")) {
            const auto t = CaseFile::tokens(line);
            const auto v = toDoubles(t, 1);
            if (t.empty() || v.empty()) { err = "temp = <tap> gia_tri"; return false; }
            const auto* ns = getSet(t[0]);
            if (!ns) { err = "Tap nut khong hop le: " + t[0]; return false; }
            o << (first ? "" : ",") << "\n {\"set\":" << jstr(t[0]) << ",\"value\":" << jnum(v[0]) << ",\"nodes\":" << jints(*ns) << "}";
            first = false;
        }
        if (first) { err = "Chua co dieu kien nhiet do (temp)"; return false; }
    }
    o << "],\n\"loads\":[";
    first = true;
    if (type == "elastic") {
        for (const auto& line : cf.getAll("load")) {   // tổng lực được phân bố theo diện tích phụ trách (như solver nội)
            const auto t = CaseFile::tokens(line);
            const auto v = toDoubles(t, 1);
            if (t.empty() || v.size() < 3) { err = "load = <tap> fx fy fz"; return false; }
            const auto* ns = getSet(t[0]);
            if (!ns) { err = "Tap nut khong hop le: " + t[0]; return false; }
            const auto fl = distributeForce(mesh, quad, *ns, {v[0], v[1], v[2]});
            o << (first ? "" : ",") << "\n {\"set\":" << jstr(t[0]) << ",\"total\":[" << jnum(v[0]) << "," << jnum(v[1]) << "," << jnum(v[2]) << "],\"nodal\":[";
            for (std::size_t i = 0; i < fl.size(); ++i)
                o << (i ? "," : "") << "[" << fl[i].node << "," << jnum(fl[i].f.x) << "," << jnum(fl[i].f.y) << "," << jnum(fl[i].f.z) << "]";
            o << "]}";
            first = false;
        }
    } else {
        for (const auto& line : cf.getAll("heat")) {
            const auto t = CaseFile::tokens(line);
            const auto v = toDoubles(t, 1);
            if (t.empty() || v.empty()) { err = "heat = <tap> watts"; return false; }
            const auto* ns = getSet(t[0]);
            if (!ns) { err = "Tap nut khong hop le: " + t[0]; return false; }
            const auto hs = distributeHeat(mesh, quad, *ns, v[0]);
            o << (first ? "" : ",") << "\n {\"set\":" << jstr(t[0]) << ",\"total_watts\":" << jnum(v[0]) << ",\"nodal\":[";
            for (std::size_t i = 0; i < hs.size(); ++i) o << (i ? "," : "") << "[" << hs[i].node << "," << jnum(hs[i].q) << "]";
            o << "]}";
            first = false;
        }
    }
    o << "],\n\"node_sets\":{";
    first = true;
    for (const auto& [name, ns] : sets) {
        o << (first ? "" : ",") << "\n " << jstr(name) << ":" << jints(ns);
        first = false;
    }
    o << "},\n\"result_contract\":\"ghi file VTK legacy ASCII UNSTRUCTURED_GRID: diem i = nut i cua mo hinh; truong nut: ";
    o << (type == "elastic" ? "displacement (VECTORS), von_mises_nodal (SCALARS, tuy chon)" : "temperature (SCALARS)") << "\"\n}\n";
    json = o.str();
    return true;
}

// Dựng lưới (+ bậc 2) theo case, dùng chung cho exportModel và solver theo hợp đồng mô hình.
bool buildModelMesh(const CaseFile& cf, std::ostream& log, const std::string& baseDir, Mesh& mesh, QuadMesh& qm, const QuadMesh*& quad,
                    std::string& err) {
    if (!buildGeometry(cf, log, baseDir, mesh, err)) return false;
    const int order = safeInt(cf.getDouble("order", 1), 1, 2);
    quad = nullptr;
    if (order == 2) {
        qm = makeQuadratic(mesh);
        mesh = qm.base;
        quad = &qm;
        log << "Phan tu bac 2 (T10): " << mesh.numNodes() << " nut\n";
    }
    return true;
}

}  // namespace

bool exportModel(const CaseFile& cf, std::ostream& log, const std::string& baseDir, const std::string& jsonPath, std::string& err) {
    try {
        const std::string type = cf.get("type", "elastic");
        if (type != "elastic" && type != "thermal") { err = "exportModel chi ho tro type = elastic | thermal (co: " + type + ")"; return false; }
        Mesh mesh;
        QuadMesh qm;
        const QuadMesh* quad = nullptr;
        if (!buildModelMesh(cf, log, baseDir, mesh, qm, quad, err)) return false;
        std::string json;
        if (!buildModelJson(cf, mesh, quad, readMaterial(cf), type, json, err)) return false;
        std::ofstream f(jsonPath, std::ios::binary);
        if (!f) { err = "Khong ghi duoc " + jsonPath; return false; }
        f << json;
        if (!f) { err = "Loi ghi " + jsonPath; return false; }
        log << "Da ghi mo hinh: " << jsonPath << " (" << mesh.numNodes() << " nut, " << mesh.numCells() << " tu dien)\n";
        return true;
    } catch (const std::exception& e) {
        err = std::string("exportModel: ") + e.what();
        return false;
    }
}

namespace {

// solver = <id plugin> (input = model): ghi mô hình, chạy chương trình ngoài, đọc <job>_result.vtk về khung 3D.
RunReport runModelSolver(const SolverInfo& si, const CaseFile& cf, const Mesh& mesh, const QuadMesh* quad, const Material& mat,
                         std::ostream& log, const std::string& baseDir, bool writeOutput) {
    RunReport rep;
    rep.type = cf.get("type", "elastic");
    std::string err, json;
    if (!buildModelJson(cf, mesh, quad, mat, rep.type, json, err)) { rep.message = err; return rep; }
    const std::string job = cf.get("job_name", "fastcae_job");
    const std::string work = fastcae::resolveWorkDir(cf.get("work_dir", ""), baseDir, "ext_" + si.id);
    std::error_code ec;
    std::filesystem::create_directories(work, ec);
    const std::string modelPath = work + "/" + job + ".fcmodel.json";
    const std::string resultPath = work + "/" + job + "_result.vtk";
    std::filesystem::remove(resultPath, ec);   // tránh đọc nhầm kết quả của lần chạy trước
    {
        std::ofstream f(modelPath, std::ios::binary);
        if (!f) { rep.message = "Khong ghi duoc " + modelPath; return rep; }
        f << json;
    }
    log << "Mo hinh: " << modelPath << " (" << mesh.numNodes() << " nut)\n";
    ExternalRunOptions o;
    o.solverId = si.id;
    o.input = modelPath;
    o.workDir = work;
    o.executableHint = cf.get("solver_path", "");
    o.extraArgs = cf.get("solver_args", "");
    o.onOutput = [&log](const std::string& t) { log << t; };
    log << "Giai bang " << si.name << " (plugin " << si.id << ")...\n";
    const auto R = runExternalSolver(o);
    if (!R.ok) { rep.message = R.message; return rep; }
    if (!std::filesystem::exists(resultPath, ec)) {
        rep.message = "Solver chay xong nhung khong ghi " + resultPath + " (hop dong: file VTK '<job>_result.vtk' trong thu muc lam viec)";
        return rep;
    }
    Mesh rm;
    VtkFields rf;
    VtkReadInfo ri;
    if (!readVtk(resultPath, rm, rf, &ri, &err)) { rep.message = "Khong doc duoc ket qua: " + err; return rep; }
    if (rm.numNodes() == mesh.numNodes()) { rep.mesh = mesh; }   // đúng hợp đồng: giữ lưới mô hình (T10 có nút giữa cạnh)
    else {
        rep.mesh = std::move(rm);
        log << "CANH BAO: so diem trong VTK (" << rep.mesh.numNodes() << ") khac so nut mo hinh (" << mesh.numNodes() << "); hien thi theo luoi cua file ket qua.\n";
    }
    rep.fields = std::move(rf);
    rep.ok = true;
    rep.message = "Hoan tat (solver ngoai: " + si.id + ")";
    rep.summary = "Solver ngoai '" + si.name + "' tren mo hinh FastCAE: " + std::to_string(rep.mesh.numNodes()) + " nut.";
    for (const auto& [name, v] : rep.fields.pointScalars)
        if (!v.empty()) rep.values["max_" + name] = *std::max_element(v.begin(), v.end());
    for (const auto& [name, v] : rep.fields.pointVectors) {
        double mx = 0;
        for (std::size_t i = 0; i + 2 < v.size(); i += 3) mx = std::max(mx, std::sqrt(v[i] * v[i] + v[i + 1] * v[i + 1] + v[i + 2] * v[i + 2]));
        rep.values["max_" + name + "_magnitude"] = mx;
    }
    if (rep.type == "elastic") {
        const auto it = rep.fields.pointVectors.find("displacement");
        if (it != rep.fields.pointVectors.end()) {
            const auto& u = it->second;
            std::vector<double> mag(u.size() / 3);
            for (std::size_t i = 0; i < mag.size(); ++i) mag[i] = std::sqrt(u[3*i]*u[3*i] + u[3*i+1]*u[3*i+1] + u[3*i+2]*u[3*i+2]);
            if (!mag.empty()) rep.maxValue = *std::max_element(mag.begin(), mag.end());
            rep.fields.pointScalars["displacement_magnitude"] = std::move(mag);
        }
        const auto vm = rep.fields.pointScalars.find("von_mises_nodal");
        if (vm != rep.fields.pointScalars.end() && !vm->second.empty()) rep.secondary = *std::max_element(vm->second.begin(), vm->second.end());
    } else {
        const auto it = rep.fields.pointScalars.find("temperature");
        if (it != rep.fields.pointScalars.end() && !it->second.empty()) {
            rep.maxValue = *std::max_element(it->second.begin(), it->second.end());
            rep.secondary = *std::min_element(it->second.begin(), it->second.end());
        }
    }
    const std::string out = writeOutput ? resolveOutput(cf.get("output"), baseDir) : std::string();
    if (!out.empty()) {
        std::string werr;
        if (writeVtk(out, rep.mesh, rep.fields, &werr)) log << "Da ghi: " << out << "\n";
        else { rep.ok = false; rep.message = werr; }
    }
    return rep;
}

}  // namespace

static RunReport runCaseImpl(const CaseFile& cf, std::ostream& log, const std::string& baseDir, bool writeOutput) {
    RunReport rep;
    {   // bộ giải tuyến tính: cg (mặc định) hoặc petsc
        const std::string ls = cf.get("linear_solver", "auto");
        if (!setLinearSolver(ls, cf.get("petsc_options", ""))) {
            rep.type = cf.get("type", "elastic");
            rep.message = ls == "petsc" ? "Ban build nay khong co PETSc (build voi -DFASTCAE_WITH_PETSC=ON). Dung linear_solver = auto."
                                      : "linear_solver chi nhan auto, direct, cg hoac petsc";
            return rep;
        }
        if (linearSolverName() == "petsc") log << "Bo giai tuyen tinh: PETSc\n";
    }
    rep.type = cf.get("type", "elastic");
    std::string err;

    if (rep.type == "frame") return runFrameCase(cf, log, baseDir, writeOutput);
    if (rep.type == "shell") return runShellCase(cf, log, baseDir, writeOutput);
    if (rep.type == "cfd3d") {
        RunReport r3 = runOpenFoam3D(cf, log, baseDir);
        const std::string out3 = (r3.ok && writeOutput) ? resolveOutput(cf.get("output"), baseDir) : std::string();
        if (!out3.empty()) {
            std::string werr;
            if (writeVtk(out3, r3.mesh, r3.fields, &werr)) log << "Da ghi: " << out3 << "\n";
            else { r3.ok = false; r3.message = werr; }
        }
        return r3;
    }
    if (rep.type == "cavity" && cf.get("solver", "builtin") == "openfoam") return runOpenFoamCavity(cf, log, baseDir);
    if (rep.type == "cavity") {
        CavityParams p;
        {
            std::string perr;
            if (!checkCavityParams(cf.getDouble("n", 41), cf.getDouble("re", 100), cf.getDouble("lid_speed", 1.0), perr)) { rep.message = perr; return rep; }
        }
        p.n = safeInt(cf.getDouble("n", 41), 5, 1000);
        p.Re = cf.getDouble("re", 100);
        p.lidSpeed = cf.getDouble("lid_speed", 1.0);
        p.maxSteps = safeInt(cf.getDouble("max_steps", 60000), 1, 100000000);
        log << "CFD khoang kin 2D: Re=" << p.Re << ", luoi " << p.n << "x" << p.n << "\n";
        auto r = solveCavity(p);
        rep.iterations = r.steps;
        rep.maxValue = *std::max_element(r.omega.begin(), r.omega.end(),
                                         [](double a, double b) { return std::abs(a) < std::abs(b); });
        rep.maxValue = std::abs(rep.maxValue);
        rep.ok = true;
        rep.message = r.converged ? "Hoi tu" : "Chua hoi tu het (tang max_steps)";
        {   // Thin 1-cell-thick slab so the GUI can display the 2D field with the same viewer.
            const std::size_t N = static_cast<std::size_t>(r.n) * r.n;
            rep.mesh = makeBoxMesh(1.0, 1.0, r.h, r.n - 1, r.n - 1, 1);
            std::vector<double> speed(2 * N), psi(2 * N), om(2 * N), vel(6 * N, 0.0);
            for (std::size_t k = 0; k < 2 * N; ++k) {
                const std::size_t q = k % N;
                speed[k] = std::hypot(r.u[q], r.v[q]);
                psi[k] = r.psi[q];
                om[k] = r.omega[q];
                vel[3 * k] = r.u[q];
                vel[3 * k + 1] = r.v[q];
            }
            rep.fields.pointScalars["speed"] = speed;
            rep.fields.pointScalars["streamfunction"] = psi;
            rep.fields.pointScalars["vorticity"] = om;
            rep.fields.pointVectors["velocity"] = vel;
        }
        const std::string out = writeOutput ? resolveOutput(cf.get("output"), baseDir) : std::string();
        if (!out.empty()) {
            if (!writeCavityVtk(out, r)) { rep.ok = false; rep.message = "Khong ghi duoc " + out; }
            else log << "Da ghi: " << out << "\n";
        }
        return rep;
    }

    if (!buildGeometry(cf, log, baseDir, rep.mesh, err)) { rep.message = err; return rep; }
    const Material mat = readMaterial(cf);

    // order = 2 -> 10-node quadratic tetrahedra (mid-edge nodes are appended to the mesh)
    const int order = safeInt(cf.getDouble("order", 1), 1, 2);
    QuadMesh qm;
    const QuadMesh* quad = nullptr;
    if (order == 2) {
        qm = makeQuadratic(rep.mesh);
        rep.mesh = qm.base;
        quad = &qm;
        log << "Phan tu bac 2 (T10): " << rep.mesh.numNodes() << " nut\n";
    } else if (order != 1) {
        rep.message = "order chi nhan 1 hoac 2";
        return rep;
    }

    {   // solver = <id> khai báo bằng *.fsolver: FastCAE dựng mô hình, chương trình ngoài giải
        const std::string sname = cf.get("solver", "builtin");
        if (sname != "builtin" && sname != "calculix") {
            const SolverInfo* ext = findSolverInfo(sname);
            if (!ext) { rep.message = "Khong biet solver '" + sname + "' (fastcae-cli solvers; plugin .fsolver dat trong thu muc FASTCAE_SOLVER_PLUGINS)"; return rep; }
            if (ext->takesModel) {
                if (rep.type != "elastic" && rep.type != "thermal") { rep.message = "Solver theo hop dong mo hinh chi ho tro type = elastic | thermal"; return rep; }
                return runModelSolver(*ext, cf, rep.mesh, quad, mat, log, baseDir, writeOutput);
            }
        }
    }

    if (rep.type == "elastic") {
        std::vector<DisplacementBC> bcs;
        for (const auto& line : cf.getAll("fix")) {  // fix = <set> [x y z]
            auto t = CaseFile::tokens(line);
            if (t.empty()) continue;
            auto nodes = selectSet(rep.mesh, quad, cf, t[0]);
            if (nodes.empty()) { rep.message = "Tap nut khong hop le: " + t[0]; return rep; }
            bool cx = true, cy = true, cz = true;
            if (t.size() > 1) {
                cx = cy = cz = false;
                for (std::size_t i = 1; i < t.size(); ++i) {
                    if (t[i] == "x") cx = true;
                    if (t[i] == "y") cy = true;
                    if (t[i] == "z") cz = true;
                }
            }
            for (int n : nodes) {
                if (cx) bcs.push_back({n, 0, 0});
                if (cy) bcs.push_back({n, 1, 0});
                if (cz) bcs.push_back({n, 2, 0});
            }
        }
        std::vector<NodalForce> loads;
        for (const auto& line : cf.getAll("load")) {  // load = <set> fx fy fz  (total force)
            auto t = CaseFile::tokens(line);
            auto v = toDoubles(t, 1);
            if (t.empty() || v.size() < 3) { rep.message = "load = <tap> fx fy fz"; return rep; }
            auto nodes = selectSet(rep.mesh, quad, cf, t[0]);
            if (nodes.empty()) { rep.message = "Tap nut khong hop le: " + t[0]; return rep; }
            // total force spread over the boundary faces of the set, weighted by area
            auto fl = distributeForce(rep.mesh, quad, nodes, {v[0], v[1], v[2]});
            loads.insert(loads.end(), fl.begin(), fl.end());
        }
        if (bcs.empty()) { rep.message = "Chua co lien ket (fix) - bai toan khong xac dinh"; return rep; }
        ElasticResult r;
        const bool wantsFaces = cf.has("pressure") || cf.has("contact");
        if (cf.get("solver", "builtin") == "calculix" && ((cf.has("analysis") && cf.get("analysis") != "static") || wantsFaces)) {
            CcxSpec spec = readCcxSpec(cf, mat, cf.get("analysis", cf.has("contact") ? "contact" : "static"));
            for (const auto& line : cf.getAll("pressure")) {   // pressure = <tap> p   (p > 0: nen vao mat)
                auto t = CaseFile::tokens(line);
                auto v = toDoubles(t, 1);
                if (t.empty() || v.empty() || !std::isfinite(v[0])) { rep.message = "pressure = <tap> gia_tri (Pa, duong = nen vao mat)"; return rep; }
                const auto nodes = selectSet(rep.mesh, quad, cf, t[0]);
                CcxPressure pr;
                pr.value = v[0];
                pr.faces = boundaryFacesOnNodes(rep.mesh, nodes);
                if (pr.faces.empty()) { rep.message = "pressure: tap '" + t[0] + "' khong chua mat bien nao (can ca 3 dinh cua mot mat tu dien)"; return rep; }
                spec.pressures.push_back(std::move(pr));
            }
            for (const auto& line : cf.getAll("contact")) {   // contact = <tap slave> <tap master> [stiffness K] [adjust d]
                auto t = CaseFile::tokens(line);
                if (t.size() < 2) { rep.message = "contact = <tap_slave> <tap_master> [stiffness K]"; return rep; }
                CcxContact ct;
                for (std::size_t i = 2; i + 1 < t.size(); i += 2) {
                    if (t[i] == "stiffness") { try { ct.stiffness = std::stod(t[i + 1]); } catch (...) { ct.stiffness = -1; } }
                    else if (t[i] == "adjust") { try { ct.adjust = std::stod(t[i + 1]); } catch (...) { ct.adjust = -1; } }
                    else if (t[i] == "friction") { rep.message = "contact: ma sat chua duoc ho tro (chi tiep xuc khong ma sat)"; return rep; }
                    else { rep.message = "contact: khoa khong biet '" + t[i] + "'"; return rep; }
                }
                if (!(ct.stiffness >= 0) || !std::isfinite(ct.stiffness) || !(ct.adjust >= 0) || !std::isfinite(ct.adjust)) { rep.message = "contact: stiffness va adjust phai la so khong am huu han"; return rep; }
                ct.slave = boundaryFacesOnNodes(rep.mesh, selectSet(rep.mesh, quad, cf, t[0]));
                ct.master = boundaryFacesOnNodes(rep.mesh, selectSet(rep.mesh, quad, cf, t[1]));
                if (ct.slave.empty() || ct.master.empty()) { rep.message = "contact: tap '" + t[ct.slave.empty() ? 0 : 1] + "' khong chua mat bien nao"; return rep; }
                spec.contacts.push_back(std::move(ct));
            }
            spec.bcs = bcs;
            spec.loads = loads;
            for (const auto& line : cf.getAll("temp")) {   // ứng suất nhiệt: temp = <tap> T
                auto t = CaseFile::tokens(line);
                auto v = toDoubles(t, 1);
                if (t.empty() || v.empty()) { rep.message = "temp = <tap> gia_tri"; return rep; }
                for (int n : selectSet(rep.mesh, quad, cf, t[0])) spec.tempBCs.emplace_back(n, v[0]);
            }
            CalculixOptions co;
            co.jobName = cf.get("job_name", "fastcae_job");
            co.workDir = fastcae::resolveWorkDir(cf.get("work_dir", ""), baseDir, "ccx_run");
            co.executable = cf.get("ccx_path", "");
            // Động lực: kiểm tra bước thời gian so với chu kỳ riêng (chạy nhanh 1 phân tích tần số). Bước quá thô làm thấp đỉnh
            // đáp ứng (tích phân ẩn có giảm chấn số): đo được 1,77 / 1,84 / 2,01 / 2,00 ở 6 / 12 / 24 / 48 bước mỗi chu kỳ.
            if (spec.analysis == "dynamic" && cf.get("check_timestep", "yes") != "no" && spec.increments > 0 && spec.totalTime > 0) {
                CcxSpec fs = spec;
                fs.analysis = "frequency";
                fs.modes = 1;
                fs.loads.clear();
                CalculixOptions fo = co;
                fo.jobName = "fastcae_freqcheck";
                fo.workDir = co.workDir + "_freq";
                const auto F = runCalculixAnalysis(rep.mesh, quad ? &quad->tets10 : nullptr, fs, fo);
                if (F.ok && !F.freqHz.empty() && F.freqHz[0] > 0) {
                    const double f1 = F.freqHz[0], dt = spec.totalTime / spec.increments;
                    const double perPeriod = 1.0 / (f1 * dt);
                    rep.values["fundamental_frequency_hz"] = f1;
                    rep.values["steps_per_period"] = perPeriod;
                    log << "Tan so co ban f1 = " << f1 << " Hz (chu ky " << 1000.0 / f1 << " ms), buoc thoi gian " << dt * 1000.0 << " ms = " << perPeriod << " buoc moi chu ky\n";
                    if (perPeriod < 20.0) {
                        const int need = static_cast<int>(std::ceil(spec.totalTime * f1 * 24.0));
                        log << "CANH BAO: buoc thoi gian qua tho (" << perPeriod << " buoc moi chu ky, nen >= 20): dinh dap ung co the thap hon that vai % den ~10%. "
                            << "Dat increments >= " << need << " (hoac check_timestep = no de tat kiem tra).\n";
                    }
                }
            }
            log << "Giai bang CalculiX (" << spec.analysis << ")...\n";
            auto R = runCalculixAnalysis(rep.mesh, quad ? &quad->tets10 : nullptr, spec, co);
            reportCcx(R, spec, log, rep);
            if (R.ok && spec.analysis == "dynamic" && !R.times.empty()) log << "Ket thuc t = " << R.times.back() << " s\n";
            if (R.ok && (spec.analysis == "frequency" || spec.analysis == "buckle")) {
                const int show = safeInt(cf.getDouble("mode", 1), 1, static_cast<int>(R.modeShapes.size()));
                if (!R.modeShapes.empty()) {
                    const auto& u = R.modeShapes[show - 1];
                    std::vector<double> mag(rep.mesh.numNodes());
                    for (std::size_t i = 0; i < mag.size(); ++i) mag[i] = std::sqrt(u[3*i]*u[3*i] + u[3*i+1]*u[3*i+1] + u[3*i+2]*u[3*i+2]);
                    rep.fields.pointVectors["displacement"] = u;
                    rep.fields.pointScalars["displacement_magnitude"] = mag;
                    if (!rep.frames.empty()) rep.currentFrame = std::min<int>(show - 1, static_cast<int>(rep.frames.size()) - 1);
                }
            }
            const std::string out = writeOutput ? resolveOutput(cf.get("output"), baseDir) : std::string();
            if (rep.ok && !out.empty()) {
                std::string werr;
                if (writeVtk(out, rep.mesh, rep.fields, &werr)) log << "Da ghi: " << out << "\n";
                else { rep.ok = false; rep.message = werr; }
            }
            return rep;
        }
        if (cf.get("solver", "builtin") == "calculix") {
            log << "Giai bang CalculiX (ccx)...\n";
            CalculixOptions co;
            co.jobName = cf.get("job_name", "fastcae_job");
            co.workDir = fastcae::resolveWorkDir(cf.get("work_dir", ""), baseDir, "ccx_run");
            co.executable = cf.get("ccx_path", "");
            auto run = runCalculix(rep.mesh, mat, bcs, loads, co, quad ? &quad->tets10 : nullptr);
            if (!run.ok) { rep.message = run.message; return rep; }
            log << "Deck: " << run.inpPath << "\nKet qua: " << run.frdPath << "\n";
            r = std::move(run.result);
        } else {
            log << "Giai dan hoi tuyen tinh (E=" << mat.E << ", nu=" << mat.nu << ")...\n";
            r = quad ? solveElasticT10(*quad, mat, bcs, loads) : solveElastic(rep.mesh, mat, bcs, loads);
        }
        rep.iterations = r.linear.iterations;
        rep.maxValue = r.maxDisplacement;
        rep.secondary = r.maxVonMises;
        rep.ok = r.linear.converged;
        rep.message = rep.ok ? "Hoi tu" : "CG khong hoi tu";
        rep.fields.pointScalars["displacement_magnitude"] = r.dispMagnitude;
        rep.fields.pointScalars["von_mises_nodal"] = r.vonMisesNodal;
        rep.fields.pointVectors["displacement"] = r.u;
        if (!r.vonMises.empty()) rep.fields.cellScalars["von_mises"] = r.vonMises;
    } else if (rep.type == "thermal") {
        std::vector<TemperatureBC> bcs;
        for (const auto& line : cf.getAll("temp")) {  // temp = <set> value
            auto t = CaseFile::tokens(line);
            auto v = toDoubles(t, 1);
            if (t.empty() || v.empty()) { rep.message = "temp = <tap> gia_tri"; return rep; }
            auto nodes = selectSet(rep.mesh, quad, cf, t[0]);
            if (nodes.empty()) { rep.message = "Tap nut khong hop le: " + t[0]; return rep; }
            for (int n : nodes) bcs.push_back({n, v[0]});
        }
        std::vector<HeatSource> src;
        for (const auto& line : cf.getAll("heat")) {  // heat = <set> total_watts
            auto t = CaseFile::tokens(line);
            auto v = toDoubles(t, 1);
            if (t.empty() || v.empty()) { rep.message = "heat = <tap> watts"; return rep; }
            auto nodes = selectSet(rep.mesh, quad, cf, t[0]);
            auto hs = distributeHeat(rep.mesh, quad, nodes, v[0]);
            src.insert(src.end(), hs.begin(), hs.end());
        }
        if (bcs.empty()) { rep.message = "Chua co dieu kien nhiet do (temp)"; return rep; }
        if (cf.get("solver", "builtin") == "calculix") {
            const std::string an = cf.get("analysis", "heat");
            CcxSpec spec = readCcxSpec(cf, mat, an);
            for (const auto& b : bcs) spec.tempBCs.emplace_back(b.node, b.value);
            for (const auto& h : src) spec.heatLoads.emplace_back(h.node, h.q);
            CalculixOptions co;
            co.jobName = cf.get("job_name", "fastcae_job");
            co.workDir = fastcae::resolveWorkDir(cf.get("work_dir", ""), baseDir, "ccx_run");
            co.executable = cf.get("ccx_path", "");
            log << "Giai nhiet bang CalculiX (" << an << ")...\n";
            auto R = runCalculixAnalysis(rep.mesh, quad ? &quad->tets10 : nullptr, spec, co);
            reportCcx(R, spec, log, rep);
            if (R.ok && !R.temperature.empty()) {
                rep.maxValue = *std::max_element(R.temperature.begin(), R.temperature.end());
                rep.secondary = *std::min_element(R.temperature.begin(), R.temperature.end());
            }
            const std::string out = writeOutput ? resolveOutput(cf.get("output"), baseDir) : std::string();
            if (rep.ok && !out.empty()) {
                std::string werr;
                if (writeVtk(out, rep.mesh, rep.fields, &werr)) log << "Da ghi: " << out << "\n";
                else { rep.ok = false; rep.message = werr; }
            }
            return rep;
        }
        log << "Giai dan nhiet on dinh (k=" << mat.k << ")...\n";
        auto r = quad ? solveThermalT10(*quad, mat, bcs, src) : solveThermal(rep.mesh, mat, bcs, src);
        rep.iterations = r.linear.iterations;
        rep.maxValue = r.maxT;
        rep.secondary = r.minT;
        rep.ok = r.linear.converged;
        rep.message = rep.ok ? "Hoi tu" : "CG khong hoi tu";
        rep.fields.pointScalars["temperature"] = r.T;
    } else {
        rep.message = "type khong ho tro: " + rep.type + " (elastic | thermal | cavity | frame | shell | cfd3d)";
        return rep;
    }

    const std::string out = writeOutput ? resolveOutput(cf.get("output"), baseDir) : std::string();
    if (rep.ok && !out.empty()) {
        std::string werr;
        if (writeVtk(out, rep.mesh, rep.fields, &werr)) log << "Da ghi: " << out << "\n";
        else { rep.ok = false; rep.message = werr; }
    }
    return rep;
}

RunReport runCase(const CaseFile& cf, std::ostream& log, const std::string& baseDir, bool writeOutput) {
    // Mọi ngoại lệ (hết bộ nhớ, dữ liệu hỏng...) thành thông báo lỗi: không làm sập chương trình hay luồng giải nền.
    try {
        RunReport rep = runCaseImpl(cf, log, baseDir, writeOutput);
        if (rep.ok) {   // đại lượng chung cho script/JSON (runner chuyên biệt đã điền các giá trị riêng ở trên)
            rep.values["nodes"] = static_cast<double>(rep.mesh.numNodes());
            rep.values["cells"] = static_cast<double>(rep.mesh.numCells());
            rep.values["iterations"] = static_cast<double>(rep.iterations);
            if (rep.type == "elastic") {
                rep.values["max_displacement"] = rep.maxValue;
                rep.values["max_von_mises"] = rep.secondary;
            } else if (rep.type == "thermal") {
                rep.values["t_max"] = rep.maxValue;
                rep.values["t_min"] = rep.secondary;
            } else if (rep.type == "cavity") {
                rep.values["max_vorticity"] = rep.maxValue;
                rep.values["u_center"] = rep.secondary;
            }
            // Chuỗi khung (dạng riêng, mốc thời gian): ghi thêm ten_0001.vtk, ten_0002.vtk... (mở bằng ParaView hoặc GUI)
            if (writeOutput && rep.frames.size() >= 2 && !cf.get("output").empty()) {
                const std::string out = resolveOutput(cf.get("output"), baseDir);
                const std::size_t dot = out.rfind('.');
                const std::string stem = dot == std::string::npos ? out : out.substr(0, dot);
                int written = 0;
                for (std::size_t k = 0; k < rep.frames.size(); ++k) {
                    char num[16];
                    std::snprintf(num, sizeof num, "_%04d", static_cast<int>(k + 1));
                    std::string werr;
                    if (writeVtk(stem + num + ".vtk", rep.mesh, rep.frames[k].fields, &werr)) ++written;
                }
                log << "Da ghi " << written << " khung: " << stem << "_0001.vtk ... \n";
            }
            // result_file = ten.fcres : lưu trọn kết quả (lưới + trường + đại lượng) để mở lại không cần giải lại
            const std::string rf = cf.get("result_file");
            if (writeOutput && !rf.empty() && rep.mesh.numCells() > 0) {
                StoredResult sr;
                sr.type = rep.type; sr.summary = rep.summary; sr.values = rep.values; sr.series = rep.series;
                sr.mesh = rep.mesh; sr.fields = rep.fields;
                for (const auto& fr : rep.frames) { StoredFrame sf; sf.label = fr.label; sf.fields = fr.fields; sr.frames.push_back(std::move(sf)); }
                sr.currentFrame = rep.currentFrame;
                std::string e2;
                const std::string path = resolveOutput(rf, baseDir);
                if (saveResult(path, sr, &e2)) log << "Da luu ket qua: " << path << "\n";
                else log << "CANH BAO: khong luu duoc ket qua: " << e2 << "\n";
            }
        }
        return rep;
    } catch (const std::bad_alloc&) {
        RunReport rep;
        rep.type = cf.get("type", "elastic");
        rep.message = "Het bo nho khi giai. Tang mesh_size (luoi thua hon), giam divisions, hoac dung order = 1.";
        return rep;
    } catch (const std::exception& e) {
        RunReport rep;
        rep.type = cf.get("type", "elastic");
        rep.message = std::string("Loi khong mong doi: ") + e.what();
        return rep;
    }
}

std::string demoCaseText(const std::string& name) {
    if (name == "cantilever")
        return "# Dam console 1.0 x 0.1 x 0.1 m, thep, luc 1 kN huong xuong tai dau tu do\n"
               "type = elastic\ngeometry = box 1.0 0.1 0.1\ndivisions = 20 3 3\n"
               "order = 2   # tu dien bac 2 (T10); dat 1 de dung tu dien 4 nut\n"
               "E = 210e9\nnu = 0.3\nfix = xmin\nload = xmax 0 0 -1000\noutput = cantilever.vtk\n";
    if (name == "conduction")
        return "# Thanh dan nhiet 1D: T=100 o x=0, T=0 o x=1\n"
               "type = thermal\ngeometry = box 1.0 0.1 0.1\ndivisions = 10 2 2\n"
               "k = 50\ntemp = xmin 100\ntemp = xmax 0\noutput = conduction.vtk\n";
    if (name == "cavity_openfoam")
        return "# Khoang kin nap truot bang OpenFOAM (icoFoam); n = so o moi canh\n"
               "type = cavity\nsolver = openfoam\nn = 20\nRe = 100\nend_time = 5\n";
    if (name == "cavity")
        return "# Dong chay khoang kin nap truot, Re = 100\n"
               "type = cavity\nn = 41\nRe = 100\noutput = cavity.vtk\n";
    return {};
}

}  // namespace fastcae
