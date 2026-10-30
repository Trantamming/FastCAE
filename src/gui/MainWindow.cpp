// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "MainWindow.hpp"
#include "core/result_store.hpp"
#include "cad/param_cad.hpp"
#include "ParamEditDialog.hpp"
#include "SeriesPlotDialog.hpp"
#include "core/vtk_sequence.hpp"
#include <QApplication>
#include <QGroupBox>
#include <QPushButton>
#include "core/history_csv.hpp"
#include "cad/cad_import.hpp"
#include "core/paths.hpp"

#include <memory>
#include <sstream>

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QRegularExpression>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QEventLoop>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QThread>
#include <QTimer>
#include <QProcess>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "AboutDialog.hpp"
#include "BcDialog.hpp"
#include "ComponentLibraryDialog.hpp"
#include "FrameEditDialogs.hpp"
#include "HelpDialog.hpp"
#include "Icons.hpp"
#include "ParametricDialog.hpp"
#include "Ribbon.hpp"
#include "ScriptDock.hpp"
#include "SolverManagerDialog.hpp"
#include "Theme.hpp"
#include "Viewport.hpp"
#include "cad/cad_import.hpp"
#include "core/about.hpp"
#include "solver/calculix_bridge.hpp"
#include "solver/openfoam_case.hpp"
#include "solver/openfoam_results.hpp"
#include "core/vtk_reader.hpp"
#include "solver/sparse.hpp"
#include "solver/solver_registry.hpp"

