// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "core/result_store.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fastcae {
namespace {
constexpr const char* kMagic = "FCRES 1";
bool bad(std::string* e, const std::string& m) { if (e) *e = m; return false; }
std::string oneLine(std::string s) { for (char& c : s) if (c == '\n' || c == '\r') c = '\x1f'; return s; }
std::string restoreLines(std::string s) { for (char& c : s) if (c == '\x1f') c = '\n'; return s; }
bool okName(const std::string& n) { return !n.empty() && n.find_first_of(" \t\r\n") == std::string::npos; }
}  // namespace

bool saveResult(const std::string& path, const StoredResult& r, std::string* error) {
    std::ofstream f(path);
    if (!f) return bad(error, "Khong ghi duoc " + path);
    f << std::setprecision(17);
    f << kMagic << "\n";
    f << "type " << (r.type.empty() ? "unknown" : r.type) << "\n";
    f << "summary " << oneLine(r.summary) << "\n";
    for (const auto& kv : r.values) { if (!okName(kv.first)) continue; f << "value " << kv.first << " " << kv.second << "\n"; }
    for (const auto& kv : r.series) {
        if (!okName(kv.first)) continue;
        f << "series " << kv.first << " " << kv.second.size();
        for (double v : kv.second) f << " " << v;
        f << "\n";
    }
    f << "bodystart " << r.mesh.bodyNodeStart.size();
    for (int v : r.mesh.bodyNodeStart) f << " " << v;
    f << "\n";
    f << "nodes " << r.mesh.nodes.size() << "\n";
    for (const auto& p : r.mesh.nodes) f << p.x << " " << p.y << " " << p.z << "\n";
    f << "tets " << r.mesh.tets.size() << "\n";
    for (const auto& t : r.mesh.tets) f << t[0] << " " << t[1] << " " << t[2] << " " << t[3] << "\n";
    auto dump = [&](const char* tag, const std::map<std::string, std::vector<double>>& m) {
        for (const auto& kv : m) {
            if (!okName(kv.first)) continue;
            f << tag << " " << kv.first << " " << kv.second.size() << "\n";
            for (std::size_t i = 0; i < kv.second.size(); ++i) f << kv.second[i] << ((i + 1) % 6 == 0 ? "\n" : " ");
            f << "\n";
        }
    };
    dump("pscalar", r.fields.pointScalars);
    dump("pvector", r.fields.pointVectors);
    dump("cscalar", r.fields.cellScalars);
    // chuỗi bước: tag riêng (fpscalar/fpvector/fcscalar + chỉ số khung) để bản đọc cũ bỏ qua an toàn
    if (!r.frames.empty()) {
        f << "frames " << r.frames.size() << " " << r.currentFrame << "\n";
        for (std::size_t k = 0; k < r.frames.size(); ++k) {
            f << "framelabel " << k << " " << oneLine(r.frames[k].label) << "\n";
            auto fdump = [&](const char* tag, const std::map<std::string, std::vector<double>>& m) {
                for (const auto& kv : m) {
                    if (!okName(kv.first)) continue;
                    f << tag << " " << k << " " << kv.first << " " << kv.second.size() << "\n";
                    for (std::size_t i = 0; i < kv.second.size(); ++i) f << kv.second[i] << ((i + 1) % 6 == 0 ? "\n" : " ");
                    f << "\n";
                }
            };
            fdump("fpscalar", r.frames[k].fields.pointScalars);
            fdump("fpvector", r.frames[k].fields.pointVectors);
            fdump("fcscalar", r.frames[k].fields.cellScalars);
        }
    }
    f << "end\n";
    return f.good() ? true : bad(error, "Loi ghi file " + path);
}

