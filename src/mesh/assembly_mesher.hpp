// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Chia lưới tứ diện cho vật thể ghép từ nhiều khối hộp song song các trục (dầm I/U/L/T, ống chữ nhật,
// tường, móng...). Mọi khối dùng chung một lưới đường thẳng nên các mặt tiếp giáp trùng nút (không cần Gmsh).
#pragma once
#include <vector>

#include "core/mesh.hpp"

namespace fastcae {

struct BoxPart {
    double x0, y0, z0, x1, y1, z1;
};

// hx, hy, hz: cỡ phần tử lớn nhất theo từng trục (mỗi đoạn giữa hai biên khối được chia đều thành ceil(dài/h) phần).
// Trả về lưới rỗng nếu không có khối hợp lệ hoặc quá nhiều ô (> maxCells).
Mesh makeAssemblyMesh(const std::vector<BoxPart>& parts, double hx, double hy, double hz,
                      std::size_t maxCells = 600000);

}  // namespace fastcae