namespace fastcae::gui {

namespace {
QString authorLine() { return QString::fromUtf8(about::kCredit); }
}  // namespace

// ------------------------------------------------------------------ dựng giao diện
MainWindow::MainWindow() {
    resize(1600, 960);
    dark_ = QSettings().value("ui/dark", true).toBool();

    view_ = new Viewport(this);
    view_->setWatermark(QString::fromUtf8("FastCAE · ") + QString::fromUtf8(about::kAuthor));

    createActions();
    buildRibbon();

    auto* central = new QWidget;
    auto* v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(ribbon_);
    v->addWidget(view_, 1);
    setCentralWidget(central);

    buildDocks();
    buildMenus();

    credit_ = new QLabel(authorLine());
    credit_->setObjectName("credit");
    busy_ = new QProgressBar;
    busy_->setRange(0, 0);   // đang chạy (không biết trước tiến độ)
    busy_->setFixedWidth(160);
    busy_->setTextVisible(false);
    busy_->hide();
    statusBar()->addPermanentWidget(busy_);
    statusBar()->addPermanentWidget(credit_);
    statusBar()->showMessage(tr("Sẵn sàng. Chọn thẻ lệnh phía trên, hoặc Tệp ▸ Ví dụ, rồi bấm Chạy (F5). Nhấn F1 để xem hướng dẫn sử dụng."));

    setTheme(dark_);
    newFromDemo("cantilever");
}

QAction* MainWindow::makeAction(const QString& text, const QString& icon, const QString& tip) {
    auto* a = new QAction(text, this);
    a->setToolTip(tip.isEmpty() ? text : tip);
    iconActions_.append({a, icon});
    return a;
}

void MainWindow::createActions() {
    auto add = [this](const char* id, const QString& text, const QString& icon, const QString& tip = {}) {
        QAction* a = makeAction(text, icon, tip);
        acts_.insert(QString::fromLatin1(id), a);
        return a;
    };
    // Tệp & hình học
    add("new", tr("Ví dụ"), "new", tr("Nạp bài toán mẫu"));
    connect(add("open", tr("Mở"), "open", tr("Mở file: bài toán, CAD/lưới, kết quả .vtk, script (Ctrl+O hoặc kéo thả)")), &QAction::triggered, this, &MainWindow::openCase);
    connect(add("save", tr("Lưu"), "save", tr("Lưu bài toán (Ctrl+S)")), &QAction::triggered, this, &MainWindow::saveCase);
    connect(add("import", tr("Nhập CAD"), "import", tr("Nhập CAD / lưới: STEP, IGES, BREP, XAO, STL, MSH, INP, UNV, BDF, MED, MESH")), &QAction::triggered, this, &MainWindow::importCad);
    connect(add("param", tr("Hình học\ntham số"), "cube", tr("Thiết kế hộp/trụ/tấm có lỗ...")), &QAction::triggered, this, &MainWindow::designParametric);
    connect(add("exportvtk", tr("Xuất VTK"), "export", tr("Xuất kết quả .vtk để mở bằng ParaView")), &QAction::triggered, this, &MainWindow::exportVtk);
    connect(add("plot", tr("Đồ thị"), "chart", tr("Vẽ các dãy số: lịch sử hội tụ, lực, chuỗi thời gian")), &QAction::triggered, this, &MainWindow::showSeriesPlot);
    connect(add("paramedit", tr("Sửa\ntham số"), "cube", tr("Sửa tham số mô hình .fcad (geometry = param ...) rồi dựng lại và giải")), &QAction::triggered, this, &MainWindow::editParamModel);
    connect(add("saveresult", tr("Lưu\nkết quả"), "save", tr("Lưu kết quả (.fcres): lưới, mọi trường, đại lượng; mở lại không cần giải")), &QAction::triggered, this, &MainWindow::saveResultFile);
    connect(add("shot", tr("Chụp ảnh"), "camera", tr("Lưu ảnh khung nhìn 3D")), &QAction::triggered, this, &MainWindow::saveScreenshot);
    // Thư viện cấu kiện và dựng khung/dàn 1D
    connect(add("library", tr("Thư viện\ncấu kiện"), "library", tr("Chọn dầm, cột, tấm, vỏ, tường, móng, dàn, khung; nhập kích thước và tạo mô hình")),
            &QAction::triggered, this, [this] { openLibrary(); });
    connect(add("lib_beam", tr("Dầm &\ncột"), "beam", tr("Dầm thép I, U, L, T, ống chữ nhật (khối đặc 3D)")), &QAction::triggered, this, [this] { openLibrary("Dầm / cột"); });
    connect(add("lib_plate", tr("Tấm &\nvỏ"), "shell", tr("Tấm phẳng, bồn trụ (phần tử vỏ, giải bằng CalculiX)")), &QAction::triggered, this, [this] { openLibrary("Tấm"); });
    connect(add("lib_wall", tr("Tường &\nmóng"), "cube", tr("Tường, móng đơn, tường chắn đất")), &QAction::triggered, this, [this] { openLibrary("Tường"); });
    connect(add("lib_frame", tr("Dàn &\nkhung"), "truss", tr("Dầm 1D, khung cổng, khung nhiều tầng, dàn Warren/Pratt, dàn mái")), &QAction::triggered, this, [this] { openLibrary("khung 1D"); });
    connect(add("an_static", tr("Phân tích\ntĩnh"), "chart", tr("Chuyển vị, phản lực, nội lực")), &QAction::triggered, this, [this] { frameSetAnalysis("static"); });
    connect(add("an_modal", tr("Dao động\nriêng"), "wave", tr("Tần số và dạng dao động riêng của khung/dàn")), &QAction::triggered, this, [this] { frameSetAnalysis("modal"); });
    connect(add("an_buckling", tr("Mất ổn định\n(Euler)"), "boom", tr("Hệ số tải tới hạn và dạng mất ổn định tuyến tính")), &QAction::triggered, this, [this] { frameSetAnalysis("buckling"); });
    connect(add("an_mode", tr("Chọn\ndạng"), "probe", tr("Chọn dạng dao động / mất ổn định để hiển thị")), &QAction::triggered, this, [this] { frameChooseMode(); });
    connect(add("fnode", tr("Thêm\nnút"), "node", tr("Thêm nút (khung/dàn 1D)")), &QAction::triggered, this, [this] { frameAddNode(); });
    connect(add("fsection", tr("Tiết diện"), "layers", tr("Khai báo tiết diện: chữ nhật, tròn, ống, chữ I, hộp, tổng quát")), &QAction::triggered, this, [this] { frameAddSection(); });
    connect(add("fmember", tr("Thêm\nthanh"), "member", tr("Nối hai nút bằng dầm, thanh dàn, cáp hoặc lò xo (có thể có bản lề)")), &QAction::triggered, this, [this] { frameAddMember(); });
    connect(add("fsupport", tr("Liên kết\nnút"), "fix", tr("Ngàm, gối cố định, gối con lăn")), &QAction::triggered, this, [this] { frameAddSupport(); });
    connect(add("fload", tr("Tải\ntại nút"), "force", tr("Lực và mômen tập trung tại nút")), &QAction::triggered, this, [this] { frameAddNodeLoad(); });
    connect(add("fudl", tr("Tải phân\nbố"), "udl", tr("Tải phân bố đều lên một thanh (N/m)")), &QAction::triggered, this, [this] { frameAddUdl(); });
    // Lập trình (Lua)
    connect(add("script_panel", tr("Bảng\nlập trình"), "gear", tr("Hiện/ẩn bảng lập trình (Lua, Python, C++)")), &QAction::triggered, this, [this] {
        scriptDock_->setVisible(!scriptDock_->isVisible());
        if (scriptDock_->isVisible()) scriptDock_->raise();
    });
    connect(add("script_run", tr("Chạy\nscript"), "play", tr("Chạy script trong bảng lập trình (F6)")), &QAction::triggered, this, [this] {
        scriptDock_->show();
        scriptDock_->raise();
        scriptDock_->run();
    });
    connect(add("script_stop", tr("Dừng\nscript"), "stop"), &QAction::triggered, this, [this] { scriptDock_->stop(); });
    connect(add("script_open", tr("Mở\nscript"), "open"), &QAction::triggered, this, [this] { scriptDock_->show(); scriptDock_->openFile(); });
    connect(add("script_save", tr("Lưu\nscript"), "save"), &QAction::triggered, this, [this] { scriptDock_->saveFile(); });
    connect(add("script_help", tr("Hàm\nlập trình"), "info", tr("Danh sách hàm lập trình (theo ngôn ngữ đang chọn)")), &QAction::triggered, this, [this] { scriptDock_->showHelp(); });
    // Lưới
    connect(add("order1", tr("Tứ diện\n4 nút (T4)"), "mesh", tr("Phần tử bậc 1: nhanh nhưng cứng khi uốn")), &QAction::triggered, this, [this] { setElementOrder(1); });
    connect(add("order2", tr("Tứ diện\n10 nút (T10)"), "mesh", tr("Phần tử bậc 2: chính xác hơn")), &QAction::triggered, this, [this] { setElementOrder(2); });
    connect(add("meshinfo", tr("Thông tin\nlưới"), "info"), &QAction::triggered, this, [this] { meshInfo(); });
    actEdges_ = add("edges", tr("Hiện cạnh lưới"), "wire", tr("Bật/tắt cạnh phần tử"));
    actEdges_->setCheckable(true);
    // Chọn mặt bằng chuột
    QAction* pk = add("pickface", tr("Chọn mặt\nbằng chuột"), "pick", tr("Bật chế độ chọn mặt: bấm lên một mặt phẳng của vật thể, rồi dùng Liên kết/Lực/Nhiệt (Esc = bỏ chọn)"));
    pk->setCheckable(true);
    connect(pk, &QAction::toggled, this, [this](bool on) { setPickMode(on); });
    // Kết cấu
    connect(add("fix", tr("Liên kết\n(ngàm)"), "fix", tr("Khoá chuyển vị trên một mặt")), &QAction::triggered, this, [this] { addCondition(BcDialog::Fix); });
    connect(add("load", tr("Lực\ntác dụng"), "force", tr("Đặt lực tổng lên một mặt")), &QAction::triggered, this, [this] { addCondition(BcDialog::Load); });
    connect(add("material", tr("Vật liệu"), "layers", tr("E, nu, rho, k")), &QAction::triggered, this, [this] { editMaterial(); });
    connect(add("runfem", tr("Chạy FEM\n(nội bộ)"), "play", tr("Đàn hồi tuyến tính bằng solver FastCAE")), &QAction::triggered, this, [this] {
        if (!ensureType("elastic", "cantilever")) runInternal();
    });
    connect(add("runccx", tr("Chạy bằng\nCalculiX"), "play", tr("Ghi .inp, chạy ccx, đọc .frd")), &QAction::triggered, this, [this] {
        ensureType("elastic", "cantilever");
        runCalculix();
    });
    QAction* pe = add("usepetsc", tr("Giải bằng\nPETSc"), "chip", tr("Dùng PETSc (CG + ICC) làm bộ giải tuyến tính thay cho CG tự viết"));
    pe->setCheckable(true);
    connect(pe, &QAction::toggled, this, [this, pe](bool on) {
        if (on && !petscAvailable()) {
            QSignalBlocker b(pe);
            pe->setChecked(false);
            QMessageBox::information(this, tr("PETSc"),
                                     tr("Bản FastCAE này chưa được build với PETSc.\nBuild lại với -DFASTCAE_WITH_PETSC=ON (cần PETSc và MPI, xem docs/BUILDING.md)."));
            return;
        }
        setCaseKey("linear_solver", on ? "petsc" : "cg");
        runSolver();
    });
    // Nhiệt
    connect(add("temp", tr("Nhiệt độ\ncưỡng bức"), "heat"), &QAction::triggered, this, [this] { addCondition(BcDialog::Temp); });
    connect(add("heat", tr("Nguồn\nnhiệt"), "heat"), &QAction::triggered, this, [this] { addCondition(BcDialog::Heat); });
    connect(add("runthermal", tr("Chạy dẫn nhiệt\ndừng"), "play"), &QAction::triggered, this, [this] {
        if (!ensureType("thermal", "conduction")) runSolver();
    });
    // CFD
    connect(add("runcavity", tr("Khoang kín 2D\n(nội bộ)"), "wave", tr("CFD 2D nắp trượt, so với Ghia 1982")), &QAction::triggered, this, [this] {
        if (!ensureType("cavity", "cavity")) runSolver();
    });
    connect(add("openfoam", tr("OpenFOAM\nkhoang kín"), "wave", tr("Ghi case icoFoam, chạy blockMesh/icoFoam/foamToVTK")), &QAction::triggered, this, [this] { runOpenFoamCavity(); });
    connect(add("su2", tr("SU2"), "wave"), &QAction::triggered, this, [this] { openSolverManager("su2"); });
    connect(add("saturne", tr("Code_Saturne"), "wave"), &QAction::triggered, this, [this] { openSolverManager("code_saturne"); });
    // Va chạm / ghép nối / FEM khác
    connect(add("radioss", tr("OpenRadioss"), "boom"), &QAction::triggered, this, [this] { openSolverManager("openradioss"); });
    connect(add("courant", tr("OpenCourant"), "boom"), &QAction::triggered, this, [this] { openSolverManager("opencourant"); });
    connect(add("chrono", tr("Project\nChrono"), "gear"), &QAction::triggered, this, [this] { openSolverManager("chrono"); });
    connect(add("precice", tr("preCICE"), "link", tr("Kiểm tra cấu hình ghép nối")), &QAction::triggered, this, [this] { openSolverManager("precice"); });
    connect(add("aster", tr("Code_Aster"), "chip"), &QAction::triggered, this, [this] { openSolverManager("code_aster"); });
    connect(add("elmer", tr("Elmer"), "chip"), &QAction::triggered, this, [this] { openSolverManager("elmer"); });
    connect(add("fenicsx", tr("FEniCSx"), "chip"), &QAction::triggered, this, [this] { openSolverManager("fenicsx"); });
    connect(add("kratos", tr("Kratos"), "chip"), &QAction::triggered, this, [this] { openSolverManager("kratos"); });
    // Chạy / dừng chung
    actRun_ = add("run", tr("Chạy"), "play", tr("Giải bài toán hiện tại (F5)"));
    actRun_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(actRun_, &QAction::triggered, this, &MainWindow::runSolver);
    actStop_ = add("stop", tr("Dừng"), "stop", tr("Dừng tiến trình solver ngoài"));
    actStop_->setEnabled(false);
    connect(actStop_, &QAction::triggered, this, [this] { stopJobs(); });
    // Hiển thị
    static const char* viewIds[] = {"iso", "front", "right", "top", "back", "left", "bottom"};
    static const char* viewNames[] = {"Đẳng cự", "Trước", "Phải", "Trên", "Sau", "Trái", "Dưới"};
    for (int i = 0; i < 7; ++i)
        connect(add(viewIds[i], tr(viewNames[i]), viewIds[i]), &QAction::triggered, this, [this, i] { view_->setStandardView(i); });
    connect(add("fit", tr("Vừa khung"), "fit"), &QAction::triggered, this, [this] { view_->fitAll(); });
    connect(add("probe", tr("Đọc giá trị"), "probe", tr("Bấm chuột trái lên bề mặt để đọc giá trị tại nút gần nhất")), &QAction::triggered, this,
            [this] { statusBar()->showMessage(tr("Bấm chuột trái (không kéo) lên bề mặt để đọc giá trị; kéo = xoay, chuột phải/giữa = dịch, cuộn = phóng."), 15000); });
    // Công cụ & trợ giúp
    connect(add("solvers", tr("Quản lý\nsolver"), "chip", tr("Solver mã nguồn mở: giấy phép, trạng thái, chạy")), &QAction::triggered, this, [this] { openSolverManager(); });
    connect(add("licenses", tr("Giấy phép"), "doc", tr("Giấy phép FastCAE và các thành phần bên thứ ba")), &QAction::triggered, this, [this] { about(1); });
    connect(add("about", tr("Giới thiệu\n& Tác giả"), "info"), &QAction::triggered, this, [this] { about(0); });
    connect(add("guide", tr("Hướng dẫn\nsử dụng"), "doc", tr("Hướng dẫn sử dụng toàn bộ tính năng của FastCAE (F1)")), &QAction::triggered, this,
            [this] { showHelp(); });
    connect(add("capability", tr("Năng lực\ntính toán CAE"), "chart", tr("Giới thiệu năng lực tính toán CAE của FastCAE và độ chính xác đã kiểm chứng")),
            &QAction::triggered, this, [this] { showCapability(); });
    actLight_ = add("light", tr("Giao diện sáng"), "sun");
    actDark_ = add("dark", tr("Giao diện tối"), "moon");
    actLight_->setCheckable(true);
    actDark_->setCheckable(true);
    auto* grp = new QActionGroup(this);
    grp->addAction(actLight_);
    grp->addAction(actDark_);
    connect(actLight_, &QAction::triggered, this, [this] { setTheme(false); });
    connect(actDark_, &QAction::triggered, this, [this] { setTheme(true); });
    connect(actEdges_, &QAction::toggled, this, [this](bool on) {
        if (edges_ && edges_->isChecked() != on) edges_->setChecked(on);
    });
}

void MainWindow::buildRibbon() {
    ribbon_ = new Ribbon;
    auto grp = [&](QHBoxLayout* page, const QString& title, std::initializer_list<const char*> ids) {
        QHBoxLayout* g = ribbon_->addGroup(page, title);
        for (const char* id : ids) ribbon_->addButton(g, act(id));
        return g;
    };

    // 0 — Tệp & Hình học
    auto* p0 = ribbon_->addPage(tr("Tệp"));
    {
        QHBoxLayout* g = ribbon_->addGroup(p0, tr("Tệp"));
        auto* demo = new QMenu(this);
        demo->addAction(tr("Dầm console (đàn hồi)"), this, [this] { newFromDemo("cantilever"); });
        demo->addAction(tr("Thanh dẫn nhiệt"), this, [this] { newFromDemo("conduction"); });
        demo->addAction(tr("Dòng chảy khoang kín (CFD)"), this, [this] { newFromDemo("cavity"); });
        demo->setObjectName("demoMenu");
        ribbon_->addButton(g, act("new"), demo);
        ribbon_->addButton(g, act("open"));
        ribbon_->addButton(g, act("save"));
    }
    grp(p0, tr("Hình học (CAD)"), {"import", "param", "paramedit"});
    grp(p0, tr("Xuất"), {"saveresult", "exportvtk", "shot"});

    // Cấu kiện kết cấu
    auto* pc = ribbon_->addPage(tr("Cấu kiện"));
    grp(pc, tr("Thư viện cấu kiện"), {"library", "lib_beam", "lib_plate", "lib_wall", "lib_frame"});
    grp(pc, tr("Dựng khung / dàn 1D"), {"fnode", "fsection", "fmember", "fsupport", "fload", "fudl"});
    grp(pc, tr("Loại phân tích khung"), {"an_static", "an_modal", "an_buckling", "an_mode"});
    grp(pc, tr("Giải"), {"run"});

    // 1 — Lưới
    auto* p1 = ribbon_->addPage(tr("Lưới"));
    grp(p1, tr("Loại phần tử"), {"order1", "order2"});
    grp(p1, tr("Hiển thị lưới"), {"edges", "meshinfo"});

    // 2 — Kết cấu (FEM)
    auto* p2 = ribbon_->addPage(tr("Kết cấu"));
    grp(p2, tr("Mô hình"), {"material", "pickface", "fix", "load"});
    grp(p2, tr("Giải"), {"runfem", "runccx", "usepetsc"});
    grp(p2, tr("FEM mã nguồn mở khác"), {"aster", "elmer", "fenicsx", "kratos"});

    // 3 — Nhiệt
    auto* p3 = ribbon_->addPage(tr("Nhiệt"));
    grp(p3, tr("Điều kiện"), {"material", "pickface", "temp", "heat"});
    grp(p3, tr("Giải"), {"runthermal", "usepetsc"});

    // 4 — CFD
    auto* p4 = ribbon_->addPage(tr("CFD"));
    grp(p4, tr("Nội bộ"), {"runcavity"});
    grp(p4, tr("OpenFOAM"), {"openfoam"});
    grp(p4, tr("CFD mã nguồn mở khác"), {"su2", "saturne"});

    // 5 — Va chạm & ghép nối
    auto* p5 = ribbon_->addPage(tr("Ghép nối"));
    grp(p5, tr("Động lực học / va chạm"), {"radioss", "courant", "chrono"});
    grp(p5, tr("Ghép nối solver"), {"precice"});

    // 6 — Kết quả & hiển thị
    auto* p6 = ribbon_->addPage(tr("Kết quả"));
    grp(p6, tr("Hướng nhìn"), {"iso", "front", "right", "top", "back", "left", "bottom"});
    grp(p6, tr("Khung nhìn"), {"fit", "edges", "probe", "pickface"});
    grp(p6, tr("Xuất"), {"plot", "saveresult", "exportvtk", "shot"});

    // Lập trình
    auto* pp = ribbon_->addPage(tr("Lập trình"));
    grp(pp, tr("Bảng lập trình"), {"script_panel", "script_run", "script_stop"});
    {
        QHBoxLayout* g = ribbon_->addGroup(pp, tr("Script"));
        ribbon_->addButton(g, act("script_open"));
        ribbon_->addButton(g, act("script_save"));
        auto* ex = new QMenu(this);
        const QStringList names = ScriptDock::exampleNames();
        QAction* exAct = makeAction(tr("Ví dụ\nscript"), "library", tr("Nạp một script mẫu"));
        acts_.insert("script_examples", exAct);
        for (int i = 0; i < names.size(); ++i) ex->addAction(names[i], this, [this, i] { scriptDock_->loadExample(i); });
        for (Language lang : {Language::Python, Language::Cpp}) {
            ex->addSeparator();
            const QStringList n2 = ScriptDock::exampleNames(lang);
            for (int i = 0; i < n2.size(); ++i) ex->addAction(n2[i], this, [this, lang, i] { scriptDock_->loadExample(lang, i); });
        }
        ribbon_->addButton(g, exAct, ex);
    }
    grp(pp, tr("Trợ giúp"), {"script_help"});

    // 7 — Công cụ
    auto* p7 = ribbon_->addPage(tr("Công cụ"));
    grp(p7, tr("Mã nguồn mở"), {"solvers", "licenses"});
    grp(p7, tr("Giao diện"), {"light", "dark"});
    grp(p7, tr("Trợ giúp"), {"guide", "capability", "about"});

    // nút Chạy / Dừng nằm cạnh thanh tab (góc phải), luôn thấy ở mọi thẻ
    auto* corner = new QWidget;
    auto* cl = new QHBoxLayout(corner);
    cl->setContentsMargins(0, 0, 6, 0);
    for (const char* id : {"run", "stop"}) {
        auto* b = new QToolButton;
        b->setDefaultAction(act(id));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(20, 20));
        cl->addWidget(b);
    }
    ribbon_->setCornerWidget(corner, Qt::TopRightCorner);
}