bool loadResult(const std::string& path, StoredResult& r, std::string* error) {
    std::ifstream f(path);
    if (!f) return bad(error, "Khong mo duoc " + path);
    std::string line;
    if (!std::getline(f, line) || line.rfind("FCRES", 0) != 0) return bad(error, "Khong phai file .fcres: " + path);
    if (line != kMagic) return bad(error, "Phien ban .fcres khong ho tro: " + line);
    r = StoredResult{};
    bool ended = false;
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag.empty()) continue;
        if (tag == "type") ls >> r.type;
        else if (tag == "summary") { std::string rest; std::getline(ls, rest); if (!rest.empty()) rest.erase(0, 1); r.summary = restoreLines(rest); }
        else if (tag == "value") { std::string n; double v; if (ls >> n >> v) r.values[n] = v; }
        else if (tag == "series") {
            std::string n; std::size_t k = 0;
            if (!(ls >> n >> k) || k > 100000000) return bad(error, "series hong");
            auto& s = r.series[n];
            s.resize(k);
            for (auto& v : s) if (!(ls >> v)) return bad(error, "series thieu so: " + n);
        } else if (tag == "bodystart") {
            std::size_t k = 0; ls >> k;
            r.mesh.bodyNodeStart.resize(k);
            for (auto& v : r.mesh.bodyNodeStart) ls >> v;
        } else if (tag == "nodes") {
            std::size_t n = 0; ls >> n;
            if (n > 200000000) return bad(error, "so nut qua lon");
            r.mesh.nodes.resize(n);
            for (auto& p : r.mesh.nodes) if (!(f >> p.x >> p.y >> p.z)) return bad(error, "thieu toa do nut");
            std::getline(f, line);
        } else if (tag == "tets") {
            std::size_t n = 0; ls >> n;
            if (n > 400000000) return bad(error, "so phan tu qua lon");
            r.mesh.tets.resize(n);
            for (auto& t : r.mesh.tets) {
                if (!(f >> t[0] >> t[1] >> t[2] >> t[3])) return bad(error, "thieu phan tu");
                for (int v : t) if (v < 0 || static_cast<std::size_t>(v) >= r.mesh.nodes.size()) return bad(error, "chi so nut ngoai khoang trong phan tu");
            }
            std::getline(f, line);
        } else if (tag == "pscalar" || tag == "pvector" || tag == "cscalar") {
            std::string n; std::size_t k = 0;
            if (!(ls >> n >> k) || k > 1000000000) return bad(error, "truong hong");
            std::vector<double> v(k);
            for (auto& x : v) if (!(f >> x)) return bad(error, "truong thieu so: " + n);
            std::getline(f, line);
            const std::size_t nn = r.mesh.nodes.size(), nc = r.mesh.tets.size();
            if (tag == "pscalar") { if (k != nn) return bad(error, "do dai truong nut sai: " + n); r.fields.pointScalars[n] = std::move(v); }
            else if (tag == "pvector") { if (k != 3 * nn) return bad(error, "do dai vector nut sai: " + n); r.fields.pointVectors[n] = std::move(v); }
            else { if (k != nc) return bad(error, "do dai truong phan tu sai: " + n); r.fields.cellScalars[n] = std::move(v); }
        } else if (tag == "frames") {
            std::size_t k = 0; int cur = 0;
            ls >> k >> cur;
            if (k > 100000) return bad(error, "so khung qua lon");
            r.frames.assign(k, StoredFrame{});
            r.currentFrame = (cur >= 0 && std::size_t(cur) < k) ? cur : 0;
        } else if (tag == "framelabel") {
            std::size_t k = 0; ls >> k;
            std::string rest; std::getline(ls, rest);
            if (k >= r.frames.size()) return bad(error, "chi so khung sai");
            if (!rest.empty()) rest.erase(0, 1);
            r.frames[k].label = restoreLines(rest);
        } else if (tag == "fpscalar" || tag == "fpvector" || tag == "fcscalar") {
            std::size_t fi = 0, k = 0; std::string n;
            if (!(ls >> fi >> n >> k) || fi >= r.frames.size() || k > 1000000000) return bad(error, "truong khung hong");
            std::vector<double> v(k);
            for (auto& x : v) if (!(f >> x)) return bad(error, "truong khung thieu so: " + n);
            std::getline(f, line);
            const std::size_t nn = r.mesh.nodes.size(), nc = r.mesh.tets.size();
            auto& F = r.frames[fi].fields;
            if (tag == "fpscalar") { if (k != nn) return bad(error, "do dai truong nut khung sai: " + n); F.pointScalars[n] = std::move(v); }
            else if (tag == "fpvector") { if (k != 3 * nn) return bad(error, "do dai vector khung sai: " + n); F.pointVectors[n] = std::move(v); }
            else { if (k != nc) return bad(error, "do dai truong phan tu khung sai: " + n); F.cellScalars[n] = std::move(v); }
        } else if (tag == "end") { ended = true; break; }
    }
    return ended ? true : bad(error, "File .fcres bi cat cut (thieu 'end')");
}

bool exportNodesCsv(const std::string& path, const StoredResult& r, std::string* error) {
    std::ofstream f(path);
    if (!f) return bad(error, "Khong ghi duoc " + path);
    f << std::setprecision(10);
    f << "node_id,x,y,z";
    for (const auto& kv : r.fields.pointScalars) f << "," << kv.first;
    for (const auto& kv : r.fields.pointVectors) f << "," << kv.first << "_x," << kv.first << "_y," << kv.first << "_z";
    f << "\n";
    for (std::size_t i = 0; i < r.mesh.nodes.size(); ++i) {
        const auto& p = r.mesh.nodes[i];
        f << i << "," << p.x << "," << p.y << "," << p.z;
        for (const auto& kv : r.fields.pointScalars) f << "," << kv.second[i];
        for (const auto& kv : r.fields.pointVectors) f << "," << kv.second[3 * i] << "," << kv.second[3 * i + 1] << "," << kv.second[3 * i + 2];
        f << "\n";
    }
    return f.good() ? true : bad(error, "Loi ghi file " + path);
}

}  // namespace fastcae
