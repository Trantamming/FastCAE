// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "cad/cad_import.hpp"
#include "cad/shape_library.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>

#include "mesh/box_mesher.hpp"

#ifdef FASTCAE_HAVE_GMSH
#include <gmsh.h>
#endif

namespace fastcae {

static std::string lowerExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    return e;
}

CadFormat detectCadFormat(const std::string& path) {
    const std::string e = lowerExt(path);
    if (e == "step" || e == "stp") return CadFormat::Step;
    if (e == "iges" || e == "igs") return CadFormat::Iges;
    if (e == "brep" || e == "brp") return CadFormat::Brep;
    if (e == "xao") return CadFormat::Xao;
    if (e == "msh") return CadFormat::Msh;
    if (e == "unv" || e == "inp" || e == "bdf" || e == "nas" || e == "med" || e == "mesh")
        return CadFormat::ForeignMesh;
    if (e == "stl") return CadFormat::Stl;
    if (e == "fcstd") return CadFormat::FreeCad;
    if (e == "sldprt" || e == "sldasm" || e == "ipt" || e == "iam" || e == "catpart" || e == "catproduct" ||
        e == "prt" || e == "asm" || e == "f3d" || e == "dwg" || e == "dxf" || e == "x_t" || e == "x_b" ||
        e == "sat" || e == "3dm" || e == "par" || e == "psm" || e == "jt" || e == "3dxml" || e == "neu")
        return CadFormat::Native;
    return CadFormat::Unknown;
}

const std::vector<std::string>& cadImportExtensions() {
    static const std::vector<std::string> v = {"step", "stp", "iges", "igs", "brep", "brp", "xao", "stl", "fcstd",
                                               "msh",  "unv", "inp",  "bdf", "nas",  "med", "mesh"};
    return v;
}

std::string cadConverterCommand() {
    const char* c = std::getenv("FASTCAE_CAD_CONVERTER");
    return c ? std::string(c) : std::string();
}

bool canImportFormat(const std::string& path) {
    switch (detectCadFormat(path)) {
        case CadFormat::Native:
        case CadFormat::Unknown:
            return !cadConverterCommand().empty();
        default:
            return true;
    }
}

std::string cadFormatAdvice(const std::string& path) {
    switch (detectCadFormat(path)) {
        case CadFormat::Step:
        case CadFormat::Iges:
        case CadFormat::Brep:
        case CadFormat::Xao:
            return "Dinh dang hinh hoc B-Rep: FastCAE doc truc tiep roi chia luoi khoi (can Gmsh/OpenCASCADE).";
        case CadFormat::Msh:
            return "File luoi Gmsh (.msh, ASCII 2.2 hoac 4.1): FastCAE doc truc tiep.";
        case CadFormat::ForeignMesh:
            return "File luoi khoi tu phan mem khac (.inp Abaqus/CalculiX, .unv I-deas, .bdf/.nas Nastran, .med, .mesh Medit): "
                   "FastCAE doc truc tiep (.inp) hoac qua Gmsh (cac loai con lai). File phai co phan tu KHOI (tu dien/luc dien/lang tru/thap).";
        case CadFormat::Stl:
            return "STL la luoi be mat: FastCAE dung Gmsh dung khoi tu cac mat roi chia luoi (can STL kin, khong ho, "
                   "tot nhat khi mo hinh co canh sac).";
        case CadFormat::FreeCad:
            return "FreeCAD .FCStd: FastCAE goi freecadcmd (dat FASTCAE_FREECAD hoac them FreeCAD vao PATH) de doi sang STEP.";
        case CadFormat::Native: {
            const std::string e = lowerExt(path);
            std::string src = "phan mem goc";
            if (e == "sldprt" || e == "sldasm") src = "SolidWorks";
            else if (e == "ipt" || e == "iam") src = "Inventor";
            else if (e == "dwg" || e == "dxf") src = "AutoCAD / Mechanical Desktop";
            else if (e == "catpart" || e == "catproduct") src = "CATIA";
            else if (e == "prt" || e == "asm") src = "NX / Pro-ENGINEER / Creo";
            else if (e == "f3d") src = "Fusion 360";
            return "Dinh dang goc cua " + src +
                   " la dinh dang dong (khong co dac ta mo) nen FastCAE khong doc truc tiep. Cach 1: dung Save As / Export "
                   "trong " + src + " de xuat sang STEP (AP203/AP214/AP242) roi nhap vao FastCAE. Cach 2: neu ban co bo "
                   "chuyen doi (CAD Exchanger, FreeCAD, Datakit...), dat bien moi truong FASTCAE_CAD_CONVERTER = lenh co {in} "
                   "va {out}, vi du: mytool --in \"{in}\" --out \"{out}\" ({out} la file .step).";
        }
        default:
            return "Dinh dang khong duoc ho tro. Dung STEP, IGES, BREP, XAO, STL, hoac luoi .msh/.inp/.unv/.bdf/.med/.mesh.";
    }
}