void MainWindow::buildMenus() {
    // Nút ribbon dùng chữ hai dòng; trong menu dùng bản một dòng (action phụ chuyển tiếp sang action gốc).
    auto menuAdd = [this](QMenu* m, QAction* a) {
        if (a->isCheckable() || !a->text().contains('\n')) {
            m->addAction(a);
            return;
        }
        QAction* proxy = m->addAction(QString(a->text()).replace('\n', ' '));
        proxy->setToolTip(a->toolTip());
        connect(proxy, &QAction::triggered, a, &QAction::trigger);
        for (const auto& pr : iconActions_)
            if (pr.first == a) {
                iconActions_.append({proxy, pr.second});
                break;
            }
    };
    auto* file = menuBar()->addMenu(tr("&Tệp"));
    auto* demo = file->addMenu(tr("Ví dụ"));
    demo->addAction(tr("Dầm console (đàn hồi)"), this, [this] { newFromDemo("cantilever"); });
    demo->addAction(tr("Thanh dẫn nhiệt"), this, [this] { newFromDemo("conduction"); });
    demo->addAction(tr("Dòng chảy khoang kín (CFD)"), this, [this] { newFromDemo("cavity"); });
    act("open")->setShortcut(QKeySequence::Open);
    act("save")->setShortcut(QKeySequence::Save);
    menuAdd(file, act("open"));
    menuAdd(file, act("save"));
    file->addAction(tr("Mở kết quả (.fcres, .vtk)…"), this, &MainWindow::openResultDialog);
    file->addSeparator();
    menuAdd(file, act("import"));
    menuAdd(file, act("saveresult"));
    menuAdd(file, act("exportvtk"));
    menuAdd(file, act("shot"));
    file->addSeparator();
    file->addAction(tr("Thoát"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto* model = menuBar()->addMenu(tr("&Mô hình"));
    for (const char* id : {"library", "param", "material", "pickface", "fix", "load", "temp", "heat", "order1", "order2", "meshinfo"}) menuAdd(model, act(id));

    auto* solve = menuBar()->addMenu(tr("&Giải"));
    for (const char* id : {"run", "runfem", "runccx", "runthermal", "runcavity", "openfoam", "stop"}) menuAdd(solve, act(id));
    solve->addSeparator();
    auto* ext = solve->addMenu(tr("Solver mã nguồn mở khác"));
    for (const char* id : {"su2", "saturne", "radioss", "courant", "chrono", "precice", "aster", "elmer", "fenicsx", "kratos"}) menuAdd(ext, act(id));

    auto* viewMenu = menuBar()->addMenu(tr("&Hiển thị"));
    for (const char* id : {"iso", "front", "right", "top", "back", "left", "bottom", "fit", "edges"}) menuAdd(viewMenu, act(id));
    viewMenu->addSeparator();
    menuAdd(viewMenu, act("light"));
    menuAdd(viewMenu, act("dark"));
    viewMenu->addSeparator();
    for (auto* d : findChildren<QDockWidget*>()) viewMenu->addAction(d->toggleViewAction());

    auto* tools = menuBar()->addMenu(tr("&Công cụ"));
    menuAdd(tools, act("solvers"));

    auto* help = menuBar()->addMenu(tr("&Trợ giúp"));
    menuAdd(help, act("guide"));
    help->actions().last()->setShortcut(QKeySequence(Qt::Key_F1));   // F1 = Hướng dẫn sử dụng
    menuAdd(help, act("capability"));
    help->addSeparator();
    menuAdd(help, act("about"));
    menuAdd(help, act("licenses"));
}

void MainWindow::buildDocks() {
    // Trái: cây mô hình (giống FeatureManager)
    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    auto* d1 = new QDockWidget(tr("Cây mô hình"), this);
    d1->setWidget(tree_);
    addDockWidget(Qt::LeftDockWidgetArea, d1);

    // Phải: bảng thuộc tính / bài toán (giống Task panel của FreeCAD)
    editor_ = new QPlainTextEdit;
    auto* d2 = new QDockWidget(tr("Bài toán (case)"), this);
    d2->setWidget(editor_);
    addDockWidget(Qt::RightDockWidgetArea, d2);

    auto* res = new QWidget;
    auto* lay = new QVBoxLayout(res);
    summary_ = new QLabel(tr("Chưa có kết quả"));
    summary_->setWordWrap(true);
    scalarBox_ = new QComboBox;
    warpSlider_ = new QSlider(Qt::Horizontal);
    warpSlider_->setRange(0, 100);
    edges_ = new QCheckBox(tr("Hiện cạnh lưới"));
    lay->addWidget(summary_);
    lay->addWidget(new QLabel(tr("Trường hiển thị")));
    lay->addWidget(scalarBox_);
    lay->addWidget(new QLabel(tr("Phóng đại biến dạng")));
    lay->addWidget(warpSlider_);
    lay->addWidget(edges_);
    {   // bước thời gian (chỉ hiện khi kết quả là chuỗi nhiều bước)
        auto* gb = new QGroupBox(tr("Bước thời gian"));
        auto* gl = new QVBoxLayout(gb);
        frameSlider_ = new QSlider(Qt::Horizontal);
        frameSlider_->setRange(0, 0);
        frameLabel_ = new QLabel;
        auto* row = new QHBoxLayout;
        playBtn_ = new QPushButton(tr("▶ Phát"));
        playBtn_->setCheckable(true);
        row->addWidget(playBtn_);
        row->addWidget(frameLabel_, 1);
        lockRange_ = new QCheckBox(tr("Khóa thang màu theo cả chuỗi"));
        gl->addLayout(row);
        gl->addWidget(frameSlider_);
        gl->addWidget(lockRange_);
        framePanel_ = gb;
        framePanel_->hide();
        playTimer_ = new QTimer(this);
        playTimer_->setInterval(450);
        connect(frameSlider_, &QSlider::valueChanged, this, [this](int i) { setFrameIndex(i); });
        connect(playTimer_, &QTimer::timeout, this, [this] {
            if (frameFields_.empty()) return;
            frameSlider_->setValue((frameSlider_->value() + 1) % int(frameFields_.size()));
        });
        connect(playBtn_, &QPushButton::toggled, this, [this](bool on) {
            if (on) playTimer_->start(); else playTimer_->stop();
            playBtn_->setText(on ? tr("⏸ Dừng") : tr("▶ Phát"));
        });
        connect(lockRange_, &QCheckBox::toggled, this, [this](bool on) { setLockRange(on); });
        lay->addWidget(framePanel_);
    }
    lay->addStretch();
    auto* d3 = new QDockWidget(tr("Kết quả"), this);
    d3->setWidget(res);
    addDockWidget(Qt::RightDockWidgetArea, d3);
    tabifyDockWidget(d2, d3);
    d2->raise();

    // Dưới: nhật ký + đầu ra solver ngoài (giống Report view)
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    auto* d4 = new QDockWidget(tr("Nhật ký"), this);
    d4->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, d4);
    extLog_ = new QPlainTextEdit;
    extLog_->setReadOnly(true);
    auto* d5 = new QDockWidget(tr("Solver ngoài"), this);
    d5->setWidget(extLog_);
    addDockWidget(Qt::BottomDockWidgetArea, d5);
    tabifyDockWidget(d4, d5);
    d4->raise();
    setAcceptDrops(true);   // kéo thả file vào cửa sổ để mở
    scriptDock_ = new ScriptDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, scriptDock_);
    tabifyDockWidget(d5, scriptDock_);
    scriptDock_->setShowHandler([this](const RunReport& r) {   // fastcae.show(res): hiển thị kết quả của script lên khung 3D
        casePath_.clear();
        finishRun(r, QString());
    });
    scriptDock_->setShowFileHandler([this](const std::string& p) { showResultFile(QString::fromStdString(p)); });   // Python: fastcae.show(file)
    scriptDock_->setOpenCaseHandler([this](const std::string& text) {   // fastcae.open_case(text)
        editor_->setPlainText(QString::fromStdString(text));
        casePath_.clear();
        refreshTitle();
        runSolver();
    });
    d4->raise();
    resizeDocks({d4}, {170}, Qt::Vertical);
    resizeDocks({d1, d2}, {220, 340}, Qt::Horizontal);

    connect(scalarBox_, &QComboBox::currentTextChanged, this, &MainWindow::scalarChanged);
    connect(warpSlider_, &QSlider::valueChanged, this, [this](int) { applyWarp(); });
    connect(edges_, &QCheckBox::toggled, view_, &Viewport::setEdgesVisible);
    connect(edges_, &QCheckBox::toggled, this, [this](bool on) {
        if (actEdges_->isChecked() != on) actEdges_->setChecked(on);
    });
    connect(view_, &Viewport::faceSelectionChanged, this, [this](bool has) {
        if (!has) {
            statusBar()->showMessage(tr("Đã bỏ chọn mặt."), 5000);
            return;
        }
        const QVector3D n = view_->selectionNormal();
        statusBar()->showMessage(
            tr("Đã chọn mặt phẳng: pháp tuyến (%1, %2, %3), %4 tam giác, diện tích %5 m². Bấm Liên kết / Lực / Nhiệt để gán, Esc để bỏ chọn.")
                .arg(n.x(), 0, 'f', 3).arg(n.y(), 0, 'f', 3).arg(n.z(), 0, 'f', 3)
                .arg(view_->selectionTriangles()).arg(view_->selectionArea(), 0, 'g', 5), 30000);
    });
    connect(view_, &Viewport::probed, this, [this](const QString& t) { statusBar()->showMessage(t, 20000); });
}

void MainWindow::applyIcons() {
    const Palette p = paletteFor(dark_);
    for (const auto& [a, kind] : iconActions_) a->setIcon(makeIcon(kind, p.ink(), p.accentColor()));
}

void MainWindow::setTheme(bool dark) {
    dark_ = dark;
    QSettings().setValue("ui/dark", dark);
    qApp->setStyleSheet(fastcae::gui::styleSheet(dark));
    view_->setDarkTheme(dark);
    if (scriptDock_) scriptDock_->setDarkTheme(dark);
    if (helpDlg_) helpDlg_->setDark(dark);
    if (capabilityDlg_) capabilityDlg_->setDark(dark);
    applyIcons();
    if (actDark_) {
        QSignalBlocker b1(actDark_), b2(actLight_);
        actDark_->setChecked(dark);
        actLight_->setChecked(!dark);
    }
}

