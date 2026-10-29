// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
// Cầu nối CalculiX mở rộng: phi tuyến hình học, dẻo, dao động riêng, mất ổn định, động lực học
// (tích phân ngầm), nhiệt dừng / quá độ, ứng suất nhiệt. FastCAE ghi file .inp, chạy `ccx` như tiến trình
// ngoài (CalculiX là GPL, chỉ được thực thi nên FastCAE vẫn là MIT) và đọc .frd/.dat về.
#pragma once
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "core/mesh.hpp"
#include "solver/calculix_bridge.hpp"
#include "solver/elastic.hpp"

namespace fastcae {

// Mặt phần tử tứ diện theo quy ước CalculiX (C3D4/C3D10): S1 = 1-2-3, S2 = 1-4-2, S3 = 2-4-3, S4 = 3-4-1.
struct ElemFace {
    int elem;   // chỉ số phần tử (0-based)
    int face;   // 1..4
};

// Mặt biên (chỉ thuộc một phần tử) có cả 3 đỉnh nằm trong tập nút `nodes` (chỉ số nút 0-based; nút giữa cạnh được bỏ qua).
std::vector<ElemFace> boundaryFacesOnNodes(const Mesh& mesh, const std::vector<int>& nodes);

struct CcxPressure {
    std::vector<ElemFace> faces;
    double value = 0;   // dương = nén vào vật thể (đẩy vào mặt)
};

struct CcxContact {
    std::vector<ElemFace> slave, master;
    double stiffness = 0;   // độ cứng phạt tuyến tính [Pa/m = N/m3]; 0 = tự chọn 1e3 * E / kích thước lớn nhất
    double adjust = 0;      // khoảng cách [m]: nút slave cách master gần hơn mức này được dời lên mặt master (đóng khe hở ban đầu nhỏ)
};

struct CcxSpec {
    // static | nlstatic | plastic | contact | frequency | buckle | dynamic | thermal_stress | heat | heat_transient
    std::string analysis = "static";
    Material mat;
    double alpha = 0;          // hệ số giãn nở nhiệt [1/K]
    double cp = 500;           // nhiệt dung riêng [J/(kg K)]
    double yield = 0;          // giới hạn chảy [Pa] (plastic)
    double hardening = 0;      // mô-đun hóa bền tuyến tính H [Pa] (plastic): sigma = yield + H * eps_p
    bool nlgeom = false;       // biến dạng lớn
    double totalTime = 1;      // thời gian bước [s] (dynamic, heat_transient); static: 1
    int increments = 10;       // số gia tối thiểu của bước (static) / số bước thời gian (dynamic, heat_transient)
    int modes = 6;             // số dạng riêng (frequency, buckle)
    Vec3 gravity{0, 0, 0};     // gia tốc trọng trường [m/s2] (vd. 0 0 -9.81)
    double initialTemp = 0;
    std::vector<DisplacementBC> bcs;
    std::vector<NodalForce> loads;
    std::vector<std::pair<int, double>> tempBCs;    // nút, nhiệt độ
    std::vector<std::pair<int, double>> heatLoads;  // nút, công suất [W]
    std::vector<CcxPressure> pressures;             // áp suất theo mặt (*DLOAD Pn)
    std::vector<CcxContact> contacts;               // cặp tiếp xúc không ma sát (analysis = contact hoặc bất kỳ phân tích tĩnh)
};

// Một khung hiển thị: một dạng riêng (frequency, buckle) hoặc một mốc thời gian / gia số (dynamic, heat_transient, nhiều gia số).
struct CcxFrame {
    std::string label;
    double time = 0;                              // thời điểm, tần số hoặc hệ số
    std::vector<double> u, dispMagnitude;         // chuyển vị 3n và độ lớn
    std::vector<double> vonMises, temperature;    // theo nút (rỗng nếu không có trong khung)
};

struct CcxAnalysisResult {
    bool ok = false;
    std::string message, inpPath, frdPath;
    ElasticResult elastic;                    // gia số cuối (hoặc dạng riêng được chọn)
    std::vector<double> temperature;          // theo nút (heat*, thermal_stress)
    std::vector<double> peeq;                 // biến dạng dẻo tương đương theo nút (plastic)
    std::vector<double> contactPressure;      // áp suất tiếp xúc [Pa] theo nút (tiếp xúc; khác 0 chỉ trên nút slave đang chạm)
    std::vector<double> contactShear;         // ứng suất cắt tiếp xúc |(CSHEAR1, CSHEAR2)| [Pa] theo nút
    std::vector<double> freqHz;               // tần số riêng [Hz] (frequency)
    std::vector<double> factors;              // hệ số mất ổn định (buckle)
    std::vector<std::vector<double>> modeShapes;   // mỗi dạng 3*n chuyển vị
    std::vector<double> times;                // thời điểm các gia số
    std::vector<double> historyMaxDisp, historyMaxTemp;   // lịch sử theo gia số
    std::vector<double> historyMaxVonMises;   // đỉnh von Mises theo từng khung thời gian (nếu có ứng suất)
    std::vector<CcxFrame> frames;             // mọi dạng riêng / mọi mốc thời gian (>= 2 khung, nếu không rỗng)
};

std::string ccxDeck(const Mesh& mesh, const std::vector<std::array<int, 10>>* tets10, const CcxSpec& s, std::string* error = nullptr);
CcxAnalysisResult runCalculixAnalysis(const Mesh& mesh, const std::vector<std::array<int, 10>>* tets10, const CcxSpec& spec,
                                      const CalculixOptions& opt = {});

struct FrdIncrement {
    double value = 0;   // thời gian / tần số ghi trong khối " 100C"
    std::map<std::string, std::map<int, std::vector<double>>> blocks;
};
bool readFrdIncrements(const std::string& path, std::vector<FrdIncrement>& out, std::string* error = nullptr);

}  // namespace fastcae