bool cadKernelAvailable() {
#ifdef FASTCAE_HAVE_GMSH
    return true;
#else
    return false;
#endif
}

#ifdef FASTCAE_HAVE_GMSH
namespace {

struct GmshSession {
    GmshSession() {
        if (!gmsh::isInitialized()) gmsh::initialize();
        gmsh::option::setNumber("General.Terminal", 0);
        gmsh::model::add("fastcae");
    }
    ~GmshSession() {
        gmsh::clear();
        gmsh::finalize();
    }
};

CadImportResult extractMesh(double scale) {
    CadImportResult res;
    std::vector<std::size_t> tags;
    std::vector<double> coord, param;
    gmsh::model::mesh::getNodes(tags, coord, param);
    std::unordered_map<std::size_t, int> remap;
    remap.reserve(tags.size());
    for (std::size_t i = 0; i < tags.size(); ++i) {
        remap[tags[i]] = static_cast<int>(i);
        res.mesh.nodes.push_back({coord[3 * i] * scale, coord[3 * i + 1] * scale, coord[3 * i + 2] * scale});
    }
    std::vector<std::size_t> etags, ntags;
    gmsh::model::mesh::getElementsByType(4, etags, ntags);  // 4 = 4-node tetrahedron
    for (std::size_t e = 0; e < etags.size(); ++e)
        res.mesh.tets.push_back({remap[ntags[4 * e]], remap[ntags[4 * e + 1]], remap[ntags[4 * e + 2]],
                                 remap[ntags[4 * e + 3]]});
    res.mesh.fixOrientation();
    res.ok = !res.mesh.tets.empty();
    res.message = res.ok ? "OK" : "Gmsh khong tao duoc luoi khoi (hinh hoc khong dong kin?)";
    return res;
}

// `sizeMeters` is in output units (metres); the CAD file is in its own units
// (`cadUnitToMeters`, e.g. 0.001 for mm). Returns false (with a message) if the
// requested size would produce an unreasonable number of elements.
bool setMeshSize(double sizeMeters, double cadUnitToMeters, std::string& err) {
    double xmin, ymin, zmin, xmax, ymax, zmax;
    gmsh::model::getBoundingBox(-1, -1, xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
    const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double unit = cadUnitToMeters > 0 ? cadUnitToMeters : 1.0;
    const double h = sizeMeters > 0 ? sizeMeters / unit : diag / 15.0;   // in CAD units
    // Rough element count: ~6 tets per cube of side h (bounding box, so an over-estimate for thin parts).
    const double est = 6.0 * std::max(dx, 1e-30 * diag) * std::max(dy, 1e-30 * diag) * std::max(dz, 1e-30 * diag) / (h * h * h);
    if (h <= 0 || est > 4e6) {
        err = "Kich thuoc luoi qua nho so voi mo hinh (uoc tinh > 4 trieu phan tu). Mo hinh dai " +
              std::to_string(diag * unit) + " m (duong cheo); hay tang mesh_size (don vi: met). " +
              "Neu file CAD tinh bang mm, nho dat unit_scale = 0.001.";
        return false;
    }
    gmsh::option::setNumber("Mesh.MeshSizeMax", h);
    return true;
}

}  // namespace
#endif


// ---------------------------------------------------------------- helpers: tim chuong trinh ngoai
static std::string findProgram(const char* envVar, const std::vector<std::string>& names) {
    namespace fs = std::filesystem;
    if (envVar) {
        if (const char* env = std::getenv(envVar)) {
            std::error_code ec;
            if (fs::exists(env, ec)) return env;
        }
    }
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return {};
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::stringstream ss(pathEnv);
    std::string dir;
    while (std::getline(ss, dir, sep))
        for (const auto& n : names) {
            std::error_code ec;
            fs::path p = fs::path(dir) / n;
            if (!dir.empty() && fs::exists(p, ec) && fs::is_regular_file(p, ec)) return p.string();
        }
    return {};
}

std::string findGmshExecutable() {
#ifdef _WIN32
    return findProgram("FASTCAE_GMSH", {"gmsh.exe"});
#else
    return findProgram("FASTCAE_GMSH", {"gmsh"});
#endif
}

std::string findFreeCadExecutable() {
    return findProgram("FASTCAE_FREECAD", {"FreeCADCmd.exe", "freecadcmd.exe", "FreeCADCmd", "freecadcmd", "FreeCAD.exe"});
}

static std::string makeTempDir() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::random_device rd;
    const fs::path dir = fs::temp_directory_path(ec) / ("fastcae_cad_" + std::to_string(rd()) + std::to_string(rd()));
    fs::create_directories(dir, ec);
    return dir.string();
}

// Chay mot dong lenh (cmd.exe tren Windows can them cap ngoac kep ngoai cung). Tra ve ma thoat.
static int runCommand(std::string cmd) {
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif
    return std::system(cmd.c_str());
}

