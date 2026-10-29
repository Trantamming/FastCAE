// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "cad/sketch_solver.hpp"

#include <algorithm>
#include <cmath>

namespace fastcae {
namespace {
constexpr double kPi = 3.14159265358979323846;
using Mat = std::vector<std::vector<double>>;

// Hạng bằng khử Gauss pivot toàn phần, ngưỡng tương đối.
int rankOf(Mat a, double relTol) {
    const int m = static_cast<int>(a.size());
    if (m == 0) return 0;
    const int n = static_cast<int>(a[0].size());
    double mx = 0;
    for (const auto& row : a) for (double v : row) mx = std::max(mx, std::fabs(v));
    if (mx == 0) return 0;
    const double tol = relTol * mx;
    int rank = 0;
    std::vector<bool> usedRow(m, false), usedCol(n, false);
    for (int step = 0; step < std::min(m, n); ++step) {
        double best = tol;
        int bi = -1, bj = -1;
        for (int i = 0; i < m; ++i) {
            if (usedRow[i]) continue;
            for (int j = 0; j < n; ++j) if (!usedCol[j] && std::fabs(a[i][j]) > best) { best = std::fabs(a[i][j]); bi = i; bj = j; }
        }
        if (bi < 0) break;
        usedRow[bi] = usedCol[bj] = true;
        ++rank;
        for (int i = 0; i < m; ++i) {
            if (i == bi) continue;
            const double f = a[i][bj] / a[bi][bj];
            if (f != 0) for (int j = 0; j < n; ++j) a[i][j] -= f * a[bi][j];
        }
    }
    return rank;
}

// Giải (A) x = b bằng Gauss có pivot từng phần; trả false nếu suy biến.
bool solveLinear(Mat A, std::vector<double> b, std::vector<double>& x) {
    const int n = static_cast<int>(b.size());
    for (int c = 0; c < n; ++c) {
        int p = c;
        for (int i = c + 1; i < n; ++i) if (std::fabs(A[i][c]) > std::fabs(A[p][c])) p = i;
        if (std::fabs(A[p][c]) < 1e-300) return false;
        std::swap(A[p], A[c]); std::swap(b[p], b[c]);
        for (int i = c + 1; i < n; ++i) {
            const double f = A[i][c] / A[c][c];
            for (int j = c; j < n; ++j) A[i][j] -= f * A[c][j];
            b[i] -= f * b[c];
        }
    }
    x.assign(n, 0);
    for (int i = n - 1; i >= 0; --i) {
        double s = b[i];
        for (int j = i + 1; j < n; ++j) s -= A[i][j] * x[j];
        x[i] = s / A[i][i];
    }
    return true;
}
}  // namespace

int SketchSystem::addPoint(double x, double y) {
    pts_.push_back(x); pts_.push_back(y);
    laidOut_ = false;
    return pointCount_++;
}
int SketchSystem::addLine(int p, int q) { lines_.push_back({p, q}); return static_cast<int>(lines_.size()) - 1; }
int SketchSystem::addCircle(int centerPoint, double radius) {
    circles_.push_back({centerPoint});
    radii0_.push_back(radius);
    laidOut_ = false;
    return static_cast<int>(circles_.size()) - 1;
}

void SketchSystem::layout() {
    if (laidOut_) return;
    x_ = pts_;
    radiusIndex_.clear();
    for (double r : radii0_) { radiusIndex_.push_back(static_cast<int>(x_.size())); x_.push_back(r); }
    laidOut_ = true;
}

int SketchSystem::rowsOf(const SketchConstraint& c) const {
    switch (c.type) {
        case SketchCon::Fix: case SketchCon::Coincident: case SketchCon::Midpoint: return 2;
        default: return 1;
    }
}

void SketchSystem::residuals(const std::vector<double>& x, std::vector<double>& r) const {
    r.clear();
    auto P = [&](int i, double& px, double& py) { px = x[2 * i]; py = x[2 * i + 1]; };
    auto L = [&](int li, double& x1, double& y1, double& x2, double& y2) {
        P(lines_[li].p, x1, y1); P(lines_[li].q, x2, y2);
    };
    for (const auto& c : cons_) {
        double ax, ay, bx, by, x1, y1, x2, y2, u1, v1, u2, v2;
        switch (c.type) {
            case SketchCon::Fix: P(c.a, ax, ay); r.push_back(ax - c.value); r.push_back(ay - c.value2); break;
            case SketchCon::Coincident: P(c.a, ax, ay); P(c.b, bx, by); r.push_back(ax - bx); r.push_back(ay - by); break;
            case SketchCon::Horizontal: P(c.a, ax, ay); P(c.b, bx, by); r.push_back(ay - by); break;
            case SketchCon::Vertical: P(c.a, ax, ay); P(c.b, bx, by); r.push_back(ax - bx); break;
            case SketchCon::Distance: P(c.a, ax, ay); P(c.b, bx, by); r.push_back(std::hypot(ax - bx, ay - by) - c.value); break;
            case SketchCon::Parallel: case SketchCon::Perpendicular: case SketchCon::Angle: {
                L(c.a, x1, y1, x2, y2); L(c.b, u1, v1, u2, v2);
                const double dx1 = x2 - x1, dy1 = y2 - y1, dx2 = u2 - u1, dy2 = v2 - v1;
                const double n1 = std::hypot(dx1, dy1), n2 = std::hypot(dx2, dy2);
                const double nn = std::max(n1 * n2, 1e-300);
                const double cross = (dx1 * dy2 - dy1 * dx2) / nn, dot = (dx1 * dx2 + dy1 * dy2) / nn;
                if (c.type == SketchCon::Parallel) r.push_back(cross);
                else if (c.type == SketchCon::Perpendicular) r.push_back(dot);
                else {
                    double d = std::atan2(cross, dot) - c.value;
                    while (d > kPi) d -= 2 * kPi;
                    while (d < -kPi) d += 2 * kPi;
                    r.push_back(d);
                }
                break;
            }
            case SketchCon::EqualLength: {
                L(c.a, x1, y1, x2, y2); L(c.b, u1, v1, u2, v2);
                r.push_back(std::hypot(x2 - x1, y2 - y1) - std::hypot(u2 - u1, v2 - v1));
                break;
            }
            case SketchCon::Radius: r.push_back(x[radiusIndex_[c.a]] - c.value); break;
            case SketchCon::EqualRadius: r.push_back(x[radiusIndex_[c.a]] - x[radiusIndex_[c.b]]); break;
            case SketchCon::PointOnLine: {
                P(c.a, ax, ay); L(c.b, x1, y1, x2, y2);
                const double len = std::max(std::hypot(x2 - x1, y2 - y1), 1e-300);
                r.push_back(((x2 - x1) * (ay - y1) - (y2 - y1) * (ax - x1)) / len);
                break;
            }
            case SketchCon::PointOnCircle: {
                P(c.a, ax, ay); P(circles_[c.b].c, bx, by);
                r.push_back(std::hypot(ax - bx, ay - by) - x[radiusIndex_[c.b]]);
                break;
            }
            case SketchCon::Tangent: {
                L(c.a, x1, y1, x2, y2); P(circles_[c.b].c, bx, by);
                const double len = std::max(std::hypot(x2 - x1, y2 - y1), 1e-300);
                const double d = ((x2 - x1) * (by - y1) - (y2 - y1) * (bx - x1)) / len;
                r.push_back(std::fabs(d) - x[radiusIndex_[c.b]]);
                break;
            }
            case SketchCon::TangentAtPoint: {
                L(c.a, x1, y1, x2, y2); P(circles_[c.b].c, bx, by); P(static_cast<int>(c.value), ax, ay);
                const double dx = x2 - x1, dy = y2 - y1, rx = ax - bx, ry = ay - by;
                const double nn = std::max(std::hypot(dx, dy) * std::hypot(rx, ry), 1e-300);
                r.push_back((dx * rx + dy * ry) / nn);
                break;
            }
            case SketchCon::Midpoint: {
                P(c.a, ax, ay); L(c.b, x1, y1, x2, y2);
                r.push_back(ax - 0.5 * (x1 + x2)); r.push_back(ay - 0.5 * (y1 + y2));
                break;
            }
        }
    }
}

SketchSolveResult SketchSystem::solve() {
    layout();
    SketchSolveResult res;
    const int n = static_cast<int>(x_.size());
    std::vector<double> r, rt;
    residuals(x_, r);
    const int m = static_cast<int>(r.size());
    if (n == 0) { res.message = "phác thảo rỗng"; return res; }

    auto jacobian = [&](const std::vector<double>& x, Mat& J) {
        J.assign(m, std::vector<double>(n, 0.0));
        std::vector<double> xp = x, rp, rm;
        for (int j = 0; j < n; ++j) {
            const double h = 1e-6 * std::max(1.0, std::fabs(x[j]));
            xp[j] = x[j] + h; residuals(xp, rp);
            xp[j] = x[j] - h; residuals(xp, rm);
            xp[j] = x[j];
            for (int i = 0; i < m; ++i) J[i][j] = (rp[i] - rm[i]) / (2 * h);
        }
    };

    auto norm2 = [](const std::vector<double>& v) { double s = 0; for (double e : v) s += e * e; return s; };
    double f = norm2(r), lambda = 1e-3;
    Mat J;
    int it = 0;
    for (; it < 400 && f > 1e-26; ++it) {
        jacobian(x_, J);
        Mat A(n, std::vector<double>(n, 0.0));
        std::vector<double> g(n, 0.0);
        for (int i = 0; i < m; ++i)
            for (int a = 0; a < n; ++a) {
                if (J[i][a] == 0) continue;
                g[a] -= J[i][a] * r[i];
                for (int b = 0; b < n; ++b) A[a][b] += J[i][a] * J[i][b];
            }
        bool improved = false;
        for (int tries = 0; tries < 30; ++tries) {
            Mat Al = A;
            for (int a = 0; a < n; ++a) Al[a][a] += lambda * std::max(A[a][a], 1e-12) + 1e-14;
            std::vector<double> d;
            if (!solveLinear(Al, g, d)) { lambda *= 10; continue; }
            std::vector<double> xn = x_;
            for (int a = 0; a < n; ++a) xn[a] += d[a];
            residuals(xn, rt);
            const double fn = norm2(rt);
            if (fn < f) { x_ = xn; r = rt; f = fn; lambda = std::max(lambda * 0.3, 1e-12); improved = true; break; }
            lambda *= 4;
        }
        if (!improved) break;
    }
    res.iterations = it;
    res.residual = std::sqrt(f);
    jacobian(x_, J);
    const int rank = rankOf(J, 1e-6);
    res.dof = n - rank;
    res.redundant = std::max(0, m - rank);
    if (res.residual > 1e-9) {
        res.ok = false;
        res.message = "Các ràng buộc mâu thuẫn hoặc không thể thỏa đồng thời (sai số còn " + std::to_string(res.residual) + " m)";
        return res;
    }
    res.ok = true;
    return res;
}

}  // namespace fastcae
