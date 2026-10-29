// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Lập trình tùy biến trong FastCAE: Lua 5.4 (MIT) nhúng sẵn, không cần cài thêm gì.
// Script dựng mô hình tham số, quét tham số, tối ưu, tính toán kỹ thuật và gọi mọi solver của FastCAE
// qua bảng `fastcae` (xem scriptApiHelp() hoặc docs/SCRIPTING.md).
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/case_runner.hpp"

namespace fastcae {

struct ScriptOutcome {
    bool ok = false;
    std::string error;   // thông báo lỗi có tên script và số dòng
};

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    // Nhận mọi đầu ra của print() và io.write() (đã gồm ký tự xuống dòng).
    void setPrinter(std::function<void(const std::string&)> printer);
    // Cờ dừng: script bị dừng ở lần kiểm tra kế tiếp (mỗi 10 nghìn lệnh); một lần giải solver đang chạy thì chờ xong.
    void setCancelFlag(const std::atomic<bool>* flag);
    // Bảng toàn cục `arg` như chương trình dòng lệnh: arg[0] = tên script, arg[1..] = tham số.
    void setArguments(const std::string& scriptName, const std::vector<std::string>& args);

    // Chỉ có trong giao diện đồ hoạ: fastcae.show(res) đưa kết quả lên khung 3D, fastcae.open_case(text) nạp và giải một case.
    // Hàm gọi lại có thể được gọi từ luồng của script; bên nhận phải tự chuyển sang luồng giao diện.
    void setShowHandler(std::function<void(const RunReport&)> handler);
    void setOpenCaseHandler(std::function<void(const std::string&)> handler);

    ScriptOutcome runString(const std::string& code, const std::string& chunkName = "script");
    ScriptOutcome runFile(const std::string& path);

    struct Impl;   // chi tiết nằm trong script_engine.cpp

private:
    std::unique_ptr<Impl> impl_;
};

// Mô tả ngắn các hàm của bảng `fastcae` (văn bản thuần, tiếng Việt) cho hộp thoại trợ giúp.
const char* scriptApiHelp();

}  // namespace fastcae
