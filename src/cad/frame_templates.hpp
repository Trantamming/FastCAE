// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Mẫu khung / dàn / dầm 1D tham số: sinh nội dung file case (node, section, member, support, nload, udl).
#pragma once
#include <string>
#include <vector>

#include "cad/shape_library.hpp"   // ShapeInfo, ShapeParam

namespace fastcae {

const std::vector<ShapeInfo>& frameTemplateCatalog();
const ShapeInfo* findFrameTemplate(const std::string& name);

// Trả về chuỗi rỗng và đặt err nếu tham số không hợp lệ.
std::string frameTemplateText(const std::string& name, const std::vector<double>& p, std::string& err);

// Mẫu tấm / vỏ (giải bằng CalculiX, phần tử vỏ S4).
const std::vector<ShapeInfo>& shellTemplateCatalog();
const ShapeInfo* findShellTemplate(const std::string& name);
std::string shellTemplateText(const std::string& name, const std::vector<double>& p, std::string& err);

}  // namespace fastcae
