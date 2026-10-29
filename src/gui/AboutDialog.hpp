// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDialog>

namespace fastcae::gui {

// Giới thiệu + tác giả, giấy phép MIT đầy đủ, và bảng giấy phép các thành phần/solver bên thứ ba.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr, int startTab = 0);
};

}  // namespace fastcae::gui
