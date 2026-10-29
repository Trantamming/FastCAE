// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/cavity_cfd.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>

namespace fastcae {

CavityResult solveCavity(const CavityParams& p) {
    const int n = std::max(p.n, 5);
    const double h = 1.0 / (n - 1);
    const std::size_t N = static_cast<std::size_t>(n) * n;
    auto id = [n](int i, int j) { return static_cast<std::size_t>(j) * n + i; };

    CavityResult r;
    r.n = n;
    r.h = h;
    r.psi.assign(N, 0.0);
    r.omega.assign(N, 0.0);
    r.u.assign(N, 0.0);
    r.v.assign(N, 0.0);

    const double U = p.lidSpeed;
    const double dt = 0.5 * std::min(0.25 * h * h * p.Re, 0.5 * h / std::max(std::abs(U), 1e-12));
    std::vector<double> wn(N, 0.0);

    for (int step = 1; step <= p.maxSteps; ++step) {
        // Poisson: laplacian(psi) = -omega, SOR sweeps.
        const double w = 2.0 / (1.0 + std::sin(std::numbers::pi / (n - 1)));  // near-optimal SOR factor
        for (int sweep = 0; sweep < 30; ++sweep) {
            for (int j = 1; j < n - 1; ++j)
                for (int i = 1; i < n - 1; ++i) {
                    const double gs = 0.25 * (r.psi[id(i + 1, j)] + r.psi[id(i - 1, j)] + r.psi[id(i, j + 1)] +
                                              r.psi[id(i, j - 1)] + h * h * r.omega[id(i, j)]);
                    r.psi[id(i, j)] += w * (gs - r.psi[id(i, j)]);
                }
        }
        // Velocities from psi: u = dpsi/dy, v = -dpsi/dx.
        for (int j = 1; j < n - 1; ++j)
            for (int i = 1; i < n - 1; ++i) {
                r.u[id(i, j)] = (r.psi[id(i, j + 1)] - r.psi[id(i, j - 1)]) / (2 * h);
                r.v[id(i, j)] = -(r.psi[id(i + 1, j)] - r.psi[id(i - 1, j)]) / (2 * h);
            }
        for (int i = 0; i < n; ++i) r.u[id(i, n - 1)] = U;  // lid

        // Wall vorticity (Thom's formula).
        for (int i = 0; i < n; ++i) {
            r.omega[id(i, 0)] = -2.0 * r.psi[id(i, 1)] / (h * h);
            r.omega[id(i, n - 1)] = -2.0 * r.psi[id(i, n - 2)] / (h * h) - 2.0 * U / h;
        }
        for (int j = 0; j < n; ++j) {
            r.omega[id(0, j)] = -2.0 * r.psi[id(1, j)] / (h * h);
            r.omega[id(n - 1, j)] = -2.0 * r.psi[id(n - 2, j)] / (h * h);
        }

        // Vorticity transport, explicit Euler.
        double change = 0;
        wn = r.omega;
        for (int j = 1; j < n - 1; ++j)
            for (int i = 1; i < n - 1; ++i) {
                const double wx = (r.omega[id(i + 1, j)] - r.omega[id(i - 1, j)]) / (2 * h);
                const double wy = (r.omega[id(i, j + 1)] - r.omega[id(i, j - 1)]) / (2 * h);
                const double lap = (r.omega[id(i + 1, j)] + r.omega[id(i - 1, j)] + r.omega[id(i, j + 1)] +
                                    r.omega[id(i, j - 1)] - 4 * r.omega[id(i, j)]) / (h * h);
                const double nw = r.omega[id(i, j)] +
                                  dt * (-r.u[id(i, j)] * wx - r.v[id(i, j)] * wy + lap / p.Re);
                change = std::max(change, std::abs(nw - r.omega[id(i, j)]));
                wn[id(i, j)] = nw;
            }
        r.omega.swap(wn);
        r.steps = step;
        r.change = change / dt;
        if (step > 200 && r.change < p.tol) {
            r.converged = true;
            break;
        }
    }
    return r;
}

bool writeCavityVtk(const std::string& path, const CavityResult& r) {
    std::ofstream os(path);
    if (!os) return false;
    const std::size_t N = static_cast<std::size_t>(r.n) * r.n;
    os << "# vtk DataFile Version 3.0\nFastCAE cavity\nASCII\nDATASET STRUCTURED_POINTS\n";
    os << "DIMENSIONS " << r.n << ' ' << r.n << " 1\nORIGIN 0 0 0\nSPACING " << r.h << ' ' << r.h << " 1\n";
    os << "POINT_DATA " << N << "\nVECTORS velocity double\n";
    for (std::size_t k = 0; k < N; ++k) os << r.u[k] << ' ' << r.v[k] << " 0\n";
    os << "SCALARS streamfunction double 1\nLOOKUP_TABLE default\n";
    for (double x : r.psi) os << x << '\n';
    os << "SCALARS vorticity double 1\nLOOKUP_TABLE default\n";
    for (double x : r.omega) os << x << '\n';
    return static_cast<bool>(os);
}

}  // namespace fastcae
