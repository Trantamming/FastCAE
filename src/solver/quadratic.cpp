// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/quadratic.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace fastcae {

namespace {

constexpr int kEdge[6][2] = {{0, 1}, {1, 2}, {0, 2}, {0, 3}, {1, 3}, {2, 3}};

unsigned long long edgeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<unsigned long long>(a) << 32) | static_cast<unsigned int>(b);
}

// Gradients of the 4 barycentric coordinates and the volume of a (straight-edged) tet.
bool baryGradients(const Mesh& m, const Tet& t, double g[4][3], double& vol) {
    const Vec3& p0 = m.nodes[t[0]];
    double J[3][3];
    for (int a = 0; a < 3; ++a) {
        const Vec3& pa = m.nodes[t[a + 1]];
        J[0][a] = pa.x - p0.x; J[1][a] = pa.y - p0.y; J[2][a] = pa.z - p0.z;
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

// Physical gradients of the 10 quadratic shape functions at barycentric point L.
void shapeGrad(const double L[4], const double g[4][3], double dN[10][3]) {
    double dL[10][4] = {};
    for (int i = 0; i < 4; ++i) dL[i][i] = 4 * L[i] - 1;
    for (int e = 0; e < 6; ++e) {
        const int i = kEdge[e][0], j = kEdge[e][1];
        dL[4 + e][i] = 4 * L[j];
        dL[4 + e][j] = 4 * L[i];
    }
    for (int a = 0; a < 10; ++a)
        for (int d = 0; d < 3; ++d) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += dL[a][k] * g[k][d];
            dN[a][d] = s;
        }
}

// 4-point degree-2 rule.
constexpr double kA = 0.5854101966249685, kB = 0.1381966011250105;
const double kGauss[4][4] = {{kA, kB, kB, kB}, {kB, kA, kB, kB}, {kB, kB, kA, kB}, {kB, kB, kB, kA}};

void buildD(const Material& mat, double D[6][6]) {
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) D[i][j] = 0;
    const double lam = mat.E * mat.nu / ((1 + mat.nu) * (1 - 2 * mat.nu));
    const double mu = mat.E / (2 * (1 + mat.nu));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) D[i][j] = lam + (i == j ? 2 * mu : 0);
    D[3][3] = D[4][4] = D[5][5] = mu;
}

void buildB(const double dN[10][3], double B[6][30]) {
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 30; ++j) B[i][j] = 0;
    for (int a = 0; a < 10; ++a) {
        const double gx = dN[a][0], gy = dN[a][1], gz = dN[a][2];
        B[0][3 * a] = gx;
        B[1][3 * a + 1] = gy;
        B[2][3 * a + 2] = gz;
        B[3][3 * a] = gy; B[3][3 * a + 1] = gx;
        B[4][3 * a + 1] = gz; B[4][3 * a + 2] = gy;
        B[5][3 * a] = gz; B[5][3 * a + 2] = gx;
    }
}

double vonMises(const double s[6]) {
    return std::sqrt(0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) + (s[2] - s[0]) * (s[2] - s[0])) +
                     3.0 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
}

}  // namespace

int QuadMesh::midNode(int a, int b) const {
    const unsigned long long k = edgeKey(a, b);
    auto it = std::lower_bound(edgeMid.begin(), edgeMid.end(), std::make_pair(k, -1));
    return (it != edgeMid.end() && it->first == k) ? it->second : -1;
}

QuadMesh makeQuadratic(const Mesh& lin) {
    QuadMesh q;
    q.base = lin;
    q.numCornerNodes = lin.nodes.size();
    std::map<unsigned long long, int> mids;
    q.tets10.reserve(lin.tets.size());
    for (const auto& t : lin.tets) {
        Tet10 e;
        for (int i = 0; i < 4; ++i) e[i] = t[i];
        for (int k = 0; k < 6; ++k) {
            const int a = t[kEdge[k][0]], b = t[kEdge[k][1]];
            const auto key = edgeKey(a, b);
            auto it = mids.find(key);
            if (it == mids.end()) {
                const Vec3 &pa = lin.nodes[a], &pb = lin.nodes[b];
                q.base.nodes.push_back({(pa.x + pb.x) / 2, (pa.y + pb.y) / 2, (pa.z + pb.z) / 2});
                it = mids.emplace(key, int(q.base.nodes.size()) - 1).first;
            }
            e[4 + k] = it->second;
        }
        q.tets10.push_back(e);
    }
    q.edgeMid.assign(mids.begin(), mids.end());
    return q;
}

