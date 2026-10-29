// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
// 10-node quadratic tetrahedra (T10) + consistent boundary load distribution.
#pragma once
#include <array>
#include <vector>

#include "core/mesh.hpp"
#include "solver/elastic.hpp"
#include "solver/thermal.hpp"

namespace fastcae {

// Edge-node order (nodes 4..9 of a T10): (0,1) (1,2) (0,2) (0,3) (1,3) (2,3).
// This is the same order as CalculiX/Abaqus C3D10 (5:1-2, 6:2-3, 7:3-1, 8:1-4, 9:2-4, 10:3-4).
using Tet10 = std::array<int, 10>;

struct QuadMesh {
    // base.nodes = original corner nodes followed by the mid-edge nodes;
    // base.tets  = the original 4-node cells (used for display and face detection).
    Mesh base;
    std::vector<Tet10> tets10;
    std::size_t numCornerNodes = 0;
    // sorted-pair edge key -> mid node index
    std::vector<std::pair<unsigned long long, int>> edgeMid;

    int midNode(int a, int b) const;   // -1 if the edge does not exist
};

QuadMesh makeQuadratic(const Mesh& linear);

// Distribute a total vector (force) or scalar (heat) over the boundary faces whose
// three corner nodes all lie in `nodeSet`, weighting by face area. For T4 every face
// node gets area/3; for T10 the corner nodes get 0 and each mid-edge node gets area/3
// (the consistent load of a quadratic triangle). If no face qualifies, the total is
// spread equally over the set (fallback, e.g. for point-like sets).
std::vector<NodalForce> distributeForce(const Mesh& mesh, const QuadMesh* quad,
                                        const std::vector<int>& nodeSet, const Vec3& total);
std::vector<HeatSource> distributeHeat(const Mesh& mesh, const QuadMesh* quad,
                                       const std::vector<int>& nodeSet, double totalWatts);

ElasticResult solveElasticT10(const QuadMesh& qm, const Material& mat,
                              const std::vector<DisplacementBC>& bcs,
                              const std::vector<NodalForce>& loads, double tol = 1e-10);

ThermalResult solveThermalT10(const QuadMesh& qm, const Material& mat,
                              const std::vector<TemperatureBC>& bcs,
                              const std::vector<HeatSource>& sources, double tol = 1e-12);

}  // namespace fastcae
