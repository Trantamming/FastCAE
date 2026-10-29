// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <map>
#include <string>
#include <vector>

namespace fastcae {

// Minimal "key = value" case file (comments start with '#').
// Repeated keys are allowed (e.g. several `fix` / `load` lines).
class CaseFile {
public:
    bool load(const std::string& path, std::string* error = nullptr);
    bool parse(const std::string& text);

    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& def = "") const;
    double getDouble(const std::string& key, double def) const;
    std::vector<std::string> getAll(const std::string& key) const;

    // Split a value into whitespace separated tokens.
    static std::vector<std::string> tokens(const std::string& value);

private:
    std::multimap<std::string, std::string> kv_;
};

}  // namespace fastcae