void MainWindow::setPickMode(bool on) {
    view_->setPickMode(on);
    if (QAction* a = act("pickface"); a && a->isChecked() != on) {
        QSignalBlocker b(a);
        a->setChecked(on);
    }
    if (on) statusBar()->showMessage(tr("Chế độ chọn mặt: bấm lên một mặt phẳng của vật thể (bấm ra ngoài hoặc Esc = bỏ chọn)."), 15000);
}

bool MainWindow::applyPickedSpec(const QString& spec) {
    QString planeLine;
    const QString name = pickedFaceName(&planeLine);
    if (name.isEmpty()) return false;
    if (!planeLine.isEmpty()) addCaseLine(planeLine);
    view_->clearFaceSelection();
    const QString kind = spec.section(' ', 0, 0);
    addCaseLine(QString("%1 = %2 %3").arg(kind, name, spec.section(' ', 1)).trimmed());
    runSolver();
    return true;
}

QString MainWindow::caseText() const { return editor_->toPlainText(); }

QString MainWindow::pickedFaceName(QString* planeLine) const {
    if (!view_->hasFaceSelection()) return {};
    const QVector3D n = view_->selectionNormal();
    const QString params = QString("plane %1 %2 %3 %4 %5")
                               .arg(double(n.x()), 0, 'g', 9).arg(double(n.y()), 0, 'g', 9).arg(double(n.z()), 0, 'g', 9)
                               .arg(view_->selectionOffset(), 0, 'g', 12).arg(view_->selectionTolerance(), 0, 'g', 6);
    int count = 0;
    for (const auto& line : editor_->toPlainText().split('\n')) {
        const QString t = line.section('#', 0, 0);
        const int eq = t.indexOf('=');
        if (eq <= 0 || t.left(eq).trimmed().compare("nodeset", Qt::CaseInsensitive) != 0) continue;
        ++count;
        const QStringList tok = t.mid(eq + 1).simplified().split(' ');
        if (tok.size() >= 2 && tok.mid(1).join(' ') == params) return tok[0];   // cùng mặt đã chọn trước đó
    }
    const QString name = QString("face%1").arg(count + 1);
    if (planeLine) *planeLine = QString("nodeset = %1 %2").arg(name, params);
    return name;
}

bool MainWindow::runScriptFile(const QString& path) {
    scriptDock_->show();
    scriptDock_->raise();
    if (!scriptDock_->loadFile(path)) return false;
    scriptDock_->run();
    return true;
}

void MainWindow::waitForScript() {
    while (scriptDock_->running()) QApplication::processEvents(QEventLoop::AllEvents, 50);
    QApplication::processEvents();
}

void MainWindow::showRibbonTab(int index) { ribbon_->setCurrentIndex(index); }

void MainWindow::refreshTitle() {
    const QString name = casePath_.isEmpty() ? tr("bài toán mới") : QFileInfo(casePath_).fileName();
    // Tiêu đề cố định: "FastCAE 1.0.0 - Trần Minh Tâm". Không nối tên file/ứng dụng
    // (Qt tự thêm "- <displayName>" nếu có, nên main_gui.cpp không đặt displayName).
    Q_UNUSED(name);
    setWindowTitle(QString("FastCAE %1 - %2").arg(FASTCAE_VERSION, QString::fromUtf8(about::kAuthor)));
}

void MainWindow::log(const QString& s) { logView_->appendPlainText(s); }

// ------------------------------------------------------------------ case-file helpers
QString MainWindow::caseValue(const QString& key) const {
    for (const auto& line : editor_->toPlainText().split('\n')) {
        const QString t = line.section('#', 0, 0);
        const int eq = t.indexOf('=');
        if (eq > 0 && t.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0) return t.mid(eq + 1).trimmed();
    }
    return {};
}

void MainWindow::setCaseKey(const QString& key, const QString& value) {
    QStringList out;
    bool done = false;
    for (const auto& line : editor_->toPlainText().split('\n')) {
        const QString t = line.section('#', 0, 0);
        const int eq = t.indexOf('=');
        if (eq > 0 && t.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0) {
            if (!done) out << QString("%1 = %2").arg(key, value);
            done = true;
            continue;
        }
        out << line;
    }
    if (!done) {
        while (!out.isEmpty() && out.last().trimmed().isEmpty()) out.removeLast();
        out << QString("%1 = %2").arg(key, value);
    }
    editor_->setPlainText(out.join('\n'));
}

void MainWindow::addCaseLine(const QString& line) {
    QString t = editor_->toPlainText();
    if (!t.endsWith('\n')) t += '\n';
    editor_->setPlainText(t + line + '\n');
}

bool MainWindow::ensureType(const QString& type, const QString& demo) {
    if (caseValue("type").compare(type, Qt::CaseInsensitive) == 0) return false;
    newFromDemo(demo);
    statusBar()->showMessage(tr("Đã nạp ví dụ phù hợp (%1) vì bài toán hiện tại thuộc loại khác.").arg(type), 8000);
    return true;
}

// ------------------------------------------------------------------ mở / lưu / nhập
void MainWindow::newFromDemo(const QString& name) {
    editor_->setPlainText(QString::fromStdString(demoCaseText(name.toStdString())));
    casePath_.clear();
    refreshTitle();
    runSolver();
}

bool MainWindow::loadCaseFile(const QString& p) {
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    editor_->setPlainText(QString::fromUtf8(f.readAll()));
    casePath_ = QFileInfo(p).absoluteFilePath();
    refreshTitle();
    runSolver();
    return true;
}

void MainWindow::selectScalar(const QString& name) {
    const int i = scalarBox_->findText(name);
    if (i >= 0) scalarBox_->setCurrentIndex(i);
}
void MainWindow::setWarpPercent(int v) { warpSlider_->setValue(v); }
void MainWindow::setEdges(bool on) { edges_->setChecked(on); }
void MainWindow::setView(int which) { view_->setStandardView(which); }
void MainWindow::clickViewport(QPoint p) {
    QMouseEvent press(QEvent::MouseButtonPress, p, view_->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, p, view_->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view_, &press);
    QApplication::sendEvent(view_, &release);
}

// Thu muc mac dinh cho hop thoai Mo/Luu: thu muc cuoi cung da dung, neu khong thi Documents/FastCAE (khong bao gio Program Files).
static QString dialogDir() {
    QSettings st("FastCAE", "FastCAE");
    QString d = st.value("lastDir").toString();
    if (d.isEmpty() || !QDir(d).exists()) {
        d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (d.isEmpty()) d = QDir::homePath();
        d += "/FastCAE";
        QDir().mkpath(d);
    }
    return d;
}
static void rememberDir(const QString& file) {
    if (file.isEmpty()) return;
    QSettings("FastCAE", "FastCAE").setValue("lastDir", QFileInfo(file).absolutePath());
}

// Bộ lọc hộp thoại lấy từ danh sách đuôi file của lõi (cad_import), để thêm định dạng chỉ cần sửa một chỗ.
static QString cadGlobs(const QStringList& only = {}) {
    QStringList g;
    for (const auto& e : cadImportExtensions()) {
        const QString q = QString::fromStdString(e);
        if (only.isEmpty() || only.contains(q)) g << "*." + q;
    }
    return g.join(' ');
}
static const QStringList kBrepExt = {"step", "stp", "iges", "igs", "brep", "brp", "xao", "fcstd"};
static const QStringList kMeshExt = {"msh", "inp", "unv", "bdf", "nas", "med", "mesh"};

static QString cadFilters() {
    return QObject::tr("CAD hình học (%1)").arg(cadGlobs(kBrepExt)) + ";;" +
           QObject::tr("Lưới bề mặt STL (%1)").arg(cadGlobs({"stl"})) + ";;" +
           QObject::tr("Lưới khối: Gmsh, Abaqus/CalculiX, Nastran, I-deas, MED, Medit (%1)").arg(cadGlobs(kMeshExt));
}

void MainWindow::openCase() {
    const QString all = QObject::tr("Tất cả định dạng hỗ trợ (*.fcae *.txt %1 *.vtk *.fcres *.fcad *.lua *.py *.cpp *.cc *.cxx)").arg(cadGlobs());
    const QString filters = all + ";;" + tr("Case FastCAE (*.fcae *.txt)") + ";;" +
                            tr("CAD và lưới (%1)").arg(cadGlobs()) + ";;" + cadFilters() + ";;" +
                            tr("Kết quả VTK ASCII (*.vtk)") + ";;" + tr("Script (*.lua *.py *.cpp *.cc *.cxx)") + ";;" +
                            tr("Tất cả (*)");
    const QString p = QFileDialog::getOpenFileName(this, tr("Mở file"), dialogDir(), filters);
    rememberDir(p);
    if (!p.isEmpty()) openAnyFile(p);
}

bool MainWindow::openAnyFile(const QString& p) {
    const QString ext = QFileInfo(p).suffix().toLower();
    if (ext == "fcae" || ext == "txt") {
        if (loadCaseFile(p)) return true;
        QMessageBox::warning(this, tr("Không mở được"), tr("Không đọc được %1").arg(p));
        return false;
    }
    if (ext == "vtk") return showResultFile(p);
    if (ext == "fcres") return showStoredResult(p);
    if (ext == "fcad") return openParamModel(p);
    if (ext == "lua" || ext == "py" || ext == "cpp" || ext == "cc" || ext == "cxx") {
        scriptDock_->show();
        scriptDock_->raise();
        if (scriptDock_->loadFile(p)) return true;
        QMessageBox::warning(this, tr("Mở script"), tr("Không đọc được %1").arg(p));
        return false;
    }
    if (canImportFormat(p.toStdString())) {
        importCadFile(p);
        return true;
    }
    QMessageBox::information(this, tr("Chưa hỗ trợ định dạng này"),
                             QString::fromStdString(cadFormatAdvice(p.toStdString())) +
                                 tr("\n\nĐịnh dạng mở trực tiếp: .fcae/.txt (bài toán); CAD: %1; "
                                    ".vtk ASCII (kết quả); .lua/.py/.cpp (script).").arg(cadGlobs()));
    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) { openAnyFile(u.toLocalFile()); break; }   // một file mỗi lần thả
}

void MainWindow::openResultDialog() {
    const QString p = QFileDialog::getOpenFileName(this, tr("Mở kết quả"), dialogDir(), tr("Kết quả (*.fcres *.vtk);;Kết quả FastCAE (*.fcres);;Kết quả VTK ASCII (*.vtk);;Tất cả (*)"));
    rememberDir(p);
    if (!p.isEmpty()) openAnyFile(p);
}

