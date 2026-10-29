// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "core/paths.hpp"
#include "solver//ccx_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace fastcae {

std::vector<ElemFace> boundaryFacesOnNodes(const Mesh& mesh, const std::vector<int>& nodes) {
    std::vector<char> in(mesh.nodes.size(), 0);
    for (int n : nodes)
        if (n >= 0 && n < static_cast<int>(in.size())) in[static_cast<std::size_t>(n)] = 1;
    static const int fl[4][3] = {{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}};
    std::map<std::array<int, 3>, int> count;
    auto key = [&](const Tet& t, int f) {
        std::array<int, 3> k{t[fl[f][0]], t[fl[f][1]], t[fl[f][2]]};
        std::sort(k.begin(), k.end());
        return k;
    };
    for (const auto& t : mesh.tets)
        for (int f = 0; f < 4; ++f) ++count[key(t, f)];
    std::vector<ElemFace> out;
    for (std::size_t c = 0; c < mesh.tets.size(); ++c)
        for (int f = 0; f < 4; ++f) {
            const auto& t = mesh.tets[c];
            if (count[key(t, f)] != 1) continue;
            if (in[static_cast<std::size_t>(t[fl[f][0]])] && in[static_cast<std::size_t>(t[fl[f][1]])] && in[static_cast<std::size_t>(t[fl[f][2]])])
                out.push_back({static_cast<int>(c), f + 1});
        }
    return out;
}

static bool isHeat(const std::string& a) { return a == "heat" || a == "heat_transient"; }

