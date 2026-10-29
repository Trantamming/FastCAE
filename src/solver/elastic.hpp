// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <vector>

#include "core/mesh.hpp"
#include "solver/sparse.hpp"

namespace fastcae {

struct Material {
    double E = 210e9;   // Young's modulus [Pa]
    double nu = 0.3;    // Poisson ratio
    double k = 50.0;    // thermal conductivity [W/(m K)]
    double rho = 7850;  // density [kg/m3]
};

struct DisplacementBC {
    int node;
    int component;  // 0=x, 1=y, 2=z
    double value;
};

struct NodalForce {
    int node;
    Vec3 f;
};

struct ElasticResult {
    std::vector<double> u;         // 3 per node
    std::vector<double> vonMises;  // per cell [Pa]
    std::vector<double> vonMisesNodal;  // averaged to nodes
    std::vector<double> dispMagnitude;  // per node
    double maxDisplacement = 0;
    double maxVonMises = 0;
    CgResult linear;
};

// Static linear elasticity, 4-node tetrahedra.
ElasticResult solveElastic(const Mesh& mesh, const Material& mat,
                           const std::vector<DisplacementBC>& bcs,
                           const std::vector<NodalForce>& loads, double tol = 1e-10);

}  // namespace fastcae
