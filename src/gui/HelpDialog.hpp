// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QTextBrowser;
class QTreeWidget;

namespace fastcae::gui {

// Hướng dẫn sử dụng toàn bộ tính năng + giới thiệu năng lực tính toán CAE.
// Nội dung nằm trong tài nguyên nhúng ":/help/huong_dan.html" (nguồn: docs/help/huong_dan.html).
// Cửa sổ không modal: mở hướng dẫn và vẫn thao tác được trên cửa sổ chính.
class HelpDialog : public QDialog {
    Q_OBJECT
public:
    // chapterAnchor rỗng: toàn bộ hướng dẫn. Khác rỗng: chỉ hiện chương có neo đó (ví dụ "nang-luc"),
    // liên kết sang chương khác sẽ phát tín hiệu openGuideRequested để mở hướng dẫn đầy đủ.
    explicit HelpDialog(QWidget* parent, bool dark, const QString& chapterAnchor = {});

    void setDark(bool dark);                 // đổi giao diện sáng/tối, giữ nguyên vị trí đang đọc
    void showAnchor(const QString& anchor);  // nhảy tới một chương, ví dụ "nang-luc", "file-case"

signals:
    void openGuideRequested(const QString& anchor);

private:
    void reload();
    void buildToc(const QString& html);
    void findText(bool backwards);

    QTreeWidget* toc_ = nullptr;
    QTextBrowser* view_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* hint_ = nullptr;
    QString html_;
    bool dark_ = true;
    bool chapterOnly_ = false;
};

}  // namespace fastcae::gui
