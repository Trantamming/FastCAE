// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <string>
#include <vector>

namespace fastcae {

// 2D incompressible lid-driven cavity on the unit square, solved with the
// stream-function / vorticity formulation (second-order central differences,
// explicit pseudo-time marching + SOR for the Poisson equation).
// This is FastCAE's first CFD module; 3D finite-volume CFD is on the roadmap.
struct CavityParams {
    int n = 41;           // grid points per side
    double Re = 100.0;    // Reynolds number
    double lidSpeed = 1;  // top-lid velocity
    int maxSteps = 60000;
    double tol = 1e-8;    // change in vorticity per step to stop
};

struct CavityResult {
    int n = 0;
    double h = 0;
    std::vector<double> psi, omega, u, v;  // size n*n, index j*n+i
    int steps = 0;
    double change = 0;
    bool converged = false;

    double uAt(int i, int j) const { return u[static_cast<std::size_t>(j) * n + i]; }
    double vAt(int i, int j) const { return v[static_cast<std::size_t>(j) * n + i]; }
};

CavityResult solveCavity(const CavityParams& p);

// Legacy VTK STRUCTURED_POINTS file (u,v,psi,omega) for ParaView / FastCAE GUI.
bool writeCavityVtk(const std::string& path, const CavityResult& r);

}  // namespace fastcae
