// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Một header cho script C++ của FastCAE: #include "sdk/fastcae_all.hpp" (tự thêm khi script không có main()).
#pragma once
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "app/case_runner.hpp"
#include "core/case_file.hpp"
#include "core/mesh.hpp"
#include "core/paths.hpp"
#include "solver/external_run.hpp"
#include "solver/solver_registry.hpp"

namespace fc {

// Chạy một case (văn bản giống file .fcae), in nhật ký ra std::cout.
inline fastcae::RunReport run(const std::string& caseText, const std::string& baseDir = "") {
    fastcae::CaseFile cf;
    cf.parse(caseText);
    std::ostringstream log;
    auto r = fastcae::runCase(cf, log, baseDir, true);
    std::cout << log.str();
    return r;
}

// Chạy solver mã nguồn mở bên ngoài, đầu ra hiện ngay trên std::cout.
//   fc::solver("code_aster", "model.export");  fc::solver("su2", "case.cfg", "-n 4");
//   fc::solver("openradioss", "model_0000.rad");  fc::solver("openfoam", "caseDir", "", {"blockMesh","simpleFoam"});
inline fastcae::ExternalRunResult solver(const std::string& id, const std::string& input, const std::string& extraArgs = "",
                                         const std::vector<std::string>& commands = {}, const std::string& workDir = "") {
    fastcae::ExternalRunOptions o;
    o.solverId = id;
    o.input = input;
    o.extraArgs = extraArgs;
    o.commands = commands;
    o.workDir = workDir;
    o.onOutput = [](const std::string& s) { std::cout << s << std::flush; };
    auto r = fastcae::runExternalSolver(o);
    std::cout << (r.ok ? "[OK] " : "[LOI] ") << r.message << "\n";
    return r;
}

// Chạy chương trình/lệnh bất kỳ (python, gmsh, paraview, một script riêng...).
inline int shell(const std::string& command, const std::string& workDir = "") {
    return fastcae::runCommandLine(command, workDir, [](const std::string& s) { std::cout << s << std::flush; });
}

inline std::string workDir(const std::string& sub = "") { return fastcae::userWorkRoot() + (sub.empty() ? "" : "/" + sub); }

}  // namespace fc

using namespace fastcae;