bool MainWindow::showResultFile(const QString& path) {
    RunReport r;
    VtkReadInfo info;
    std::string err;
    if (!readVtk(path.toStdString(), r.mesh, r.fields, &info, &err)) {
        log(tr("✗ Không hiển thị được %1: %2").arg(path, QString::fromStdString(err)));
        statusBar()->showMessage(tr("Không mở được kết quả: ") + QString::fromStdString(err), 12000);
        return false;
    }
    r.ok = true;
    r.type = "external";
    r.message = QString(tr("Đã nạp %1: %2 nút, %3 ô (%4 tứ diện)%5"))
                    .arg(QFileInfo(path).fileName()).arg(r.mesh.numNodes()).arg(info.cellsRead).arg(info.tetsProduced)
                    .arg(info.cellsSkipped ? tr(", bỏ qua %1 ô bề mặt/đường").arg(info.cellsSkipped) : QString()).toStdString();
    {   // lịch sử hội tụ cạnh file kết quả (SU2 history.csv...)
        const std::string hist = findHistoryBeside(path.toStdString());
        std::string he;
        if (!hist.empty() && readHistoryCsv(hist, r.series, &he))
            r.message += tr("; lịch sử hội tụ %1 (%2 dãy, nút Đồ thị)").arg(QFileInfo(QString::fromStdString(hist)).fileName()).arg(r.series.size()).toStdString();
    }
    casePath_.clear();
    finishRun(r, QString());
    {   // chuỗi bước: flow_00000.vtk, flow_00002.vtk ... cùng thư mục -> thanh trượt thời gian
        const auto seq = findVtkSequence(path.toStdString());
        if (seq.size() >= 2) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            std::vector<VtkFields> frames;
            QStringList labels;
            int cur = 0, skipped = 0;
            for (const auto& fr : seq) {
                Mesh m2; VtkFields f2; std::string e2;
                if (!readVtk(fr.path, m2, f2, nullptr, &e2) || m2.nodes.size() != last_.mesh.nodes.size()) { ++skipped; continue; }
                if (QFileInfo(QString::fromStdString(fr.path)) == QFileInfo(path)) cur = int(frames.size());
                labels << QFileInfo(QString::fromStdString(fr.path)).completeBaseName();
                frames.push_back(std::move(f2));
            }
            QApplication::restoreOverrideCursor();
            if (frames.size() >= 2) {
                setupFrames(std::move(frames), labels, cur);
                log(tr("Chuỗi bước thời gian: %1 khung%2").arg(frameFields_.size()).arg(skipped ? tr(" (bỏ qua %1 file khác lưới)").arg(skipped) : QString()));
            }
        }
    }
    // chọn sẵn trường đầu tiên để thấy màu ngay (ưu tiên nhiệt độ, áp suất, vận tốc, chuyển vị, ứng suất)
    for (const char* pref : {"von_mises_nodal", "von_mises", "temperature", "T", "p", "U_magnitude", "speed", "displacement_magnitude"}) {
        const int idx = scalarBox_->findText(pref);
        if (idx >= 0) { scalarBox_->setCurrentIndex(idx); return true; }
    }
    if (scalarBox_->count() > 1) scalarBox_->setCurrentIndex(1);
    return true;
}

void MainWindow::saveCase() {
    QString p = casePath_.isEmpty()
                    ? QFileDialog::getSaveFileName(this, tr("Lưu bài toán"), dialogDir() + "/case.fcae", tr("Case FastCAE (*.fcae)"))
                    : casePath_;
    if (p.isEmpty()) return;
    rememberDir(p);
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Không lưu được"), tr("Không ghi được %1\n%2\nHãy chọn thư mục khác (ví dụ Documents).").arg(p, f.errorString()));
        return;
    }
    QString text = editor_->toPlainText();
    // đóng dấu tác giả ở đầu file case (một lần)
    if (!text.contains("Tran Minh Tam") && !text.contains(QString::fromUtf8(about::kAuthor)))
        text = QString::fromUtf8("# Tạo bởi FastCAE %1 — tác giả %2 (%3)\n").arg(FASTCAE_VERSION, QString::fromUtf8(about::kAuthor),
                                                                               QString::fromUtf8(about::kLicense)) + text;
    f.write(text.toUtf8());
    casePath_ = p;
    refreshTitle();
}

void MainWindow::importCad() {
    const QString p = QFileDialog::getOpenFileName(
        this, tr("Nhập CAD"), dialogDir(),
        tr("Tất cả CAD và lưới (%1);;").arg(cadGlobs()) + cadFilters() + tr(";;Tất cả (*)"));
    if (p.isEmpty()) return;
    rememberDir(p);
    importCadFile(p);
}

void MainWindow::importCadFile(const QString& p) {
    const std::string pstd = p.toStdString();
    if (!canImportFormat(pstd)) {   // định dạng đóng (SLDPRT, IPT, CATPart...) mà chưa có bộ chuyển đổi
        QMessageBox::information(this, tr("Định dạng cần chuyển đổi"), QString::fromStdString(cadFormatAdvice(pstd)));
        return;
    }
    const CadFormat fmt = detectCadFormat(pstd);
    const bool isMesh = fmt == CadFormat::Msh || fmt == CadFormat::ForeignMesh;   // lưới có sẵn: không chia lưới lại
    bool ok = false;
    const QStringList units = {"mm", "cm", "m", "inch"};
    const QString unit = QInputDialog::getItem(this, tr("Nhập CAD"), tr("Đơn vị của file CAD:"), units, 0, false, &ok);
    if (!ok) return;
    const double scale = unit == "mm" ? 0.001 : unit == "cm" ? 0.01 : unit == "inch" ? 0.0254 : 1.0;
    double hmm = 0;
    if (!isMesh) {
        hmm = QInputDialog::getDouble(this, tr("Nhập CAD"), tr("Cỡ phần tử tối đa (mm; 0 = tự động):"), 0.0, 0.0, 1e6, 3, &ok);
        if (!ok) return;
    }
    QStringList keep;
    for (const auto& line : editor_->toPlainText().split('\n')) {
        const QString t = line.trimmed();
        if (t.startsWith("geometry") || t.startsWith("divisions") || t.startsWith("mesh_size") ||
            t.startsWith("unit_scale") || t.startsWith("order"))
            continue;
        keep << line;
    }
    QString head = QString("geometry = file %1\nunit_scale = %2\norder = 2\n").arg(p).arg(scale);
    if (hmm > 0) head += QString("mesh_size = %1\n").arg(hmm / 1000.0, 0, 'g', 8);
    editor_->setPlainText(head + keep.join('\n'));
    runSolver();
}

void MainWindow::designParametric() {
    ParametricDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    QStringList keep;
    for (const auto& line : editor_->toPlainText().split('\n')) {
        const QString t = line.trimmed();
        if (t.startsWith("geometry") || t.startsWith("divisions") || t.startsWith("mesh_size") ||
            t.startsWith("unit_scale"))
            continue;
        keep << line;
    }
    editor_->setPlainText(dlg.geometryLines() + keep.join('\n'));
    runSolver();
}

// ------------------------------------------------------------------ giải
void MainWindow::runSolver() {
    if (running_) {   // xếp hàng: sẽ giải lại với nội dung case mới nhất ngay khi lần hiện tại xong
        pendingRun_ = true;
        statusBar()->showMessage(tr("Đang giải; yêu cầu mới sẽ chạy ngay sau đó."), 5000);
        return;
    }
    CaseFile cf;
    cf.parse(editor_->toPlainText().toStdString());
    const std::string base = casePath_.isEmpty() ? std::string() : QFileInfo(casePath_).absolutePath().toStdString();
    running_ = true;
    act("run")->setEnabled(false);
    busy_->show();
    statusBar()->showMessage(tr("Đang giải (chạy nền, vẫn thao tác được khung nhìn)..."));

    struct Out { RunReport rep; std::string log; };
    auto out = std::make_shared<Out>();
    QThread* th = QThread::create([cf, base, out] {
        std::ostringstream os;
        out->rep = runCase(cf, os, base, /*writeOutput=*/false);
        out->log = os.str();
    });
    connect(th, &QThread::finished, this, [this, th, out] {
        th->deleteLater();
        running_ = false;
        act("run")->setEnabled(true);
        busy_->hide();
        if (pendingRun_) {   // kết quả này đã cũ so với case hiện tại: bỏ qua, giải lại
            pendingRun_ = false;
            log(QString::fromStdString(out->log).trimmed());
            runSolver();
            return;
        }
        finishRun(out->rep, QString::fromStdString(out->log));
        emit runFinished();
    });
    th->start();
}

void MainWindow::waitForRun() {
    if (!running_) return;
    QEventLoop loop;
    connect(this, &MainWindow::runFinished, &loop, &QEventLoop::quit);
    loop.exec();
}

void MainWindow::finishRun(const RunReport& rep, const QString& logText) {
    clearFrames();
    last_ = rep;
    log(logText.trimmed());
    log(QString("=> %1").arg(QString::fromStdString(last_.message)));
    if (last_.mesh.numCells() > 0) {
        view_->setModel(last_.mesh, last_.fields);
        scalarBox_->blockSignals(true);
        scalarBox_->clear();
        scalarBox_->addItem("");
        scalarBox_->addItems(view_->scalarNames());
        scalarBox_->blockSignals(false);
        const QString first = (last_.type == "elastic" || last_.type == "frame" || last_.type == "shell") ? "von_mises_nodal"
                              : last_.type == "thermal" ? "temperature" : "speed";
        const int idx = scalarBox_->findText(first);
        if (last_.ok && idx >= 0) scalarBox_->setCurrentIndex(idx);
        // Lưới dày: cạnh đen che hết màu -> tự tắt cạnh (người dùng vẫn bật lại được bằng ô "Hiện cạnh lưới").
        if (view_->surfaceEdgeCount() > 4000 && edges_->isChecked()) edges_->setChecked(false);
        // Có chuyển vị: đặt sẵn phóng đại 30% để thấy dạng biến dạng ngay.
        warpSlider_->setValue(view_->hasDisplacement() ? 30 : 0);
        view_->setStandardView(last_.type == "cavity" ? 3 : 0);
    }
    if (last_.ok) {
        const bool foam = caseValue("solver").compare("openfoam", Qt::CaseInsensitive) == 0;
        if (!last_.summary.empty())
            summary_->setText(QString::fromStdString(last_.summary));
        else if (last_.type == "elastic")
            summary_->setText(tr("Chuyển vị lớn nhất: %1 m\nvon Mises lớn nhất: %2 Pa\nCG: %3 vòng")
                                  .arg(last_.maxValue, 0, 'g', 5).arg(last_.secondary, 0, 'g', 5).arg(last_.iterations));
        else if (last_.type == "thermal")
            summary_->setText(tr("Tmax = %1\nTmin = %2").arg(last_.maxValue, 0, 'g', 6).arg(last_.secondary, 0, 'g', 6));
        else if (foam)
            summary_->setText(tr("OpenFOAM (icoFoam)\nu tại tâm khoang = %1 m/s\n|ω|max = %2 1/s")
                                  .arg(last_.secondary, 0, 'g', 6).arg(last_.maxValue, 0, 'g', 4));
        else
            summary_->setText(tr("CFD: %1 bước thời gian giả, |ω|max = %2").arg(last_.iterations).arg(last_.maxValue, 0, 'g', 4));
        statusBar()->showMessage(tr("Xong: ") + QString::fromStdString(last_.message), 8000);
    } else {
        summary_->setText(tr("Lỗi: ") + QString::fromStdString(last_.message));
        statusBar()->showMessage(tr("Lỗi: ") + QString::fromStdString(last_.message), 12000);
    }
    if (last_.ok && last_.frames.size() >= 2 && last_.mesh.numCells() > 0) {   // chuỗi từ solver (dạng riêng, mốc thời gian)
        std::vector<VtkFields> fr;
        QStringList lb;
        for (const auto& f : last_.frames) { fr.push_back(f.fields); lb << QString::fromStdString(f.label); }
        setupFrames(std::move(fr), lb, last_.currentFrame);
    }
    refreshTree();
}

// ------------------------------------------------------------------ thư viện cấu kiện và dựng khung
void MainWindow::openLibrary(const QString& category) {
    ComponentLibraryDialog dlg(this, category);
    if (dlg.exec() != QDialog::Accepted || dlg.caseText().isEmpty()) return;
    editor_->setPlainText(dlg.caseText());
    casePath_.clear();
    refreshTitle();
    runSolver();
}

QStringList MainWindow::caseValues(const QString& key) const {
    QStringList out;
    for (const auto& line : editor_->toPlainText().split('\n')) {
        const QString t = line.section('#', 0, 0);
        const int eq = t.indexOf('=');
        if (eq > 0 && t.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0) out << t.mid(eq + 1).trimmed();
    }
    return out;
}

bool MainWindow::requireFrame() {
    if (caseValue("type").compare("frame", Qt::CaseInsensitive) == 0) return true;
    QMessageBox::information(this, tr("Khung / dàn 1D"),
                             tr("Bài toán hiện tại không phải khung/dàn. Mở Cấu kiện ▸ Dàn & khung để tạo một khung mẫu, rồi chỉnh sửa bằng các nút này."));
    return false;
}

