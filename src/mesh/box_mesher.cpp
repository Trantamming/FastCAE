// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "mesh/box_mesher.hpp"

#include <algorithm>
#include <array>

namespace fastcae {

Mesh makeBoxMesh(double lx, double ly, double lz, int nx, int ny, int nz) {
    nx = std::max(nx, 1); ny = std::max(ny, 1); nz = std::max(nz, 1);
    Mesh m;
    m.nodes.reserve(static_cast<std::size_t>(nx + 1) * (ny + 1) * (nz + 1));
    auto id = [&](int i, int j, int k) { return (k * (ny + 1) + j) * (nx + 1) + i; };
    for (int k = 0; k <= nz; ++k)
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i)
                m.nodes.push_back({lx * i / nx, ly * j / ny, lz * k / nz});

    // The six monotone lattice paths from corner (0,0,0) to (1,1,1).
    static const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                for (const auto& p : perms) {
                    std::array<int, 3> o{0, 0, 0};
                    Tet t;
                    t[0] = id(i, j, k);
                    for (int s = 0; s < 3; ++s) {
                        o[p[s]] = 1;
                        t[s + 1] = id(i + o[0], j + o[1], k + o[2]);
                    }
                    m.tets.push_back(t);
                }
    m.fixOrientation();
    return m;
}

}  // namespace fastcae
