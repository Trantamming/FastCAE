// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "core/case_file.hpp"
#include "core/mesh.hpp"
#include "core/vtk_writer.hpp"

namespace fastcae {

// Một khung của chuỗi kết quả (dạng riêng, mốc thời gian, gia số).
struct RunFrame {
    std::string label;
    VtkFields fields;
};

struct RunReport {
    bool ok = false;
    std::string message;
    std::string type;        // elastic | thermal | cavity | frame | shell
    double maxValue = 0;     // max |u| (elastic), max T (thermal), max |omega| (cavity)
    double secondary = 0;    // max von Mises (elastic), min T (thermal)
    int iterations = 0;
    // Đại lượng kỹ thuật có tên cho script/JSON: max_displacement, max_stress, frequency_1_hz, reaction_fz...
    std::map<std::string, double> values;
    std::map<std::string, std::vector<double>> series;   // dãy số: node_uz, member_n_max, frequencies_hz...
    std::string summary;     // tóm tắt nhiều dòng cho giao diện (khung, tấm/vỏ); rỗng = dùng mẫu theo type
    Mesh mesh;               // empty for cavity
    VtkFields fields;
    std::vector<RunFrame> frames;   // chuỗi nhiều khung cùng lưới (>= 2) hoặc rỗng
    int currentFrame = 0;           // khung đang là `fields`
};

// Executes a case described by a CaseFile and (optionally) writes the VTK
// file named by `output`. Shared by the CLI and the GUI.
// `baseDir`: folder of the case file (relative geometry/output paths resolve against it).
// `writeOutput=false` (GUI) skips writing the `output` VTK file; use the Export menu instead.
RunReport runCase(const CaseFile& cf, std::ostream& log, const std::string& baseDir = "", bool writeOutput = true);

// Dựng mô hình từ case (lưới + vật liệu + liên kết + tải) KHÔNG giải, ghi JSON trung lập (*.fcmodel.json).
// Đây là điểm vào cho solver ngoài / thuật toán FEM tự lập trình; định dạng mô tả trong docs/ARCHITECTURE.md.
// Hỗ trợ type = elastic | thermal. Lỗi: trả false và điền `err`.
bool exportModel(const CaseFile& cf, std::ostream& log, const std::string& baseDir, const std::string& jsonPath, std::string& err);

// Built-in example cases (text in case-file syntax).
std::string demoCaseText(const std::string& name);

}  // namespace fastcae
