// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Thông tin tác giả và giấy phép dùng chung cho CLI, GUI, file xuất và bộ cài.
#pragma once

#ifndef FASTCAE_VERSION
#define FASTCAE_VERSION "1.0.0"
#endif

namespace fastcae::about {

inline constexpr const char* kProduct   = "FastCAE";
inline constexpr const char* kAuthor    = "Trần Minh Tâm";
inline constexpr const char* kEmail     = "tam.ming.zhan@gmail.com";
inline constexpr const char* kCopyright = "Copyright (c) 2026 Trần Minh Tâm";
inline constexpr const char* kLicense   = "MIT License";
inline constexpr const char* kSpdx      = "MIT";
inline constexpr const char* kHomepage  = "https://github.com/tranminhtam/FastCAE";

// Dòng ngắn dùng trong thanh trạng thái, watermark, banner CLI, chú thích file xuất.
inline constexpr const char* kCredit    = "FastCAE · Tác giả: Trần Minh Tâm · MIT";

}  // namespace fastcae::about
