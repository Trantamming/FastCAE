// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Đọc file lịch sử hội tụ dạng CSV của solver ngoài (SU2 history.csv, và bảng CSV có dòng tiêu đề bất kỳ):
// mỗi cột số thành một dãy. Tiêu đề được bỏ khoảng trắng và dấu ngoặc kép; cột không phải số bị bỏ qua.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace fastcae {

bool readHistoryCsv(const std::string& path, std::map<std::string, std::vector<double>>& series, std::string* error = nullptr);

// Tìm file lịch sử nằm cạnh file kết quả (history.csv, history.dat, convergence.csv); rỗng nếu không có.
std::string findHistoryBeside(const std::string& resultPath);

}  // namespace fastcae
