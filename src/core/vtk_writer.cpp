// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "core/vtk_writer.hpp"

#include "core/about.hpp"

#include <fstream>
#include <iomanip>

namespace fastcae {

bool writeVtk(const std::string& path, const Mesh& mesh, const VtkFields& f, std::string* error) {
    std::ofstream os(path);
    if (!os) {
        if (error) *error = "Cannot open " + path + " for writing";
        return false;
    }
    os << std::setprecision(9);
    os << "# vtk DataFile Version 3.0\nFastCAE result - Tran Minh Tam (MIT)\nASCII\nDATASET UNSTRUCTURED_GRID\n";
    os << "POINTS " << mesh.numNodes() << " double\n";
    for (const auto& p : mesh.nodes) os << p.x << ' ' << p.y << ' ' << p.z << '\n';
    os << "CELLS " << mesh.numCells() << ' ' << mesh.numCells() * 5 << '\n';
    for (const auto& t : mesh.tets) os << "4 " << t[0] << ' ' << t[1] << ' ' << t[2] << ' ' << t[3] << '\n';
    os << "CELL_TYPES " << mesh.numCells() << '\n';
    for (std::size_t i = 0; i < mesh.numCells(); ++i) os << "10\n";

    if (!f.pointScalars.empty() || !f.pointVectors.empty()) {
        os << "POINT_DATA " << mesh.numNodes() << '\n';
        for (const auto& [name, v] : f.pointScalars) {
            os << "SCALARS " << name << " double 1\nLOOKUP_TABLE default\n";
            for (double x : v) os << x << '\n';
        }
        for (const auto& [name, v] : f.pointVectors) {
            os << "VECTORS " << name << " double\n";
            for (std::size_t i = 0; i + 2 < v.size(); i += 3) os << v[i] << ' ' << v[i + 1] << ' ' << v[i + 2] << '\n';
        }
    }
    if (!f.cellScalars.empty()) {
        os << "CELL_DATA " << mesh.numCells() << '\n';
        for (const auto& [name, v] : f.cellScalars) {
            os << "SCALARS " << name << " double 1\nLOOKUP_TABLE default\n";
            for (double x : v) os << x << '\n';
        }
    }
    return static_cast<bool>(os);
}

}  // namespace fastcae
