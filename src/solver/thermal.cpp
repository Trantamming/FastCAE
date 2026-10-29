// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/thermal.hpp"

#include <algorithm>
#include <cmath>

namespace fastcae {

ThermalResult solveThermal(const Mesh& mesh, const Material& mat,
                           const std::vector<TemperatureBC>& bcs,
                           const std::vector<HeatSource>& sources, double tol) {
    const int n = static_cast<int>(mesh.numNodes());
    std::vector<Triplet> trip;
    trip.reserve(mesh.numCells() * 16);

    for (std::size_t c = 0; c < mesh.numCells(); ++c) {
        const auto& t = mesh.tets[c];
        const Vec3& p0 = mesh.nodes[t[0]];
        double J[3][3];
        for (int a = 0; a < 3; ++a) {
            const Vec3& pa = mesh.nodes[t[a + 1]];
            J[0][a] = pa.x - p0.x; J[1][a] = pa.y - p0.y; J[2][a] = pa.z - p0.z;
        }
        const double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) -
                           J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0]) +
                           J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
        if (std::abs(det) < 1e-300) continue;
        const double vol = std::abs(det) / 6.0;
        double inv[3][3];
        inv[0][0] = (J[1][1] * J[2][2] - J[1][2] * J[2][1]) / det;
        inv[0][1] = (J[0][2] * J[2][1] - J[0][1] * J[2][2]) / det;
        inv[0][2] = (J[0][1] * J[1][2] - J[0][2] * J[1][1]) / det;
        inv[1][0] = (J[1][2] * J[2][0] - J[1][0] * J[2][2]) / det;
        inv[1][1] = (J[0][0] * J[2][2] - J[0][2] * J[2][0]) / det;
        inv[1][2] = (J[0][2] * J[1][0] - J[0][0] * J[1][2]) / det;
        inv[2][0] = (J[1][0] * J[2][1] - J[1][1] * J[2][0]) / det;
        inv[2][1] = (J[0][1] * J[2][0] - J[0][0] * J[2][1]) / det;
        inv[2][2] = (J[0][0] * J[1][1] - J[0][1] * J[1][0]) / det;
        double g[4][3];
        for (int a = 0; a < 3; ++a)
            for (int d = 0; d < 3; ++d) g[a + 1][d] = inv[a][d];
        for (int d = 0; d < 3; ++d) g[0][d] = -(g[1][d] + g[2][d] + g[3][d]);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                const double s = g[i][0] * g[j][0] + g[i][1] * g[j][1] + g[i][2] * g[j][2];
                trip.push_back({t[i], t[j], mat.k * vol * s});
            }
    }
    CsrMatrix K = buildCsr(n, trip);

    std::vector<double> f(n, 0.0);
    for (const auto& s : sources) f[s.node] += s.q;
    std::vector<int> dofs;
    std::vector<double> vals;
    for (const auto& b : bcs) {
        dofs.push_back(b.node);
        vals.push_back(b.value);
    }
    applyDirichlet(K, f, dofs, vals);

    ThermalResult res;
    res.T.assign(n, 0.0);
    for (std::size_t i = 0; i < dofs.size(); ++i) res.T[dofs[i]] = vals[i];
    res.linear = solveCg(K, f, res.T, tol);
    if (!res.T.empty()) {
        res.minT = *std::min_element(res.T.begin(), res.T.end());
        res.maxT = *std::max_element(res.T.begin(), res.T.end());
    }
    return res;
}

}  // namespace fastcae
