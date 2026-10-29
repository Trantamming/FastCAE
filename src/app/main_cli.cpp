// FastCAE command line front end.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <map>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <vector>

#include "app/case_runner.hpp"
#include "script/cxx_runner.hpp"
#include "solver/external_run.hpp"
#include "cad/cad_import.hpp"
#include "cad/param_cad.hpp"
#include "core/result_store.hpp"
#include "core/vtk_reader.hpp"
#include "core/history_csv.hpp"
#include "core/vtk_sequence.hpp"
#include <algorithm>
#include "core/about.hpp"
#include "core/case_file.hpp"
#include "cad/frame_templates.hpp"
#include "cad/shape_library.hpp"
#include "script/script_engine.hpp"
#include "solver/solver_registry.hpp"
#include "core/vtk_writer.hpp"

#ifndef FASTCAE_VERSION
#define FASTCAE_VERSION "0.1.0"
#endif

static void usage() {
    std::cout <<
        "FastCAE " FASTCAE_VERSION " - phan mem CAE ma nguon mo (tac gia: Tran Minh Tam)\n\n"
        "Cach dung:\n"
        "  fastcae-cli run <case.fcae>              Chay mot bai toan tu file case\n"
        "  fastcae-cli param <mo_hinh.fcad> [--set ten=gt] [--size h] [--out x.vtk] [--geo x.geo]   CAD tham so -> luoi\n"
        "  fastcae-cli result info|vtk|csv <ket_qua.fcres> [dau_ra] [--frame N] | result import <x.vtk> <ra.fcres>   Xem / xuat / nhap ket qua solver ngoai ket qua da luu (case: result_file = x.fcres)\n"
        "  fastcae-cli model <case.fcae> <ra.fcmodel.json>   Dung mo hinh (luoi, tap nut, vat lieu, lien ket, tai) ma khong giai: de solver ngoai / thuat toan tu lap trinh dung\n"
        "  fastcae-cli demo <cantilever|conduction|cavity> [thu_muc]\n"
        "                                           Chay vi du co san, ghi file .vtk\n"
        "  fastcae-cli import <model.step|stl|xao|msh|inp|unv|bdf|med|mesh|FCStd> [--size h_met] [--scale s] [--out file.vtk]\n"
        "                                           Nhap CAD va chia luoi khoi\n"
        "  fastcae-cli formats                      Liet ke dinh dang CAD ho tro\n"
        "  fastcae-cli solvers [--json]             Liet ke solver ma nguon mo, giay phep, trang thai cai dat\n"
        "  fastcae-cli solver-run <id> <input> [--dir D] [--args \"...\"] [--cmd lenh]...\n"
        "                                           Chay Code_Aster/OpenRadioss/SU2/OpenFOAM/CalculiX/Elmer tren file input\n"
        "  fastcae-cli cxx <file.cpp> [tham so...]  Bien dich va chay script C++ voi FastCAE SDK (khong co main() thi tu boc)\n"
        "  fastcae-cli run <case> --json            Ket qua dang JSON (cho Python va cong cu khac)\n"
        "  fastcae-cli script <file.lua> [tham so]  Chay script Lua (hoac: script -e \"code\")\n"
        "  fastcae-cli templates                    Liet ke thu vien cau kien (khoi dac va khung/dan 1D)\n"
        "  fastcae-cli template <ten> [tham so...]  In file case mau (de trong tham so = mac dinh)\n"
        "  fastcae-cli about                        Tac gia va giay phep\n"
        "  fastcae-cli licenses                     Giay phep thanh phan ben thu ba (da/chua doi chieu nguon goc)\n"
        "  fastcae-cli --version\n\n"
        "Kieu bai toan (type): elastic, thermal, cavity, frame, shell. Xem examples/ va docs/USER_GUIDE.md.\n";
}