namespace {
bool isNumber(const QString& s) {
    bool ok = false;
    s.toDouble(&ok);
    return ok;
}
}  // namespace

void MainWindow::frameSetAnalysis(const QString& kind) {
    if (!requireFrame()) return;
    setCaseKey("analysis", kind);
    if (kind != "static" && caseValue("mode").isEmpty()) setCaseKey("mode", "1");
    runSolver();
}

void MainWindow::frameChooseMode() {
    if (!requireFrame()) return;
    const QString a = caseValue("analysis").toLower();
    if (a != "modal" && a != "buckling") {
        QMessageBox::information(this, tr("Chọn dạng"), tr("Chỉ dùng cho phân tích dao động riêng hoặc mất ổn định. Bấm nút Dao động riêng hoặc Mất ổn định trước."));
        return;
    }
    bool ok = false;
    const int cur = caseValue("mode").toInt() > 0 ? caseValue("mode").toInt() : 1;
    const int k = QInputDialog::getInt(this, tr("Chọn dạng"), tr("Dạng số (1 = thấp nhất):"), cur, 1, 40, 1, &ok);
    if (!ok) return;
    const int modes = std::max(caseValue("modes").toInt(), 6);
    if (k > modes) setCaseKey("modes", QString::number(k));
    setCaseKey("mode", QString::number(k));
    runSolver();
}

void MainWindow::frameAddNode() {
    if (!requireFrame()) return;
    int next = 1;
    for (const auto& v : caseValues("node")) next = std::max(next, v.section(' ', 0, 0).toInt() + 1);
    const auto r = askFields(this, tr("Thêm nút"), tr("Nút số %1. Toạ độ tính bằng mét (Z hướng lên).").arg(next),
                             {{FieldSpec::Text, "X (m)", "0", {}}, {FieldSpec::Text, "Y (m)", "0", {}}, {FieldSpec::Text, "Z (m)", "0", {}}});
    if (r.isEmpty()) return;
    if (!isNumber(r[0]) || !isNumber(r[1]) || !isNumber(r[2])) {
        QMessageBox::warning(this, tr("Thêm nút"), tr("Toạ độ phải là số."));
        return;
    }
    addCaseLine(QString("node = %1 %2 %3 %4").arg(next).arg(r[0], r[1], r[2]));
    runSolver();
}

void MainWindow::frameAddSection() {
    if (!requireFrame()) return;
    const int n = caseValues("section").size() + 1;
    const auto r = askFields(
        this, tr("Tiết diện"),
        tr("Tham số theo loại (mét):\n rect: b h · circle: d · pipe: d t · ibeam: b h tf tw · box: b h t · general: A Iy Iz J"),
        {{FieldSpec::Text, tr("Tên"), QString("S%1").arg(n), {}},
         {FieldSpec::Combo, tr("Loại"), "rect", {"rect", "circle", "pipe", "ibeam", "box", "general"}},
         {FieldSpec::Text, tr("Tham số"), "0.2 0.4", {}}});
    if (r.isEmpty() || r[0].isEmpty()) return;
    addCaseLine(QString("section = %1 %2 %3").arg(r[0], r[1], r[2]));
    runSolver();
}

void MainWindow::frameAddMember() {
    if (!requireFrame()) return;
    QStringList nodes, secs;
    for (const auto& v : caseValues("node")) nodes << v.section(' ', 0, 0);
    for (const auto& v : caseValues("section")) secs << v.section(' ', 0, 0);
    if (nodes.size() < 2) {
        QMessageBox::information(this, tr("Thêm thanh"), tr("Cần ít nhất 2 nút. Dùng nút Thêm nút trước."));
        return;
    }
    if (secs.isEmpty()) {
        QMessageBox::information(this, tr("Thêm thanh"), tr("Chưa có tiết diện. Dùng nút Tiết diện trước."));
        return;
    }
    const auto r = askFields(this, tr("Thêm thanh"), tr("Dầm chịu uốn; thanh dàn chỉ chịu dọc trục; cáp chỉ chịu kéo; lò xo có độ cứng k (N/m)."),
                             {{FieldSpec::Combo, tr("Nút đầu"), nodes.first(), nodes},
                              {FieldSpec::Combo, tr("Nút cuối"), nodes.last(), nodes},
                              {FieldSpec::Combo, tr("Tiết diện"), secs.first(), secs},
                              {FieldSpec::Combo, tr("Loại thanh"), "beam", {"beam", "truss", "cable", "spring"}},
                              {FieldSpec::Text, tr("Độ cứng lò xo k (N/m, chỉ cho spring)"), "1e6", {}},
                              {FieldSpec::Check, tr("Bản lề ở đầu thanh (đầu 1)"), "", {}},
                              {FieldSpec::Check, tr("Bản lề ở cuối thanh (đầu 2)"), "", {}}});
    if (r.isEmpty()) return;
    if (r[0] == r[1]) {
        QMessageBox::warning(this, tr("Thêm thanh"), tr("Hai nút phải khác nhau."));
        return;
    }
    QString line;
    if (r[3] == "spring") line = QString("member = %1 %2 - spring %3").arg(r[0], r[1], r[4]);
    else {
        line = QString("member = %1 %2 %3 %4").arg(r[0], r[1], r[2], r[3]);
        if (r[3] == "beam") {
            if (!r[5].isEmpty()) line += " pin1";
            if (!r[6].isEmpty()) line += " pin2";
        }
    }
    addCaseLine(line);
    runSolver();
}

void MainWindow::frameAddSupport() {
    if (!requireFrame()) return;
    QStringList nodes;
    for (const auto& v : caseValues("node")) nodes << v.section(' ', 0, 0);
    if (nodes.isEmpty()) return;
    const auto r = askFields(this, tr("Liên kết nút"), tr("Bậc tự do bị khoá: fixed (ngàm) · pinned (gối cố định) · hoặc liệt kê x y z rx ry rz (ví dụ gối con lăn: z)."),
                             {{FieldSpec::Combo, tr("Nút"), nodes.first(), nodes}, {FieldSpec::Text, tr("Bậc tự do khoá"), "fixed", {}}});
    if (r.isEmpty() || r[1].isEmpty()) return;
    addCaseLine(QString("support = %1 %2").arg(r[0], r[1]));
    runSolver();
}

void MainWindow::frameAddNodeLoad() {
    if (!requireFrame()) return;
    QStringList nodes;
    for (const auto& v : caseValues("node")) nodes << v.section(' ', 0, 0);
    if (nodes.isEmpty()) return;
    const auto r = askFields(this, tr("Tải tại nút"), tr("Lực (N) và mômen (N·m) theo hệ toạ độ chung; Z hướng lên."),
                             {{FieldSpec::Combo, tr("Nút"), nodes.last(), nodes}, {FieldSpec::Text, "Fx", "0", {}}, {FieldSpec::Text, "Fy", "0", {}},
                              {FieldSpec::Text, "Fz", "-1000", {}}, {FieldSpec::Text, "Mx", "0", {}}, {FieldSpec::Text, "My", "0", {}},
                              {FieldSpec::Text, "Mz", "0", {}}});
    if (r.isEmpty()) return;
    for (int i = 1; i < r.size(); ++i)
        if (!isNumber(r[i])) {
            QMessageBox::warning(this, tr("Tải tại nút"), tr("Giá trị tải phải là số."));
            return;
        }
    addCaseLine(QString("nload = %1 %2 %3 %4 %5 %6 %7").arg(r[0], r[1], r[2], r[3], r[4], r[5], r[6]));
    runSolver();
}

void MainWindow::frameAddUdl() {
    if (!requireFrame()) return;
    const int nm = caseValues("member").size();
    if (nm == 0) return;
    QStringList ids;
    for (int i = 1; i <= nm; ++i) ids << QString::number(i);
    const auto r = askFields(this, tr("Tải phân bố"), tr("Tải đều theo chiều dài thanh (N/m), hệ toạ độ chung; Z hướng lên. Thanh đánh số theo thứ tự khai báo."),
                             {{FieldSpec::Combo, tr("Thanh số"), ids.last(), ids}, {FieldSpec::Text, "qx", "0", {}}, {FieldSpec::Text, "qy", "0", {}},
                              {FieldSpec::Text, "qz", "-10000", {}}});
    if (r.isEmpty()) return;
    for (int i = 1; i < r.size(); ++i)
        if (!isNumber(r[i])) {
            QMessageBox::warning(this, tr("Tải phân bố"), tr("Giá trị tải phải là số."));
            return;
        }
    addCaseLine(QString("udl = %1 %2 %3 %4").arg(r[0], r[1], r[2], r[3]));
    runSolver();
}

void MainWindow::runInternal() {
    setCaseKey("solver", "builtin");
    runSolver();
}

void MainWindow::runCalculix() {
    const QString saved = SolverManagerDialog::savedPath("calculix");
    if (caseValue("ccx_path").isEmpty() && !saved.isEmpty()) setCaseKey("ccx_path", saved);
    if (findCalculix(caseValue("ccx_path").toStdString()).empty()) {
        const auto r = QMessageBox::question(
            this, tr("Chưa tìm thấy CalculiX"),
            tr("Không tìm thấy chương trình ccx (PATH, biến FASTCAE_CCX hoặc đường dẫn đã đặt).\n"
               "FastCAE vẫn ghi file .inp để bạn chạy tay. Mở bảng Quản lý solver để đặt đường dẫn?"));
        if (r == QMessageBox::Yes) openSolverManager("calculix");
    }
    setCaseKey("solver", "calculix");
    if (caseValue("job_name").isEmpty()) setCaseKey("job_name", "fastcae_job");
    if (caseValue("work_dir").isEmpty()) {
        const QString base = casePath_.isEmpty() ? QString::fromStdString(fastcae::userWorkRoot())
                                                 : QFileInfo(casePath_).absolutePath();
        setCaseKey("work_dir", base + "/ccx_run");
    }
    runSolver();
}

void MainWindow::addCondition(int kind) {
    const QString type = caseValue("type").toLower();
    const bool structural = kind == BcDialog::Fix || kind == BcDialog::Load;
    if ((structural && type != "elastic") || (!structural && type != "thermal")) {
        QMessageBox::information(this, tr("Điều kiện không phù hợp"),
                                 structural ? tr("Liên kết và lực dùng cho bài toán kết cấu (type = elastic). Chọn Tệp ▸ Ví dụ ▸ Dầm console.")
                                            : tr("Nhiệt độ và nguồn nhiệt dùng cho bài toán truyền nhiệt (type = thermal). Chọn Tệp ▸ Ví dụ ▸ Thanh dẫn nhiệt."));
        return;
    }
    QString planeLine;
    const QString picked = pickedFaceName(&planeLine);
    QString desc;
    if (!picked.isEmpty()) {
        const QVector3D n = view_->selectionNormal();
        desc = tr("%1 (n = %2, %3, %4; %5 m²)").arg(picked).arg(n.x(), 0, 'f', 2).arg(n.y(), 0, 'f', 2).arg(n.z(), 0, 'f', 2)
                   .arg(view_->selectionArea(), 0, 'g', 4);
    }
    BcDialog dlg(static_cast<BcDialog::Kind>(kind), this, picked, desc);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString line = dlg.caseLine();
    if (line.isEmpty()) return;
    if (dlg.usesPicked()) {
        if (!planeLine.isEmpty()) addCaseLine(planeLine);   // tập nút hình học (mặt phẳng) – không phụ thuộc lưới
        view_->clearFaceSelection();
    }
    addCaseLine(line);
    runSolver();
}

