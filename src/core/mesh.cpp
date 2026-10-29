// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include <map>
#include <array>
#include <algorithm>
#include "core/mesh.hpp"

#include <algorithm>
#include <limits>

namespace fastcae {

std::vector<int> Mesh::nodesWhere(const std::function<bool(const Vec3&)>& pred) const {
    std::vector<int> out;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (pred(nodes[i])) out.push_back(static_cast<int>(i));
    return out;
}

void Mesh::bounds(Vec3& lo, Vec3& hi) const {
    const double inf = std::numeric_limits<double>::infinity();
    lo = {inf, inf, inf};
    hi = {-inf, -inf, -inf};
    for (const auto& p : nodes) {
        lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
        lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
        lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
    }
}

double Mesh::tetVolume(std::size_t c) const {
    const auto& t = tets[c];
    const Vec3 &a = nodes[t[0]], &b = nodes[t[1]], &cc = nodes[t[2]], &d = nodes[t[3]];
    const double bx = b.x - a.x, by = b.y - a.y, bz = b.z - a.z;
    const double cx = cc.x - a.x, cy = cc.y - a.y, cz = cc.z - a.z;
    const double dx = d.x - a.x, dy = d.y - a.y, dz = d.z - a.z;
    const double det = bx * (cy * dz - cz * dy) - by * (cx * dz - cz * dx) + bz * (cx * dy - cy * dx);
    return det / 6.0;
}

double Mesh::totalVolume() const {
    double v = 0;
    for (std::size_t c = 0; c < tets.size(); ++c) v += std::abs(tetVolume(c));
    return v;
}

int Mesh::fixOrientation() {
    int flipped = 0;
    for (std::size_t c = 0; c < tets.size(); ++c) {
        if (tetVolume(c) < 0) {
            std::swap(tets[c][2], tets[c][3]);
            ++flipped;
        }
    }
    return flipped;
}

std::vector<int> selectNodes(const Mesh& m, const std::string& name, double relTol) {
    Vec3 lo, hi;
    m.bounds(lo, hi);
    const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-300});
    const double tol = relTol * size + 1e-12 * size;
    auto near = [&](double a, double b) { return std::abs(a - b) <= tol; };

    if (name == "all") return m.nodesWhere([](const Vec3&) { return true; });
    if (name == "xmin") return m.nodesWhere([&](const Vec3& p) { return near(p.x, lo.x); });
    if (name == "xmax") return m.nodesWhere([&](const Vec3& p) { return near(p.x, hi.x); });
    if (name == "ymin") return m.nodesWhere([&](const Vec3& p) { return near(p.y, lo.y); });
    if (name == "ymax") return m.nodesWhere([&](const Vec3& p) { return near(p.y, hi.y); });
    if (name == "zmin") return m.nodesWhere([&](const Vec3& p) { return near(p.z, lo.z); });
    if (name == "zmax") return m.nodesWhere([&](const Vec3& p) { return near(p.z, hi.z); });
    return {};
}

std::vector<int> selectPlaneNodes(const Mesh& m, const PlaneSel& plane) {
    // đếm số lần mỗi mặt tam giác xuất hiện; chỉ một lần = mặt biên
    std::map<std::array<int, 3>, int> count;
    static const int faces[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    for (const auto& t : m.tets)
        for (const auto& f : faces) {
            std::array<int, 3> k = {t[f[0]], t[f[1]], t[f[2]]};
            std::sort(k.begin(), k.end());
            ++count[k];
        }
    auto onPlane = [&](int i) {
        const Vec3& p = m.nodes[i];
        return std::abs(plane.n.x * p.x + plane.n.y * p.y + plane.n.z * p.z - plane.d) <= plane.tol;
    };
    std::vector<char> mark(m.nodes.size(), 0);
    for (const auto& [k, c] : count)
        if (c == 1 && onPlane(k[0]) && onPlane(k[1]) && onPlane(k[2]))
            for (int i : k) mark[i] = 1;
    std::vector<int> out;
    for (std::size_t i = 0; i < mark.size(); ++i)
        if (mark[i]) out.push_back(static_cast<int>(i));
    return out;
}

}  // namespace fastcae
