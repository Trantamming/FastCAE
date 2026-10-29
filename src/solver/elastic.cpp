// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/elastic.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace fastcae {

namespace {

// Gradients of the 4 linear shape functions and element volume.
bool tetGradients(const Mesh& m, std::size_t c, double g[4][3], double& vol) {
    const auto& t = m.tets[c];
    const Vec3& p0 = m.nodes[t[0]];
    double J[3][3];  // columns are edge vectors
    for (int a = 0; a < 3; ++a) {
        const Vec3& pa = m.nodes[t[a + 1]];
        J[0][a] = pa.x - p0.x;
        J[1][a] = pa.y - p0.y;
        J[2][a] = pa.z - p0.z;
    }
    const double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) -
                       J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0]) +
                       J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
    vol = std::abs(det) / 6.0;
    if (std::abs(det) < 1e-300) return false;
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
    for (int a = 0; a < 3; ++a)
        for (int d = 0; d < 3; ++d) g[a + 1][d] = inv[a][d];
    for (int d = 0; d < 3; ++d) g[0][d] = -(g[1][d] + g[2][d] + g[3][d]);
    return true;
}

void buildD(const Material& mat, double D[6][6]) {
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) D[i][j] = 0;
    const double lam = mat.E * mat.nu / ((1 + mat.nu) * (1 - 2 * mat.nu));
    const double mu = mat.E / (2 * (1 + mat.nu));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) D[i][j] = lam + (i == j ? 2 * mu : 0);
    D[3][3] = D[4][4] = D[5][5] = mu;
}

void buildB(const double g[4][3], double B[6][12]) {
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 12; ++j) B[i][j] = 0;
    for (int a = 0; a < 4; ++a) {
        const double gx = g[a][0], gy = g[a][1], gz = g[a][2];
        B[0][3 * a] = gx;
        B[1][3 * a + 1] = gy;
        B[2][3 * a + 2] = gz;
        B[3][3 * a] = gy; B[3][3 * a + 1] = gx;
        B[4][3 * a + 1] = gz; B[4][3 * a + 2] = gy;
        B[5][3 * a] = gz; B[5][3 * a + 2] = gx;
    }
}

}  // namespace

ElasticResult solveElastic(const Mesh& mesh, const Material& mat,
                           const std::vector<DisplacementBC>& bcs,
                           const std::vector<NodalForce>& loads, double tol) {
    const int nd = static_cast<int>(mesh.numNodes()) * 3;
    double D[6][6];
    buildD(mat, D);

    std::vector<Triplet> trip;
    trip.reserve(mesh.numCells() * 144);
    for (std::size_t c = 0; c < mesh.numCells(); ++c) {
        double g[4][3], vol;
        if (!tetGradients(mesh, c, g, vol)) continue;
        double B[6][12];
        buildB(g, B);
        double DB[6][12];
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 12; ++j) {
                double s = 0;
                for (int k = 0; k < 6; ++k) s += D[i][k] * B[k][j];
                DB[i][j] = s;
            }
        const auto& t = mesh.tets[c];
        for (int i = 0; i < 12; ++i)
            for (int j = 0; j < 12; ++j) {
                double s = 0;
                for (int k = 0; k < 6; ++k) s += B[k][i] * DB[k][j];
                trip.push_back({3 * t[i / 3] + i % 3, 3 * t[j / 3] + j % 3, s * vol});
            }
    }
    CsrMatrix K = buildCsr(nd, trip);

    std::vector<double> f(nd, 0.0);
    for (const auto& l : loads) {
        f[3 * l.node] += l.f.x;
        f[3 * l.node + 1] += l.f.y;
        f[3 * l.node + 2] += l.f.z;
    }
    std::vector<int> dofs;
    std::vector<double> vals;
    for (const auto& b : bcs) {
        dofs.push_back(3 * b.node + b.component);
        vals.push_back(b.value);
    }
    applyDirichlet(K, f, dofs, vals);

    ElasticResult res;
    res.u.assign(nd, 0.0);
    for (std::size_t i = 0; i < dofs.size(); ++i) res.u[dofs[i]] = vals[i];
    res.linear = solveCg(K, f, res.u, tol);

    // Post-processing: von Mises per cell and volume-weighted nodal average.
    res.vonMises.assign(mesh.numCells(), 0.0);
    res.vonMisesNodal.assign(mesh.numNodes(), 0.0);
    std::vector<double> wsum(mesh.numNodes(), 0.0);
    for (std::size_t c = 0; c < mesh.numCells(); ++c) {
        double g[4][3], vol;
        if (!tetGradients(mesh, c, g, vol)) continue;
        double B[6][12];
        buildB(g, B);
        const auto& t = mesh.tets[c];
        double ue[12];
        for (int i = 0; i < 12; ++i) ue[i] = res.u[3 * t[i / 3] + i % 3];
        double eps[6] = {0, 0, 0, 0, 0, 0}, s[6] = {0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 12; ++j) eps[i] += B[i][j] * ue[j];
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j) s[i] += D[i][j] * eps[j];
        const double vm = std::sqrt(0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) +
                                           (s[2] - s[0]) * (s[2] - s[0])) +
                                    3.0 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
        res.vonMises[c] = vm;
        res.maxVonMises = std::max(res.maxVonMises, vm);
        for (int a = 0; a < 4; ++a) {
            res.vonMisesNodal[t[a]] += vm * vol;
            wsum[t[a]] += vol;
        }
    }
    for (std::size_t i = 0; i < mesh.numNodes(); ++i)
        if (wsum[i] > 0) res.vonMisesNodal[i] /= wsum[i];

    res.dispMagnitude.assign(mesh.numNodes(), 0.0);
    for (std::size_t i = 0; i < mesh.numNodes(); ++i) {
        const double m = std::sqrt(res.u[3 * i] * res.u[3 * i] + res.u[3 * i + 1] * res.u[3 * i + 1] +
                                   res.u[3 * i + 2] * res.u[3 * i + 2]);
        res.dispMagnitude[i] = m;
        res.maxDisplacement = std::max(res.maxDisplacement, m);
    }
    return res;
}

}  // namespace fastcae