std::string ccxDeck(const Mesh& mesh, const std::vector<std::array<int, 10>>* tets10, const CcxSpec& s, std::string* error) {
    static const char* known[] = {"static", "nlstatic", "plastic", "contact", "frequency", "buckle", "dynamic", "thermal_stress", "heat", "heat_transient"};
    if (std::find_if(std::begin(known), std::end(known), [&](const char* k) { return s.analysis == k; }) == std::end(known)) {
        if (error) *error = "analysis khong ho tro: " + s.analysis + " (static|nlstatic|plastic|contact|frequency|buckle|dynamic|thermal_stress|heat|heat_transient)";
        return {};
    }
    if (s.analysis == "plastic" && !(s.yield > 0)) { if (error) *error = "plastic can yield > 0 (va hardening >= 0)"; return {}; }
    if ((s.analysis == "dynamic" || s.analysis == "heat_transient") && !(s.totalTime > 0)) { if (error) *error = "time phai > 0"; return {}; }
    if (s.increments < 1) { if (error) *error = "increments phai >= 1"; return {}; }
    const bool heat = isHeat(s.analysis);
    const bool dyn = s.analysis == "dynamic";
    std::ostringstream os;
    os << std::setprecision(12);
    os << "** FastCAE -> CalculiX (" << s.analysis << ")\n*HEADING\nFastCAE job\n*NODE, NSET=NALL\n";
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i)
        os << i + 1 << ", " << mesh.nodes[i].x << ", " << mesh.nodes[i].y << ", " << mesh.nodes[i].z << "\n";
    const char* et = heat ? (tets10 ? "DC3D10" : "DC3D4") : (tets10 ? "C3D10" : "C3D4");
    os << "*ELEMENT, TYPE=" << et << ", ELSET=EALL\n";
    if (tets10) {
        for (std::size_t c = 0; c < tets10->size(); ++c) {
            os << c + 1;
            for (int a : (*tets10)[c]) os << ", " << a + 1;
            os << "\n";
        }
    } else {
        for (std::size_t c = 0; c < mesh.tets.size(); ++c) {
            const auto& t = mesh.tets[c];
            os << c + 1 << ", " << t[0] + 1 << ", " << t[1] + 1 << ", " << t[2] + 1 << ", " << t[3] + 1 << "\n";
        }
    }
    os << "*MATERIAL, NAME=MAT1\n";
    if (!heat) os << "*ELASTIC\n" << s.mat.E << ", " << s.mat.nu << "\n";
    os << "*DENSITY\n" << s.mat.rho << "\n";
    if (s.alpha != 0) os << "*EXPANSION\n" << s.alpha << "\n";
    if (heat || s.analysis == "thermal_stress") os << "*CONDUCTIVITY\n" << s.mat.k << "\n*SPECIFIC HEAT\n" << s.cp << "\n";
    if (s.analysis == "plastic")
        os << "*PLASTIC\n" << s.yield << ", 0.\n" << s.yield + 10.0 * s.hardening << ", 10.\n";
    os << "*SOLID SECTION, ELSET=EALL, MATERIAL=MAT1\n";
    for (std::size_t k = 0; k < s.contacts.size(); ++k) {
        const auto& ct = s.contacts[k];
        if (ct.slave.empty() || ct.master.empty()) { if (error) *error = "contact: mot trong hai mat tiep xuc rong (kiem tra tap nut)"; return {}; }
        const double K = ct.stiffness > 0 ? ct.stiffness : 1e3 * s.mat.E / std::max(1e-9, [&]() {
            Vec3 lo, hi; mesh.bounds(lo, hi); return std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}); }());
        auto surf = [&](const char* nm, const std::vector<ElemFace>& fs) {
            os << "*SURFACE, NAME=" << nm << k + 1 << ", TYPE=ELEMENT\n";
            for (const auto& f : fs) os << f.elem + 1 << ", S" << f.face << "\n";
        };
        surf("SLAVE", ct.slave);
        surf("MASTER", ct.master);
        os << "*SURFACE INTERACTION, NAME=IC" << k + 1 << "\n*SURFACE BEHAVIOR, PRESSURE-OVERCLOSURE=LINEAR\n" << K << "\n";
        os << "*CONTACT PAIR, INTERACTION=IC" << k + 1 << ", TYPE=SURFACE TO SURFACE";
        if (ct.adjust > 0) os << ", ADJUST=" << ct.adjust;
        os << "\nSLAVE" << k + 1 << ", MASTER" << k + 1 << "\n";
    }
    if (heat || s.analysis == "thermal_stress" || s.alpha != 0)
        os << "*INITIAL CONDITIONS, TYPE=TEMPERATURE\nNALL, " << s.initialTemp << "\n";
    if (dyn || s.analysis == "heat_transient") os << "*AMPLITUDE, NAME=AST\n0., 1.\n";
    const std::string amp = (dyn || s.analysis == "heat_transient") ? ", AMPLITUDE=AST" : "";

    auto mechBc = [&]() {
        if (s.bcs.empty()) return;
        os << "*BOUNDARY\n";
        for (const auto& b : s.bcs) os << b.node + 1 << ", " << b.component + 1 << ", " << b.component + 1 << ", " << b.value << "\n";
    };
    auto mechLoads = [&]() {
        if (!s.loads.empty()) {
            os << "*CLOAD" << amp << "\n";
            for (const auto& l : s.loads) {
                if (l.f.x != 0) os << l.node + 1 << ", 1, " << l.f.x << "\n";
                if (l.f.y != 0) os << l.node + 1 << ", 2, " << l.f.y << "\n";
                if (l.f.z != 0) os << l.node + 1 << ", 3, " << l.f.z << "\n";
            }
        }
        if (!s.pressures.empty()) {
            os << "*DLOAD" << amp << "\n";
            // CalculiX: áp suất dương tác dụng theo pháp tuyến NGOÀI?  Đã kiểm chứng bằng ccx: P dương = NÉN vào mặt (xem scripts/08)
            for (const auto& pr : s.pressures)
                for (const auto& f : pr.faces) os << f.elem + 1 << ", P" << f.face << ", " << pr.value << "\n";
        }
        const double g = std::sqrt(s.gravity.x * s.gravity.x + s.gravity.y * s.gravity.y + s.gravity.z * s.gravity.z);
        if (g > 0) os << "*DLOAD" << amp << "\nEALL, GRAV, " << g << ", " << s.gravity.x / g << ", " << s.gravity.y / g << ", " << s.gravity.z / g << "\n";
    };
    const double dt0 = 1.0 / s.increments;
    const std::string nl = s.nlgeom ? ", NLGEOM" : "";

    if (s.analysis == "static" || s.analysis == "nlstatic" || s.analysis == "plastic" || s.analysis == "contact" || s.analysis == "thermal_stress") {
        const bool nlg = s.nlgeom || s.analysis == "nlstatic";   // contact: tăng dần phi tuyến nhưng NLGEOM chỉ khi người dùng bật
        os << "*STEP" << (nlg ? ", NLGEOM" : "") << ", INC=1000\n*STATIC\n" << dt0 << ", 1., 1e-6, " << dt0 << "\n";
        mechBc();
        mechLoads();
        if (s.analysis == "thermal_stress" && !s.tempBCs.empty()) {
            os << "*TEMPERATURE\n";
            for (const auto& [n, t] : s.tempBCs) os << n + 1 << ", " << t << "\n";
        }
        os << "*NODE FILE\nU" << (s.analysis == "thermal_stress" ? ", NT" : "") << "\n*EL FILE\nS" << (s.analysis == "plastic" ? ", PEEQ" : "") << "\n";
        if (!s.contacts.empty()) os << "*CONTACT FILE\nCDIS, CSTR\n";   // độ xuyên/trượt + ứng suất tiếp xúc (áp suất, cắt) trên nút slave
        os << "*END STEP\n";
    } else if (s.analysis == "frequency") {
        os << "*STEP\n*FREQUENCY\n" << std::max(1, s.modes) << "\n";
        mechBc();
        os << "*NODE FILE\nU\n*END STEP\n";
    } else if (s.analysis == "buckle") {
        os << "*STEP\n*STATIC\n";
        mechBc();
        mechLoads();
        // CalculiX lấy tải tham chiếu của buckling từ CHÍNH bước *BUCKLE (không phải từ bước tĩnh trước đó): phải lặp lại tải.
        // Tải tới hạn = hệ số * tải đã khai báo.
        os << "*NODE FILE\nU\n*END STEP\n*STEP\n*BUCKLE\n" << std::max(1, s.modes) << "\n";
        mechLoads();
        os << "*NODE FILE\nU\n*END STEP\n";
    } else if (dyn) {
        os << "*STEP" << nl << ", INC=100000\n*DYNAMIC, DIRECT\n" << s.totalTime / s.increments << ", " << s.totalTime << "\n";
        mechBc();
        mechLoads();
        os << "*NODE FILE\nU\n*EL FILE\nS\n*END STEP\n";   // ứng suất theo thời gian (von Mises từng khung)
    } else if (s.analysis == "heat") {
        os << "*STEP\n*HEAT TRANSFER, STEADY STATE\n";
        if (!s.tempBCs.empty()) { os << "*BOUNDARY\n"; for (const auto& [n, t] : s.tempBCs) os << n + 1 << ", 11, 11, " << t << "\n"; }
        if (!s.heatLoads.empty()) { os << "*CFLUX\n"; for (const auto& [n, q] : s.heatLoads) os << n + 1 << ", 11, " << q << "\n"; }
        os << "*NODE FILE\nNT\n*END STEP\n";
    } else {   // heat_transient
        os << "*STEP, INC=100000\n*HEAT TRANSFER\n" << s.totalTime / s.increments << ", " << s.totalTime << "\n";
        if (!s.tempBCs.empty()) { os << "*BOUNDARY" << amp << "\n"; for (const auto& [n, t] : s.tempBCs) os << n + 1 << ", 11, 11, " << t << "\n"; }
        if (!s.heatLoads.empty()) { os << "*CFLUX" << amp << "\n"; for (const auto& [n, q] : s.heatLoads) os << n + 1 << ", 11, " << q << "\n"; }
        os << "*NODE FILE\nNT\n*END STEP\n";
    }
    return os.str();
}

