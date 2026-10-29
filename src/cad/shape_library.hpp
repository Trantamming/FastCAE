// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Thư viện cấu kiện kết cấu tham số (dầm I/U/L/T, ống chữ nhật, tấm, tường, móng...).
// Dựng bằng bộ chia lưới ghép khối, không cần Gmsh. Mọi kích thước tính bằng mét.
// Trục dọc của dầm/tường là X; tiết diện nằm trong mặt YZ (Z hướng lên).
#pragma once
#include <string>
#include <vector>

#include "core/mesh.hpp"

namespace fastcae {

struct ShapeParam {
    std::string key;       // "L", "b", "tf"...
    std::string label;     // nhãn tiếng Việt
    double def;            // giá trị mặc định (m)
};

struct ShapeInfo {
    std::string name;      // dùng trong case: geometry = ibeam ...
    std::string label;     // tên hiển thị
    std::string category;  // nhóm cấu kiện
    std::string note;      // mô tả ngắn, cách đặt tải/ngàm mặc định
    std::vector<ShapeParam> params;
};

const std::vector<ShapeInfo>& shapeCatalog();
const ShapeInfo* findShape(const std::string& name);

// Dựng lưới. h <= 0: tự chọn (2 phần tử qua chiều dày mỏng nhất). Trả về false và đặt err nếu lỗi.
bool buildShapeMesh(const std::string& name, const std::vector<double>& p, double h, Mesh& out, std::string& err);

// Nội dung file case hoàn chỉnh (hình học, vật liệu, ngàm, tải mặc định) cho cấu kiện.
// material: "steel", "concrete", "aluminium".
std::string shapeCaseText(const std::string& name, const std::vector<double>& p, const std::string& material = "steel");

}  // namespace fastcae
