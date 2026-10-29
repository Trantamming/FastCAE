// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDialog>
#include <QMap>
#include <QString>
#include <vector>

class QDoubleSpinBox;

namespace fastcae::gui {

// Bảng sửa tham số của mô hình .fcad: mỗi tham số một ô số (mét). Giá trị đã khác mặc định được tô đậm.
class ParamEditDialog : public QDialog {
    Q_OBJECT
public:
    struct Item { QString name; double value; double defaultValue; };
    ParamEditDialog(QWidget* parent, const QString& title, const std::vector<Item>& items);
    // Các tham số có giá trị khác mặc định (để ghi thành param_set).
    QMap<QString, double> changed() const;

private:
    std::vector<Item> items_;
    std::vector<QDoubleSpinBox*> boxes_;
};

}  // namespace fastcae::gui
