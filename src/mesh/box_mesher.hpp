// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include "core/mesh.hpp"

namespace fastcae {

// Structured box [0,lx]x[0,ly]x[0,lz] split into conforming tetrahedra
// (Kuhn/Freudenthal subdivision, 6 tets per hexahedron).
Mesh makeBoxMesh(double lx, double ly, double lz, int nx, int ny, int nz);

}  // namespace fastcae
