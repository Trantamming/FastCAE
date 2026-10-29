// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Lưu / nạp kết quả tính toán dạng file riêng .fcres (văn bản, độ chính xác đầy đủ 17 chữ số):
// lưới + mọi trường (nút: vô hướng, vector; phần tử: vô hướng) + đại lượng kỹ thuật + dãy số + tóm tắt.
// Mở lại trong GUI/CLI mà không cần chạy lại solver; xuất tiếp sang VTK hoặc CSV.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/mesh.hpp"
#include "core/vtk_writer.hpp"

namespace fastcae {

// Một bước của chuỗi kết quả (cùng lưới, trường khác nhau).
struct StoredFrame {
    std::string label;       // vd. "flow_00012" hoặc "t = 0.5"
    VtkFields fields;
};

struct StoredResult {
    std::string type;                                   // elastic | thermal | cfd3d | ...
    std::string summary;
    std::map<std::string, double> values;
    std::map<std::string, std::vector<double>> series;
    Mesh mesh;
    VtkFields fields;                                   // bước đang hiển thị / bước duy nhất
    std::vector<StoredFrame> frames;                    // rỗng nếu không phải chuỗi bước
    int currentFrame = 0;
};

bool saveResult(const std::string& path, const StoredResult& r, std::string* error = nullptr);
bool loadResult(const std::string& path, StoredResult& r, std::string* error = nullptr);

// Bảng CSV: node_id,x,y,z,<trường vô hướng>,<vector>_x,_y,_z ...
bool exportNodesCsv(const std::string& path, const StoredResult& r, std::string* error = nullptr);

}  // namespace fastcae
