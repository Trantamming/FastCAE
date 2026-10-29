// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// CAD tham số theo cây tính năng (feature tree) cho FastCAE.
//   - bảng tham số có biểu thức (L = 2*R + 5, sin, cos, sqrt, min, max, pi...), đổi một tham số là dựng lại toàn bộ;
//   - tính năng: box, cylinder, sphere, cone, torus, extrude / revolve từ phác thảo (đa giác, hình tròn), cut / fuse /
//     intersect, mảng thẳng / mảng tròn, đối xứng, tịnh tiến, xoay;
//   - bo góc (fillet) / vát cạnh (chamfer) chọn cạnh bằng hộp bao, qua Gmsh Python chạy như tiến trình riêng (giữ FastCAE MIT);
//   - phác thảo có ràng buộc (csketch): điểm/đường/tròn + ràng buộc hình học và kích thước (có thể là biểu thức của tham số),
//     giải bằng bộ giải 2D riêng (cad/sketch_solver), báo bậc tự do còn lại / mâu thuẫn / dư ràng buộc;
//   - sinh script Gmsh (nhân OpenCASCADE) rồi chia lưới khối bằng `gmsh` ngoài; không buộc liên kết GPL vào FastCAE.
// Chưa có: cung tròn/spline trong phác thảo, vỏ mỏng, bo góc xen kẽ giữa các tính năng.
// Cú pháp file .fcad: xem docs/PARAMETRIC_CAD.md.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/mesh.hpp"

namespace fastcae {

// Bo góc / vát cạnh áp dụng SAU CÙNG lên thân kết quả; cạnh chọn bằng hộp bao (mọi cạnh nằm trọn trong hộp).
struct PostOp {
    bool chamfer = false;     // false = bo góc (fillet)
    double size = 0;          // bán kính bo / khoảng vát (m)
    double box[6] = {0, 0, 0, 0, 0, 0};   // xmin ymin zmin xmax ymax zmax
};

struct ParamModelResult {
    bool ok = false;
    std::string message;                       // lỗi (kèm số dòng) hoặc tóm tắt
    std::string geo;                           // script Gmsh đã sinh
    std::map<std::string, double> parameters;  // giá trị tham số sau khi tính biểu thức (đã áp ghi đè)
    std::string resultBody;                    // tên tính năng cuối cùng được lấy làm vật thể
    std::vector<PostOp> postOps;               // có bo góc/vát cạnh: cần gmsh Python (pip install gmsh), geo không có Physical/kích thước lưới
    std::vector<std::string> warnings;         // vd: phác thảo còn bậc tự do, ràng buộc dư
    double meshSizeUsed = 0;
    int orderUsed = 1;
};

// Biên dịch văn bản .fcad thành script Gmsh. `overrides`: ghi đè giá trị tham số (quét tham số, tối ưu).
// Đơn vị trong file là mét. `meshSize` <= 0: tự động; `order` 1 hoặc 2 (tứ diện 4 / 10 nút).
ParamModelResult compileParamModel(const std::string& text, const std::map<std::string, double>& overrides,
                                   double meshSize, int order);

struct ParamMeshResult {
    bool ok = false;
    std::string message;
    Mesh mesh;
    std::map<std::string, double> parameters;
    std::string geo;
    std::vector<std::string> warnings;
};

// Biên dịch + chạy gmsh ngoài + đọc lưới. workDir rỗng = thư mục tạm.
ParamMeshResult meshParamModel(const std::string& text, const std::map<std::string, double>& overrides,
                               double meshSize, int order, const std::string& workDir = "");

// Tính một biểu thức số với bảng biến (dùng chung cho script/test).
bool evalExpression(const std::string& expr, const std::map<std::string, double>& vars, double& out, std::string& err);

}  // namespace fastcae