// ---- JSON đầu ra cho `run --json` (để Python và công cụ khác dùng)
static std::string jsonEscape(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o + "\"";
}
static std::string jsonNum(double v) {
    if (!(v == v) || v > 1e308 || v < -1e308) return "null";
    char b[40];
    std::snprintf(b, sizeof b, "%.17g", v);
    return b;
}
static void printJson(const fastcae::RunReport& rep, const std::string& log) {
    std::cout << "{\"ok\":" << (rep.ok ? "true" : "false") << ",\"message\":" << jsonEscape(rep.message) << ",\"type\":" << jsonEscape(rep.type)
              << ",\"summary\":" << jsonEscape(rep.summary) << ",\"iterations\":" << rep.iterations << ",\"max_value\":" << jsonNum(rep.maxValue)
              << ",\"secondary\":" << jsonNum(rep.secondary) << ",\"nodes\":" << rep.mesh.numNodes() << ",\"cells\":" << rep.mesh.numCells()
              << ",\"log\":" << jsonEscape(log) << ",\"values\":{";
    bool first = true;
    for (const auto& kv : rep.values) { std::cout << (first ? "" : ",") << jsonEscape(kv.first) << ":" << jsonNum(kv.second); first = false; }
    std::cout << "},\"series\":{";
    first = true;
    for (const auto& kv : rep.series) {
        std::cout << (first ? "" : ",") << jsonEscape(kv.first) << ":[";
        for (std::size_t i = 0; i < kv.second.size(); ++i) std::cout << (i ? "," : "") << jsonNum(kv.second[i]);
        std::cout << "]";
        first = false;
    }
    std::cout << "},\"fields\":{";
    first = true;
    for (const auto& kv : rep.fields.pointScalars) {
        if (kv.second.empty()) continue;
        double mn = kv.second[0], mx = kv.second[0], sum = 0;
        for (double v : kv.second) { mn = std::min(mn, v); mx = std::max(mx, v); sum += v; }
        std::cout << (first ? "" : ",") << jsonEscape(kv.first) << ":{\"min\":" << jsonNum(mn) << ",\"max\":" << jsonNum(mx) << ",\"mean\":"
                  << jsonNum(sum / static_cast<double>(kv.second.size())) << "}";
        first = false;
    }
    std::cout << "},\"vector_fields\":[";
    first = true;
    for (const auto& kv : rep.fields.pointVectors) { std::cout << (first ? "" : ",") << jsonEscape(kv.first); first = false; }
    std::cout << "]}\n";
}

