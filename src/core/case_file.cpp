// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "core/case_file.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fastcae {

static std::string trim(const std::string& s) {
    auto b = std::find_if_not(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
    auto e = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return b < e ? std::string(b, e) : std::string();
}

bool CaseFile::load(const std::string& path, std::string* error) {
    std::ifstream is(path);
    if (!is) {
        if (error) *error = "Cannot open case file: " + path;
        return false;
    }
    std::stringstream ss;
    ss << is.rdbuf();
    return parse(ss.str());
}

bool CaseFile::parse(const std::string& text) {
    kv_.clear();
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!k.empty()) kv_.emplace(k, v);
    }
    return true;
}

bool CaseFile::has(const std::string& key) const { return kv_.count(key) > 0; }

std::string CaseFile::get(const std::string& key, const std::string& def) const {
    auto it = kv_.find(key);
    return it == kv_.end() ? def : it->second;
}

double CaseFile::getDouble(const std::string& key, double def) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) return def;
    try {
        return std::stod(it->second);
    } catch (...) {
        return def;
    }
}

std::vector<std::string> CaseFile::getAll(const std::string& key) const {
    std::vector<std::string> out;
    auto r = kv_.equal_range(key);
    for (auto it = r.first; it != r.second; ++it) out.push_back(it->second);
    return out;
}

std::vector<std::string> CaseFile::tokens(const std::string& value) {
    std::istringstream is(value);
    std::vector<std::string> out;
    std::string t;
    while (is >> t) out.push_back(t);
    return out;
}

}  // namespace fastcae
