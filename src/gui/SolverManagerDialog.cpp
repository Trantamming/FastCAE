// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "SolverManagerDialog.hpp"

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "solver/solver_registry.hpp"

namespace fastcae::gui {

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

QString SolverManagerDialog::savedPath(const QString& id) {
    return QSettings().value("solver/" + id + "/path").toString();
}

void SolverManagerDialog::applySavedPaths() {
    for (const SolverInfo& s : allSolvers()) {
        if (s.envVar.empty() || s.id == "openfoam") continue;   // OpenFOAM dùng etc/bashrc, có đường riêng
        const QString p = savedPath(q(s.id));
        if (p.isEmpty()) continue;
        const std::string exe = findExecutable(s.steps.empty() ? std::vector<std::string>{} : s.steps.front().exeNames, p.toStdString());
        if (!exe.empty()) qputenv(s.envVar.c_str(), QByteArray::fromStdString(exe));
    }
}

SolverManagerDialog::SolverManagerDialog(QWidget* parent, const QString& selectId) : QDialog(parent) {
    setWindowTitle(tr("Solver mã nguồn mở"));
    resize(1280, 720);
    auto* lay = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Nhóm:")));
    filter_ = new QComboBox;
    filter_->addItem(tr("Tất cả"), -1);
    for (auto c : {SolverCategory::Fem, SolverCategory::Cfd, SolverCategory::Dynamics, SolverCategory::Coupling, SolverCategory::Library})
        filter_->addItem(QString::fromUtf8(categoryName(c)), int(c));
    top->addWidget(filter_);
    top->addStretch();
    lay->addLayout(top);

    table_ = new QTableWidget;
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({tr("Solver"), tr("Nhóm"), tr("Giấy phép"), tr("Mức ràng buộc"), tr("Tích hợp trong FastCAE"), tr("Trạng thái")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    for (int c = 0; c < 5; ++c) table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    lay->addWidget(table_, 3);

    detail_ = new QTextBrowser;
    detail_->setOpenExternalLinks(true);
    lay->addWidget(detail_, 2);

    auto* bar = new QHBoxLayout;
    run_ = new QPushButton(tr("▶  Chạy trên file input..."));
    path_ = new QPushButton(tr("Đặt đường dẫn chương trình..."));
    path_->setProperty("flat", true);
    home_ = new QPushButton(tr("Trang chủ"));
    home_->setProperty("flat", true);
    auto* close = new QPushButton(tr("Đóng"));
    close->setProperty("flat", true);
    bar->addWidget(run_);
    bar->addWidget(path_);
    bar->addWidget(home_);
    bar->addStretch();
    bar->addWidget(close);
    lay->addLayout(bar);

    connect(filter_, &QComboBox::currentIndexChanged, this, &SolverManagerDialog::refresh);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &SolverManagerDialog::onSelection);
    connect(run_, &QPushButton::clicked, this, &SolverManagerDialog::runSelected);
    connect(path_, &QPushButton::clicked, this, &SolverManagerDialog::choosePath);
    connect(home_, &QPushButton::clicked, this, &SolverManagerDialog::openHome);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
    if (!selectId.isEmpty())
        for (int r = 0; r < table_->rowCount(); ++r)
            if (table_->item(r, 0)->data(Qt::UserRole).toString() == selectId) table_->selectRow(r);
    onSelection();
}

QString SolverManagerDialog::currentId() const {
    const auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? QString() : table_->item(rows.first().row(), 0)->data(Qt::UserRole).toString();
}

void SolverManagerDialog::refresh() {
    const QString keep = currentId();
    const int cat = filter_->currentData().toInt();
    table_->setRowCount(0);
    for (const auto& s : allSolvers()) {
        if (cat >= 0 && int(s.category) != cat) continue;
        const int r = table_->rowCount();
        table_->insertRow(r);
        QString status;
        if (s.integration == Integration::Builtin) status = tr("Có sẵn");
        else if (s.integration == Integration::LibraryOnly) status = tr("— (thư viện)");
        else if (s.integration == Integration::Embedded) status = tr("Nhúng / đi kèm (thư viện)");
        else {
            const std::string exe = solverRuntimePath(s, savedPath(q(s.id)).toStdString());
            status = exe.empty() ? tr("Chưa tìm thấy") : tr("Đã cài: ") + q(exe);
        }
        const QString cells[6] = {q(s.name), QString::fromUtf8(categoryName(s.category)), q(s.license),
                                  QString::fromUtf8(copyleftName(s.copyleft)), QString::fromUtf8(integrationName(s.integration)), status};
        for (int c = 0; c < 6; ++c) table_->setItem(r, c, new QTableWidgetItem(cells[c]));
        table_->item(r, 0)->setData(Qt::UserRole, q(s.id));
    }
    for (int r = 0; r < table_->rowCount(); ++r)
        if (table_->item(r, 0)->data(Qt::UserRole).toString() == keep) table_->selectRow(r);
    onSelection();
}

void SolverManagerDialog::onSelection() {
    const SolverInfo* s = findSolverInfo(currentId().toStdString());
    run_->setEnabled(false);
    path_->setEnabled(false);
    home_->setEnabled(s != nullptr);
    if (!s) {
        detail_->setHtml(tr("<p>Chọn một dòng để xem chi tiết giấy phép và cách tích hợp.</p>"));
        return;
    }
    const bool runnable = s->integration == Integration::Bridge || s->integration == Integration::Launcher;
    const std::string exe = runnable ? solverRuntimePath(*s, savedPath(q(s->id)).toStdString()) : std::string();
    run_->setEnabled(runnable && !exe.empty());
    path_->setEnabled(runnable);

    QString cmd;
    for (const auto& st : s->steps) {
        QString names;
        for (const auto& n : st.exeNames) names += (names.isEmpty() ? "" : " | ") + q(n);
        cmd += QString("<li><code>%1 %2</code></li>").arg(names.toHtmlEscaped(), q(st.args).toHtmlEscaped());
    }
    detail_->setHtml(
        tr("<h3>%1</h3><p>%2</p>"
           "<p><b>Giấy phép:</b> %3 — <i>%4</i>%5<br><b>Lưu ý:</b> %6</p>"
           "<p><b>Mức tích hợp:</b> %7%8</p>%9")
            .arg(q(s->name).toHtmlEscaped(), q(s->capability).toHtmlEscaped(), q(s->license).toHtmlEscaped(),
                 QString::fromUtf8(copyleftName(s->copyleft)),
                 s->licenseChecked ? tr(" (đã đối chiếu nguồn tra cứu)") : tr(" (<b>chưa đối chiếu</b> - hãy kiểm tra trước khi phân phối)"),
                 q(s->licenseNote).toHtmlEscaped(), QString::fromUtf8(integrationName(s->integration)),
                 s->inputHint.empty() ? QString() : tr("<br><b>File input:</b> %1").arg(q(s->inputHint).toHtmlEscaped()),
                 cmd.isEmpty() ? QString() : tr("<p><b>Lệnh chạy:</b></p><ul>%1</ul>").arg(cmd)));
}

void SolverManagerDialog::choosePath() {
    const SolverInfo* s = findSolverInfo(currentId().toStdString());
    if (!s) return;
    const QString p = QFileDialog::getOpenFileName(this, tr("Chọn chương trình của %1").arg(q(s->name)), savedPath(q(s->id)));
    if (p.isEmpty()) return;
    QSettings().setValue("solver/" + q(s->id) + "/path", p);
    applySavedPaths();
    refresh();
}

void SolverManagerDialog::runSelected() {
    const SolverInfo* s = findSolverInfo(currentId().toStdString());
    if (!s) return;
    const QString filter = s->inputFilter.empty() ? tr("Tất cả (*)") : q(s->inputFilter) + ";;" + tr("Tất cả (*)");
    const QString f = QFileDialog::getOpenFileName(this, tr("Chọn file input cho %1").arg(q(s->name)), {}, filter);
    if (f.isEmpty()) return;
    emit runRequested(q(s->id), f);
    accept();
}

void SolverManagerDialog::openHome() {
    if (const SolverInfo* s = findSolverInfo(currentId().toStdString()))
        QDesktopServices::openUrl(QUrl(q(s->homepage)));
}

}  // namespace fastcae::gui
