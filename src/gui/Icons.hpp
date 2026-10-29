// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Icon vẽ bằng mã (QPainter) theo màu chủ đề: không cần file ảnh, sắc nét ở mọi DPI.
#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

namespace fastcae::gui {

// kind: play stop new open save import export cube sketch mesh fix force heat wave link boom gear info
//       sun moon fit iso front right top back left bottom wire camera probe chart layers doc chip
QIcon makeIcon(const QString& kind, const QColor& ink, const QColor& accent);

}  // namespace fastcae::gui