static std::string slashes(std::string p) {   // duong dan cho script .geo: dau \ la ky tu thoat
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

static std::string logErrors(const std::string& logPath) {
    std::string tail, line;
    std::ifstream lg(logPath);
    while (std::getline(lg, line))
        if (line.find("Error") != std::string::npos) tail += line + "\n";
    return tail;
}

// ---------------------------------------------------------------- Gmsh .msh reader
namespace {

// Cac mat (chi so cuc bo cua nut goc) cua phan tu khoi tuyen tinh. Khoi duoc cat thanh tu dien quanh mot nut tam.
const std::vector<std::vector<int>> kHexFaces = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
const std::vector<std::vector<int>> kPrismFaces = {{0, 2, 1}, {3, 4, 5}, {0, 1, 4, 3}, {1, 2, 5, 4}, {2, 0, 3, 5}};
const std::vector<std::vector<int>> kPyramidFaces = {{0, 3, 2, 1}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};

// Cat khoi loi thanh tu dien: moi mat tam giac -> 1 tu dien, mat tu giac -> 2 (duong cheo xuat phat tu nut co chi so
// nho nhat, nen hai phan tu ke nhau chon cung duong cheo va luoi van lien tuc).
void addPolyhedron(Mesh& m, const int* c, int nc, const std::vector<std::vector<int>>& faces) {
    Vec3 g;
    for (int i = 0; i < nc; ++i) {
        g.x += m.nodes[static_cast<std::size_t>(c[i])].x;
        g.y += m.nodes[static_cast<std::size_t>(c[i])].y;
        g.z += m.nodes[static_cast<std::size_t>(c[i])].z;
    }
    g.x /= nc; g.y /= nc; g.z /= nc;
    const int center = static_cast<int>(m.nodes.size());
    m.nodes.push_back(g);
    for (const auto& f : faces) {
        if (f.size() == 3) {
            m.tets.push_back({c[f[0]], c[f[1]], c[f[2]], center});
        } else {
            int k = 0;
            for (int i = 1; i < 4; ++i)
                if (c[f[static_cast<std::size_t>(i)]] < c[f[static_cast<std::size_t>(k)]]) k = i;
            const int q0 = c[f[static_cast<std::size_t>(k)]], q1 = c[f[static_cast<std::size_t>((k + 1) % 4)]],
                      q2 = c[f[static_cast<std::size_t>((k + 2) % 4)]], q3 = c[f[static_cast<std::size_t>((k + 3) % 4)]];
            m.tets.push_back({q0, q1, q2, center});
            m.tets.push_back({q0, q2, q3, center});
        }
    }
}

// So nut cua tung kieu phan tu Gmsh (-1 = khong biet). Cac nut goc luon nam dau danh sach.
int gmshNodesOf(int type) {
    switch (type) {
        case 1: return 2;   case 2: return 3;   case 3: return 4;   case 4: return 4;   case 5: return 8;
        case 6: return 6;   case 7: return 5;   case 8: return 3;   case 9: return 6;   case 10: return 9;
        case 11: return 10; case 12: return 27; case 13: return 18; case 14: return 14; case 15: return 1;
        case 16: return 8;  case 17: return 20; case 18: return 15; case 19: return 13; case 20: return 9;
        case 21: return 10; case 22: return 12; case 23: return 15; case 24: return 15; case 25: return 21;
        case 26: return 4;  case 27: return 5;  case 28: return 6;  case 29: return 20; case 30: return 35;
        case 31: return 56; case 92: return 64; case 93: return 125;
        default: return -1;
    }
}

// Them mot phan tu khoi vao luoi (bo qua phan tu duong/mat). Tra ve false neu thieu nut.
void addVolumeCell(Mesh& m, int type, const std::vector<int>& idx) {
    switch (type) {
        case 4: case 11: case 29: case 30: case 31:
            m.tets.push_back({idx[0], idx[1], idx[2], idx[3]});
            break;
        case 5: case 12: case 17: case 92: case 93:
            addPolyhedron(m, idx.data(), 8, kHexFaces);
            break;
        case 6: case 13: case 18:
            addPolyhedron(m, idx.data(), 6, kPrismFaces);
            break;
        case 7: case 14: case 19:
            addPolyhedron(m, idx.data(), 5, kPyramidFaces);
            break;
        default: break;
    }
}

bool isVolumeType(int type) {
    switch (type) {
        case 4: case 11: case 29: case 30: case 31: case 5: case 12: case 17: case 92: case 93:
        case 6: case 13: case 18: case 7: case 14: case 19:
            return true;
        default: return false;
    }
}

// Bo cac nut khong dung (nut giua canh cua phan tu bac 2, nut mat/duong) de ma tran cung khong bi suy bien.
void dropUnusedNodes(Mesh& m) {
    std::vector<int> map(m.nodes.size(), -1);
    for (const auto& t : m.tets)
        for (int v : t) map[static_cast<std::size_t>(v)] = 0;
    std::vector<Vec3> kept;
    kept.reserve(m.nodes.size());
    for (std::size_t i = 0; i < m.nodes.size(); ++i)
        if (map[i] == 0) { map[i] = static_cast<int>(kept.size()); kept.push_back(m.nodes[i]); }
    for (auto& t : m.tets)
        for (int& v : t) v = map[static_cast<std::size_t>(v)];
    m.nodes = std::move(kept);
}

}  // namespace

bool readGmshMsh(const std::string& path, Mesh& mesh, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    std::ifstream is(path);
    if (!is) return fail("Khong mo duoc file luoi: " + path);
    mesh = Mesh{};
    std::unordered_map<std::size_t, int> remap;
    std::string tok;
    double version = 0;
    int fileType = 0;
    std::size_t surfaceOnly = 0;
    std::vector<std::size_t> nd;
    std::vector<int> idx;
    auto addCell = [&](int type) -> bool {   // nd da co cac nut cua phan tu; tra ve false neu thieu nut
        if (!isVolumeType(type)) { ++surfaceOnly; return true; }
        idx.assign(nd.size(), 0);
        for (std::size_t k = 0; k < nd.size(); ++k) {
            auto it = remap.find(nd[k]);
            if (it == remap.end()) return false;
            idx[k] = it->second;
        }
        addVolumeCell(mesh, type, idx);
        return true;
    };
    while (is >> tok) {
        if (tok == "$MeshFormat") {
            int dataSize = 0;
            is >> version >> fileType >> dataSize;
            if (fileType != 0) return fail("Chi doc duoc file .msh dang ASCII (Gmsh: Mesh.Binary = 0).");
        } else if (tok == "$Nodes") {
            if (version >= 4.0) {
                std::size_t blocks, total, minTag, maxTag;
                is >> blocks >> total >> minTag >> maxTag;
                for (std::size_t b = 0; b < blocks; ++b) {
                    int dim, entity, parametric;
                    std::size_t n;
                    is >> dim >> entity >> parametric >> n;
                    std::vector<std::size_t> tags(n);
                    for (auto& t : tags) is >> t;
                    for (std::size_t i = 0; i < n; ++i) {
                        double x, y, z;
                        is >> x >> y >> z;
                        if (parametric) { double u; for (int k = 0; k < dim; ++k) is >> u; }
                        remap[tags[i]] = static_cast<int>(mesh.nodes.size());
                        mesh.nodes.push_back({x, y, z});
                    }
                }
            } else {
                std::size_t n;
                is >> n;
                for (std::size_t i = 0; i < n; ++i) {
                    std::size_t id;
                    double x, y, z;
                    is >> id >> x >> y >> z;
                    remap[id] = static_cast<int>(mesh.nodes.size());
                    mesh.nodes.push_back({x, y, z});
                }
            }
        } else if (tok == "$Elements") {
            if (version >= 4.0) {
                std::size_t blocks, total, minTag, maxTag;
                is >> blocks >> total >> minTag >> maxTag;
                for (std::size_t b = 0; b < blocks; ++b) {
                    int dim, entity, type;
                    std::size_t n;
                    is >> dim >> entity >> type >> n;
                    const int nn = gmshNodesOf(type);
                    if (nn < 0) return fail("Kieu phan tu .msh chua ho tro: " + std::to_string(type));
                    nd.resize(static_cast<std::size_t>(nn));
                    for (std::size_t i = 0; i < n; ++i) {
                        std::size_t tag;
                        is >> tag;
                        for (auto& v : nd) is >> v;
                        if (!addCell(type)) return fail("File .msh: phan tu tham chieu nut khong ton tai.");
                    }
                }
            } else {
                std::size_t n;
                is >> n;
                for (std::size_t i = 0; i < n; ++i) {
                    std::size_t id, ntags;
                    int type;
                    is >> id >> type >> ntags;
                    for (std::size_t k = 0; k < ntags; ++k) { std::size_t t; is >> t; }
                    const int nn = gmshNodesOf(type);
                    if (nn < 0) { std::string rest; std::getline(is, rest); ++surfaceOnly; continue; }   // kieu la: bo qua dong
                    nd.resize(static_cast<std::size_t>(nn));
                    for (auto& v : nd) is >> v;
                    if (!addCell(type)) return fail("File .msh: phan tu tham chieu nut khong ton tai.");
                }
            }
        }
    }
    if (mesh.tets.empty())
        return fail(surfaceOnly
                        ? "File luoi chi co phan tu be mat/duong (khong co phan tu khoi). Hay chia luoi 3D (tu dien, luc dien...) "
                          "truoc khi nhap; FastCAE can luoi khoi de tinh ung suat."
                        : "File luoi khong co phan tu khoi. Hay chia luoi 3D trong Gmsh.");
    dropUnusedNodes(mesh);
    mesh.fixOrientation();
    return true;
}

// ---------------------------------------------------------------- Abaqus / CalculiX .inp
bool readAbaqusInp(const std::string& path, Mesh& mesh, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    std::ifstream is(path);
    if (!is) return fail("Khong mo duoc file luoi: " + path);
    mesh = Mesh{};
    std::unordered_map<long long, int> remap;
    struct Raw { int type; std::vector<long long> nd; };   // type = kieu Gmsh tuong ung (4 tu dien, 5 luc dien, 6 lang tru, 7 thap)
    std::vector<Raw> cells;
    enum class Sec { None, Node, Element } sec = Sec::None;
    int corners = 0, nodesPer = 0, gtype = 0;
    std::vector<long long> pend;   // phan tu viet tren nhieu dong (C3D20 co 20 nut)
    std::size_t skipped = 0;
    auto upper = [](std::string t) { for (auto& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return t; };
    auto splitCommas = [](const std::string& line) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : line) {
            if (c == ',') { out.push_back(cur); cur.clear(); } else cur += c;
        }
        out.push_back(cur);
        return out;
    };
    auto trim = [](std::string t) {
        const auto a = t.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string();
        return t.substr(a, t.find_last_not_of(" \t\r\n") - a + 1);
    };
    std::string line;
    while (std::getline(is, line)) {
        line = trim(line);
        if (line.empty() || line.rfind("**", 0) == 0) continue;
        if (line[0] == '*') {
            sec = Sec::None;
            pend.clear();
            const std::string u = upper(line);
            const auto parts = splitCommas(u);
            const std::string key = trim(parts[0]);
            if (key == "*NODE") {
                sec = Sec::Node;
            } else if (key == "*ELEMENT") {
                std::string type;
                for (std::size_t i = 1; i < parts.size(); ++i) {
                    std::string kv = trim(parts[i]);
                    kv.erase(std::remove(kv.begin(), kv.end(), ' '), kv.end());
                    if (kv.rfind("TYPE=", 0) == 0) type = kv.substr(5);
                }
                // C3D4 / C3D10 / C3D8 / C3D20 / C3D6 / C3D15 / C3D5 / C3D13 (kem hau to R, H, I, M, T...; DC3D = nhiet)
                const auto at = type.find("C3D");
                nodesPer = 0; corners = 0; gtype = 0;
                if (at != std::string::npos && at <= 1) {
                    nodesPer = std::atoi(type.c_str() + at + 3);
                    switch (nodesPer) {
                        case 4: case 10: gtype = 4; corners = 4; break;
                        case 8: case 20: gtype = 5; corners = 8; break;
                        case 6: case 15: gtype = 6; corners = 6; break;
                        case 5: case 13: gtype = 7; corners = 5; break;
                        default: nodesPer = 0; break;
                    }
                }
                sec = Sec::Element;
            }
            continue;
        }
        if (sec == Sec::Node) {
            const auto t = splitCommas(line);
            if (t.size() < 3) continue;
            const long long id = std::atoll(t[0].c_str());
            remap[id] = static_cast<int>(mesh.nodes.size());
            mesh.nodes.push_back({std::atof(t[1].c_str()), std::atof(t[2].c_str()), t.size() > 3 ? std::atof(t[3].c_str()) : 0.0});
        } else if (sec == Sec::Element) {
            if (nodesPer == 0) { ++skipped; continue; }   // kieu khong phai khoi (vo, dam, mat): bo qua
            for (const auto& tk : splitCommas(line)) {
                const std::string v = trim(tk);
                if (!v.empty()) pend.push_back(std::atoll(v.c_str()));
            }
            if (static_cast<int>(pend.size()) >= nodesPer + 1) {
                Raw r;
                r.type = gtype;
                r.nd.assign(pend.begin() + 1, pend.begin() + 1 + corners);
                cells.push_back(std::move(r));
                pend.clear();
            }
        }
    }
    if (mesh.nodes.empty()) return fail("File .inp khong co khoi *NODE.");
    std::vector<int> idx;
    for (const auto& c : cells) {
        idx.assign(c.nd.size(), 0);
        for (std::size_t k = 0; k < c.nd.size(); ++k) {
            auto it = remap.find(c.nd[k]);
            if (it == remap.end()) return fail("File .inp: phan tu tham chieu nut khong ton tai (" + std::to_string(c.nd[k]) + ").");
            idx[k] = it->second;
        }
        addVolumeCell(mesh, c.type, idx);
    }
    if (mesh.tets.empty())
        return fail(skipped ? "File .inp khong co phan tu khoi C3D4/C3D10/C3D8/C3D20/C3D6/C3D15 (chi co vo/dam/mat). FastCAE can luoi khoi."
                            : "File .inp khong co phan tu khoi (*ELEMENT, TYPE=C3D...). Lenh *INCLUDE khong duoc theo.");
    dropUnusedNodes(mesh);
    mesh.fixOrientation();
    return true;
}

