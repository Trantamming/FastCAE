// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
// Bridge to CalculiX (ccx): FastCAE writes the .inp deck, runs ccx as a separate
// process and reads the .frd results back. CalculiX is GPL; because it is only
// executed as an external program, FastCAE itself stays MIT.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/mesh.hpp"
#include <array>

#include "solver/elastic.hpp"

namespace fastcae {

struct CalculixOptions {
    std::string jobName = "fastcae_job";
    std::string workDir = ".";
    std::string executable;     // empty = auto-detect (env FASTCAE_CCX, then PATH)
};

// Write a linear static analysis deck: C3D4 tets, or C3D10 when `tets10` is given
// (node order is the same as FastCAE's T10: 4 corners then edges 1-2, 2-3, 3-1, 1-4, 2-4, 3-4).
// Node/element ids are 1-based.
bool writeCalculixInp(const std::string& path, const Mesh& mesh, const Material& mat,
                      const std::vector<DisplacementBC>& bcs, const std::vector<NodalForce>& loads,
                      std::string* error = nullptr,
                      const std::vector<std::array<int, 10>>* tets10 = nullptr);

// Parsed nodal results of an ASCII .frd file: block name -> (node index -> components).
// Block names as written by ccx: "DISP" (3 comps), "STRESS" (6 comps: SXX SYY SZZ SXY SYZ SZX).
struct FrdResults {
    std::map<std::string, std::map<int, std::vector<double>>> blocks;
};
bool readFrd(const std::string& path, FrdResults& out, std::string* error = nullptr);

// Locate the ccx executable; empty string if not found.
std::string findCalculix(const std::string& hint = "");

struct CalculixRun {
    bool ok = false;
    std::string message;
    std::string inpPath, frdPath;
    ElasticResult result;   // filled when ok
};

// Full round trip: write deck, run ccx, parse results into an ElasticResult.
CalculixRun runCalculix(const Mesh& mesh, const Material& mat, const std::vector<DisplacementBC>& bcs,
                        const std::vector<NodalForce>& loads, const CalculixOptions& opt = {},
                        const std::vector<std::array<int, 10>>* tets10 = nullptr);

}  // namespace fastcae