int main(int argc, char** argv) {
    using namespace fastcae;
    if (argc < 2 || !std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h")) {
        usage();
        return argc < 2 ? 1 : 0;
    }
    const std::string cmd = argv[1];

    if (cmd == "--version") {
        std::cout << "FastCAE " FASTCAE_VERSION "\n" << about::kCopyright << " - " << about::kLicense << "\n";
        return 0;
    }
    if (cmd == "templates") {
        for (const auto& s : shapeCatalog()) {
            std::cout << s.name << " | " << s.category << " | " << s.label << " |";
            for (const auto& p : s.params) std::cout << ' ' << p.key << '=' << p.def;
            std::cout << "\n";
        }
        for (const auto& s : shellTemplateCatalog()) {
            std::cout << s.name << " | " << s.category << " | " << s.label << " |";
            for (const auto& p : s.params) std::cout << ' ' << p.key << '=' << p.def;
            std::cout << "\n";
        }
        for (const auto& s : frameTemplateCatalog()) {
            std::cout << s.name << " | " << s.category << " | " << s.label << " |";
            for (const auto& p : s.params) std::cout << ' ' << p.key << '=' << p.def;
            std::cout << "\n";
        }
        return 0;
    }
    if (cmd == "template") {
        if (argc < 3) { std::cerr << "Dung: fastcae-cli template <ten> [tham so...]\n"; return 2; }
        const std::string name = argv[2];
        std::vector<double> p;
        for (int i = 3; i < argc; ++i) p.push_back(std::atof(argv[i]));
        const ShapeInfo* info = findShape(name);
        if (!info) info = findFrameTemplate(name);
        if (!info) info = findShellTemplate(name);
        if (!info) { std::cerr << "Khong co mau: " << name << " (xem: fastcae-cli templates)\n"; return 2; }
        if (p.empty()) for (const auto& q : info->params) p.push_back(q.def);
        std::string err;
        const std::string text = findShape(name)           ? shapeCaseText(name, p)
                                 : findFrameTemplate(name) ? frameTemplateText(name, p, err)
                                                           : shellTemplateText(name, p, err);
        if (text.empty()) { std::cerr << err << "\n"; return 2; }
        std::cout << text;
        return 0;
    }
    if (cmd == "about") {
        std::cout << "FastCAE " FASTCAE_VERSION "\nTac gia: " << about::kAuthor << "\n" << about::kCopyright << "\n"
                  << "Giay phep: " << about::kLicense << " (xem LICENSE, THIRD_PARTY_NOTICES.txt)\n" << about::kHomepage << "\n";
        return 0;
    }
    if (cmd == "licenses") {
        std::cout << licenseReport();
        return 0;
    }
    if (cmd == "solvers" && argc >= 3 && !std::strcmp(argv[2], "--json")) {
        std::cout << solversJson() << "\n";
        return 0;
    }
    if (cmd == "solver-run" && argc >= 4) {
        ExternalRunOptions o;
        o.solverId = argv[2];
        o.input = argv[3];
        for (int i = 4; i + 1 < argc; i += 2) {
            const std::string k = argv[i];
            if (k == "--dir") o.workDir = argv[i + 1];
            else if (k == "--args") o.extraArgs = argv[i + 1];
            else if (k == "--override") o.argsOverride = argv[i + 1];
            else if (k == "--exe") o.executableHint = argv[i + 1];
            else if (k == "--cmd") o.commands.push_back(argv[i + 1]);
        }
        o.onOutput = [](const std::string& t) { std::cout << t << std::flush; };
        const auto r = runExternalSolver(o);
        std::cout << (r.ok ? "[OK] " : "[LOI] ") << r.message << "\n";
        for (const auto& f : r.files) std::cout << "FILE " << f << "\n";
        return r.ok ? 0 : 1;
    }
    if (cmd == "cxx" && argc >= 3) {
        std::vector<std::string> a;
        for (int i = 3; i < argc; ++i) a.push_back(argv[i]);
        std::error_code ec;
        const auto exeDir = std::filesystem::path(argv[0]).has_parent_path()
                                ? std::filesystem::absolute(std::filesystem::path(argv[0]), ec).parent_path().string() : std::string();
        const auto r = buildAndRunCxx(argv[2], exeDir, a, [](const std::string& t) { std::cout << t << std::flush; });
        if (!r.ok) std::cerr << "[LOI] " << r.message << "\n";
        return r.ok ? 0 : 1;
    }
    if (cmd == "solvers") {
        for (const auto& s : allSolvers()) {
            std::string state = "-";
            if (s.integration == Integration::Builtin) state = "co san";
            else if (!s.steps.empty()) state = solverRuntimePath(s).empty() ? "chua tim thay" : solverRuntimePath(s);
            std::cout << s.id << " | " << s.name << " | " << s.license << " | " << copyleftName(s.copyleft) << " | "
                      << integrationName(s.integration) << " | " << state << "\n";
        }
        return 0;
    }
    if (cmd == "formats") {
        std::cout << "Nhan CAD (Gmsh + OpenCASCADE) trong chuong trinh: " << (cadKernelAvailable() ? "co" : "khong") << "\n"
                  << "Chuong trinh gmsh ngoai (PATH / FASTCAE_GMSH): "
                  << (findGmshExecutable().empty() ? "khong tim thay" : findGmshExecutable()) << "\n"
                  << "Doc truc tiep, khong can Gmsh: luoi .msh (ASCII 2.2 / 4.1), luoi Abaqus/CalculiX .inp (C3D4/10/8/20/6/15)\n"
                  << "Doc qua Gmsh (can nhan CAD hoac gmsh ngoai):\n"
                  << "  B-Rep      : STEP (.step .stp), IGES (.iges .igs), BREP (.brep .brp), XAO (.xao)\n"
                  << "  Be mat     : STL (.stl) -> dung khoi roi chia luoi (STL phai kin)\n"
                  << "  Luoi khoi  : I-deas (.unv), Nastran (.bdf .nas), MED (.med), Medit (.mesh), .msh nhi phan\n"
                  << "Phan tu khoi doc duoc: tu dien (4/10 nut), luc dien, lang tru, thap (tuyen tinh hoac bac 2) -> doi ra tu dien\n"
                  << "Qua FreeCAD (freecadcmd trong PATH / FASTCAE_FREECAD): .FCStd -> STEP: "
                  << (findFreeCadExecutable().empty() ? "khong tim thay" : findFreeCadExecutable()) << "\n"
                  << "Dinh dang dong (SLDPRT, IPT, CATPart, PRT, F3D, DWG, X_T...): xuat STEP tu phan mem goc, hoac dat\n"
                  << "  FASTCAE_CAD_CONVERTER = lenh co {in} va {out} (hien tai: "
                  << (cadConverterCommand().empty() ? "chua dat" : cadConverterCommand()) << ")\n";
        return 0;
    }
    if (cmd == "script" && argc >= 3) {   // fastcae-cli script file.lua [tham so...]   |   fastcae-cli script -e "code"
        ScriptEngine eng;
        eng.setPrinter([](const std::string& s) { std::cout << s << std::flush; });
        ScriptOutcome out;
        if (std::string(argv[2]) == "-e" && argc >= 4) {
            eng.setArguments("-e", {});
            out = eng.runString(argv[3], "-e");
        } else {
            std::vector<std::string> args;
            for (int i = 3; i < argc; ++i) args.push_back(argv[i]);
            eng.setArguments(argv[2], args);
            out = eng.runFile(argv[2]);
        }
        if (!out.ok) { std::cerr << "Loi script: " << out.error << "\n"; return 1; }
        return 0;
    }
    if (cmd == "model" && argc >= 4) {   // fastcae-cli model case.fcae ra.fcmodel.json : dựng mô hình, không giải
        CaseFile cf;
        std::string err;
        if (!cf.load(argv[2], &err)) { std::cerr << err << "\n"; return 2; }
        const auto base = std::filesystem::path(argv[2]).parent_path().string();
        if (!exportModel(cf, std::cout, base, argv[3], err)) { std::cerr << "[LOI] " << err << "\n"; return 1; }
        return 0;
    }
    if (cmd == "run" && argc >= 3) {
        CaseFile cf;
        std::string err;
        if (!cf.load(argv[2], &err)) { std::cerr << err << "\n"; return 2; }
        const auto base = std::filesystem::path(argv[2]).parent_path().string();
        if (argc >= 4 && std::string(argv[3]) == "--json") {   // một đối tượng JSON duy nhất trên stdout
            std::ostringstream logStream;
            auto rep = runCase(cf, logStream, base);
            printJson(rep, logStream.str());
            return rep.ok ? 0 : 1;
        }
        auto rep = runCase(cf, std::cout, base);
        std::cout << "Ket qua: " << rep.message << " (" << rep.iterations << " vong lap)\n";
        if (rep.type == "elastic")
            std::cout << "  |u|max = " << rep.maxValue << " m,  von Mises max = " << rep.secondary << " Pa\n";
        else if (rep.type == "thermal")
            std::cout << "  Tmax = " << rep.maxValue << ",  Tmin = " << rep.secondary << "\n";
        return rep.ok ? 0 : 1;
    }
    if (cmd == "demo" && argc >= 3) {
        const std::string text = demoCaseText(argv[2]);
        if (text.empty()) { std::cerr << "Vi du khong ton tai: " << argv[2] << "\n"; return 2; }
        CaseFile cf;
        cf.parse(text);
        // Redirect output into the requested directory.
        if (argc >= 4) {
            std::filesystem::create_directories(argv[3]);
            const std::string out = (std::filesystem::path(argv[3]) / cf.get("output")).string();
            std::string t = text;
            auto pos = t.find("output = ");
            t = t.substr(0, pos) + "output = " + out + "\n";
            cf.parse(t);
        }
        auto rep = runCase(cf, std::cout);
        std::cout << "Ket qua: " << rep.message << " (" << rep.iterations << " vong lap)\n";
        if (rep.type == "elastic") {
            // Beam theory: delta = P L^3 / (3 E I), I = b h^3 / 12.
            const double P = 1000, L = 1.0, E = 210e9, I = 0.1 * 0.1 * 0.1 * 0.1 / 12.0;
            std::cout << "  |u|max (FEM)  = " << rep.maxValue << " m\n"
                      << "  Ly thuyet dam = " << P * L * L * L / (3 * E * I) << " m\n"
                      << "  von Mises max = " << rep.secondary << " Pa\n"
                      << "  Ung suat goc dam (ly thuyet) = " << 6.0e6 << " Pa\n"
                      << "  (order = 2: tu dien bac 2. Voi order = 1, tu dien 4 nut chi ra ~60% do vong - xem docs/THEORY.md)\n";
        } else if (rep.type == "thermal") {
            std::cout << "  Tmax = " << rep.maxValue << ", Tmin = " << rep.secondary << "\n";
        }
        return rep.ok ? 0 : 1;
    }
    if (cmd == "import" && argc >= 3) {
        CadImportOptions opt;
        std::string out = "imported.vtk";
        for (int i = 3; i + 1 < argc; i += 2) {
            if (!std::strcmp(argv[i], "--size")) opt.meshSize = std::atof(argv[i + 1]);
            else if (!std::strcmp(argv[i], "--scale")) opt.scaleToMeters = std::atof(argv[i + 1]);
            else if (!std::strcmp(argv[i], "--out")) out = argv[i + 1];
        }
        auto r = importCad(argv[2], opt);
        if (!r.ok) { std::cerr << r.message << "\n"; return 1; }
        std::cout << "Luoi: " << r.mesh.numNodes() << " nut, " << r.mesh.numCells()
                  << " phan tu, the tich = " << r.mesh.totalVolume() << "\n";
        std::string err;
        if (!writeVtk(out, r.mesh, {}, &err)) { std::cerr << err << "\n"; return 1; }
        std::cout << "Da ghi: " << out << "\n";
        return 0;
    }
    if (cmd == "result" && argc >= 5 && !std::strcmp(argv[2], "import")) {
        // fastcae-cli result import <ket_qua.vtk> <ra.fcres> : kết quả solver ngoài (OpenFOAM/SU2/Code_Aster...) -> .fcres
        StoredResult sr; std::string e; VtkReadInfo vi;
        if (!readVtk(argv[3], sr.mesh, sr.fields, &vi, &e)) { std::cerr << e << "\n"; return 1; }
        sr.type = "external";
        sr.summary = vi.extruded2d ? "Ket qua 2D, hien thi duoi dang lop mong day " + std::to_string(vi.extrudeThickness) + " m" : "Ket qua nhap tu VTK";
        sr.values["nodes"] = double(sr.mesh.numNodes());
        sr.values["cells"] = double(sr.mesh.numCells());
        // chuỗi bước (flow_00000.vtk, flow_00002.vtk...): nạp mọi khung cùng lưới
        {
            const auto seq = findVtkSequence(argv[3]);
            if (seq.size() >= 2) {
                int ok = 0, skipped = 0;
                for (const auto& fr : seq) {
                    Mesh m2; VtkFields f2; std::string e2;
                    if (!readVtk(fr.path, m2, f2, nullptr, &e2) || m2.nodes.size() != sr.mesh.nodes.size()) { ++skipped; continue; }
                    StoredFrame sf;
                    sf.label = std::filesystem::path(fr.path).stem().string();
                    sf.fields = std::move(f2);
                    if (fr.path == std::string(argv[3]) || std::filesystem::equivalent(fr.path, argv[3])) sr.currentFrame = ok;
                    sr.frames.push_back(std::move(sf));
                    ++ok;
                }
                if (ok >= 2) { sr.fields = sr.frames[sr.currentFrame].fields; std::cout << "Da nap chuoi buoc: " << ok << " khung" << (skipped ? " (bo qua " + std::to_string(skipped) + " khung khac luoi)" : "") << "\n"; }
                else sr.frames.clear();
            }
        }
        std::string hist = findHistoryBeside(argv[3]);
        for (int i = 5; i + 1 < argc; i += 2) if (!std::strcmp(argv[i], "--history")) hist = argv[i + 1];
        if (!hist.empty()) {
            std::string he;
            if (readHistoryCsv(hist, sr.series, &he)) std::cout << "Da nap lich su hoi tu: " << hist << " (" << sr.series.size() << " day so)\n";
            else std::cerr << "CANH BAO: " << he << "\n";
        }
        if (!saveResult(argv[4], sr, &e)) { std::cerr << e << "\n"; return 1; }
        std::cout << "Da nhap " << argv[3] << ": " << sr.mesh.numNodes() << " nut, " << sr.mesh.numCells() << " tu dien"
                  << (vi.extruded2d ? " (luoi 2D duoc dun thanh lop mong)" : "") << "\nDa ghi: " << argv[4] << "\n";
        return 0;
    }
    if (cmd == "result" && argc >= 4) {
        // fastcae-cli result info|vtk|csv <file.fcres> [dau_ra]
        StoredResult sr; std::string e;
        if (!loadResult(argv[3], sr, &e)) { std::cerr << e << "\n"; return 1; }
        const std::string sub = argv[2];
        // --frame N (từ 1): xuất khung thứ N của chuỗi thay cho khung đang hiển thị
        for (int i = 5; i + 1 < argc; ++i)
            if (!std::strcmp(argv[i], "--frame")) {
                const int k = std::atoi(argv[i + 1]);
                if (k < 1 || k > static_cast<int>(sr.frames.size())) { std::cerr << "--frame ngoai khoang 1.." << sr.frames.size() << "\n"; return 1; }
                sr.fields = sr.frames[static_cast<std::size_t>(k - 1)].fields;
            }
        if (sub == "info") {
            std::cout << "Loai: " << sr.type << "\nNut: " << sr.mesh.numNodes() << "  Phan tu: " << sr.mesh.numCells() << "\n";
            for (const auto& kv : sr.fields.pointScalars) {
                double lo = 1e300, hi = -1e300;
                for (double v : kv.second) { lo = std::min(lo, v); hi = std::max(hi, v); }
                std::cout << "  truong nut (vo huong) " << kv.first << ": [" << lo << ", " << hi << "]\n";
            }
            for (const auto& kv : sr.fields.pointVectors) std::cout << "  truong nut (vector) " << kv.first << "\n";
            for (const auto& kv : sr.fields.cellScalars) std::cout << "  truong phan tu " << kv.first << "\n";
            for (const auto& kv : sr.values) std::cout << "  " << kv.first << " = " << kv.second << "\n";
            if (!sr.frames.empty()) std::cout << "  chuoi buoc: " << sr.frames.size() << " khung (" << sr.frames.front().label << " ... " << sr.frames.back().label << ")\n";
            for (const auto& kv : sr.series) if (!kv.second.empty()) std::cout << "  day so " << kv.first << ": " << kv.second.size() << " diem, cuoi = " << kv.second.back() << "\n";
            if (!sr.summary.empty()) std::cout << sr.summary << "\n";
            return 0;
        }
        if ((sub == "vtk" || sub == "csv") && argc >= 5) {
            const bool ok = sub == "vtk" ? writeVtk(argv[4], sr.mesh, sr.fields, &e) : exportNodesCsv(argv[4], sr, &e);
            if (!ok) { std::cerr << e << "\n"; return 1; }
            std::cout << "Da ghi: " << argv[4] << "\n";
            return 0;
        }
    }
    if (cmd == "param" && argc >= 3) {
        // fastcae-cli param model.fcad [--set ten=gia_tri]... [--size h] [--order 1|2] [--out mesh.vtk] [--geo model.geo]
        std::map<std::string, double> ov;
        double size = 0; int order = 1; std::string out, geoOut;
        for (int i = 3; i + 1 < argc; i += 2) {
            if (!std::strcmp(argv[i], "--set")) {
                const std::string kv = argv[i + 1];
                const auto eq = kv.find('=');
                if (eq == std::string::npos) { std::cerr << "--set ten=gia_tri\n"; return 1; }
                double v; std::string e;
                if (!evalExpression(kv.substr(eq + 1), ov, v, e)) { std::cerr << e << "\n"; return 1; }
                ov[kv.substr(0, eq)] = v;
            } else if (!std::strcmp(argv[i], "--size")) size = std::atof(argv[i + 1]);
            else if (!std::strcmp(argv[i], "--order")) order = std::atoi(argv[i + 1]);
            else if (!std::strcmp(argv[i], "--out")) out = argv[i + 1];
            else if (!std::strcmp(argv[i], "--geo")) geoOut = argv[i + 1];
        }
        std::ifstream f(argv[2]);
        if (!f) { std::cerr << "Khong mo duoc " << argv[2] << "\n"; return 1; }
        std::stringstream buf; buf << f.rdbuf();
        const auto r = meshParamModel(buf.str(), ov, size, order);
        if (!geoOut.empty()) { std::ofstream g(geoOut); g << r.geo; }
        for (const auto& w : r.warnings) std::cerr << "CANH BAO: " << w << "\n";
        if (!r.ok) { std::cerr << r.message << "\n"; return 1; }
        std::cout << r.message << "\nThe tich = " << r.mesh.totalVolume() << "\n";
        for (const auto& kv : r.parameters) std::cout << "  " << kv.first << " = " << kv.second << "\n";
        if (!out.empty()) {
            std::string err;
            if (!writeVtk(out, r.mesh, {}, &err)) { std::cerr << err << "\n"; return 1; }
            std::cout << "Da ghi: " << out << "\n";
        }
        return 0;
    }
    usage();
    return 1;
}