// Giống readFrd nhưng giữ riêng từng gia số (khối " 100C...").
bool readFrdIncrements(const std::string& path, std::vector<FrdIncrement>& out, std::string* error) {
    std::ifstream is(path);
    if (!is) { if (error) *error = "Khong mo duoc " + path; return false; }
    out.clear();
    std::string line, current;
    std::vector<double>* lastRec = nullptr;
    while (std::getline(is, line)) {
        if (line.size() < 3) continue;
        const std::string tag = line.substr(0, 3);
        if (line.size() >= 24 && line.compare(0, 7, "  100CL") == 0) {
            out.emplace_back();
            out.back().value = std::atof(line.substr(12, 12).c_str());
            current.clear();
            lastRec = nullptr;
        } else if (tag == " -4") {
            std::istringstream ss(line.substr(3));
            int nc = 0;
            ss >> current >> nc;
            lastRec = nullptr;
        } else if (tag == " -3") {
            current.clear();
            lastRec = nullptr;
        } else if (tag == " -1" && !current.empty() && !out.empty()) {
            if (line.size() < 13) continue;
            const int id = std::atoi(line.substr(3, 10).c_str());
            auto& rec = out.back().blocks[current][id - 1];
            rec.clear();
            for (std::size_t pos = 13; pos + 12 <= line.size(); pos += 12) rec.push_back(std::atof(line.substr(pos, 12).c_str()));
            lastRec = &rec;
        } else if (tag == " -2" && lastRec) {
            for (std::size_t pos = 13; pos + 12 <= line.size(); pos += 12) lastRec->push_back(std::atof(line.substr(pos, 12).c_str()));
        }
    }
    if (out.empty()) { if (error) *error = "Khong co khoi ket qua trong " + path; return false; }
    return true;
}

