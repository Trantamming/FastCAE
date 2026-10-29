// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Tấm và vỏ mỏng giải bằng CalculiX (phần tử vỏ S4): FastCAE sinh lưới mặt trung bình, ghi deck .inp,
// chạy ccx, đọc chuyển vị và ứng suất (mặt trên/dưới) rồi dựng hình để hiển thị.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "core/mesh.hpp"
#include "core/vtk_writer.hpp"
#include "solver/elastic.hpp"   // Material

namespace fastcae {

struct ShellMesh {
    std::vector<Vec3> nodes;
    std::vector<std::array<int, 4>> quads;
    std::vector<Vec3> normals;       // pháp tuyến đơn vị theo nút
    std::vector<Vec3> pressureDir;   // hướng tác dụng của áp suất dương theo nút
    double thickness = 0;
    double nx = 0, ny = 0;           // số ô theo hai phương (thông tin)
};

// Tấm phẳng trong mặt z = 0: x in [0,Lx], y in [0,Ly]. Áp suất dương hướng -Z (xuống).
ShellMesh makeShellPlate(double Lx, double Ly, double t, int nx, int ny);
// Vỏ trụ trục Z từ 0 đến L, bán kính R (mặt trung bình). Áp suất dương hướng ra ngoài (áp suất trong bồn).
ShellMesh makeShellCylinder(double R, double L, double t, int nc, int nl);

// Tập nút theo tên: xmin xmax ymin ymax zmin zmax all (theo hộp bao, dung sai tương đối).
std::vector<int> selectShellNodes(const ShellMesh& m, const std::string& name);

struct ShellFix {
    std::vector<int> nodes;
    std::array<bool, 6> dof{};       // ux uy uz rx ry rz
};
struct ShellLoad {
    int node = 0;
    Vec3 f;
};

// Áp suất p [Pa] trên toàn mặt: lực nút = p * (diện tích quanh nút / 4) theo hướng pressureDir.
std::vector<ShellLoad> shellPressureLoads(const ShellMesh& m, double p);
// Lực tổng chia đều cho các nút.
std::vector<ShellLoad> shellForceLoads(const std::vector<int>& nodes, const Vec3& total);

struct ShellOptions {
    std::string workDir = ".";
    std::string jobName = "fastcae_shell";
    std::string executable;          // rỗng: tự dò (PATH, FASTCAE_CCX)
};

struct ShellResult {
    bool ok = false;
    std::string message;
    std::string inpPath;
    std::vector<double> u;           // 3 thành phần mỗi nút
    std::vector<double> dispMagnitude;
    std::vector<double> vonMises;    // lớn nhất qua chiều dày (mặt trên/dưới/giữa)
    double maxDisplacement = 0, maxVonMises = 0;
};

bool writeShellInp(const std::string& path, const ShellMesh& m, const Material& mat, const std::vector<ShellFix>& fixes,
                   const std::vector<ShellLoad>& loads, std::string* error = nullptr);
ShellResult runCalculixShell(const ShellMesh& m, const Material& mat, const std::vector<ShellFix>& fixes,
                             const std::vector<ShellLoad>& loads, const ShellOptions& opt);

// Dựng khối mỏng để hiển thị (hai lớp nút ± t_render/2 theo pháp tuyến); trường nhân đôi cho hai lớp.
void shellRender(const ShellMesh& m, const ShellResult& r, Mesh& mesh, VtkFields& fields, double renderThickness = 0);

}  // namespace fastcae
