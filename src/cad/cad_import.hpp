// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <string>
#include <vector>

#include "core/mesh.hpp"

namespace fastcae {

// Step/Iges/Brep/Xao : B-Rep (OpenCASCADE qua Gmsh) -> chia luoi khoi
// Stl                : luoi be mat kin -> tao khoi roi chia luoi (qua Gmsh)
// Msh                : luoi Gmsh (doc truc tiep)
// ForeignMesh        : luoi khoi tu phan mem khac: .unv .inp(Abaqus/CalculiX) .bdf/.nas(Nastran) .med .mesh(Medit) .su2 (doc qua Gmsh)
// FreeCad            : .FCStd (doi sang STEP bang freecadcmd neu co)
// Native             : dinh dang dong (SLDPRT, IPT, CATPart, PRT...): chi nhap duoc khi co bo chuyen doi ngoai (FASTCAE_CAD_CONVERTER)
enum class CadFormat { Step, Iges, Brep, Msh, Stl, FreeCad, Native, Unknown, Xao, ForeignMesh };

// Detect format from the file extension.
CadFormat detectCadFormat(const std::string& path);

// Moi duoi file (khong dau cham, chu thuong) ma FastCAE nhap duoc truc tiep. Dung de dung bo loc hop thoai Mo file.
const std::vector<std::string>& cadImportExtensions();

// True neu dinh dang nay co duong nhap (kể ca can Gmsh/FreeCAD ngoai). Native/Unknown: chi khi co FASTCAE_CAD_CONVERTER.
bool canImportFormat(const std::string& path);

// Bo chuyen doi ngoai cho dinh dang dong (bien moi truong FASTCAE_CAD_CONVERTER): lenh co {in} va {out},
// vi du:  mytool --in "{in}" --out "{out}"   ({out} luon la file .step). Tra ve "" neu chua dat.
std::string cadConverterCommand();

// Human readable (Vietnamese) explanation of how to bring the file into
// FastCAE. Proprietary formats (SLDPRT, IPT, CATPart, PRT, F3D, DWG...) cannot
// be read without the vendor kernels, so the supported route is STEP/IGES.
std::string cadFormatAdvice(const std::string& path);

// True if this build can read B-Rep CAD in-process (Gmsh + OpenCASCADE linked in).
bool cadKernelAvailable();

// Path of FreeCAD's command-line program (env FASTCAE_FREECAD, then PATH), or "" if none.
std::string findFreeCadExecutable();

// Path of an external `gmsh` executable (env FASTCAE_GMSH, then PATH), or "" if none.
// Without the linked library, STEP/IGES/BREP are meshed by running this program.
std::string findGmshExecutable();

// Read an ASCII Gmsh .msh file (format 2.2 or 4.1). Volume cells are kept: tetrahedra (4/10 nodes) as they are;
// hexahedra, prisms and pyramids (linear or quadratic: corner nodes used) are split into tetrahedra around a
// cell-centre node (face diagonals chosen from node ids, so neighbouring cells stay conforming). Unused nodes are dropped.
bool readGmshMsh(const std::string& path, Mesh& mesh, std::string* error = nullptr);

// Read an Abaqus / CalculiX .inp mesh (*NODE, *ELEMENT with C3D4, C3D10, C3D8, C3D20, C3D6, C3D15, C3D5...).
// Same cell handling as readGmshMsh. No Gmsh needed. Shells/beams are skipped; *INCLUDE is not followed.
bool readAbaqusInp(const std::string& path, Mesh& mesh, std::string* error = nullptr);

struct CadImportOptions {
    double meshSize = 0;         // maximum element size in METRES; 0 = automatic (model diagonal / 15)
    double scaleToMeters = 1.0;  // units of the CAD file: STEP from SolidWorks is usually mm -> 0.001
};

struct CadImportResult {
    bool ok = false;
    std::string message;
    Mesh mesh;
};

// Import a STEP / IGES / BREP file and generate a tetrahedral volume mesh.
CadImportResult importCad(const std::string& path, const CadImportOptions& opt = {});

// Parametric primitives usable from the built-in designer / case files.
// name: "box" (lx ly lz), "cylinder" (radius length), "sphere" (radius).
// Box works with the built-in mesher; cylinder/sphere need the CAD kernel.
CadImportResult makePrimitive(const std::string& name, const std::vector<double>& params,
                              double meshSize, int boxDivX = 0, int boxDivY = 0, int boxDivZ = 0);

}  // namespace fastcae
