// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// CFD 3D qua OpenFOAM (tiến trình ngoài): hộp/kênh/ống dẫn dựng bằng blockMesh, ghi case đầy đủ
// (simpleFoam dừng hoặc pimpleFoam quá độ; tầng lưu, k-epsilon hoặc k-omega SST), chạy, đọc U/p/k về lưới hiển thị.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "core/mesh.hpp"

namespace fastcae {

struct OpenFoam3D {
    double lx = 1, ly = 1, lz = 1;
    int nx = 20, ny = 20, nz = 20;
    double nu = 1e-5;                              // độ nhớt động học [m2/s]
    std::array<double, 3> inletU{1, 0, 0};         // vận tốc vào [m/s]
    // 6 mặt: xmin xmax ymin ymax zmin zmax; mỗi mặt: inlet | outlet | wall | symmetry | slip
    std::array<std::string, 6> face{"inlet", "outlet", "wall", "wall", "wall", "wall"};
    // Lưới tứ diện từ CAD (nhập qua Gmsh): nếu khác nullptr thì ghi polyMesh trực tiếp từ lưới này (không dùng blockMesh);
    // 6 mặt phẳng của hộp bao (xmin..zmax) được nhận diện theo hình học, mọi mặt biên còn lại thuộc patch "walls" (vai trò faceOther).
    const Mesh* cadMesh = nullptr;
    std::string faceOther = "wall";
    std::string model = "laminar";                 // laminar | kepsilon | komegasst
    bool transient = false;
    double endTime = 1.0;                          // quá độ [s]
    double deltaT = 1e-3;                          // quá độ: bước đầu (tự điều chỉnh theo Co <= 0.9)
    int maxIter = 1000;                            // dừng: số vòng lặp tối đa (có residualControl)
    double turbIntensity = 0.05;                   // cường độ rối ở cửa vào
    double lengthScale = 0.0;                      // thang chiều dài rối [m]; 0 = 7% kích thước nhỏ nhất của mặt cắt
    // Lược đồ gradient của OpenFOAM. Rỗng = tự chọn: lưới tứ diện CAD dùng "pointCellsLeastSquares", lưới lục diện (blockMesh)
    // dùng "Gauss linear". Lý do: "Gauss linear" không nhất quán trên lưới tứ diện lệch (skewness ~0,7-0,8) nên áp suất tầng lưu
    // cao hơn lý thuyết ~15% và KHÔNG giảm khi làm mịn lưới (đo trên ống D=1, h = 0,25..0,08); xem docs/SOLVERS.md.
    std::string gradScheme;
};

// Ghi polyMesh (points/faces/owner/neighbour/boundary) từ lưới tứ diện. Ô OpenFOAM i = tứ diện i của lưới.
// Trả false + thông báo nếu một mặt phẳng được gán inlet/outlet/... không có mặt biên nào.
bool writePolyMeshFromTets(const std::string& caseDir, const Mesh& mesh, const OpenFoam3D& p, std::string* error = nullptr,
                           std::vector<std::pair<std::string, std::string>>* patchRoles = nullptr);   // (tên patch, vai trò)

bool validateOpenFoam3D(const OpenFoam3D& p, std::string* error);
bool writeOpenFoam3D(const std::string& dir, const OpenFoam3D& p, std::string* error = nullptr);

// Tên ứng dụng OpenFOAM tương ứng (simpleFoam | pimpleFoam).
std::string openFoam3DSolver(const OpenFoam3D& p);

// Lệnh shell: nạp bashrc (nếu có), blockMesh rồi solver, ghi log.blockMesh / log.<solver>.
std::string openFoam3DShellScript(const std::string& bashrc, const std::string& caseDir, const OpenFoam3D& p);

struct OpenFoam3DResult {
    std::string time;
    std::vector<double> p, k, nut;                 // theo nút lưới makeBoxMesh(lx,ly,lz,nx,ny,nz)
    std::vector<double> ux, uy, uz;                // theo nút
    std::vector<double> speed;
    double maxSpeed = 0, pMin = 0, pMax = 0;
    double massImbalance = 0;                      // |lưu lượng ra - vào| / lưu lượng vào (0 nếu không đo được)
};

bool readOpenFoam3D(const std::string& caseDir, const OpenFoam3D& p, OpenFoam3DResult& out, std::string* error = nullptr);

}  // namespace fastcae