void MainWindow::editMaterial() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Vật liệu"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    lay->addLayout(form);
    struct Field { const char* key; const char* label; const char* def; QLineEdit* edit; };
    Field fields[] = {{"e", "Mô đun đàn hồi E (Pa)", "2.1e11", nullptr}, {"nu", "Hệ số Poisson nu", "0.3", nullptr},
                      {"rho", "Khối lượng riêng rho (kg/m³)", "7850", nullptr}, {"k", "Hệ số dẫn nhiệt k (W/m·K)", "50", nullptr}};
    for (auto& f : fields) {
        f.edit = new QLineEdit(caseValue(f.key).isEmpty() ? f.def : caseValue(f.key));
        form->addRow(tr(f.label), f.edit);
    }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    for (auto& f : fields) {
        bool ok = false;
        f.edit->text().toDouble(&ok);
        if (ok) setCaseKey(f.key, f.edit->text().trimmed());
    }
    runSolver();
}

void MainWindow::setElementOrder(int order) {
    if (caseValue("type").compare("elastic", Qt::CaseInsensitive) != 0 && caseValue("type").compare("thermal", Qt::CaseInsensitive) != 0) {
        QMessageBox::information(this, tr("Loại phần tử"), tr("Chỉ áp dụng cho bài toán kết cấu hoặc truyền nhiệt."));
        return;
    }
    setCaseKey("order", QString::number(order));
    runSolver();
}

void MainWindow::meshInfo() {
    if (last_.mesh.numCells() == 0) {
        QMessageBox::information(this, tr("Thông tin lưới"), tr("Chưa có lưới khối (bài toán CFD 2D dùng lưới ô vuông riêng)."));
        return;
    }
    Vec3 lo, hi;
    last_.mesh.bounds(lo, hi);
    QMessageBox::information(
        this, tr("Thông tin lưới"),
        tr("Số nút: %1\nSố phần tử tứ diện: %2\nThể tích: %3 m³\nHộp bao: [%4, %5, %6] → [%7, %8, %9] m")
            .arg(last_.mesh.numNodes()).arg(last_.mesh.numCells()).arg(last_.mesh.totalVolume(), 0, 'g', 6)
            .arg(lo.x, 0, 'g', 5).arg(lo.y, 0, 'g', 5).arg(lo.z, 0, 'g', 5)
            .arg(hi.x, 0, 'g', 5).arg(hi.y, 0, 'g', 5).arg(hi.z, 0, 'g', 5));
}

// ------------------------------------------------------------------ solver ngoài
void MainWindow::openSolverManager(const QString& selectId) {
    SolverManagerDialog dlg(this, selectId);
    connect(&dlg, &SolverManagerDialog::runRequested, this, [this](const QString& id, const QString& f) { runExternal(id, f); });
    dlg.exec();
}

void MainWindow::runExternal(const QString& id, const QString& inputFile) {
    const SolverInfo* s = findSolverInfo(id.toStdString());
    if (!s || s->steps.empty()) return;
    const QString hint = SolverManagerDialog::savedPath(id);
    QList<Job> jobs;
    std::string first;
    for (std::size_t i = 0; i < s->steps.size(); ++i) {
        std::string h = i == 0 ? hint.toStdString() : QFileInfo(QString::fromStdString(first)).absolutePath().toStdString();
        std::string exe = findExecutable(s->steps[i].exeNames, h, i == 0 ? s->envVar : "");
        if (exe.empty() && i > 0) exe = findExecutable(s->steps[i].exeNames);
        if (exe.empty()) {
            QMessageBox::warning(this, tr("Thiếu chương trình"),
                                 tr("Không tìm thấy chương trình của %1. Cài đặt rồi đặt đường dẫn trong bảng Quản lý solver.")
                                     .arg(QString::fromStdString(s->name)));
            return;
        }
        if (i == 0) first = exe;
        jobs.append({QString::fromStdString(exe),
                     QProcess::splitCommand(QString::fromStdString(expandArgs(s->steps[i].args, inputFile.toStdString()))),
                     QFileInfo(inputFile).absolutePath(),
                     QString::fromStdString(s->name) + (s->steps.size() > 1 ? QString(" (bước %1/%2)").arg(i + 1).arg(s->steps.size()) : QString())});
    }
    startJobs(jobs);
}

void MainWindow::runOpenFoamCavity() {
    bool ok = false;
    const int n = QInputDialog::getInt(this, tr("OpenFOAM - khoang kín nắp trượt"), tr("Số ô mỗi cạnh (20 nhanh, 41 mịn hơn):"), 20, 4, 200, 1, &ok);
    if (!ok) return;
    const double re = QInputDialog::getDouble(this, tr("OpenFOAM - khoang kín nắp trượt"), tr("Số Reynolds (≤ khoảng 1000 với lưới này):"), 100, 1, 5000, 1, &ok);
    if (!ok) return;
    editor_->setPlainText(QString::fromStdString(demoCaseText("cavity_openfoam")));
    casePath_.clear();
    refreshTitle();
    setCaseKey("n", QString::number(n));
    setCaseKey("re", QString::number(re));
    const QString saved = SolverManagerDialog::savedPath("openfoam");
    if (QFileInfo(saved).fileName() == "bashrc") setCaseKey("openfoam_bashrc", saved);
    setCaseKey("work_dir", QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/FastCAE/foam_run");
    log(tr("OpenFOAM: case ghi vào %1/cavity; log solver: log.blockMesh, log.icoFoam").arg(caseValue("work_dir")));
    runSolver();
}

void MainWindow::startJobs(const QList<Job>& jobs) {
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        QMessageBox::information(this, tr("Solver ngoài"), tr("Đang có một tiến trình chạy. Bấm Dừng trước khi chạy tiếp."));
        return;
    }
    queue_ = jobs;
    if (!proc_) {
        proc_ = new QProcess(this);
        proc_->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc_, &QProcess::readyRead, this, [this] { extLog_->appendPlainText(QString::fromLocal8Bit(proc_->readAll()).trimmed()); });
        connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                extLog_->appendPlainText(tr("✗ Không khởi động được tiến trình."));
                queue_.clear();
                actStop_->setEnabled(false);
            }
        });
        connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
            if (st != QProcess::NormalExit || code != 0) {
                extLog_->appendPlainText(tr("✗ Kết thúc với mã %1").arg(code));
                queue_.clear();
                actStop_->setEnabled(false);
                statusBar()->showMessage(tr("Solver ngoài lỗi hoặc bị dừng (mã %1). Xem thẻ 'Solver ngoài'.").arg(code), 12000);
                return;
            }
            extLog_->appendPlainText(tr("✓ Hoàn tất"));
            nextJob();
        });
    }
    // chuyển sang thẻ "Solver ngoài"
    for (auto* d : findChildren<QDockWidget*>())
        if (d->widget() == extLog_) d->raise();
    extLog_->clear();
    actStop_->setEnabled(true);
    nextJob();
}

void MainWindow::nextJob() {
    if (queue_.isEmpty()) {
        actStop_->setEnabled(false);
        statusBar()->showMessage(tr("Solver ngoài: xong tất cả các bước."), 10000);
        return;
    }
    const Job j = queue_.takeFirst();
    extLog_->appendPlainText(QString("▶ %1\n  %2 %3\n  (thư mục: %4)").arg(j.label, j.program, j.args.join(' '), j.workDir));
    statusBar()->showMessage(tr("Đang chạy: %1").arg(j.label));
    proc_->setWorkingDirectory(j.workDir);
    proc_->start(j.program, j.args);
}

void MainWindow::stopJobs() {
    queue_.clear();
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        proc_->kill();
        extLog_->appendPlainText(tr("■ Đã dừng theo yêu cầu."));
    }
    actStop_->setEnabled(false);
}

// ------------------------------------------------------------------ khác
void MainWindow::refreshTree() {
    tree_->clear();
    auto* geo = new QTreeWidgetItem(tree_, QStringList(tr("Hình học")));
    auto* mesh = new QTreeWidgetItem(tree_, QStringList(tr("Lưới")));
    auto* mat = new QTreeWidgetItem(tree_, QStringList(tr("Vật liệu")));
    auto* bc = new QTreeWidgetItem(tree_, QStringList(tr("Điều kiện biên / tải")));
    auto* sol = new QTreeWidgetItem(tree_, QStringList(tr("Solver")));
    auto* res = new QTreeWidgetItem(tree_, QStringList(tr("Kết quả")));
    CaseFile cf;
    cf.parse(editor_->toPlainText().toStdString());
    new QTreeWidgetItem(geo, QStringList(QString::fromStdString(cf.get("geometry", "-"))));
    if (cf.get("type", "elastic") == "frame")
        new QTreeWidgetItem(mesh, QStringList(tr("%1 nút, %2 thanh (khung 1D)").arg(cf.getAll("node").size()).arg(cf.getAll("member").size())));
    else
        new QTreeWidgetItem(mesh, QStringList(tr("%1 nút, %2 phần tử").arg(last_.mesh.numNodes()).arg(last_.mesh.numCells())));
    new QTreeWidgetItem(mat, QStringList(QString("E=%1  nu=%2  k=%3")
                                             .arg(QString::fromStdString(cf.get("e", "2.1e11")))
                                             .arg(QString::fromStdString(cf.get("nu", "0.3")))
                                             .arg(QString::fromStdString(cf.get("k", "50")))));
    for (const char* key : {"nodeset", "support", "fix", "load", "nload", "udl", "pressure", "temp", "heat"})
        for (const auto& v : cf.getAll(key))
            new QTreeWidgetItem(bc, QStringList(QString("%1: %2").arg(key).arg(QString::fromStdString(v))));
    new QTreeWidgetItem(sol, QStringList(QString("%1 — %2").arg(QString::fromStdString(cf.get("type", "elastic")),
                                                                  QString::fromStdString(cf.get("solver", "builtin")))));
    for (const auto& n : view_->scalarNames()) new QTreeWidgetItem(res, QStringList(n));
    tree_->expandAll();
}

void MainWindow::scalarChanged(const QString& name) { view_->showScalar(name); }

void MainWindow::exportVtk() {
    if (last_.mesh.numCells() == 0) return;
    const QString p = QFileDialog::getSaveFileName(this, tr("Xuất VTK"), dialogDir() + "/result.vtk", "VTK (*.vtk)");
    if (p.isEmpty()) return;
    std::string err;
    if (!writeVtk(p.toStdString(), last_.mesh, last_.fields, &err))
        QMessageBox::warning(this, "FastCAE", QString::fromStdString(err));
}

void MainWindow::saveScreenshot() {
    const QString p = QFileDialog::getSaveFileName(this, tr("Lưu ảnh khung nhìn"), dialogDir() + "/fastcae.png", "PNG (*.png)");
    if (p.isEmpty()) return;
    if (!view_->grab().save(p)) QMessageBox::warning(this, "FastCAE", tr("Không lưu được ảnh."));
}

void MainWindow::about(int tab) {
    AboutDialog dlg(this, tab);
    dlg.exec();
}

void MainWindow::showCapability() {
    if (!capabilityDlg_) {
        capabilityDlg_ = new HelpDialog(this, dark_, QStringLiteral("nang-luc"));
        connect(capabilityDlg_, &HelpDialog::openGuideRequested, this, [this](const QString& a) { showHelp(a); });
    }
    capabilityDlg_->setDark(dark_);
    capabilityDlg_->show();
    capabilityDlg_->raise();
    capabilityDlg_->activateWindow();
}

void MainWindow::showHelp(const QString& anchor) {
    if (!helpDlg_) helpDlg_ = new HelpDialog(this, dark_);   // không modal; thuộc cửa sổ chính nên tự huỷ cùng nó
    helpDlg_->setDark(dark_);
    helpDlg_->show();
    helpDlg_->raise();
    helpDlg_->activateWindow();
    // chờ cửa sổ dàn trang xong rồi mới cuộn tới chương cần mở
    QTimer::singleShot(0, helpDlg_, [dlg = helpDlg_, anchor] { dlg->showAnchor(anchor); });
}

