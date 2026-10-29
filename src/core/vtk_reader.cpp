// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "core/vtk_reader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <fstream>
#include <sstream>
#include <vector>

namespace fastcae {
namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

struct Tokens {
    std::vector<std::string> t;
    std::size_t i = 0;
    bool more() const { return i < t.size(); }
    const std::string& peek() const { return t[i]; }
    std::string next() { return t[i++]; }
    double num() { return std::stod(t[i++]); }
    long inum() { return std::stol(t[i++]); }
};

// Tách ô thành tứ diện (chỉ số cục bộ trong ô). Trả về false nếu loại ô không hỗ trợ.
bool splitCell(int type, const std::vector<int>& c, std::vector<Tet>& out) {
    auto add = [&](int a, int b, int d, int e) { out.push_back({c[a], c[b], c[d], c[e]}); };
    switch (type) {
        case 10: case 24:                       // tứ diện (tuyến tính / bậc hai: chỉ lấy 4 nút đỉnh)
            if (c.size() < 4) return false;
            add(0, 1, 2, 3);
            return true;
        case 12: case 25: case 29:              // lục diện: 6 tứ diện
            if (c.size() < 8) return false;
            add(0, 1, 3, 7); add(0, 1, 7, 4); add(1, 4, 7, 5);
            add(1, 2, 3, 7); add(1, 2, 7, 6); add(1, 5, 7, 6);
            return true;
        case 13: case 26: case 32:              // lăng trụ: 3 tứ diện
            if (c.size() < 6) return false;
            add(0, 1, 2, 3); add(1, 2, 3, 4); add(2, 3, 4, 5);
            return true;
        case 14: case 27:                       // chóp đáy tứ giác: 2 tứ diện
            if (c.size() < 5) return false;
            add(0, 1, 2, 4); add(0, 2, 3, 4);
            return true;
        default:
            return false;
    }
}

}  // namespace