// ---------------------------------------------------------------- nhap qua Gmsh (chuong trinh ngoai)
namespace {

enum class GmshJob { Volume, Stl, Convert };

CadImportResult finishImport(const std::string& outMsh, const CadImportOptions& opt, bool foreign) {
    CadImportResult res;
    std::string err;
    if (!readGmshMsh(outMsh, res.mesh, &err)) {
        res.message = foreign ? err + " (nguon: luoi nhap tu phan mem khac)" : err;
        return res;
    }
    for (auto& p : res.mesh.nodes) { p.x *= opt.scaleToMeters; p.y *= opt.scaleToMeters; p.z *= opt.scaleToMeters; }
    res.ok = true;
    res.message = "OK";
    return res;
}

// Chay gmsh ngoai: Volume = CAD B-Rep -> luoi khoi; Stl = luoi be mat -> khoi -> luoi khoi; Convert = doi luoi sang .msh 2.2.
CadImportResult importViaGmshExe(const std::string& exe, const std::string& path, const CadImportOptions& opt, GmshJob job) {
    namespace fs = std::filesystem;
    CadImportResult res;
    std::error_code ec;
    const fs::path dir = makeTempDir();
    const fs::path out = dir / "out.msh", log = dir / "gmsh.log";
    const double hCad = opt.meshSize > 0 ? opt.meshSize / (opt.scaleToMeters > 0 ? opt.scaleToMeters : 1.0) : 0.0;
    std::ostringstream cmd;
    cmd << "\"" << exe << "\" ";
    if (job == GmshJob::Stl) {
        const fs::path geo = dir / "stl_volume.geo";
        std::ofstream g(geo);
        g << "Merge \"" << slashes(fs::absolute(path, ec).string()) << "\";\n";
        if (hCad > 0) g << "Mesh.MeshSizeMax = " << hCad << ";\n";
        g << "ClassifySurfaces{40 * Pi / 180, 1, 1, Pi};\n"
             "CreateGeometry;\n"
             "s[] = Surface{:};\n"
             "Surface Loop(1) = s[];\n"
             "Volume(1) = {1};\n";
        g.close();
        cmd << "\"" << geo.string() << "\" -3";
    } else if (job == GmshJob::Volume) {
        cmd << "\"" << path << "\" -3";
        if (hCad > 0) cmd << " -clmax " << hCad;
    } else {
        cmd << "\"" << path << "\" -save";
    }
    cmd << " -format msh2 -o \"" << out.string() << "\" > \"" << log.string() << "\" 2>&1";
    const int rc = runCommand(cmd.str());
    if (rc != 0 || !fs::exists(out, ec)) {
        res.message = "gmsh that bai (ma " + std::to_string(rc) + "). " + logErrors(log.string()) + "Xem " + log.string();
        return res;
    }
    res = finishImport(out.string(), opt, job == GmshJob::Convert);
    if (res.ok) fs::remove_all(dir, ec);
    return res;
}

// Chuyen mot file bang chuong trinh ngoai thanh STEP (FreeCAD cho .FCStd; FASTCAE_CAD_CONVERTER cho dinh dang dong).
// Tra ve duong dan STEP ("" neu loi, kem thong bao).
std::string convertToStep(const std::string& path, bool fcstd, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = makeTempDir();
    const fs::path stepOut = dir / "converted.step", log = dir / "convert.log";
    std::string cmd;
    if (fcstd) {
        const std::string fc = findFreeCadExecutable();
        if (fc.empty()) {
            err = "Khong tim thay FreeCAD (freecadcmd). Cai FreeCAD roi them thu muc bin vao PATH hoac dat FASTCAE_FREECAD; "
                  "hoac trong FreeCAD chon Export -> STEP roi nhap file STEP.";
            return {};
        }
        const fs::path py = dir / "fcstd_to_step.py";
        std::ofstream o(py);
        o << "import sys\nimport FreeCAD\nimport Import\n"
             "doc = FreeCAD.openDocument(sys.argv[-2])\n"
             "shapes = [o for o in doc.Objects if hasattr(o, 'Shape') and not o.Shape.isNull() and not o.InList and o.Shape.Solids]\n"
             "if not shapes:\n    raise SystemExit('Khong co khoi dac nao trong file')\n"
             "Import.export(shapes, sys.argv[-1])\n";
        o.close();
        cmd = "\"" + fc + "\" \"" + py.string() + "\" \"" + path + "\" \"" + stepOut.string() + "\" > \"" + log.string() + "\" 2>&1";
    } else {
        cmd = cadConverterCommand();
        auto replaceAll = [&](const std::string& key, const std::string& val) {
            for (std::size_t at = cmd.find(key); at != std::string::npos; at = cmd.find(key, at + val.size()))
                cmd.replace(at, key.size(), val);
        };
        if (cmd.find("{in}") == std::string::npos || cmd.find("{out}") == std::string::npos) {
            err = "FASTCAE_CAD_CONVERTER phai chua {in} va {out}, vi du: mytool --in \"{in}\" --out \"{out}\".";
            return {};
        }
        replaceAll("{in}", path);
        replaceAll("{out}", stepOut.string());
        cmd += " > \"" + log.string() + "\" 2>&1";
    }
    const int rc = runCommand(cmd);
    if (rc != 0 || !fs::exists(stepOut, ec)) {
        err = "Chuyen doi sang STEP that bai (ma " + std::to_string(rc) + "). Xem " + log.string();
        return {};
    }
    return stepOut.string();
}

}  // namespace

