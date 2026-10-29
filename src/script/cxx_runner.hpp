// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Biên dịch và chạy script C++ của người dùng với FastCAE SDK (fastcae_core). Không phụ thuộc Qt.
// Cần trình biên dịch C++20 trên máy: g++/clang++ (MinGW trên Windows) hoặc cl (MSVC).
#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "solver/external_run.hpp"

namespace fastcae {

struct CxxToolchain {
    std::string compiler;    // đường dẫn g++/clang++/cl
    bool msvc = false;
    std::string includeDir;  // thư mục chứa app/, core/, solver/...
    std::string libDir;      // thư mục chứa libfastcae_core.a, libfastcae_lua.a (hoặc .lib)
    std::string error;       // rỗng nếu đủ điều kiện
};

// compilerHint: đường dẫn người dùng đặt (rỗng = FASTCAE_CXX, g++, clang++, c++, cl). exeDir: thư mục fastcae-cli/fastcae.
CxxToolchain findCxxToolchain(const std::string& exeDir, const std::string& compilerHint = "");

struct CxxRunResult {
    bool ok = false;
    std::string message;
    std::string exePath;
};

// Biên dịch file .cpp (nếu không có main() thì bọc phần mã vào main() và thêm sdk/fastcae_all.hpp),
// rồi chạy với tham số args. buildDir rỗng = thư mục tạm của người dùng.
CxxRunResult buildAndRunCxx(const std::string& sourcePath, const std::string& exeDir, const std::vector<std::string>& args,
                            const OutputFn& onOutput, const std::atomic<bool>* cancel = nullptr, const std::string& buildDir = "",
                            const std::string& compilerHint = "", bool runAfterBuild = true);

}  // namespace fastcae