bool MainWindow::showStoredResult(const QString& path) {
    StoredResult sr;
    std::string err;
    if (!loadResult(path.toStdString(), sr, &err)) {
        log(tr("✗ Không mở được kết quả %1: %2").arg(path, QString::fromStdString(err)));
        statusBar()->showMessage(tr("Không mở được kết quả: ") + QString::fromStdString(err), 12000);
        QMessageBox::warning(this, "FastCAE", QString::fromStdString(err));
        return false;
    }
    RunReport r;
    r.ok = true;
    r.type = sr.type;
    r.summary = sr.summary;
    r.values = sr.values;
    r.series = sr.series;
    r.mesh = std::move(sr.mesh);
    r.fields = std::move(sr.fields);
    r.message = tr("Đã nạp kết quả %1: %2 nút, %3 phần tử").arg(QFileInfo(path).fileName()).arg(r.mesh.numNodes()).arg(r.mesh.numCells()).toStdString();
    casePath_.clear();
    const bool hasFrames = sr.frames.size() >= 2;
    std::vector<VtkFields> fr;
    QStringList lb;
    for (auto& f : sr.frames) { lb << QString::fromStdString(f.label); fr.push_back(std::move(f.fields)); }
    finishRun(r, QString());
    if (hasFrames) setupFrames(std::move(fr), lb, sr.currentFrame);
    return true;
}

void MainWindow::saveResultFile() {
    if (last_.mesh.numCells() == 0 || !last_.ok) {
        QMessageBox::information(this, "FastCAE", tr("Chưa có kết quả để lưu. Hãy giải bài toán (F5) hoặc mở một kết quả trước."));
        return;
    }
    const QString p = QFileDialog::getSaveFileName(this, tr("Lưu kết quả"), dialogDir() + "/result.fcres", tr("Kết quả FastCAE (*.fcres)"));
    if (p.isEmpty()) return;
    rememberDir(p);
    StoredResult sr;
    sr.type = last_.type; sr.summary = last_.summary; sr.values = last_.values; sr.series = last_.series;
    sr.mesh = last_.mesh; sr.fields = last_.fields;
    for (std::size_t k = 0; k < frameFields_.size(); ++k) {
        StoredFrame sf;
        sf.label = int(k) < frameLabels_.size() ? frameLabels_[int(k)].toStdString() : std::string();
        sf.fields = frameFields_[k];
        sr.frames.push_back(std::move(sf));
    }
    sr.currentFrame = frameSlider_ ? frameSlider_->value() : 0;
    std::string err;
    if (!saveResult(p.toStdString(), sr, &err)) { QMessageBox::warning(this, "FastCAE", QString::fromStdString(err)); return; }
    statusBar()->showMessage(tr("Đã lưu kết quả: ") + p, 8000);
    log(tr("Đã lưu kết quả: ") + p);
}

bool MainWindow::openParamModel(const QString& path) {
    if (path.contains(QRegularExpression("\\s"))) {
        QMessageBox::information(this, "FastCAE", tr("Đường dẫn chứa khoảng trắng; hãy chuyển file .fcad vào thư mục không có khoảng trắng."));
        return false;
    }
    if (findGmshExecutable().empty()) {
        QMessageBox::information(this, "FastCAE", tr("Mô hình tham số cần chương trình gmsh để chia lưới. Cài gmsh và đặt vào PATH (hoặc biến FASTCAE_GMSH)."));
        return false;
    }
    const QString text = QString("# Mô hình tham số: sửa param_set rồi bấm Chạy (F5) để dựng lại và giải\n"
                                 "type = elastic\ngeometry = param %1\n# param_set = ten=gia_tri\nmesh_size = 0.01\norder = 2\n"
                                 "E = 210e9\nnu = 0.3\nfix = xmin\nload = xmax 0 0 -1000\n").arg(path);
    editor_->setPlainText(text);
    casePath_.clear();
    refreshTitle();
    runSolver();
    return true;
}

// Đường dẫn file .fcad của case hiện tại (geometry = param <file>); rỗng nếu case không dùng mô hình tham số.
static QString paramModelPath(const QString& geometryValue, const QString& casePath) {
    const QStringList t = geometryValue.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (t.size() < 2 || t[0] != "param") return QString();
    QString p = t[1];
    if (QFileInfo(p).isRelative() && !casePath.isEmpty()) p = QFileInfo(casePath).absolutePath() + "/" + p;
    return p;
}

void MainWindow::editParamModel() {
    const QString path = paramModelPath(caseValue("geometry"), casePath_);
    if (path.isEmpty()) {
        QMessageBox::information(this, "FastCAE", tr("Case hiện tại không dùng mô hình tham số. Mở file .fcad (Mở…) hoặc đặt geometry = param <file.fcad>."));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "FastCAE", tr("Không đọc được %1").arg(path)); return; }
    const std::string text = QString::fromUtf8(f.readAll()).toStdString();
    // giá trị mặc định (không ghi đè) và giá trị đang áp dụng (theo param_set của case)
    const ParamModelResult base = compileParamModel(text, {}, 0, 1);
    if (!base.ok) { QMessageBox::warning(this, "FastCAE", QString::fromStdString(base.message)); return; }
    std::map<std::string, double> ov;
    for (const QString& kv : caseValues("param_set")) {
        const int eq = kv.indexOf('=');
        double v;
        std::string e;
        if (eq > 0 && evalExpression(kv.mid(eq + 1).toStdString(), ov, v, e)) ov[kv.left(eq).trimmed().toStdString()] = v;
    }
    const ParamModelResult cur = compileParamModel(text, ov, 0, 1);
    std::vector<ParamEditDialog::Item> items;
    for (const auto& kv : base.parameters) {
        const auto it = cur.ok ? cur.parameters.find(kv.first) : base.parameters.end();
        items.push_back({QString::fromStdString(kv.first), (cur.ok && it != cur.parameters.end()) ? it->second : kv.second, kv.second});
    }
    if (items.empty()) { QMessageBox::information(this, "FastCAE", tr("Mô hình không có tham số (param).")); return; }
    ParamEditDialog dlg(this, QFileInfo(path).fileName(), items);
    if (dlg.exec() == QDialog::Accepted) applyParamOverrides(dlg.changed());
}

bool MainWindow::applyParamOverrides(const QMap<QString, double>& values) {
    QStringList out;
    for (const QString& line : editor_->toPlainText().split('\n')) {
        const QString key = line.section('#', 0, 0).section('=', 0, 0).trimmed();
        if (key.compare("param_set", Qt::CaseInsensitive) == 0) continue;   // thay toàn bộ param_set cũ
        out << line;
    }
    while (!out.isEmpty() && out.last().trimmed().isEmpty()) out.removeLast();
    QString extra;
    for (auto it = values.begin(); it != values.end(); ++it) extra += QString("param_set = %1=%2\n").arg(it.key()).arg(it.value(), 0, 'g', 12);
    editor_->setPlainText(out.join('\n') + "\n" + extra);
    refreshTitle();
    runSolver();
    return true;
}

QDialog* MainWindow::createPlotDialog() {
    if (last_.series.empty()) return nullptr;
    return new SeriesPlotDialog(this, last_.series, QString::fromStdString(last_.type));
}

void MainWindow::showSeriesPlot() {
    QDialog* d = createPlotDialog();
    if (!d) {
        QMessageBox::information(this, "FastCAE", tr("Kết quả hiện tại chưa có dãy số nào để vẽ. Mở kết quả solver ngoài kèm history.csv, hoặc mở file .fcres có dãy số."));
        return;
    }
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->show();
}

void MainWindow::clearFrames() {
    if (playBtn_ && playBtn_->isChecked()) playBtn_->setChecked(false);
    frameFields_.clear();
    frameLabels_.clear();
    rangeCache_.clear();
    if (view_) view_->setColorRangeProvider(nullptr);
    if (lockRange_) { lockRange_->blockSignals(true); lockRange_->setChecked(false); lockRange_->blockSignals(false); }
    if (framePanel_) framePanel_->hide();
}

void MainWindow::setupFrames(std::vector<VtkFields> frames, const QStringList& labels, int current) {
    frameFields_ = std::move(frames);
    frameLabels_ = labels;
    rangeCache_.clear();
    view_->setColorRangeProvider([this](const QString& name, double& lo, double& hi) {
        if (!lockRange_->isChecked() || frameFields_.empty()) return false;
        auto it = rangeCache_.find(name);
        if (it == rangeCache_.end()) {
            double a = 1e300, b = -1e300;
            for (const auto& f : frameFields_) {
                const auto s = f.pointScalars.find(name.toStdString());
                if (s == f.pointScalars.end()) continue;
                for (double v : s->second) { a = std::min(a, v); b = std::max(b, v); }
            }
            it = rangeCache_.emplace(name, std::make_pair(a, b)).first;
        }
        if (it->second.first > it->second.second) return false;
        lo = it->second.first; hi = it->second.second;
        return true;
    });
    frameSlider_->blockSignals(true);
    frameSlider_->setRange(0, int(frameFields_.size()) - 1);
    frameSlider_->setValue(std::clamp(current, 0, int(frameFields_.size()) - 1));
    frameSlider_->blockSignals(false);
    framePanel_->show();
    for (auto* d : findChildren<QDockWidget*>()) if (d->windowTitle() == tr("Kết quả")) d->raise();   // hiện bảng điều khiển bước
    setFrameIndex(frameSlider_->value());
}

void MainWindow::setFrameIndex(int i) {
    if (frameFields_.empty()) return;
    i = std::clamp(i, 0, int(frameFields_.size()) - 1);
    if (frameSlider_->value() != i) { frameSlider_->blockSignals(true); frameSlider_->setValue(i); frameSlider_->blockSignals(false); }
    last_.fields = frameFields_[std::size_t(i)];
    view_->setFields(frameFields_[std::size_t(i)]);
    applyWarp();
    frameLabel_->setText(QString("%1  (%2/%3)").arg(i < frameLabels_.size() ? frameLabels_[i] : QString()).arg(i + 1).arg(frameFields_.size()));
}

void MainWindow::setLockRange(bool on) {
    if (lockRange_->isChecked() != on) { lockRange_->blockSignals(true); lockRange_->setChecked(on); lockRange_->blockSignals(false); }
    view_->showScalar(scalarBox_->currentText());
    applyWarp();
}

// Hệ số phóng đại biến dạng: v% của 20% kích thước mô hình, chia cho biên độ chuyển vị lớn nhất.
// Biên độ lấy từ chính trường displacement_magnitude (khung đang xem; cả chuỗi nếu khóa thang màu), không phụ thuộc kết quả
// có từ lần giải hay nạp từ file (.fcres/.vtk không có maxValue).
void MainWindow::applyWarp() {
    Vec3 lo, hi;
    last_.mesh.bounds(lo, hi);
    const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-12});
    auto maxOf = [](const VtkFields& f) {
        const auto it = f.pointScalars.find("displacement_magnitude");
        double m = 0;
        if (it != f.pointScalars.end()) for (double v : it->second) m = std::max(m, v);
        return m;
    };
    double umax = 0;
    if (!frameFields_.empty() && lockRange_ && lockRange_->isChecked()) { for (const auto& f : frameFields_) umax = std::max(umax, maxOf(f)); }
    else umax = maxOf(last_.fields);
    if (umax <= 0) umax = (last_.type == "elastic" || last_.type == "frame" || last_.type == "shell") ? std::max(last_.maxValue, 1e-30) : 1;
    view_->setDeformationScale(warpSlider_->value() / 100.0 * 0.2 * size / umax);
}

}  // namespace fastcae::gui
