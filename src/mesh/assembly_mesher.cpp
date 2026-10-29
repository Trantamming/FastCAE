// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "mesh/assembly_mesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace fastcae {

namespace {

// Các toạ độ biên của mọi khối theo một trục, sau đó chia nhỏ từng đoạn cho tới cỡ h.
std::vector<double> axisGrid(const std::vector<BoxPart>& parts, int axis, double h, double tol) {
    std::vector<double> c;
    for (const auto& p : parts) {
        const double lo[3] = {p.x0, p.y0, p.z0}, hi[3] = {p.x1, p.y1, p.z1};
        c.push_back(lo[axis]);
        c.push_back(hi[axis]);
    }
    std::sort(c.begin(), c.end());
    std::vector<double> u;
    for (double v : c)
        if (u.empty() || v - u.back() > tol) u.push_back(v);
    std::vector<double> g;
    for (std::size_t i = 0; i + 1 < u.size(); ++i) {
        const int n = std::max(1, static_cast<int>(std::ceil((u[i + 1] - u[i]) / h - 1e-9)));
        for (int k = 0; k < n; ++k) g.push_back(u[i] + (u[i + 1] - u[i]) * k / n);
    }
    if (!u.empty()) g.push_back(u.back());
    return g;
}

}  // namespace

Mesh makeAssemblyMesh(const std::vector<BoxPart>& parts, double hx, double hy, double hz, std::size_t maxCells) {
    Mesh m;
    if (parts.empty() || hx <= 0 || hy <= 0 || hz <= 0) return m;
    double size = 0;
    for (const auto& p : parts) size = std::max({size, p.x1 - p.x0, p.y1 - p.y0, p.z1 - p.z0});
    const double tol = 1e-9 * std::max(size, 1e-12);
    const auto gx = axisGrid(parts, 0, hx, tol), gy = axisGrid(parts, 1, hy, tol), gz = axisGrid(parts, 2, hz, tol);
    const int nx = static_cast<int>(gx.size()) - 1, ny = static_cast<int>(gy.size()) - 1, nz = static_cast<int>(gz.size()) - 1;
    if (nx < 1 || ny < 1 || nz < 1) return m;
    if (static_cast<std::size_t>(nx) * ny * nz > maxCells) return m;

    // ô nằm trong khối nào đó (xét tâm ô)
    std::vector<char> keep(static_cast<std::size_t>(nx) * ny * nz, 0);
    auto cid = [&](int i, int j, int k) { return (static_cast<std::size_t>(k) * ny + j) * nx + i; };
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const double cx = 0.5 * (gx[i] + gx[i + 1]), cy = 0.5 * (gy[j] + gy[j + 1]), cz = 0.5 * (gz[k] + gz[k + 1]);
                for (const auto& p : parts)
                    if (cx > p.x0 && cx < p.x1 && cy > p.y0 && cy < p.y1 && cz > p.z0 && cz < p.z1) {
                        keep[cid(i, j, k)] = 1;
                        break;
                    }
            }

    // đánh số nút gọn: chỉ nút thuộc ô được giữ
    const int px = nx + 1, py = ny + 1;
    auto nid = [&](int i, int j, int k) { return (static_cast<std::size_t>(k) * py + j) * px + i; };
    std::vector<int> map(static_cast<std::size_t>(px) * py * (nz + 1), -1);
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                if (keep[cid(i, j, k)])
                    for (int dk = 0; dk <= 1; ++dk)
                        for (int dj = 0; dj <= 1; ++dj)
                            for (int di = 0; di <= 1; ++di) {
                                int& v = map[nid(i + di, j + dj, k + dk)];
                                if (v < 0) {
                                    v = static_cast<int>(m.nodes.size());
                                    m.nodes.push_back({gx[i + di], gy[j + dj], gz[k + dk]});
                                }
                            }
    // Kuhn/Freudenthal: cùng một kiểu chia cho mọi ô nên các mặt chung trùng nhau
    static const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                if (!keep[cid(i, j, k)]) continue;
                for (const auto& p : perms) {
                    std::array<int, 3> o{0, 0, 0};
                    Tet t;
                    t[0] = map[nid(i, j, k)];
                    for (int s = 0; s < 3; ++s) {
                        o[p[s]] = 1;
                        t[s + 1] = map[nid(i + o[0], j + o[1], k + o[2])];
                    }
                    m.tets.push_back(t);
                }
            }
    m.fixOrientation();
    return m;
}

}  // namespace fastcae