// ---------------------------------------------------------------- boundary loads
namespace {

struct FaceShare { std::vector<std::pair<int, double>> share; double area = 0; };

// Boundary faces fully inside `set`, with the per-node share of unit total load.
std::vector<std::pair<int, double>> faceWeights(const Mesh& m, const QuadMesh* q, const std::vector<int>& set) {
    std::set<int> in(set.begin(), set.end());
    static const int F[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    // Count faces to find the boundary ones.
    std::map<std::array<int, 3>, int> count;
    for (const auto& t : m.tets)
        for (const auto& f : F) {
            std::array<int, 3> k = {t[f[0]], t[f[1]], t[f[2]]};
            std::sort(k.begin(), k.end());
            ++count[k];
        }
    std::map<int, double> w;
    double total = 0;
    for (const auto& t : m.tets)
        for (const auto& f : F) {
            const int a = t[f[0]], b = t[f[1]], c = t[f[2]];
            if (!in.count(a) || !in.count(b) || !in.count(c)) continue;
            std::array<int, 3> k = {a, b, c};
            std::sort(k.begin(), k.end());
            if (count[k] != 1) continue;
            const Vec3 &pa = m.nodes[a], &pb = m.nodes[b], &pc = m.nodes[c];
            const double ux = pb.x - pa.x, uy = pb.y - pa.y, uz = pb.z - pa.z;
            const double vx = pc.x - pa.x, vy = pc.y - pa.y, vz = pc.z - pa.z;
            const double area = 0.5 * std::sqrt((uy * vz - uz * vy) * (uy * vz - uz * vy) +
                                                (uz * vx - ux * vz) * (uz * vx - ux * vz) +
                                                (ux * vy - uy * vx) * (ux * vy - uy * vx));
            total += area;
            if (q) {
                const int ids[3] = {q->midNode(a, b), q->midNode(b, c), q->midNode(a, c)};
                for (int id : ids) if (id >= 0) w[id] += area / 3.0;
            } else {
                w[a] += area / 3.0; w[b] += area / 3.0; w[c] += area / 3.0;
            }
        }
    std::vector<std::pair<int, double>> out;
    if (total <= 0) return out;
    for (const auto& [n, v] : w) out.push_back({n, v / total});
    return out;
}

}  // namespace

std::vector<NodalForce> distributeForce(const Mesh& mesh, const QuadMesh* quad,
                                        const std::vector<int>& nodeSet, const Vec3& total) {
    std::vector<NodalForce> out;
    auto w = faceWeights(mesh, quad, nodeSet);
    if (w.empty()) {
        const double s = nodeSet.empty() ? 0.0 : 1.0 / nodeSet.size();
        for (int n : nodeSet) out.push_back({n, {total.x * s, total.y * s, total.z * s}});
        return out;
    }
    for (const auto& [n, f] : w) out.push_back({n, {total.x * f, total.y * f, total.z * f}});
    return out;
}

std::vector<HeatSource> distributeHeat(const Mesh& mesh, const QuadMesh* quad,
                                       const std::vector<int>& nodeSet, double totalWatts) {
    std::vector<HeatSource> out;
    auto w = faceWeights(mesh, quad, nodeSet);
    if (w.empty()) {
        const double s = nodeSet.empty() ? 0.0 : 1.0 / nodeSet.size();
        for (int n : nodeSet) out.push_back({n, totalWatts * s});
        return out;
    }
    for (const auto& [n, f] : w) out.push_back({n, totalWatts * f});
    return out;
}

// ---------------------------------------------------------------- elasticity
ElasticResult solveElasticT10(const QuadMesh& qm, const Material& mat,
                              const std::vector<DisplacementBC>& bcs,
                              const std::vector<NodalForce>& loads, double tol) {
    const Mesh& m = qm.base;
    const int n = static_cast<int>(m.numNodes());
    const int nd = 3 * n;
    double D[6][6];
    buildD(mat, D);

    std::vector<Triplet> trip;
    trip.reserve(qm.tets10.size() * 900);
    for (std::size_t c = 0; c < qm.tets10.size(); ++c) {
        double g[4][3], vol;
        if (!baryGradients(m, m.tets[c], g, vol)) continue;
        double Ke[30][30] = {};
        for (const auto& L : kGauss) {
            double dN[10][3], B[6][30], DB[6][30];
            shapeGrad(L, g, dN);
            buildB(dN, B);
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 30; ++j) {
                    double s = 0;
                    for (int k = 0; k < 6; ++k) s += D[i][k] * B[k][j];
                    DB[i][j] = s;
                }
            const double w = vol * 0.25;
            for (int i = 0; i < 30; ++i)
                for (int j = 0; j < 30; ++j) {
                    double s = 0;
                    for (int k = 0; k < 6; ++k) s += B[k][i] * DB[k][j];
                    Ke[i][j] += w * s;
                }
        }
        const auto& t = qm.tets10[c];
        for (int i = 0; i < 30; ++i)
            for (int j = 0; j < 30; ++j)
                trip.push_back({3 * t[i / 3] + i % 3, 3 * t[j / 3] + j % 3, Ke[i][j]});
    }
    CsrMatrix K = buildCsr(nd, trip);
    trip.clear();
    trip.shrink_to_fit();

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
    res.linear = solveCg(K, f, res.u, tol, 100000);

