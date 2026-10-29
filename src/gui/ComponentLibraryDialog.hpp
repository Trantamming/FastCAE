// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDialog>
#include <QString>

class QComboBox;
class QFormLayout;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QDoubleSpinBox;

namespace fastcae::gui {

// Thư viện cấu kiện kết cấu: chọn loại (dầm, cột, tấm, vỏ, tường, móng, dàn, khung...), nhập kích thước,
// bấm "Tạo mô hình" để sinh file case hoàn chỉnh (hình học, vật liệu, liên kết, tải mặc định).
class ComponentLibraryDialog : public QDialog {
    Q_OBJECT
public:
    explicit ComponentLibraryDialog(QWidget* parent = nullptr, const QString& preferCategory = {});
    QString caseText() const { return text_; }

private slots:
    void onSelect();
    void accept() override;

private:
    enum Kind { Solid = 0, Frame = 1, Shell = 2 };
    struct Entry {
        Kind kind;
        QString name;
    };
    void clearForm();

    QTreeWidget* tree_ = nullptr;
    QLabel* note_ = nullptr;
    QLabel* analysis_ = nullptr;
    QFormLayout* form_ = nullptr;
    QComboBox* material_ = nullptr;
    QLabel* materialLabel_ = nullptr;
    std::vector<QDoubleSpinBox*> spins_;
    Entry current_{Solid, {}};
    QString text_;
};

}  // namespace fastcae::gui
