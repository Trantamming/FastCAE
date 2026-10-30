// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "HelpDialog.hpp"

#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "Theme.hpp"

namespace fastcae::gui {

namespace {

constexpr int kAnchorRole = Qt::UserRole + 1;

QString loadHelpHtml() {
    QFile f(QStringLiteral(":/help/huong_dan.html"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("<p>Không đọc được tài liệu hướng dẫn (tài nguyên :/help/huong_dan.html).</p>");
    QString t = QString::fromUtf8(f.readAll());
    if (t.startsWith(QChar(0xFEFF))) t.remove(0, 1);
    return t;
}

// Bảng kiểu cho QTextBrowser (tập con CSS của Qt) theo giao diện sáng/tối.
QString helpStyleSheet(bool dark) {
    const Palette p = paletteFor(dark);
    const QString noteBg = dark ? QStringLiteral("#16314a") : QStringLiteral("#e4f0ff");
    const QString warnBg = dark ? QStringLiteral("#4a3a14") : QStringLiteral("#fff3d6");
    const QString codeBg = dark ? QStringLiteral("#0e1117") : QStringLiteral("#eef1f6");
    QString s = QString::fromUtf8(R"(
body { color: @text@; font-size: 14px; }
h1 { color: @accent@; font-size: 26px; margin-top: 6px; margin-bottom: 4px; }
h2 { color: @accent@; font-size: 20px; margin-top: 30px; margin-bottom: 8px; border-bottom: 1px solid @border@; }
h3 { color: @text@; font-size: 16px; margin-top: 18px; margin-bottom: 4px; }
p, li { line-height: 140%; }
a { color: @accent@; text-decoration: none; }
code { font-family: "JetBrains Mono", "Consolas", "DejaVu Sans Mono", monospace; background-color: @code@; }
pre { font-family: "JetBrains Mono", "Consolas", "DejaVu Sans Mono", monospace; font-size: 12px; background-color: @code@; margin-top: 6px; margin-bottom: 8px; }
table { border-color: @border@; }
th { background-color: @panel2@; font-weight: 600; }
td.note { background-color: @note@; }
td.warn { background-color: @warn@; }
.dim { color: @dim@; }
hr { color: @border@; }
)");
    s.replace("@text@", p.text).replace("@accent@", p.accent).replace("@border@", p.border).replace("@panel2@", p.panel2)
        .replace("@dim@", p.dim).replace("@code@", codeBg).replace("@note@", noteBg).replace("@warn@", warnBg);
    return s;
}

QString stripTags(QString s) {
    s.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    s.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return s.simplified();
}

}  // namespace

HelpDialog::HelpDialog(QWidget* parent, bool dark) : QDialog(parent), dark_(dark) {
    setWindowTitle(tr("FastCAE - Hướng dẫn sử dụng"));
    setModal(false);
    resize(1180, 780);
    auto* lay = new QVBoxLayout(this);

    // thanh tìm kiếm
    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Tìm:")));
    search_ = new QLineEdit;
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(tr("Gõ từ khoá rồi nhấn Enter; dùng ▲ ▼ để chuyển giữa các kết quả"));
    bar->addWidget(search_, 1);
    auto* prev = new QPushButton(tr("▲"));
    auto* next = new QPushButton(tr("▼"));
    for (auto* b : {prev, next}) {
        b->setProperty("flat", true);
        b->setFixedWidth(44);
    }
    prev->setToolTip(tr("Kết quả trước"));
    next->setToolTip(tr("Kết quả sau"));
    bar->addWidget(prev);
    bar->addWidget(next);
    hint_ = new QLabel;
    hint_->setMinimumWidth(150);
    bar->addWidget(hint_);
    lay->addLayout(bar);

    auto* split = new QSplitter;
    toc_ = new QTreeWidget;
    toc_->setHeaderHidden(true);
    toc_->setMinimumWidth(230);
    toc_->setIndentation(14);
    split->addWidget(toc_);

    view_ = new QTextBrowser;
    view_->setOpenLinks(false);   // tự xử lý để liên kết nội bộ (#neo) và liên kết ngoài đều rõ ràng
    view_->setReadOnly(true);
    split->addWidget(view_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 920});
    lay->addWidget(split, 1);

    auto* br = new QHBoxLayout;
    auto* top = new QPushButton(tr("Về đầu trang"));
    top->setProperty("flat", true);
    auto* close = new QPushButton(tr("Đóng"));
    br->addWidget(top);
    br->addStretch();
    br->addWidget(close);
    lay->addLayout(br);

    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(top, &QPushButton::clicked, this, [this] { view_->verticalScrollBar()->setValue(0); });
    connect(next, &QPushButton::clicked, this, [this] { findText(false); });
    connect(prev, &QPushButton::clicked, this, [this] { findText(true); });
    connect(search_, &QLineEdit::returnPressed, this, [this] { findText(false); });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { hint_->clear(); });
    connect(toc_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it, int) {
        const QString a = it->data(0, kAnchorRole).toString();
        if (!a.isEmpty()) view_->scrollToAnchor(a);
    });
    connect(view_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const QString frag = url.fragment();
        if (url.scheme().isEmpty() && !frag.isEmpty()) view_->scrollToAnchor(frag);   // liên kết nội bộ "#chuong"
        else if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("mailto"))
            QDesktopServices::openUrl(url);
    });

    html_ = loadHelpHtml();
    buildToc(html_);
    reload();
}

