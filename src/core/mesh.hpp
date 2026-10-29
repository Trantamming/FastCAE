// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
// FastCAE - core mesh data structures
#pragma once
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace fastcae {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

using Tet = std::array<int, 4>;

// Unstructured linear tetrahedral mesh (the common currency between
// CAD import / meshers / solvers / post-processing).
struct Mesh {
    std::vector<Vec3> nodes;
    std::vector<Tet> tets;
    // Đầu chỉ số nút của từng vật thể khi mô hình gồm nhiều vật thể không chung nút ({0, n1, n1+n2, ...}); rỗng = một vật thể.
    std::vector<int> bodyNodeStart;

    std::size_t numNodes() const { return nodes.size(); }
    std::size_t numCells() const { return tets.size(); }

    // Indices of nodes satisfying a predicate.
    std::vector<int> nodesWhere(const std::function<bool(const Vec3&)>& pred) const;

    // Axis-aligned bounding box.
    void bounds(Vec3& lo, Vec3& hi) const;

    // Signed volume of a tetrahedron.
    double tetVolume(std::size_t cell) const;
    double totalVolume() const;

    // Make all tets positively oriented. Returns number of flipped cells.
    int fixOrientation();
};

// Boundary selectors by name ("xmin", "xmax", "ymin", ... , "all"), using
// the mesh bounding box and a relative tolerance.
std::vector<int> selectNodes(const Mesh& m, const std::string& name, double relTol = 1e-9);

// Mặt phẳng n·x = d (n đơn vị) cho phép chọn một mặt phẳng của vật thể bằng hình học, không phụ thuộc lưới.
struct PlaneSel {
    Vec3 n{1, 0, 0};
    double d = 0;
    double tol = 1e-9;
};

// Các nút ĐỈNH của những tam giác biên (mặt tứ diện chỉ xuất hiện một lần) nằm trọn trên mặt phẳng.
// Mặt phẳng cắt xuyên vật thể không chọn nút bên trong. Rỗng nếu không có mặt nào khớp.
std::vector<int> selectPlaneNodes(const Mesh& m, const PlaneSel& plane);

}  // namespace fastcae
