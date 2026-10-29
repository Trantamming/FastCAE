// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Ghi case OpenFOAM mẫu (khoang kín nắp trượt, icoFoam). OpenFOAM chạy như tiến trình ngoài.
#pragma once
#include <string>

namespace fastcae {

struct OpenFoamCavity {
    int cells = 20;          // số ô mỗi cạnh (lưới n x n x 1)
    double reynolds = 100;   // Re = U*L/nu, cạnh L = 0.1 m
    double lidSpeed = 1.0;   // m/s
    double endTime = 10.0;   // s
};

// Ghi system/, constant/, 0/ vào `dir`. Trả về false và đặt *error nếu không ghi được.
bool writeOpenFoamCavity(const std::string& dir, const OpenFoamCavity& p, std::string* error = nullptr);

}  // namespace fastcae
