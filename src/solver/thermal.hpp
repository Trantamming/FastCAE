// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <vector>

#include "core/mesh.hpp"
#include "solver/elastic.hpp"  // Material
#include "solver/sparse.hpp"

namespace fastcae {

struct TemperatureBC {
    int node;
    double value;  // [K or C]
};

struct HeatSource {
    int node;
    double q;  // nodal heat input [W]
};

struct ThermalResult {
    std::vector<double> T;
    double minT = 0, maxT = 0;
    CgResult linear;
};

// Steady-state heat conduction, 4-node tetrahedra.
ThermalResult solveThermal(const Mesh& mesh, const Material& mat,
                           const std::vector<TemperatureBC>& bcs,
                           const std::vector<HeatSource>& sources, double tol = 1e-12);

}  // namespace fastcae
