// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "cad/shape_library.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "mesh/assembly_mesher.hpp"

namespace fastcae {

namespace {

using P = std::vector<ShapeParam>;

std::vector<ShapeInfo> makeCatalog() {
    std::vector<ShapeInfo> c;
    c.push_back({"ibeam", "Dầm thép chữ I", "Dầm / cột",
                 "Dầm công xôn: ngàm đầu x = 0, lực 1 kN hướng xuống ở đầu x = L (so với lý thuyết dầm).",
                 P{{"L", "Chiều dài L", 3.0}, {"b", "Bề rộng cánh b", 0.15}, {"h", "Chiều cao h", 0.30},
                   {"tf", "Dày cánh tf", 0.0107}, {"tw", "Dày bụng tw", 0.0071}}});
    c.push_back({"channel", "Dầm thép chữ U", "Dầm / cột", "Dầm công xôn, ngàm x = 0, lực đầu tự do.",
                 P{{"L", "Chiều dài L", 3.0}, {"b", "Bề rộng cánh b", 0.075}, {"h", "Chiều cao h", 0.20},
                   {"tf", "Dày cánh tf", 0.0115}, {"tw", "Dày bụng tw", 0.0085}}});
    c.push_back({"angle", "Thép góc chữ L", "Dầm / cột", "Dầm công xôn, ngàm x = 0, lực đầu tự do.",
                 P{{"L", "Chiều dài L", 2.0}, {"a", "Cạnh ngang a", 0.10}, {"b", "Cạnh đứng b", 0.10}, {"t", "Chiều dày t", 0.01}}});
    c.push_back({"tee", "Dầm thép chữ T", "Dầm / cột", "Dầm công xôn, ngàm x = 0, lực đầu tự do.",
                 P{{"L", "Chiều dài L", 3.0}, {"b", "Bề rộng cánh b", 0.15}, {"h", "Chiều cao h", 0.15},
                   {"tf", "Dày cánh tf", 0.01}, {"tw", "Dày bụng tw", 0.007}}});
    c.push_back({"rect_tube", "Ống thép chữ nhật", "Dầm / cột", "Dầm công xôn, ngàm x = 0, lực đầu tự do.",
                 P{{"L", "Chiều dài L", 3.0}, {"b", "Bề rộng b", 0.10}, {"h", "Chiều cao h", 0.15}, {"t", "Chiều dày t", 0.005}}});
    c.push_back({"plate", "Tấm chữ nhật", "Tấm", "Tấm chịu uốn: ngàm cạnh x = 0, lực 1 kN hướng xuống ở cạnh x = L.",
                 P{{"L", "Dài L", 1.0}, {"W", "Rộng W", 0.5}, {"t", "Dày t", 0.02}}});
    c.push_back({"plate_hole", "Tấm có lỗ vuông", "Tấm", "Tấm chịu kéo dọc trục 10 kN, ngàm cạnh x = 0 (tập trung ứng suất quanh lỗ).",
                 P{{"L", "Dài L", 1.0}, {"W", "Rộng W", 0.5}, {"t", "Dày t", 0.02}, {"a", "Cạnh lỗ a", 0.1}}});
    c.push_back({"wall", "Tường / vách", "Tường & móng", "Ngàm chân tường (z = 0), lực ngang 20 kN ở đỉnh theo phương y.",
                 P{{"L", "Dài L", 3.0}, {"H", "Cao H", 3.0}, {"t", "Dày t", 0.2}}});
    c.push_back({"pad_footing", "Móng đơn + cổ cột", "Tường & móng",
                 "Mặt đáy cố định (nền cứng), lực nén 200 kN trên đỉnh cổ cột. Chưa mô hình đàn hồi nền.",
                 P{{"B", "Bề rộng B (x)", 1.5}, {"Lf", "Bề dài Lf (y)", 1.5}, {"t", "Dày bản móng t", 0.5},
                   {"cw", "Cạnh cổ cột cw", 0.4}, {"ch", "Cao cổ cột ch", 0.6}}});
    c.push_back({"retaining_wall", "Tường chắn đất (chữ T ngược)", "Tường & móng",
                 "Đáy cố định, lực ngang 20 kN ở đỉnh thân tường (thay cho áp lực đất, chưa mô hình đất).",
                 P{{"L", "Dài L", 2.0}, {"H", "Cao tổng H", 3.0}, {"ts", "Dày thân ts", 0.3}, {"B", "Bề rộng đế B", 2.0},
                   {"tb", "Dày đế tb", 0.4}, {"toe", "Mũi đế toe", 0.5}}});
    return c;
}

// Các khối hộp của cấu kiện + kích thước mỏng nhất (để chọn cỡ lưới).
bool shapeParts(const std::string& n, const std::vector<double>& p, std::vector<BoxPart>& parts, double& thin, std::string& err) {
    auto need = [&](std::size_t k) {
        if (p.size() < k) {
            err = n + " can " + std::to_string(k) + " tham so";
            return false;
        }
        for (std::size_t i = 0; i < k; ++i)
            if (!(p[i] > 0)) {
                err = "Tham so phai duong";
                return false;
            }
        return true;
    };
    parts.clear();
    if (n == "ibeam" || n == "channel" || n == "tee") {
        if (!need(5)) return false;
        const double L = p[0], b = p[1], h = p[2], tf = p[3], tw = p[4];
        if (2 * tf >= h && n != "tee") { err = "Canh qua day so voi chieu cao"; return false; }
        if (tw >= b) { err = "Day bung phai nho hon be rong canh"; return false; }
        if (n == "ibeam") {
            parts = {{0, 0, 0, L, b, tf}, {0, 0, h - tf, L, b, h}, {0, (b - tw) / 2, tf, L, (b + tw) / 2, h - tf}};
        } else if (n == "channel") {
            parts = {{0, 0, 0, L, b, tf}, {0, 0, h - tf, L, b, h}, {0, 0, tf, L, tw, h - tf}};
        } else {
            if (tf >= h) { err = "Day canh phai nho hon chieu cao"; return false; }
            parts = {{0, 0, h - tf, L, b, h}, {0, (b - tw) / 2, 0, L, (b + tw) / 2, h - tf}};
        }
        thin = std::min(tf, tw);
    } else if (n == "angle") {
        if (!need(4)) return false;
        const double L = p[0], a = p[1], b = p[2], t = p[3];
        if (t >= a || t >= b) { err = "Chieu day phai nho hon hai canh"; return false; }
        parts = {{0, 0, 0, L, a, t}, {0, 0, 0, L, t, b}};
        thin = t;
    } else if (n == "rect_tube") {
        if (!need(4)) return false;
        const double L = p[0], b = p[1], h = p[2], t = p[3];
        if (2 * t >= b || 2 * t >= h) { err = "Chieu day qua lon"; return false; }
        parts = {{0, 0, 0, L, b, t}, {0, 0, h - t, L, b, h}, {0, 0, t, L, t, h - t}, {0, b - t, t, L, b, h - t}};
        thin = t;
    } else if (n == "plate") {
        if (!need(3)) return false;
        parts = {{0, 0, 0, p[0], p[1], p[2]}};
        thin = p[2];
    } else if (n == "plate_hole") {
        if (!need(4)) return false;
        const double L = p[0], W = p[1], t = p[2], a = p[3];
        if (a >= L || a >= W) { err = "Lo qua lon"; return false; }
        const double x0 = (L - a) / 2, x1 = (L + a) / 2, y0 = (W - a) / 2, y1 = (W + a) / 2;
        parts = {{0, 0, 0, x0, W, t}, {x1, 0, 0, L, W, t}, {x0, 0, 0, x1, y0, t}, {x0, y1, 0, x1, W, t}};
        thin = std::min(t, a / 2);
    } else if (n == "wall") {
        if (!need(3)) return false;
        parts = {{0, 0, 0, p[0], p[2], p[1]}};
        thin = p[2];
    } else if (n == "pad_footing") {
        if (!need(5)) return false;
        const double B = p[0], Lf = p[1], t = p[2], cw = p[3], ch = p[4];
        if (cw >= B || cw >= Lf) { err = "Co cot lon hon mong"; return false; }
        parts = {{0, 0, 0, B, Lf, t}, {(B - cw) / 2, (Lf - cw) / 2, t, (B + cw) / 2, (Lf + cw) / 2, t + ch}};
        thin = std::min({t, cw / 2});
    } else if (n == "retaining_wall") {
        if (!need(6)) return false;
        const double L = p[0], H = p[1], ts = p[2], B = p[3], tb = p[4], toe = p[5];
        if (tb >= H || toe + ts > B) { err = "Kich thuoc tuong chan khong hop le"; return false; }
        parts = {{0, 0, 0, L, B, tb}, {0, toe, tb, L, toe + ts, H}};
        thin = std::min(ts, tb);
    } else {
        err = "Khong co cau kien: " + n;
        return false;
    }
    return true;
}

}  // namespace

const std::vector<ShapeInfo>& shapeCatalog() {
    static const std::vector<ShapeInfo> c = makeCatalog();
    return c;
}

const ShapeInfo* findShape(const std::string& name) {
    for (const auto& s : shapeCatalog())
        if (s.name == name) return &s;
    return nullptr;
}

bool buildShapeMesh(const std::string& name, const std::vector<double>& p, double h, Mesh& out, std::string& err) {
    std::vector<BoxPart> parts;
    double thin = 0;
    if (!shapeParts(name, p, parts, thin, err)) return false;
    double hx, hy, hz;
    if (h > 0) {
        hx = hy = hz = h;
    } else {
        // 1 phần tử qua bề dày mỏng nhất (nên dùng order = 2); dọc trục khoảng 40 phần tử. Thô dần nếu quá nhiều ô.
        double L = 0;
        for (const auto& q : parts) L = std::max({L, q.x1 - q.x0});
        double sy = 0, sz = 0;   // kích thước tiết diện (mặt YZ) của cả cấu kiện
        {
            double y0 = 1e300, y1 = -1e300, z0 = 1e300, z1 = -1e300;
            for (const auto& q : parts) {
                y0 = std::min(y0, q.y0); y1 = std::max(y1, q.y1);
                z0 = std::min(z0, q.z0); z1 = std::max(z1, q.z1);
            }
            sy = y1 - y0;
            sz = z1 - z0;
        }
        // Ít nhất 1 phần tử qua bề dày mỏng nhất, nhưng không mịn hơn ~1/8 kích thước tiết diện (giữ số phần tử vừa phải).
        hy = hz = std::max(thin, std::max(sy, sz) / 8.0);
        hx = std::max(hy, L / 30.0);
        for (int guard = 0; guard < 12; ++guard) {
            out = makeAssemblyMesh(parts, hx, hy, hz, 60000);
            if (out.numCells() > 0) break;
            hx *= 1.5;
            hy *= 1.5;
            hz *= 1.5;
        }
        if (out.numCells() == 0) {
            err = "Khong chia duoc luoi (qua nhieu o)";
            return false;
        }
        return true;
    }
    out = makeAssemblyMesh(parts, hx, hy, hz);
    if (out.numCells() == 0) {
        err = "Khong chia duoc luoi: cho mesh_size lon hon (qua nhieu o)";
        return false;
    }
    return true;
}

std::string shapeCaseText(const std::string& name, const std::vector<double>& p, const std::string& material) {
    const ShapeInfo* info = findShape(name);
    double E = 210e9, nu = 0.3, rho = 7850;
    if (material == "concrete") { E = 30e9; nu = 0.2; rho = 2500; }
    else if (material == "aluminium") { E = 70e9; nu = 0.33; rho = 2700; }
    std::ostringstream o;
    o.precision(10);
    o << "# " << (info ? info->label : name) << " (thu vien cau kien FastCAE, tac gia Tran Minh Tam)\n";
    if (info) o << "# " << info->note << "\n";
    o << "type = elastic\ngeometry = " << name;
    for (double v : p) o << ' ' << v;
    o << "\norder = 2   # phan tu 10 nut: can thiet khi chi co 1 phan tu qua be day\n";
    o << "E = " << E << "\nnu = " << nu << "\nrho = " << rho << "\n";
    if (name == "plate_hole") o << "fix = xmin\nload = xmax 10000 0 0\n";
    else if (name == "wall") o << "fix = zmin\nload = zmax 0 20000 0\n";
    else if (name == "pad_footing") o << "fix = zmin\nload = zmax 0 0 -200000\n";
    else if (name == "retaining_wall") o << "fix = zmin\nload = zmax 0 -20000 0\n";
    else o << "fix = xmin\nload = xmax 0 0 -1000\n";
    o << "output = " << name << ".vtk\n";
    return o.str();
}

}  // namespace fastcae
