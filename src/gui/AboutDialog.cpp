// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "AboutDialog.hpp"

#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "core/about.hpp"
#include "solver/solver_registry.hpp"

namespace fastcae::gui {

namespace {
QString resourceText(const char* path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QObject::tr("(không đọc được %1)").arg(path);
    QString t = QString::fromUtf8(f.readAll());
    if (t.startsWith(QChar(0xFEFF))) t.remove(0, 1);
    return t;
}
}  // namespace

AboutDialog::AboutDialog(QWidget* parent, int startTab) : QDialog(parent) {
    setWindowTitle(tr("Giới thiệu FastCAE"));
    resize(900, 640);
    auto* lay = new QVBoxLayout(this);

    auto* head = new QHBoxLayout;
    auto* logo = new QLabel;
    logo->setPixmap(QPixmap(":/fastcae.png").scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    head->addWidget(logo);
    auto* title = new QLabel(tr("<span style='font-size:22px;font-weight:700'>FastCAE %1</span><br>"
                                "Phần mềm CAE mã nguồn mở · Tác giả <b>%2</b><br>"
                                "<a href='mailto:%3'>%3</a>")
                                 .arg(FASTCAE_VERSION, QString::fromUtf8(about::kAuthor), QString::fromUtf8(about::kEmail)));
    title->setTextFormat(Qt::RichText);
    title->setOpenExternalLinks(true);
    head->addWidget(title, 1);
    lay->addLayout(head);

    auto* tabs = new QTabWidget;
    lay->addWidget(tabs, 1);

    // --- Giới thiệu
    auto* intro = new QTextBrowser;
    intro->setOpenExternalLinks(true);
    intro->setHtml(
        tr("<h3>Giới thiệu</h3>"
           "<p>FastCAE là phần mềm CAE mã nguồn mở: phân tích kết cấu (FEM), truyền nhiệt, CFD cơ bản, nhập CAD "
           "(STEP/IGES/BREP/XAO/STL, lưới MSH/INP/UNV/BDF/MED/MESH) và hiển thị kết quả bằng OpenGL tự viết (không cần ParaView/VTK). "
           "FastCAE điều phối các solver mã nguồn mở trưởng thành (CalculiX, OpenFOAM, OpenRadioss/OpenCourant, "
           "Code_Aster, Elmer, SU2, ...) như các tiến trình ngoài.</p>"
           "<p>Hướng dẫn đầy đủ từng tính năng và giới thiệu năng lực tính toán CAE: <b>Trợ giúp ▸ Hướng dẫn sử dụng</b> (F1).</p>"
           "<h3>Tác giả và bản quyền</h3>"
           "<p><b>%1</b><br>Email: <a href='mailto:%5'>%5</a><br>%2<br>Giấy phép: <b>%3</b> (xem thẻ <i>Giấy phép</i>).<br>"
           "Trang dự án: <a href='%4'>%4</a></p>"
           "<h3>Lưu ý</h3>"
           "<p>Các solver bên thứ ba có giấy phép riêng (GPL, LGPL, AGPL, BSD...). FastCAE chạy chúng như chương trình "
           "độc lập; nếu bạn <b>đóng gói kèm</b> chương trình GPL/AGPL vào bộ cài thì phải tuân điều khoản phân phối "
           "của chúng. Thông tin trong thẻ <i>Thành phần bên thứ ba</i> là tổng hợp kỹ thuật, không phải tư vấn pháp lý.</p>")
            .arg(QString::fromUtf8(about::kAuthor), QString::fromUtf8(about::kCopyright),
                 QString::fromUtf8(about::kLicense), QString::fromUtf8(about::kHomepage), QString::fromUtf8(about::kEmail)));
    tabs->addTab(intro, tr("Giới thiệu"));

    // --- Giấy phép
    auto* lic = new QPlainTextEdit;
    lic->setReadOnly(true);
    lic->setPlainText(resourceText(":/LICENSE.txt"));
    tabs->addTab(lic, tr("Giấy phép"));

    // --- Bên thứ ba
    auto* page = new QWidget;
    auto* pl = new QVBoxLayout(page);
    auto* table = new QTableWidget;
    const auto& reg = allSolvers();
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({tr("Thành phần"), tr("Giấy phép"), tr("Mức ràng buộc"), tr("Cách dùng"), tr("Đã đối chiếu")});
    table->setRowCount(int(reg.size()));
    int r = 0;
    for (const auto& s : reg) {
        if (s.id == "builtin") continue;
        const QString cells[5] = {QString::fromStdString(s.name), QString::fromStdString(s.license), QString::fromUtf8(copyleftName(s.copyleft)),
                                  QString::fromUtf8(integrationName(s.integration)),
                                  s.licenseChecked ? tr("Có (có nguồn tra cứu)") : tr("Chưa - cần kiểm tra")};
        for (int c = 0; c < 5; ++c) {
            auto* it = new QTableWidgetItem(cells[c]);
            it->setToolTip(QString::fromStdString(s.licenseNote));   // lưu ý chi tiết khi rê chuột
            table->setItem(r, c, it);
        }
        ++r;
    }
    table->setRowCount(r);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    pl->addWidget(table, 1);
    auto* note = new QLabel(tr("Tất cả đều là giấy phép phần mềm tự do/mã nguồn mở đã được OSI công nhận; điểm khác nhau là "
                               "mức ràng buộc khi nhúng hoặc đóng gói. Rê chuột lên một dòng để xem lưu ý chi tiết. "
                               "<b>%1</b> mục chưa đối chiếu nguồn gốc. Chi tiết: docs/LICENSES.md và THIRD_PARTY_NOTICES.txt.")
                                .arg(int(unverifiedLicenseCount())));
    note->setTextFormat(Qt::RichText);
    note->setWordWrap(true);
    pl->addWidget(note);
    tabs->addTab(page, tr("Thành phần bên thứ ba"));

    auto* notices = new QPlainTextEdit;
    notices->setReadOnly(true);
    notices->setPlainText(resourceText(":/THIRD_PARTY_NOTICES.txt"));
    tabs->addTab(notices, tr("Thông báo bản quyền"));

    // --- Văn bản giấy phép đi kèm (LGPL cho Qt/OpenCASCADE, GPL và ngoại lệ GCC cho thư viện chạy MinGW)
    auto* texts = new QWidget;
    auto* tl = new QVBoxLayout(texts);
    auto* pick = new QComboBox;
    auto* body = new QPlainTextEdit;
    body->setReadOnly(true);
    struct LicFile { const char* label; const char* res; };
    static const LicFile kFiles[] = {{"GNU LGPL 3.0 (Qt, OpenCASCADE/LGPL)", ":/licenses/LGPL-3.0.txt"},
                                     {"GNU GPL 3.0 (libgomp, libstdc++, libgcc)", ":/licenses/GPL-3.0.txt"},
                                     {"Ngoại lệ Thư viện Chạy của GCC 3.1", ":/licenses/GCC-Runtime-Library-Exception-3.1.txt"},
                                     {"winpthreads (MIT/BSD, mingw-w64)", ":/licenses/winpthreads-COPYING.txt"},
                                     {"Lua (MIT)", ":/LICENSE-Lua.txt"}};
    for (const auto& lf : kFiles) pick->addItem(tr(lf.label), QString::fromLatin1(lf.res));
    auto show = [pick, body] { body->setPlainText(resourceText(pick->currentData().toString().toUtf8().constData())); };
    connect(pick, &QComboBox::currentIndexChanged, this, show);
    show();
    tl->addWidget(pick);
    tl->addWidget(body, 1);
    tabs->addTab(texts, tr("Văn bản giấy phép"));

    tabs->setCurrentIndex(startTab);
    auto* close = new QPushButton(tr("Đóng"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* br = new QHBoxLayout;
    auto* aboutQt = new QPushButton(tr("Giới thiệu về Qt"));   // cửa sổ chuẩn của Qt nêu giấy phép LGPL
    connect(aboutQt, &QPushButton::clicked, this, [this] { QMessageBox::aboutQt(this); });
    br->addWidget(aboutQt);
    br->addStretch();
    br->addWidget(close);
    lay->addLayout(br);
}

}  // namespace fastcae::gui