bool readVtk(const std::string& path, Mesh& mesh, VtkFields& fields, VtkReadInfo* info, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("Không mở được " + path);
    std::string header;
    std::getline(f, header);                    // "# vtk DataFile Version x.y"
    std::string title;
    std::getline(f, title);
    std::string format;
    f >> format;
    if (upper(format) != "ASCII")
        return fail("Chỉ đọc được VTK dạng ASCII (file này là " + format +
                    "). Với OpenFOAM dùng: foamToVTK -ascii. Với SU2 chọn OUTPUT_FILES= (VTK) rồi chuyển ASCII bằng ParaView/meshio.");
    std::stringstream rest;
    rest << f.rdbuf();
    Tokens tk;
    for (std::string w; rest >> w;) tk.t.push_back(w);

    mesh = Mesh{};
    fields = VtkFields{};
    VtkReadInfo inf;
    std::vector<int> cellType;
    std::vector<std::vector<int>> cells;
    std::size_t nPoints = 0, nCells = 0;
    enum class Sec { None, Point, Cell } sec = Sec::None;
    struct RawCellField { std::string name; std::vector<double> v; };
    std::vector<RawCellField> rawCellFields;

    try {
        while (tk.more()) {
            const std::string k = upper(tk.next());
            if (k == "DATASET") {
                const std::string kind = upper(tk.next());
                if (kind != "UNSTRUCTURED_GRID" && kind != "POLYDATA")
                    return fail("Chỉ hỗ trợ DATASET UNSTRUCTURED_GRID (file có " + kind + ")");
            } else if (k == "POINTS") {
                nPoints = std::size_t(tk.inum());
                tk.next();                      // float/double
                mesh.nodes.resize(nPoints);
                for (auto& p : mesh.nodes) { p.x = tk.num(); p.y = tk.num(); p.z = tk.num(); }
            } else if (k == "CELLS") {
                nCells = std::size_t(tk.inum());
                tk.inum();                      // tổng kích thước danh sách
                cells.resize(nCells);
                for (auto& c : cells) {
                    const long m = tk.inum();
                    c.resize(std::size_t(m));
                    for (auto& id : c) id = int(tk.inum());
                }
            } else if (k == "CELL_TYPES") {
                const std::size_t n = std::size_t(tk.inum());
                cellType.resize(n);
                for (auto& t : cellType) t = int(tk.inum());
            } else if (k == "POINT_DATA") { tk.inum(); sec = Sec::Point; }
            else if (k == "CELL_DATA") { tk.inum(); sec = Sec::Cell; }
            else if (k == "SCALARS") {
                const std::string name = tk.next();
                tk.next();                      // kiểu dữ liệu
                int comps = 1;
                if (tk.more() && upper(tk.peek()) != "LOOKUP_TABLE") comps = int(tk.inum());
                if (tk.more() && upper(tk.peek()) == "LOOKUP_TABLE") { tk.next(); tk.next(); }
                const std::size_t cnt = (sec == Sec::Cell ? nCells : nPoints) * std::size_t(comps);
                std::vector<double> v(cnt);
                for (auto& x : v) x = tk.num();
                if (comps == 1) {
                    if (sec == Sec::Cell) rawCellFields.push_back({name, std::move(v)});
                    else fields.pointScalars[name] = std::move(v);
                } else if (comps == 3 && sec == Sec::Point) {
                    fields.pointVectors[name] = std::move(v);
                }
            } else if (k == "VECTORS" || k == "NORMALS") {
                const std::string name = tk.next();
                tk.next();
                const std::size_t cnt = (sec == Sec::Cell ? nCells : nPoints) * 3;
                std::vector<double> v(cnt);
                for (auto& x : v) x = tk.num();
                if (sec == Sec::Point && k == "VECTORS") fields.pointVectors[name] = std::move(v);
            } else if (k == "FIELD") {
                tk.next();                      // tên field data
                const long nArr = tk.inum();
                for (long a = 0; a < nArr; ++a) {
                    const std::string name = tk.next();
                    const long comps = tk.inum();
                    const long tuples = tk.inum();
                    tk.next();
                    std::vector<double> v(std::size_t(comps * tuples));
                    for (auto& x : v) x = tk.num();
                    if (comps == 1 && std::size_t(tuples) == nPoints && sec != Sec::Cell) fields.pointScalars[name] = std::move(v);
                    else if (comps == 3 && std::size_t(tuples) == nPoints && sec != Sec::Cell) fields.pointVectors[name] = std::move(v);
                    else if (comps == 1 && std::size_t(tuples) == nCells) rawCellFields.push_back({name, std::move(v)});
                }
            }
            // từ khoá khác (METADATA, LOOKUP_TABLE...) bị bỏ qua
        }
    } catch (const std::exception&) {
        return fail("File VTK bị cắt cụt hoặc sai định dạng gần vị trí token " + std::to_string(tk.i));
    }

    if (mesh.nodes.empty()) return fail("File VTK không có POINTS");
    if (cells.empty() || cellType.size() != cells.size()) return fail("File VTK thiếu CELLS/CELL_TYPES (cần lưới thể tích)");

    // Lưới 2D (SU2, OpenFOAM 2D...): chỉ có tam giác (5) / tứ giác (9) nằm trong một mặt phẳng z = hằng.
    // Đùn thành lớp mỏng (nêm / hex) rồi dùng lại bộ tách tứ diện; trường nút được nhân đôi cho hai lớp.
    {
        bool anyVolume = false, onlyFlat = !cells.empty();
        for (std::size_t ci = 0; ci < cells.size(); ++ci) {
            const int t = cellType[ci];
            if (t == 5 || t == 9) continue;
            if (t == 1 || t == 3) continue;                    // điểm, đoạn thẳng (biên): bỏ qua, không cản
            onlyFlat = false;
            if (t == 10 || t == 12 || t == 13 || t == 14 || t == 24 || t == 25 || t == 26) anyVolume = true;
        }
        bool hasFlat = false;
        for (std::size_t ci = 0; ci < cells.size(); ++ci) if (cellType[ci] == 5 || cellType[ci] == 9) { hasFlat = true; break; }
        double z0 = mesh.nodes[0].z, zspan = 0;
        for (const auto& p : mesh.nodes) zspan = std::max(zspan, std::fabs(p.z - z0));
        double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
        for (const auto& p : mesh.nodes) { lo[0] = std::min(lo[0], p.x); hi[0] = std::max(hi[0], p.x); lo[1] = std::min(lo[1], p.y); hi[1] = std::max(hi[1], p.y); }
        const double ext = std::max(hi[0] - lo[0], hi[1] - lo[1]);
        if (hasFlat && onlyFlat && !anyVolume && ext > 0 && zspan <= 1e-9 * std::max(ext, 1.0)) {
            const std::size_t N = mesh.nodes.size();
            const double h = 0.004 * ext;
            mesh.nodes.reserve(2 * N);
            for (std::size_t i = 0; i < N; ++i) { Vec3 q = mesh.nodes[i]; q.z += h; mesh.nodes.push_back(q); }
            auto dup = [&](std::map<std::string, std::vector<double>>& m, std::size_t comps) {
                for (auto& kv : m) {
                    if (kv.second.size() != N * comps) continue;
                    std::vector<double> d = kv.second;
                    d.insert(d.end(), kv.second.begin(), kv.second.end());
                    kv.second = std::move(d);
                }
            };
            dup(fields.pointScalars, 1);
            dup(fields.pointVectors, 3);
            std::vector<std::vector<int>> nc;
            std::vector<int> nt, origIdx;
            for (std::size_t ci = 0; ci < cells.size(); ++ci) {
                const int t = cellType[ci];
                if (t != 5 && t != 9) continue;
                std::vector<int> c = cells[ci];
                for (int id : cells[ci]) c.push_back(id + int(N));
                nc.push_back(c);
                nt.push_back(t == 5 ? 13 : 12);
                origIdx.push_back(int(ci));
            }
            // ánh xạ trường theo ô: giữ đúng chỉ số ô gốc cho các tứ diện sinh ra
            for (auto& rf : rawCellFields) {
                std::vector<double> v(nc.size());
                for (std::size_t k = 0; k < nc.size(); ++k) v[k] = rf.v[std::size_t(origIdx[k])];
                rf.v = std::move(v);
            }
            cells = std::move(nc);
            cellType = std::move(nt);
            inf.extruded2d = true;
            inf.extrudeThickness = h;
        }
    }

    std::vector<int> parent;                    // ô gốc của từng tứ diện (để nhân giá trị theo ô)
    std::vector<Tet> tmp;
    for (std::size_t ci = 0; ci < cells.size(); ++ci) {
        bool bad = false;
        for (int id : cells[ci]) if (id < 0 || std::size_t(id) >= mesh.nodes.size()) bad = true;
        tmp.clear();
        if (bad || !splitCell(cellType[ci], cells[ci], tmp)) { ++inf.cellsSkipped; continue; }
        ++inf.cellsRead;
        for (const auto& t : tmp) { mesh.tets.push_back(t); parent.push_back(int(ci)); }
    }
    inf.tetsProduced = int(mesh.tets.size());
    if (mesh.tets.empty())
        return fail("File VTK không có ô thể tích (chỉ có " + std::to_string(inf.cellsSkipped) +
                    " ô bề mặt/đường). Hãy xuất lưới 3D hoặc kết quả trên thể tích.");
    mesh.fixOrientation();                      // fixOrientation chỉ đảo thứ tự nút, không đổi chỉ số nút
    for (auto& rf : rawCellFields) {
        std::vector<double> v(mesh.tets.size());
        for (std::size_t t = 0; t < v.size(); ++t) v[t] = rf.v[std::size_t(parent[t])];
        fields.cellScalars[rf.name] = std::move(v);
    }
    // Khung 3D tô màu theo nút: thêm độ lớn của trường vectơ và quy trường theo ô về nút (trung bình các ô kề).
    for (const auto& kv : fields.pointVectors) {
        if (kv.second.size() != mesh.nodes.size() * 3) continue;
        std::vector<double> mag(mesh.nodes.size());
        for (std::size_t n = 0; n < mag.size(); ++n)
            mag[n] = std::sqrt(kv.second[3 * n] * kv.second[3 * n] + kv.second[3 * n + 1] * kv.second[3 * n + 1] +
                               kv.second[3 * n + 2] * kv.second[3 * n + 2]);
        fields.pointScalars[kv.first + "_magnitude"] = std::move(mag);
    }
    for (const auto& kv : fields.cellScalars) {
        std::vector<double> sum(mesh.nodes.size(), 0.0), cnt(mesh.nodes.size(), 0.0);
        for (std::size_t t = 0; t < mesh.tets.size(); ++t)
            for (int id : mesh.tets[t]) { sum[std::size_t(id)] += kv.second[t]; cnt[std::size_t(id)] += 1; }
        for (std::size_t n = 0; n < sum.size(); ++n) sum[n] = cnt[n] > 0 ? sum[n] / cnt[n] : 0.0;
        const std::string key = fields.pointScalars.count(kv.first) ? kv.first + "_cell" : kv.first;
        fields.pointScalars[key] = std::move(sum);
    }
    if (info) *info = inf;
    return true;
}

}  // namespace fastcae
