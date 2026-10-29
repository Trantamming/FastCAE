// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Chuỗi kết quả theo bước (flow_00000.vtk, flow_00002.vtk, ...: SU2, OpenFOAM foamToVTK, CalculiX...).
#pragma once
#include <string>
#include <vector>

namespace fastcae {

struct VtkFrameFile {
    std::string path;
    long long index = 0;     // số cuối tên file (bước / vòng lặp)
};

// Tìm các file cùng thư mục, cùng tiền tố, đuôi .vtk, kết thúc bằng số (kể cả file được cho), sắp theo số tăng dần.
// Trả về rỗng nếu file được cho không có đuôi số; trả về 1 phần tử nếu không có file anh em.
std::vector<VtkFrameFile> findVtkSequence(const std::string& anyFramePath);

}  // namespace fastcae
