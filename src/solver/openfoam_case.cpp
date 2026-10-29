// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/openfoam_case.hpp"

#include "solver/openfoam_results.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace fastcae {

namespace {

std::string header(const char* cls, const char* object, const char* location = nullptr) {
    std::ostringstream o;
    o << "/*--------------------------------*- C++ -*----------------------------------*\\\n"
         "  Tao boi FastCAE - Tac gia: Tran Minh Tam (MIT)\n"
         "\\*---------------------------------------------------------------------------*/\n"
         "FoamFile\n{\n    version     2.0;\n    format      ascii;\n    class       "
      << cls << ";\n";
    if (location) o << "    location    \"" << location << "\";\n";
    o << "    object      " << object << ";\n}\n\n";
    return o.str();
}

bool put(const fs::path& p, const std::string& text, std::string* err) {
    std::ofstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "Khong ghi duoc " + p.string();
        return false;
    }
    f << text;
    return true;
}

}  // namespace

bool writeOpenFoamCavity(const std::string& dir, const OpenFoamCavity& p, std::string* error) {
    const double L = 0.1;
    const int n = std::max(4, p.cells);
    const double U = p.lidSpeed > 0 ? p.lidSpeed : 1.0;
    const double nu = U * L / std::max(p.reynolds, 1e-6);
    const double dx = L / n;
    const double dt = 0.2 * dx / U;                         // Courant ~ 0.2
    const int writeEvery = std::max(1, static_cast<int>(p.endTime / dt / 20));

    std::error_code ec;
    const fs::path root(dir);
    for (const char* sub : {"system", "constant", "0"}) fs::create_directories(root / sub, ec);
    if (ec) {
        if (error) *error = "Khong tao duoc thu muc " + dir;
        return false;
    }

    cleanOpenFoamResults(dir);
    std::ostringstream s;
    s << header("dictionary", "blockMeshDict") << "convertToMeters 0.1;\n\nvertices\n(\n"
      << "    (0 0 0)\n    (1 0 0)\n    (1 1 0)\n    (0 1 0)\n"
      << "    (0 0 0.1)\n    (1 0 0.1)\n    (1 1 0.1)\n    (0 1 0.1)\n);\n\n"
      << "blocks\n(\n    hex (0 1 2 3 4 5 6 7) (" << n << ' ' << n << " 1) simpleGrading (1 1 1)\n);\n\n"
      << "edges\n(\n);\n\nboundary\n(\n"
      << "    movingWall\n    {\n        type wall;\n        faces\n        (\n            (3 7 6 2)\n        );\n    }\n"
      << "    fixedWalls\n    {\n        type wall;\n        faces\n        (\n            (0 4 7 3)\n            (2 6 5 1)\n            (1 5 4 0)\n        );\n    }\n"
      << "    frontAndBack\n    {\n        type empty;\n        faces\n        (\n            (0 3 2 1)\n            (4 5 6 7)\n        );\n    }\n"
      << ");\n";
    if (!put(root / "system" / "blockMeshDict", s.str(), error)) return false;

    s.str("");
    s << header("dictionary", "controlDict", "system") << "application     icoFoam;\n\nstartFrom       startTime;\n"
      << "startTime       0;\nstopAt          endTime;\nendTime         " << p.endTime << ";\ndeltaT          " << dt
      << ";\nwriteControl    timeStep;\nwriteInterval   " << writeEvery
      << ";\npurgeWrite      0;\nwriteFormat     ascii;\nwritePrecision  6;\nwriteCompression off;\n"
         "timeFormat      general;\ntimePrecision   6;\nrunTimeModifiable true;\n";
    if (!put(root / "system" / "controlDict", s.str(), error)) return false;

    s.str("");
    s << header("dictionary", "fvSchemes", "system")
      << "ddtSchemes\n{\n    default         Euler;\n}\n\n"
         "gradSchemes\n{\n    default         Gauss linear;\n    grad(p)         Gauss linear;\n}\n\n"
         "divSchemes\n{\n    default         none;\n    div(phi,U)      Gauss linear;\n}\n\n"
         "laplacianSchemes\n{\n    default         Gauss linear orthogonal;\n}\n\n"
         "interpolationSchemes\n{\n    default         linear;\n}\n\n"
         "snGradSchemes\n{\n    default         orthogonal;\n}\n";
    if (!put(root / "system" / "fvSchemes", s.str(), error)) return false;

    s.str("");
    s << header("dictionary", "fvSolution", "system")
      << "solvers\n{\n    p\n    {\n        solver          PCG;\n        preconditioner  DIC;\n"
         "        tolerance       1e-06;\n        relTol          0.05;\n    }\n\n"
         "    pFinal\n    {\n        $p;\n        relTol          0;\n    }\n\n"
         "    U\n    {\n        solver          smoothSolver;\n        smoother        symGaussSeidel;\n"
         "        tolerance       1e-05;\n        relTol          0;\n    }\n}\n\n"
         "PISO\n{\n    nCorrectors     2;\n    nNonOrthogonalCorrectors 0;\n    pRefCell        0;\n    pRefValue       0;\n}\n";
    if (!put(root / "system" / "fvSolution", s.str(), error)) return false;

    s.str("");
    s << header("dictionary", "transportProperties", "constant") << "nu              [0 2 -1 0 0 0 0] " << nu << ";\n";
    if (!put(root / "constant" / "transportProperties", s.str(), error)) return false;

    s.str("");
    s << header("volVectorField", "U", "0")
      << "dimensions      [0 1 -1 0 0 0 0];\n\ninternalField   uniform (0 0 0);\n\nboundaryField\n{\n"
         "    movingWall\n    {\n        type            fixedValue;\n        value           uniform ("
      << U << " 0 0);\n    }\n\n    fixedWalls\n    {\n        type            noSlip;\n    }\n\n"
         "    frontAndBack\n    {\n        type            empty;\n    }\n}\n";
    if (!put(root / "0" / "U", s.str(), error)) return false;

    s.str("");
    s << header("volScalarField", "p", "0")
      << "dimensions      [0 2 -2 0 0 0 0];\n\ninternalField   uniform 0;\n\nboundaryField\n{\n"
         "    movingWall\n    {\n        type            zeroGradient;\n    }\n\n"
         "    fixedWalls\n    {\n        type            zeroGradient;\n    }\n\n"
         "    frontAndBack\n    {\n        type            empty;\n    }\n}\n";
    return put(root / "0" / "p", s.str(), error);
}

}  // namespace fastcae