void HelpDialog::buildToc(const QString& html) {
    toc_->clear();
    // <h2><a name="neo">Tiêu đề</a></h2> và <h3>…</h3>: h2 là chương, h3 là mục con
    static const QRegularExpression re(QStringLiteral("<h([23])[^>]*>\\s*<a name=\"([^\"]+)\">(.*?)</a>"),
                                       QRegularExpression::DotMatchesEverythingOption);
    QTreeWidgetItem* chapter = nullptr;
    // đầu trang
    auto* head = new QTreeWidgetItem(toc_, QStringList(tr("Đầu trang")));
    head->setData(0, kAnchorRole, QStringLiteral("dau-trang"));
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString title = stripTags(m.captured(3));
        const QString anchor = m.captured(2);
        if (m.captured(1) == QLatin1String("2")) {
            chapter = new QTreeWidgetItem(toc_, QStringList(title));
            QFont f = chapter->font(0);
            f.setBold(true);
            chapter->setFont(0, f);
            chapter->setData(0, kAnchorRole, anchor);
        }
        else if (chapter) {   // mục con h3 (chỉ khi có <a name>)
            auto* sub = new QTreeWidgetItem(chapter, QStringList(title));
            sub->setData(0, kAnchorRole, anchor);
        }
    }
}

void HelpDialog::reload() {
    const int scroll = view_->verticalScrollBar()->value();
    view_->document()->setDefaultStyleSheet(helpStyleSheet(dark_));
    view_->setHtml(html_);
    view_->verticalScrollBar()->setValue(scroll);
}

void HelpDialog::setDark(bool dark) {
    if (dark == dark_) return;
    dark_ = dark;
    reload();
}

void HelpDialog::showAnchor(const QString& anchor) {
    if (anchor.isEmpty() || anchor == QLatin1String("dau-trang")) view_->verticalScrollBar()->setValue(0);
    else view_->scrollToAnchor(anchor);
    for (int i = 0; i < toc_->topLevelItemCount(); ++i) {
        auto* it = toc_->topLevelItem(i);
        if (it->data(0, kAnchorRole).toString() == anchor) {
            toc_->setCurrentItem(it);
            break;
        }
    }
}

void HelpDialog::findText(bool backwards) {
    const QString text = search_->text().trimmed();
    if (text.isEmpty()) {
        hint_->clear();
        return;
    }
    QTextDocument::FindFlags flags;
    if (backwards) flags |= QTextDocument::FindBackward;
    if (view_->find(text, flags)) {
        hint_->clear();
        return;
    }
    // hết tài liệu: quay vòng từ đầu (hoặc từ cuối khi tìm ngược)
    QTextCursor c = view_->textCursor();
    c.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
    view_->setTextCursor(c);
    if (view_->find(text, flags)) hint_->setText(tr("Đã quay vòng"));
    else hint_->setText(tr("Không tìm thấy"));
}

}  // namespace fastcae::gui