#ifndef FASTCAE_HAVE_GMSH
static const char* kNoKernelMessage =
    "Chua co nhan CAD. Cach de nhat: cai Gmsh (gmsh.info, ban co san .exe) va them thu muc chua gmsh vao PATH "
    "(hoac dat bien moi truong FASTCAE_GMSH tro toi gmsh.exe); FastCAE se goi gmsh de chia luoi. "
    "Hoac chia luoi trong Gmsh roi nhap file .msh. Xem docs/BUILDING.md";
#endif

CadImportResult importCad(const std::string& path, const CadImportOptions& opt) {
    CadImportResult res;
    const CadFormat fmt = detectCadFormat(path);

    // Dinh dang can chuyen sang STEP truoc (FreeCAD, hoac dinh dang dong khi co bo chuyen doi ngoai)
    if (fmt == CadFormat::FreeCad || ((fmt == CadFormat::Native || fmt == CadFormat::Unknown) && !cadConverterCommand().empty())) {
        std::string err;
        const std::string step = convertToStep(path, fmt == CadFormat::FreeCad, err);
        if (step.empty()) { res.message = err; return res; }
        return importCad(step, opt);
    }
    if (fmt == CadFormat::Native || fmt == CadFormat::Unknown) {
        res.message = cadFormatAdvice(path);
        return res;
    }

    if (fmt == CadFormat::Msh) {
        std::string err;
        if (readGmshMsh(path, res.mesh, &err)) {
            for (auto& p : res.mesh.nodes) { p.x *= opt.scaleToMeters; p.y *= opt.scaleToMeters; p.z *= opt.scaleToMeters; }
            res.ok = true;
            res.message = "OK";
            return res;
        }
        res.mesh = Mesh{};
        // .msh nhi phan: nho Gmsh (neu co) doi sang ASCII 2.2
        std::ifstream f(path, std::ios::binary);
        std::string head(256, '\0');
        f.read(head.data(), 256);
        const bool binary = head.find("$MeshFormat") != std::string::npos && err.find("ASCII") != std::string::npos;
        const std::string exe = findGmshExecutable();
        if (binary && !exe.empty()) return importViaGmshExe(exe, path, opt, GmshJob::Convert);
        res.message = err;
        return res;
    }

    if (fmt == CadFormat::ForeignMesh && lowerExt(path) == "inp") {   // Abaqus/CalculiX: doc truc tiep, khong can Gmsh
        std::string err;
        if (!readAbaqusInp(path, res.mesh, &err)) { res.mesh = Mesh{}; res.message = err; return res; }
        for (auto& p : res.mesh.nodes) { p.x *= opt.scaleToMeters; p.y *= opt.scaleToMeters; p.z *= opt.scaleToMeters; }
        res.ok = true;
        res.message = "OK";
        return res;
    }
    const bool isMeshFile = fmt == CadFormat::ForeignMesh;
    const bool isStl = fmt == CadFormat::Stl;
    if (!isMeshFile && !isStl && fmt != CadFormat::Step && fmt != CadFormat::Iges && fmt != CadFormat::Brep &&
        fmt != CadFormat::Xao) {
        res.message = cadFormatAdvice(path);
        return res;
    }
#ifdef FASTCAE_HAVE_GMSH
    try {
        GmshSession session;
        if (isMeshFile) {   // luoi ngoai: Gmsh doc, ghi lai .msh 2.2, dung lai bo doc cua FastCAE
            gmsh::merge(path);
            namespace fs = std::filesystem;
            const std::string dir = makeTempDir();
            const std::string out = (fs::path(dir) / "out.msh").string();
            gmsh::option::setNumber("Mesh.MshFileVersion", 2.2);
            gmsh::option::setNumber("Mesh.Binary", 0);
            gmsh::option::setNumber("Mesh.SaveAll", 1);
            gmsh::write(out);
            auto r = finishImport(out, opt, true);
            std::error_code ec;
            fs::remove_all(dir, ec);
            return r;
        }
        if (isStl) {
            gmsh::merge(path);
            gmsh::model::mesh::classifySurfaces(40.0 * 3.14159265358979323846 / 180.0, true, true, 3.14159265358979323846);
            gmsh::model::mesh::createGeometry();
            std::vector<std::pair<int, int>> surfs;
            gmsh::model::getEntities(surfs, 2);
            if (surfs.empty()) { res.message = "Khong doc duoc mat nao tu file STL: " + path; return res; }
            std::vector<int> tags;
            for (const auto& s : surfs) tags.push_back(s.second);
            const int loop = gmsh::model::geo::addSurfaceLoop(tags);
            gmsh::model::geo::addVolume({loop});
            gmsh::model::geo::synchronize();
            if (!setMeshSize(opt.meshSize, opt.scaleToMeters, res.message)) return res;
            gmsh::model::mesh::generate(3);
            return extractMesh(opt.scaleToMeters);
        }
        std::vector<std::pair<int, int>> out;
        if (fmt == CadFormat::Xao) {
            gmsh::merge(path);
            gmsh::model::occ::synchronize();
            gmsh::model::getEntities(out, -1);
        } else {
            gmsh::model::occ::importShapes(path, out);
            gmsh::model::occ::synchronize();
        }
        if (out.empty()) {
            res.message = "Khong doc duoc hinh hoc tu " + path;
            return res;
        }
        // IGES (and some STEP files) hold only loose surfaces: sew them into closed solids.
        std::vector<std::pair<int, int>> vols;
        gmsh::model::getEntities(vols, 3);
        if (vols.empty()) {
            double x0, y0, z0, x1, y1, z1;
            gmsh::model::getBoundingBox(-1, -1, x0, y0, z0, x1, y1, z1);
            const double diag = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
            std::vector<std::pair<int, int>> healed;
            gmsh::model::occ::healShapes(healed, {}, 1e-5 * diag, true, true, true, true, true);
            gmsh::model::occ::synchronize();
            gmsh::model::getEntities(vols, 3);
            if (vols.empty()) {
                res.message = "File chi chua cac mat roi va khong khep kin thanh khoi (kiem tra mo hinh, hoac xuat STEP dang solid).";
                return res;
            }
        }
        if (!setMeshSize(opt.meshSize, opt.scaleToMeters, res.message)) return res;
        gmsh::model::mesh::generate(3);
        return extractMesh(opt.scaleToMeters);
    } catch (const std::exception& e) {
        res.message = std::string("Loi Gmsh/OpenCASCADE: ") + e.what();
        return res;
    }
#else
    const std::string exe = findGmshExecutable();
    if (!exe.empty())
        return importViaGmshExe(exe, path, opt, isMeshFile ? GmshJob::Convert : isStl ? GmshJob::Stl : GmshJob::Volume);
    res.message = kNoKernelMessage;
    return res;
#endif
}