// .dat: bảng "E I G E N V A L U E   O U T P U T" (tần số) hoặc "B U C K L I N G   F A C T O R   O U T P U T".
static std::vector<double> parseDatTable(const std::string& path, const std::string& header, int column) {
    std::vector<double> v;
    std::ifstream is(path);
    std::string line;
    bool in = false;
    while (std::getline(is, line)) {
        if (!in) {
            if (line.find(header) != std::string::npos) in = true;
            continue;
        }
        std::istringstream ss(line);
        std::vector<double> cols;
        double x;
        while (ss >> x) cols.push_back(x);
        if (cols.size() > static_cast<std::size_t>(column) && std::abs(cols[0] - std::round(cols[0])) < 1e-12 && cols[0] >= 1 && cols[0] == static_cast<double>(v.size() + 1))
            v.push_back(cols[column]);
        else if (!v.empty() && cols.empty() && line.find_first_not_of(" \t\r") == std::string::npos) continue;
        else if (!v.empty() && !cols.empty() && cols.size() <= static_cast<std::size_t>(column)) break;
    }
    return v;
}

CcxAnalysisResult runCalculixAnalysis(const Mesh& mesh, const std::vector<std::array<int, 10>>* tets10, const CcxSpec& spec,
                                      const CalculixOptions& opt) {
    CcxAnalysisResult R;
    std::error_code ec;
    fs::create_directories(opt.workDir, ec);
    const fs::path dir = fs::absolute(fastcae::resolveWorkDir(opt.workDir, "", "ccx_run"));
    R.inpPath = (dir / (opt.jobName + ".inp")).string();
    R.frdPath = (dir / (opt.jobName + ".frd")).string();
    const std::string datPath = (dir / (opt.jobName + ".dat")).string();

    std::string err;
    const std::string deck = ccxDeck(mesh, tets10, spec, &err);
    if (deck.empty()) { R.message = err; return R; }
    {
        std::ofstream f(R.inpPath);
        if (!f) { R.message = "Khong ghi duoc " + R.inpPath; return R; }
        f << deck;
    }
    const std::string exe = findCalculix(opt.executable);
    if (exe.empty()) {
        R.message = "Khong tim thay CalculiX (ccx). Da ghi file " + R.inpPath + ". Cai ccx va dat bien moi truong FASTCAE_CCX hoac them vao PATH.";
        return R;
    }
    fs::remove(R.frdPath, ec);
    fs::remove(datPath, ec);
#ifdef _WIN32
    const std::string cmd = "cd /d \"" + dir.string() + "\" && \"" + exe + "\" " + opt.jobName + " > " + opt.jobName + ".log 2>&1";
#else
    const std::string cmd = "cd \"" + dir.string() + "\" && \"" + exe + "\" " + opt.jobName + " > " + opt.jobName + ".log 2>&1";
#endif
    const int rc = std::system(cmd.c_str());
    if (rc != 0 || !fs::exists(R.frdPath)) {
        R.message = "ccx that bai (ma " + std::to_string(rc) + "), xem " + (dir / (opt.jobName + ".log")).string();
        return R;
    }
    std::vector<FrdIncrement> inc;
    if (!readFrdIncrements(R.frdPath, inc, &err)) { R.message = err; return R; }

    const std::size_t n = mesh.numNodes();
    auto dispOf = [&](const FrdIncrement& f, std::vector<double>& u, std::vector<double>* mag, double* mx) {
        u.assign(3 * n, 0.0);
        if (mag) mag->assign(n, 0.0);
        if (mx) *mx = 0;
        auto it = f.blocks.find("DISP");
        if (it == f.blocks.end()) return false;
        for (const auto& [node, v] : it->second)
            if (node >= 0 && node < static_cast<int>(n) && v.size() >= 3) {
                for (int k = 0; k < 3; ++k) u[3 * node + k] = v[k];
                const double m = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (mag) (*mag)[node] = m;
                if (mx) *mx = std::max(*mx, m);
            }
        return true;
    };
    auto tempOf = [&](const FrdIncrement& f, std::vector<double>& T, double* mx) {
        T.assign(n, 0.0);
        if (mx) *mx = -1e300;
        auto it = f.blocks.find("NDTEMP");
        if (it == f.blocks.end()) return false;
        for (const auto& [node, v] : it->second)
            if (node >= 0 && node < static_cast<int>(n) && !v.empty()) { T[node] = v[0]; if (mx) *mx = std::max(*mx, v[0]); }
        return true;
    };
    auto stressOf = [&](const FrdIncrement& f, ElasticResult& e) {
        e.vonMisesNodal.assign(n, 0.0);
        e.maxVonMises = 0;
        auto it = f.blocks.find("STRESS");
        if (it == f.blocks.end()) return;
        for (const auto& [node, s] : it->second)
            if (node >= 0 && node < static_cast<int>(n) && s.size() >= 6) {
                const double vm = std::sqrt(0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) + (s[2] - s[0]) * (s[2] - s[0])) +
                                            3.0 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
                e.vonMisesNodal[node] = vm;
                e.maxVonMises = std::max(e.maxVonMises, vm);
            }
    };

    ElasticResult& e = R.elastic;
    const bool modal = spec.analysis == "frequency" || spec.analysis == "buckle";
    for (const auto& f : inc) {
        // ccx ghi U, S, NT... thành các khối riêng cùng một thời điểm: mỗi thời điểm chỉ tính một lần
        if (R.times.empty() || R.times.back() != f.value) R.times.push_back(f.value);
        std::vector<double> u, T;
        double mu = 0, mt = 0;
        if (dispOf(f, u, nullptr, &mu)) R.historyMaxDisp.push_back(mu);
        if (tempOf(f, T, &mt)) R.historyMaxTemp.push_back(mt);
        if (modal && !u.empty()) R.modeShapes.push_back(u);
    }
    const FrdIncrement& last = inc.back();
    if (modal) {
        if (spec.analysis == "frequency") {
            R.freqHz = parseDatTable(datPath, "E I G E N V A L U E", 3);   // MODE, EIGENVALUE, REAL (rad/s), CYCLES/TIME (Hz), IMAG
        } else {
            R.factors = parseDatTable(datPath, "B U C K L I N G", 1);
        }
        if (R.modeShapes.empty()) { R.message = "Khong doc duoc dang rieng tu .frd"; return R; }
        // mode hiển thị = dạng đầu tiên của bước cuối (với buckle, bỏ gia số tĩnh trước đó)
        const std::size_t want = spec.analysis == "buckle" ? std::min(R.modeShapes.size(), static_cast<std::size_t>(std::max(1, spec.modes))) : R.modeShapes.size();
        if (spec.analysis == "buckle" && R.modeShapes.size() > want) R.modeShapes.erase(R.modeShapes.begin(), R.modeShapes.begin() + (R.modeShapes.size() - want));
        for (std::size_t k = 0; k < R.modeShapes.size(); ++k) {
            CcxFrame fr;
            fr.u = R.modeShapes[k];
            fr.dispMagnitude.assign(n, 0.0);
            for (std::size_t i = 0; i < n; ++i)
                fr.dispMagnitude[i] = std::sqrt(fr.u[3 * i] * fr.u[3 * i] + fr.u[3 * i + 1] * fr.u[3 * i + 1] + fr.u[3 * i + 2] * fr.u[3 * i + 2]);
            const bool freq = spec.analysis == "frequency";
            const std::vector<double>& tab = freq ? R.freqHz : R.factors;
            fr.time = k < tab.size() ? tab[k] : 0.0;
            fr.label = "dang " + std::to_string(k + 1) + (k < tab.size() ? (freq ? " - " + std::to_string(tab[k]) + " Hz" : " - he so " + std::to_string(tab[k])) : "");
            R.frames.push_back(std::move(fr));
        }
        if (R.frames.size() < 2) R.frames.clear();
        e.u = R.modeShapes.front();
        e.dispMagnitude.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            e.dispMagnitude[i] = std::sqrt(e.u[3 * i] * e.u[3 * i] + e.u[3 * i + 1] * e.u[3 * i + 1] + e.u[3 * i + 2] * e.u[3 * i + 2]);
            e.maxDisplacement = std::max(e.maxDisplacement, e.dispMagnitude[i]);
        }
        if (R.freqHz.empty() && R.factors.empty()) { R.message = "Khong doc duoc bang tan so/he so tu .dat (xem " + datPath + ")"; return R; }
    } else {
        // ccx ghi U, S, NT... trong các khối " 100CL" riêng của cùng một gia số: lấy khối cuối có mặt của từng đại lượng.
        auto lastWith = [&](const char* name) -> const FrdIncrement* {
            for (auto it = inc.rbegin(); it != inc.rend(); ++it)
                if (it->blocks.count(name)) return &*it;
            return nullptr;
        };
        {   // khung theo mốc thời gian: gộp mọi khối cùng một giá trị thời gian
            std::map<double, FrdIncrement> byTime;
            for (const auto& f : inc) {
                auto& g = byTime[f.value];
                g.value = f.value;
                for (const auto& kv : f.blocks) g.blocks[kv.first] = kv.second;
            }
            if (byTime.size() >= 2) {
                for (const auto& [t, g] : byTime) {
                    CcxFrame fr;
                    fr.time = t;
                    fr.label = "t = " + std::to_string(t);
                    double mx = 0;
                    dispOf(g, fr.u, &fr.dispMagnitude, &mx);
                    if (g.blocks.count("STRESS")) { ElasticResult tmp; stressOf(g, tmp); fr.vonMises = std::move(tmp.vonMisesNodal); }
                    double mt = 0;
                    if (g.blocks.count("NDTEMP")) tempOf(g, fr.temperature, &mt);
                    if (g.blocks.count("DISP") || g.blocks.count("NDTEMP")) R.frames.push_back(std::move(fr));
                }
                if (R.frames.size() < 2) R.frames.clear();
                bool anyVm = false;
                for (const auto& fr : R.frames) if (!fr.vonMises.empty()) anyVm = true;
                if (anyVm)
                    for (const auto& fr : R.frames)
                        R.historyMaxVonMises.push_back(fr.vonMises.empty() ? 0.0 : *std::max_element(fr.vonMises.begin(), fr.vonMises.end()));
            }
        }
        double mu = 0;
        if (auto* f = lastWith("DISP")) dispOf(*f, e.u, &e.dispMagnitude, &mu);
        e.maxDisplacement = mu;
        if (auto* f = lastWith("STRESS")) stressOf(*f, e);
        double mt = 0;
        if (auto* f = lastWith("NDTEMP")) tempOf(*f, R.temperature, &mt);
        if (const FrdIncrement* f = lastWith("CONTACT")) {   // *CONTACT FILE: COPEN, CSLIP1, CSLIP2, CPRESS, CSHEAR1, CSHEAR2
            R.contactPressure.assign(n, 0.0);
            R.contactShear.assign(n, 0.0);
            for (const auto& [node, v] : f->blocks.at("CONTACT"))
                if (node >= 0 && node < static_cast<int>(n) && v.size() >= 6) {
                    R.contactPressure[node] = v[3];
                    R.contactShear[node] = std::hypot(v[4], v[5]);
                }
        }
        for (const char* nm : {"PEEQ", "PE"}) {
            const FrdIncrement* f = lastWith(nm);
            if (!f) continue;
            auto it = f->blocks.find(nm);
            R.peeq.assign(n, 0.0);
            for (const auto& [node, v] : it->second)
                if (node >= 0 && node < static_cast<int>(n) && !v.empty()) R.peeq[node] = v.back();
            break;
        }
    }
    e.linear.converged = true;
    R.ok = true;
    R.message = "CalculiX hoan tat";
    return R;
}

}  // namespace fastcae
