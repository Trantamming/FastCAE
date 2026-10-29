// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Thanh lệnh dạng ribbon (tab + nhóm nút lớn) giống CommandManager của SolidWorks.
#pragma once
#include <QTabWidget>

class QAction;
class QFrame;
class QHBoxLayout;
class QMenu;

namespace fastcae::gui {

class Ribbon : public QTabWidget {
    Q_OBJECT
public:
    explicit Ribbon(QWidget* parent = nullptr);

    // Thêm tab; trả về layout ngang chứa các nhóm.
    QHBoxLayout* addPage(const QString& title);
    // Thêm nhóm có tiêu đề nhỏ phía dưới; trả về layout ngang chứa nút.
    QHBoxLayout* addGroup(QHBoxLayout* page, const QString& title);
    // Nút lớn (icon trên, chữ dưới) gắn với QAction; menu != nullptr -> nút thả xuống.
    void addButton(QHBoxLayout* group, QAction* action, QMenu* menu = nullptr);
};

}  // namespace fastcae::gui
