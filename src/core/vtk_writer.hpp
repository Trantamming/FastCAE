// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/mesh.hpp"

namespace fastcae {

struct VtkFields {
    // name -> one value per node
    std::map<std::string, std::vector<double>> pointScalars;
    // name -> 3 values per node (interleaved)
    std::map<std::string, std::vector<double>> pointVectors;
    // name -> one value per cell
    std::map<std::string, std::vector<double>> cellScalars;
};

// Legacy ASCII VTK unstructured grid; opens in ParaView and in FastCAE GUI.
bool writeVtk(const std::string& path, const Mesh& mesh, const VtkFields& fields,
              std::string* error = nullptr);

}  // namespace fastcae
