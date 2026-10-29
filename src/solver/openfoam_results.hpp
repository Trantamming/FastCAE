// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Đọc kết quả OpenFOAM (file ASCII trong thư mục thời gian) và dựng lệnh chạy OpenFOAM qua shell
// (OpenFOAM cần nạp môi trường etc/bashrc trước khi chạy). OpenFOAM chạy như tiến trình riêng.
#pragma once
#include <array>
#include <string>
#include <vector>

namespace fastcae {

// Đọc `internalField` ASCII dạng `uniform ...` hoặc `nonuniform List<vector|scalar> N ( ... )`.
bool readFoamVectorField(const std::string& path, std::vector<std::array<double, 3>>& out, std::string* error = nullptr);
bool readFoamScalarField(const std::string& path, std::vector<double>& out, std::string* error = nullptr);

// Xóa kết quả cũ của một case trước khi chạy lại (thư mục thời gian số khác 0, processor*, VTK, postProcessing, file log.*)
// để không đọc nhầm thư mục thời gian cũ lớn hơn. Không đụng tới system/, constant/, 0/ hay file khác.
void cleanOpenFoamResults(const std::string& caseDir);

// Tên thư mục thời gian lớn nhất (số) trong case; rỗng nếu không có.
std::string latestFoamTime(const std::string& caseDir);

// Kết quả khoang kín 2D (lưới n x n x 1 từ blockMesh, ô theo thứ tự x chạy nhanh: chỉ số j*n+i).
struct OpenFoamCavityResult {
    int n = 0;
    double length = 0.1;            // cạnh khoang (m)
    double lidSpeed = 1.0;
    std::string time;
    std::vector<double> u, v, p;    // dữ liệu tại tâm ô
    double uCell(int i, int j) const { return u[static_cast<std::size_t>(j) * n + i]; }
    double vCell(int i, int j) const { return v[static_cast<std::size_t>(j) * n + i]; }
};

bool readOpenFoamCavity(const std::string& caseDir, double length, double lidSpeed, OpenFoamCavityResult& out,
                        std::string* error = nullptr);

// Nội suy dữ liệu ô sang lưới (n+1) x (n+1) nút (trung bình các ô kề; nút biên lấy điều kiện biên: nắp = lidSpeed, tường = 0).
void cavityNodalFields(const OpenFoamCavityResult& r, std::vector<double>& u, std::vector<double>& v, std::vector<double>& p);

// Tìm etc/bashrc của OpenFOAM: gợi ý người dùng -> FASTCAE_OPENFOAM_BASHRC -> $WM_PROJECT_DIR/etc/bashrc -> vị trí phổ biến.
std::string findOpenFoamBashrc(const std::string& hint = "");

// Lệnh shell chạy blockMesh + icoFoam (+ foamToVTK nếu `vtk`) trong case; nếu bashrc rỗng thì dùng PATH hiện tại.
std::string openFoamShellScript(const std::string& bashrc, const std::string& caseDir, bool vtk);

}  // namespace fastcae
