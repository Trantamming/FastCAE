// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "core/vtk_sequence.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fastcae {
namespace {
// "flow_00012" -> prefix "flow_", số 12; false nếu không kết thúc bằng chữ số
bool splitStem(const std::string& stem, std::string& prefix, long long& num) {
    std::size_t e = stem.size();
    std::size_t b = e;
    while (b > 0 && std::isdigit(static_cast<unsigned char>(stem[b - 1]))) --b;
    if (b == e || e - b > 12) return false;
    prefix = stem.substr(0, b);
    num = std::stoll(stem.substr(b));
    return true;
}
}  // namespace

std::vector<VtkFrameFile> findVtkSequence(const std::string& anyFramePath) {
    namespace fs = std::filesystem;
    std::vector<VtkFrameFile> out;
    const fs::path p(anyFramePath);
    std::string prefix;
    long long n = 0;
    if (!splitStem(p.stem().string(), prefix, n)) return out;
    std::error_code ec;
    fs::path dir = p.parent_path();
    if (dir.empty()) dir = ".";
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string x = e.path().extension().string();
        for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (x != ext) continue;
        std::string pre2;
        long long n2 = 0;
        if (!splitStem(e.path().stem().string(), pre2, n2) || pre2 != prefix) continue;
        out.push_back({e.path().string(), n2});
    }
    std::sort(out.begin(), out.end(), [](const VtkFrameFile& a, const VtkFrameFile& b) { return a.index < b.index; });
    return out;
}

}  // namespace fastcae
