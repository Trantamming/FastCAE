// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace fastcae::gui {

// Hộp thoại thêm điều kiện biên / tải: ngàm (fix), lực (load), nhiệt độ (temp), nguồn nhiệt (heat).
// Trả về một dòng case-file, ví dụ "load = xmax 0 0 -1000".
class BcDialog : public QDialog {
    Q_OBJECT
public:
    enum Kind { Fix, Load, Temp, Heat };
    // pickedName/pickedDesc: mặt đang chọn bằng chuột (tên tập nút sẽ ghi vào case, mô tả hiển thị); rỗng = không có.
    explicit BcDialog(Kind kind, QWidget* parent = nullptr, const QString& pickedName = {}, const QString& pickedDesc = {});
    QString caseLine() const;
    bool usesPicked() const;   // người dùng đang dùng mặt chọn bằng chuột

private:
    Kind kind_;
    QString pickedName_;
    QComboBox* set_ = nullptr;
    QDoubleSpinBox *v1_ = nullptr, *v2_ = nullptr, *v3_ = nullptr;
    QCheckBox *cx_ = nullptr, *cy_ = nullptr, *cz_ = nullptr;
};

}  // namespace fastcae::gui
