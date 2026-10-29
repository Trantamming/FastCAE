// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Danh mục solver / thư viện mã nguồn mở mà FastCAE tích hợp hoặc có thể liên kết,
// kèm giấy phép, mức ràng buộc copyleft và cách gọi (lệnh mẫu). Không phụ thuộc Qt.
//
// Nguyên tắc: solver ngoài chạy như TIẾN TRÌNH RIÊNG (ghi input -> chạy -> đọc kết quả)
// nên FastCAE giữ nguyên giấy phép MIT dù solver là GPL/AGPL. Đây là nhận định kỹ thuật,
// không phải tư vấn pháp lý.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace fastcae {

enum class SolverCategory { Fem, Cfd, Dynamics, Coupling, Library };

// Mức "cho phép dùng tự do" của giấy phép.
enum class Copyleft {
    Permissive,   // MIT / BSD / Apache: dùng, nhúng, đóng gói tự do (giữ thông báo bản quyền)
    Weak,         // LGPL: nhúng được nếu liên kết động + cho phép thay thư viện
    Strong,       // GPL: sản phẩm gộp (liên kết) phải GPL; chạy tiến trình riêng thì không
    Network       // AGPL: như GPL, cộng nghĩa vụ khi cung cấp qua mạng
};

// Mức tích hợp thực tế trong FastCAE v0.1.
enum class Integration {
    Builtin,      // solver tự viết, đã kiểm chứng
    Bridge,       // cầu nối chuyên biệt (ghi deck, chạy, đọc kết quả)
    Launcher,     // chạy chương trình ngoài theo lệnh mẫu trên file input có sẵn
    Embedded,     // thư viện nhúng trong chương trình (tuỳ chọn khi build), ví dụ PETSc
    LibraryOnly,  // thư viện: chỉ liên kết ở mức mã nguồn, chưa có mô-đun
    Plugin        // do người dùng khai báo bằng file manifest *.fsolver (không cần biên dịch lại FastCAE)
};

struct CommandStep {
    std::vector<std::string> exeNames;   // tên file thực thi để dò (không .exe; tự thêm trên Windows)
    std::string args;                    // mẫu tham số: {input} {dir} {base} {base0001}
};

struct SolverInfo {
    std::string id;                      // "calculix", "openfoam"...
    std::string name;
    SolverCategory category = SolverCategory::Fem;
    std::string license;                 // ghi ngắn: "GPL", "LGPL-2.1", "BSD-3-Clause"...
    Copyleft copyleft = Copyleft::Strong;
    std::string licenseNote;             // lưu ý đáng biết
    bool licenseChecked = false;         // đã đối chiếu nguồn tra cứu (xem docs/LICENSES.md)
    std::string homepage;
    Integration integration = Integration::Launcher;
    std::string capability;              // làm được gì
    std::string envVar;                  // biến môi trường trỏ tới file chạy chính
    std::vector<CommandStep> steps;      // các bước chạy (starter -> engine...)
    std::string inputFilter;             // bộ lọc hộp thoại chọn file
    std::string inputHint;               // mô tả file input
    // --- solver khai báo bằng *.fsolver (Integration::Plugin): "hợp đồng mô hình vào - kết quả ra" ---
    // takesModel = true: FastCAE dựng mô hình (lưới, vật liệu, liên kết, tải), ghi <job>.fcmodel.json, chạy các bước,
    // rồi đọc <job>_result.vtk (điểm i của VTK = nút i của mô hình). Dùng được trong case bằng `solver = <id>`.
    bool takesModel = false;
    std::string pluginFile;              // đường dẫn file .fsolver đã nạp
};

const std::vector<SolverInfo>& allSolvers();
const SolverInfo* findSolverInfo(const std::string& id);

// Tìm file thực thi: đường dẫn người dùng đặt -> biến môi trường -> PATH. Rỗng nếu không thấy.
std::string findExecutable(const std::vector<std::string>& names, const std::string& userHint = "",
                           const std::string& envVar = "");

// Solver có thể chạy ngay (đủ file thực thi cho mọi bước)? Trả về đường dẫn bước đầu tiên.
std::string findSolverExecutable(const SolverInfo& s, const std::string& userHint = "");

// Đường dẫn dùng để chạy solver (file thực thi; với OpenFOAM có thể là etc/bashrc). Rỗng nếu chưa cài.
std::string solverRuntimePath(const SolverInfo& s, const std::string& userHint = "");

// Thay {input} {dir} {base} {base0001} {result} trong mẫu tham số.
// {result} = <dir>/<base>_result.vtk, với <base> đã bỏ hậu tố ".fcmodel" (đầu ra theo hợp đồng mô hình).
std::string expandArgs(const std::string& tmpl, const std::string& inputPath);

// Số mục chưa đối chiếu giấy phép với nguồn gốc (GUI và CLI hiển thị để người phát hành chú ý).
std::size_t unverifiedLicenseCount();

// Báo cáo giấy phép dạng văn bản cho `fastcae-cli licenses` (mọi thành phần trừ solver tích hợp sẵn).
std::string licenseReport();

const char* categoryName(SolverCategory c);
const char* copyleftName(Copyleft c);
const char* integrationName(Integration i);


// Thư mục dò manifest solver (*.fsolver): biến FASTCAE_SOLVER_PLUGINS (nhiều thư mục, ngăn bằng ':' hoặc ';'),
// rồi <thư mục dữ liệu người dùng>/FastCAE/solvers.d, rồi <thư mục cài>/share/fastcae/solvers.d (plugin đi kèm bản cài). Trả về danh sách thư mục thực sự dò.
std::vector<std::string> solverPluginDirs();

}  // namespace fastcae