CadImportResult makePrimitive(const std::string& name, const std::vector<double>& p, double meshSize,
                              int dx, int dy, int dz) {
    CadImportResult res;
    if (name == "box") {
        if (p.size() < 3) {
            res.message = "box can 3 tham so: lx ly lz";
            return res;
        }
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(p[i]) || p[i] <= 0) { res.message = "box: lx ly lz phai la so duong huu han"; return res; }
        // Giới hạn kích thước lưới: tránh hết bộ nhớ/treo khi nhập nhầm (vd. divisions = 20 999999999 3 hoặc mesh_size quá nhỏ).
        constexpr double kMaxCells = 5.0e6;
        if (dx <= 0 || dy <= 0 || dz <= 0) {
            const double h = meshSize > 0 ? meshSize : std::max({p[0], p[1], p[2]}) / 10.0;
            if (!std::isfinite(h) || h <= 0) { res.message = "mesh_size phai la so duong huu han"; return res; }
            const double fx = std::max(1.0, std::ceil(p[0] / h)), fy = std::max(1.0, std::ceil(p[1] / h)), fz = std::max(1.0, std::ceil(p[2] / h));
            if (6.0 * fx * fy * fz > kMaxCells) {
                res.message = "Luoi qua min (uoc tinh > 5 trieu phan tu): tang mesh_size hoac giam divisions";
                return res;
            }
            dx = static_cast<int>(fx); dy = static_cast<int>(fy); dz = static_cast<int>(fz);
        }
        if (6.0 * static_cast<double>(dx) * dy * dz > kMaxCells) {
            res.message = "Luoi qua min (> 5 trieu phan tu): giam divisions (hoac tang mesh_size)";
            return res;
        }
        res.mesh = makeBoxMesh(p[0], p[1], p[2], dx, dy, dz);
        res.ok = true;
        res.message = "OK";
        return res;
    }
    if (findShape(name)) {   // thư viện cấu kiện: dựng bằng bộ chia lưới ghép khối, không cần Gmsh
        res.ok = buildShapeMesh(name, p, meshSize, res.mesh, res.message);
        if (res.ok) res.message = "OK";
        return res;
    }
#ifdef FASTCAE_HAVE_GMSH
    try {
        GmshSession session;
        if (name == "cylinder" && p.size() >= 2) {
            gmsh::model::occ::addCylinder(0, 0, 0, p[1], 0, 0, p[0]);  // axis along +x
        } else if (name == "sphere" && p.size() >= 1) {
            gmsh::model::occ::addSphere(0, 0, 0, p[0]);
        } else {
            res.message = "Primitive khong hop le: " + name;
            return res;
        }
        gmsh::model::occ::synchronize();
        if (!setMeshSize(meshSize, 1.0, res.message)) return res;
        gmsh::model::mesh::generate(3);
        return extractMesh(1.0);
    } catch (const std::exception& e) {
        res.message = std::string("Loi Gmsh: ") + e.what();
        return res;
    }
#else
    res.message = "Primitive '" + name + "' can nhan CAD (Gmsh/OpenCASCADE). Hien chi 'box' co san.";
    return res;
#endif
}

}  // namespace fastcae
