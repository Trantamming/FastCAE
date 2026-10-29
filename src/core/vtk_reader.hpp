// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Đọc file VTK legacy ASCII (UNSTRUCTURED_GRID) do OpenFOAM (foamToVTK), SU2, Code_Aster
// (qua meshio/ParaView), CalculiX (qua ccx2paraview) hoặc chính FastCAE ghi, để hiển thị lên khung 3D.
// Ô tứ diện/lục diện/lăng trụ/chóp được tách thành tứ diện; ô 2D (tam giác, tứ giác) bị bỏ qua và được đếm.
#pragma once
#include <string>

#include "core/mesh.hpp"
#include "core/vtk_writer.hpp"

namespace fastcae {

struct VtkReadInfo {
    int cellsRead = 0;       // số ô 3D đã đọc
    int tetsProduced = 0;    // số tứ diện sau khi tách
    bool extruded2d = false; // lưới 2D (tam giác/tứ giác phẳng) được đùn thành lớp mỏng để hiển thị trên khung 3D
    double extrudeThickness = 0;
    int cellsSkipped = 0;    // ô không hỗ trợ (2D, 1D, đa diện...)
};

bool readVtk(const std::string& path, Mesh& mesh, VtkFields& fields, VtkReadInfo* info = nullptr,
             std::string* error = nullptr);

}  // namespace fastcae
