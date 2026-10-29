// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDialog>

class QComboBox;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace fastcae::gui {

// Bảng "Solver mã nguồn mở": giấy phép, mức ràng buộc, mức tích hợp, trạng thái cài đặt; đặt đường dẫn và chạy.
class SolverManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit SolverManagerDialog(QWidget* parent = nullptr, const QString& selectId = {});

    static void applySavedPaths();                 // đẩy đường dẫn đã lưu vào biến môi trường của solver (FASTCAE_CCX...) cho mọi luồng chạy
    static QString savedPath(const QString& id);   // đường dẫn người dùng đã đặt (QSettings)

signals:
    void runRequested(const QString& solverId, const QString& inputFile);

private slots:
    void refresh();
    void onSelection();
    void choosePath();
    void runSelected();
    void openHome();

private:
    QString currentId() const;

    QTableWidget* table_ = nullptr;
    QComboBox* filter_ = nullptr;
    QTextBrowser* detail_ = nullptr;
    QPushButton *run_ = nullptr, *path_ = nullptr, *home_ = nullptr;
};

}  // namespace fastcae::gui
