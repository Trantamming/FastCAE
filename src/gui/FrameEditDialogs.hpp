// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QString>
#include <QStringList>
#include <vector>

class QWidget;

namespace fastcae::gui {

struct FieldSpec {
    enum Type { Text, Combo, Check } type = Text;
    QString label;
    QString value;          // giá trị ban đầu (Text), chuỗi "1" nếu Check mặc định bật
    QStringList items;      // Combo
};

// Hộp thoại nhập nhanh một dòng case. Trả về giá trị theo thứ tự trường; rỗng nếu người dùng huỷ.
// Check trả "1" hoặc "".
QStringList askFields(QWidget* parent, const QString& title, const QString& hint, const std::vector<FieldSpec>& fields);

}  // namespace fastcae::gui