    // Stress recovery: evaluate at the 4 corners and 6 mid-edge points, volume-weighted nodal averaging; cell value at centroid.
    res.vonMises.assign(qm.tets10.size(), 0.0);
    res.vonMisesNodal.assign(n, 0.0);
    std::vector<double> wsum(n, 0.0);
    for (std::size_t c = 0; c < qm.tets10.size(); ++c) {
        double g[4][3], vol;
        if (!baryGradients(m, m.tets[c], g, vol)) continue;
        const auto& t = qm.tets10[c];
        double ue[30];
        for (int i = 0; i < 30; ++i) ue[i] = res.u[3 * t[i / 3] + i % 3];
        auto stressAt = [&](const double L[4]) {
            double dN[10][3], B[6][30], eps[6] = {}, s[6] = {};
            shapeGrad(L, g, dN);
            buildB(dN, B);
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 30; ++j) eps[i] += B[i][j] * ue[j];
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 6; ++j) s[i] += D[i][j] * eps[j];
            return vonMises(s);
        };
        const double centroid[4] = {0.25, 0.25, 0.25, 0.25};
        res.vonMises[c] = stressAt(centroid);
        for (int a = 0; a < 4; ++a) {
            double L[4] = {0, 0, 0, 0};
            L[a] = 1;
            const double vm = stressAt(L);
            res.vonMisesNodal[t[a]] += vm * vol;
            wsum[t[a]] += vol;
        }
        // Nút giữa cạnh (thứ tự T10: 1-2, 2-3, 3-1, 1-4, 2-4, 3-4): tính tại trung điểm cạnh, cũng trung bình theo thể tích.
        // Trước đây bỏ qua nên các nút này luôn bằng 0 (VTK/CSV/.fcres có số 0 giả, thang màu luôn bắt đầu từ 0).
        static const int edge[6][2] = {{0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}};
        for (int e = 0; e < 6; ++e) {
            double L[4] = {0, 0, 0, 0};
            L[edge[e][0]] = 0.5;
            L[edge[e][1]] = 0.5;
            const double vm = stressAt(L);
            res.vonMisesNodal[t[4 + e]] += vm * vol;
            wsum[t[4 + e]] += vol;
        }
    }
    for (int i = 0; i < n; ++i) {
        if (wsum[i] > 0) res.vonMisesNodal[i] /= wsum[i];
        res.maxVonMises = std::max(res.maxVonMises, res.vonMisesNodal[i]);
    }
    res.dispMagnitude.assign(n, 0.0);
    for (int i = 0; i < n; ++i) {
        const double mag = std::sqrt(res.u[3 * i] * res.u[3 * i] + res.u[3 * i + 1] * res.u[3 * i + 1] +
                                     res.u[3 * i + 2] * res.u[3 * i + 2]);
        res.dispMagnitude[i] = mag;
        res.maxDisplacement = std::max(res.maxDisplacement, mag);
    }
    return res;
}

// ---------------------------------------------------------------- heat conduction
ThermalResult solveThermalT10(const QuadMesh& qm, const Material& mat,
                              const std::vector<TemperatureBC>& bcs,
                              const std::vector<HeatSource>& sources, double tol) {
    const Mesh& m = qm.base;
    const int n = static_cast<int>(m.numNodes());
    std::vector<Triplet> trip;
    trip.reserve(qm.tets10.size() * 100);
    for (std::size_t c = 0; c < qm.tets10.size(); ++c) {
        double g[4][3], vol;
        if (!baryGradients(m, m.tets[c], g, vol)) continue;
        double Ke[10][10] = {};
        for (const auto& L : kGauss) {
            double dN[10][3];
            shapeGrad(L, g, dN);
            const double w = vol * 0.25 * mat.k;
            for (int i = 0; i < 10; ++i)
                for (int j = 0; j < 10; ++j)
                    Ke[i][j] += w * (dN[i][0] * dN[j][0] + dN[i][1] * dN[j][1] + dN[i][2] * dN[j][2]);
        }
        const auto& t = qm.tets10[c];
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j) trip.push_back({t[i], t[j], Ke[i][j]});
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
    res.linear = solveCg(K, f, res.T, tol, 100000);
    // Only nodes that belong to cells are meaningful for min/max.
    res.minT = 1e300;
    res.maxT = -1e300;
    for (const auto& t : qm.tets10)
        for (int a : t) {
            res.minT = std::min(res.minT, res.T[a]);
            res.maxT = std::max(res.maxT, res.T[a]);
        }
    return res;
}

}  // namespace fastcae
