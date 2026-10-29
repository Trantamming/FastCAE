// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Chạy solver mã nguồn mở bên ngoài (Code_Aster, OpenRadioss/OpenCourant, SU2, OpenFOAM, CalculiX, Elmer...)
// và chương trình bất kỳ như TIẾN TRÌNH RIÊNG. Dùng chung cho Lua, Python (qua fastcae-cli) và C++.
// Không phụ thuộc Qt.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace fastcae {

using OutputFn = std::function<void(const std::string&)>;

struct ExternalRunOptions {
    std::string solverId;         // "code_aster", "openradioss", "su2", "openfoam", "calculix", "elmer"...
    std::string input;            // file input (hoặc thư mục case với OpenFOAM)
    std::string workDir;          // rỗng = thư mục chứa input
    std::string executableHint;   // đường dẫn file chạy do người dùng chỉ định
    std::string extraArgs;        // nối vào cuối mỗi bước (vd "-np 4", "--ncpus 8")
    std::string argsOverride;     // thay mẫu tham số mặc định (có {input} {dir} {base} {base0001})
    std::vector<std::string> commands;  // OpenFOAM: danh sách lệnh (vd {"blockMesh","simpleFoam"}); rỗng = tự suy ra
    OutputFn onOutput;            // nhận từng dòng đầu ra (stdout + stderr)
    const std::atomic<bool>* cancel = nullptr;
};

struct ExternalRunResult {
    bool ok = false;
    int exitCode = -1;
    std::string message;                // lý do thất bại hoặc tóm tắt
    std::vector<std::string> commands;  // các lệnh đã chạy
    std::string workDir;                // thư mục đã chạy
    std::vector<std::string> files;     // file mới/được sửa trong workDir sau khi chạy (ví dụ .vtk, .frd, .rmed, .csv)
    std::string log;                    // toàn bộ đầu ra
};

// Chạy một dòng lệnh trong thư mục workDir (qua shell hệ thống), stream đầu ra.
int runCommandLine(const std::string& commandLine, const std::string& workDir, const OutputFn& onOutput,
                   const std::atomic<bool>* cancel = nullptr, std::string* captured = nullptr);

ExternalRunResult runExternalSolver(const ExternalRunOptions& opt);

// Danh mục solver dạng JSON: id, tên, giấy phép, trạng thái cài, đường dẫn, mẫu lệnh.
std::string solversJson();

}  // namespace fastcae
