// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "core/history_csv.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fastcae {
namespace {
std::string trimQuotes(std::string s) {
    auto bad = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '"' || c == '\''; };
    while (!s.empty() && bad(s.front())) s.erase(s.begin());
    while (!s.empty() && bad(s.back())) s.pop_back();
    return s;
}
std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQ = false;
    for (char c : line) {
        if (c == '"') inQ = !inQ;
        if (c == ',' && !inQ) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}
bool parseNum(const std::string& s, double& v) {
    const std::string t = trimQuotes(s);
    if (t.empty()) return false;
    char* end = nullptr;
    v = std::strtod(t.c_str(), &end);
    return end && *end == 0;
}
}  // namespace

bool readHistoryCsv(const std::string& path, std::map<std::string, std::vector<double>>& series, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "Khong mo duoc " + path; return false; }
    std::string line;
    if (!std::getline(f, line)) { if (error) *error = "File rong: " + path; return false; }
    std::vector<std::string> names;
    for (const auto& h : splitCsv(line)) names.push_back(trimQuotes(h));
    std::vector<std::vector<double>> cols(names.size());
    std::vector<bool> numeric(names.size(), true);
    std::size_t rows = 0;
    while (std::getline(f, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        const auto cells = splitCsv(line);
        for (std::size_t c = 0; c < names.size(); ++c) {
            double v = 0;
            if (c >= cells.size() || !parseNum(cells[c], v)) { numeric[c] = false; continue; }
            cols[c].push_back(v);
        }
        ++rows;
        if (rows > 20000000) break;
    }
    int added = 0;
    for (std::size_t c = 0; c < names.size(); ++c) {
        if (!numeric[c] || names[c].empty() || cols[c].size() != rows || rows == 0) continue;
        series[names[c]] = std::move(cols[c]);
        ++added;
    }
    if (added == 0) { if (error) *error = "Khong co cot so nao trong " + path; return false; }
    return true;
}

std::string findHistoryBeside(const std::string& resultPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::path(resultPath).parent_path();
    for (const char* n : {"history.csv", "history.dat", "convergence.csv"}) {
        const fs::path p = dir / n;
        if (fs::exists(p, ec)) return p.string();
    }
    return "";
}

}  // namespace fastcae
