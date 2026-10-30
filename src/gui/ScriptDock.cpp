// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "ScriptDock.hpp"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMainWindow>
#include <QScreen>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>

#include "core/paths.hpp"
#include "script/script_engine.hpp"

namespace fastcae::gui {

namespace {

struct Example {
    const char* file;
    const char* title;
};
const Example kExamples[] = {
    {"00_xin_chao.lua", "00  Xin chào, dầm công xôn trong script"},
    {"01_tiet_dien.lua", "01  Đặc trưng tiết diện"},
    {"02_quet_tham_so_dam.lua", "02  Quét tham số chiều cao dầm"},
    {"03_toi_uu_tiet_dien.lua", "03  Tối ưu tiết diện"},
    {"04_dan_so_khoang.lua", "04  Dàn Warren theo số khoang"},
    {"05_tan_so_theo_chieu_dai.lua", "05  Tần số riêng theo chiều dài"},
    {"06_to_hop_tai_trong.lua", "06  Tổ hợp tải trọng khung cổng"},
    {"07_hoi_tu_luoi_tam.lua", "07  Hội tụ lưới tấm (cần CalculiX)"},
};

const Example kPyExamples[] = {
    {"10_solver_ngoai.py", "10  Python: gọi Code_Aster / OpenRadioss / SU2 / OpenFOAM"},
    {"11_quet_tham_so.py", "11  Python: quét tham số dầm"},
    {"14_hien_thi_ket_qua.py", "14  Python: đẩy kết quả .vtk lên khung 3D (fastcae.show)"},
};
const Example kCppExamples[] = {
    {"12_solver_ngoai.cpp", "12  C++: gọi solver ngoài (không cần main)"},
    {"13_dam_voi_main.cpp", "13  C++: dùng thư viện fastcae_core (có main)"},
};

// Tô màu cú pháp Lua / Python / C++ (từ khoá, chuỗi, chú thích, số, API fastcae)
class LuaHighlighter : public QSyntaxHighlighter {
public:
    explicit LuaHighlighter(QTextDocument* d) : QSyntaxHighlighter(d) { setDark(true); }
    void setDark(bool dark) {
        dark_ = dark;
        rehighlight();
    }
    void setLang(Language l) {
        lang_ = l;
        rehighlight();
    }

protected:
    void highlightBlock(const QString& text) override {
        QTextCharFormat kw, str, com, num, api;
        kw.setForeground(dark_ ? QColor("#79b8ff") : QColor("#0b5cad"));
        kw.setFontWeight(QFont::Bold);
        str.setForeground(dark_ ? QColor("#9ece6a") : QColor("#1a7f37"));
        com.setForeground(dark_ ? QColor("#7d8590") : QColor("#6e7781"));
        com.setFontItalic(true);
        num.setForeground(dark_ ? QColor("#ff9e64") : QColor("#b45309"));
        api.setForeground(dark_ ? QColor("#bb9af7") : QColor("#8250df"));
        static const QRegularExpression luaKw(
            "\\b(and|break|do|else|elseif|end|false|for|function|goto|if|in|local|nil|not|or|repeat|return|then|true|until|while)\\b");
        static const QRegularExpression pyKw(
            "\\b(and|as|assert|break|class|continue|def|del|elif|else|except|False|finally|for|from|global|if|import|in|is|lambda|None|nonlocal|not|or|pass|raise|return|True|try|while|with|yield)\\b");
        static const QRegularExpression cppKw(
            "\\b(auto|bool|break|case|catch|char|class|const|constexpr|continue|double|else|enum|false|float|for|if|include|int|long|namespace|new|nullptr|return|short|signed|sizeof|static|std|struct|switch|throw|true|try|typedef|unsigned|using|void|while)\\b");
        const QRegularExpression& kwRe = lang_ == Language::Python ? pyKw : lang_ == Language::Cpp ? cppKw : luaKw;
        static const QRegularExpression numRe("\\b\\d+(\\.\\d+)?([eE][+-]?\\d+)?\\b");
        static const QRegularExpression apiRe("\\b(fastcae|fc)\\b");
        static const QRegularExpression strRe("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'");
        for (auto it = numRe.globalMatch(text); it.hasNext();) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), num); }
        for (auto it = kwRe.globalMatch(text); it.hasNext();) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), kw); }
        for (auto it = apiRe.globalMatch(text); it.hasNext();) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), api); }
        for (auto it = strRe.globalMatch(text); it.hasNext();) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), str); }
        const int c = text.indexOf(lang_ == Language::Lua ? "--" : lang_ == Language::Python ? "#" : "//");
        if (c >= 0) setFormat(c, text.length() - c, com);
    }

private:
    bool dark_ = true;
    Language lang_ = Language::Lua;
};

const char* pythonApiHelp() {
    return
        "PYTHON — import fastcae (python/fastcae.py, chỉ cần thư viện chuẩn; gọi fastcae-cli)\n\n"
        "Bài toán nội tại\n"
        "  fastcae.template(name, *params) -> text     mẫu case (dầm, khung, tấm...)\n"
        "  fastcae.case(type=..., ...) -> text         dựng văn bản case từ tham số\n"
        "  fastcae.run(text) / run_file(path) -> Result   Result.values / .series / .fields / .raise_for_status()\n"
        "  fastcae.read_vtk(path) -> dict              đọc .vtk để dùng numpy/matplotlib\n\n"
        "Solver mã nguồn mở bên ngoài (chạy như tiến trình riêng, đầu ra hiện ngay)\n"
        "  fastcae.solvers()                           danh mục: id, giấy phép, đã cài chưa, đường dẫn\n"
        "  fastcae.code_aster('model.export')          Code_Aster (run_aster)\n"
        "  fastcae.openradioss('model_0000.rad')       OpenRadioss: starter rồi engine (solver_id='opencourant' cho OpenCourant)\n"
        "  fastcae.su2('case.cfg', args='-n 4')        SU2_CFD\n"
        "  fastcae.openfoam('caseDir', ['blockMesh','simpleFoam','foamToVTK'])   tự nạp etc/bashrc\n"
        "  fastcae.calculix('beam.inp')   fastcae.elmer('case.sif')\n"
        "  fastcae.run_solver(id, input, workdir, args, commands, exe)   dạng tổng quát\n"
        "  fastcae.run_program('gmsh -3 a.geo')        lệnh bất kỳ (gmsh, paraview, trình riêng...)\n"
        "  Kết quả SolverRun: .ok .exit_code .message .log .files .find('.vtk','.frd','.rmed','.csv')\n\n"
        "Gọi thư viện Python khác: import numpy, scipy, meshio, pyvista, fenics, code_aster (nếu đã cài) như bình thường — bảng này chạy\n"
        "đúng trình thông dịch Python của bạn (đặt FASTCAE_PYTHON để chọn bản khác, ví dụ môi trường conda có Code_Aster).\n";
}

const char* cppApiHelp() {
    return
        "C++ — biên dịch với FastCAE SDK (fastcae_core) bằng g++/clang++ (MinGW) hoặc cl (MSVC), rồi chạy\n\n"
        "Hai kiểu viết:\n"
        "  1) Không có main(): phần mã được bọc vào main(), đã #include sdk/fastcae_all.hpp và using namespace fastcae.\n"
        "  2) Có main(): tự #include \"sdk/fastcae_all.hpp\" và dùng bất kỳ header nào của FastCAE (app/, core/, solver/, cad/...).\n\n"
        "Hàm tiện ích (namespace fc)\n"
        "  fc::run(caseText)                        chạy bài toán nội tại, trả về RunReport (.values, .series, .mesh, .fields)\n"
        "  fc::solver(id, input, extraArgs, {lenh...}, workDir)   chạy solver ngoài: \"code_aster\", \"openradioss\", \"opencourant\",\n"
        "                                           \"su2\", \"openfoam\", \"calculix\", \"elmer\"\n"
        "  fc::shell(command, workDir)              chạy lệnh bất kỳ, đầu ra hiện ngay\n"
        "  allSolvers(), solverRuntimePath(s)       danh mục và trạng thái cài\n"
        "  runExternalSolver(ExternalRunOptions)    dạng đầy đủ (onOutput, cancel, argsOverride...)\n\n"
        "Gọi thư viện C++ mã nguồn mở khác (MFEM, deal.II, PETSc, Eigen...): thêm #include rồi đặt cờ trình biên dịch qua biến môi trường\n"
        "FASTCAE_CXX_FLAGS (ví dụ -I... -L... -lmfem), hoặc dùng fc::shell để chạy CMake dự án riêng.\n"
        "Cần: trình biên dịch C++20 (WinLibs MinGW-w64 hoặc MSVC) và SDK (cmake --install build --component sdk, hoặc biến FASTCAE_SDK).\n";
}

QString readResource(const QString& name) {
    QFile f(":/scripts/" + name);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

}  // namespace

ScriptDock::ScriptDock(QWidget* parent) : QDockWidget(tr("Lập trình"), parent) {
    setObjectName("scriptDock");
    auto* body = new QWidget;
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QToolBar;
    langCombo_ = new QComboBox;
    langCombo_->addItems({"Lua", "Python", "C++"});
    langCombo_->setToolTip(tr("Ngôn ngữ lập trình: Lua (nhúng sẵn), Python (cần Python trên máy), C++ (cần g++/MinGW hoặc MSVC)"));
    bar->addWidget(langCombo_);
    connect(langCombo_, &QComboBox::activated, this, [this](int i) { setLanguage(static_cast<Language>(i)); });
    bar->addSeparator();
    actRun_ = bar->addAction(tr("▶  Chạy (F6)"), this, &ScriptDock::run);
    actRun_->setShortcut(QKeySequence(Qt::Key_F6));
    actStop_ = bar->addAction(tr("■  Dừng"), this, &ScriptDock::stop);
    actStop_->setEnabled(false);
    bar->addSeparator();
    bar->addAction(tr("Mở..."), this, &ScriptDock::openFile);
    bar->addAction(tr("Lưu..."), this, &ScriptDock::saveFile);
    bar->addSeparator();
    auto* combo = new QComboBox;
    combo->addItem(tr("Ví dụ..."));
    for (int l = 0; l < 3; ++l)
        for (const auto& n : exampleNames(static_cast<Language>(l))) combo->addItem(n, l);
    bar->addWidget(combo);
    connect(combo, &QComboBox::activated, this, [this, combo](int i) {
        if (i > 0) {
            const Language l = static_cast<Language>(combo->itemData(i).toInt());
            int idx = 0;
            for (int k = 1; k < i; ++k) if (combo->itemData(k).toInt() == static_cast<int>(l)) ++idx;
            loadExample(l, idx);
        }
        combo->setCurrentIndex(0);
    });
    bar->addSeparator();
    bar->addAction(tr("API"), this, &ScriptDock::showHelp);
    bar->addSeparator();
    actMax_ = bar->addAction(tr("⤢  Phóng to (F7)"), this, &ScriptDock::toggleMaximize);
    actMax_->setShortcut(QKeySequence(Qt::Key_F7));
    actMax_->setToolTip(tr("Phóng to bảng lập trình để soạn cho rộng; bấm lại để thu nhỏ. Cũng có thể kéo mép trên của bảng, "
                           "kéo thanh chia giữa trình soạn và đầu ra, hoặc bấm nút nổi ở góc phải thanh tiêu đề rồi kéo giãn cửa sổ."));
    bar->addAction(tr("A+"), this, [this] { zoomText(+1); })->setToolTip(tr("Tăng cỡ chữ"));
    bar->addAction(tr("A−"), this, [this] { zoomText(-1); })->setToolTip(tr("Giảm cỡ chữ"));
    lay->addWidget(bar);

    auto* split = new QSplitter(Qt::Vertical);
    editor_ = new QPlainTextEdit;
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    editor_->setFont(mono);
    editor_->setTabStopDistance(2 * editor_->fontMetrics().horizontalAdvance(' ') * 2);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    highlighter_ = new LuaHighlighter(editor_->document());
    console_ = new QPlainTextEdit;
    console_->setReadOnly(true);
    console_->setFont(mono);
    split->addWidget(editor_);
    split->addWidget(console_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);
    setWidget(body);

    editor_->setPlainText(readResource("00_xin_chao.lua"));
    console_->setPlainText(tr("Bảng lập trình: chọn Lua, Python hoặc C++ ở góc trái. Bấm Chạy (F6). Xem nút API để biết hàm gọi Code_Aster, OpenRadioss, SU2, OpenFOAM, CalculiX...\n"
                              "Thư mục làm việc: thư mục chứa script (hoặc Tài liệu/FastCAE).\n"));
}

void ScriptDock::zoomText(int delta) {
    for (QPlainTextEdit* e : {editor_, console_}) {
        if (delta > 0) e->zoomIn(delta); else e->zoomOut(-delta);
    }
}

void ScriptDock::toggleMaximize() {
    auto* mw = qobject_cast<QMainWindow*>(parentWidget());
    if (!maximized_) {
        savedFloating_ = isFloating();
        savedGeom_ = geometry();
        savedHeight_ = height();
        if (isFloating()) {
            const QScreen* sc = screen() ? screen() : QGuiApplication::primaryScreen();
            setGeometry(sc->availableGeometry().adjusted(40, 40, -40, -40));
        } else if (mw) {
            mw->resizeDocks({this}, {static_cast<int>(mw->height() * 0.85)}, Qt::Vertical);
        }
        maximized_ = true;
        actMax_->setText(tr("⤡  Thu nhỏ (F7)"));
    } else {
        if (isFloating()) {
            if (savedFloating_) setGeometry(savedGeom_); else resize(900, 520);
        } else if (mw) {
            mw->resizeDocks({this}, {savedHeight_ > 0 ? savedHeight_ : 260}, Qt::Vertical);
        }
        maximized_ = false;
        actMax_->setText(tr("⤢  Phóng to (F7)"));
    }
}

ScriptDock::~ScriptDock() {
    if (proc_) {
        proc_->kill();
        proc_->waitForFinished(2000);
    }
    if (thread_) {
        cancel_ = true;
        thread_->wait(5000);
    }
}

void ScriptDock::setDarkTheme(bool dark) { static_cast<LuaHighlighter*>(highlighter_)->setDark(dark); }

namespace {
template <std::size_t N>
QStringList titles(const Example (&a)[N]) {
    QStringList l;
    for (const auto& e : a) l << QString::fromUtf8(e.title);
    return l;
}
}  // namespace

QStringList ScriptDock::exampleNames() { return titles(kExamples); }

QStringList ScriptDock::exampleNames(Language lang) {
    return lang == Language::Python ? titles(kPyExamples) : lang == Language::Cpp ? titles(kCppExamples) : titles(kExamples);
}

void ScriptDock::loadExample(int index) { loadExample(Language::Lua, index); }

void ScriptDock::loadExample(Language lang, int index) {
    const Example* ex = lang == Language::Python ? kPyExamples : lang == Language::Cpp ? kCppExamples : kExamples;
    const int n = lang == Language::Python ? int(sizeof(kPyExamples) / sizeof(Example))
                  : lang == Language::Cpp  ? int(sizeof(kCppExamples) / sizeof(Example))
                                           : int(sizeof(kExamples) / sizeof(Example));
    if (index < 0 || index >= n) return;
    setLanguage(lang);
    editor_->setPlainText(readResource(ex[index].file));
    path_.clear();
    console_->appendPlainText(tr("— Đã nạp ví dụ: %1").arg(QString::fromUtf8(ex[index].title)));
    show();
    raise();
}

void ScriptDock::setLanguage(Language lang) {
    if (lang == lang_ && langCombo_->currentIndex() == static_cast<int>(lang)) return;
    if (running_) return;
    texts_[static_cast<int>(lang_)] = editor_->toPlainText();
    paths_[static_cast<int>(lang_)] = path_;
    lang_ = lang;
    langCombo_->setCurrentIndex(static_cast<int>(lang));
    static_cast<LuaHighlighter*>(highlighter_)->setLang(lang);
    const int i = static_cast<int>(lang);
    if (!texts_[i].isEmpty()) editor_->setPlainText(texts_[i]);
    else if (lang == Language::Python) editor_->setPlainText(readResource(kPyExamples[0].file));
    else if (lang == Language::Cpp) editor_->setPlainText(readResource(kCppExamples[0].file));
    else editor_->setPlainText(readResource(kExamples[0].file));
    path_ = paths_[i];
}

bool ScriptDock::loadFile(const QString& p) {
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString ext = QFileInfo(p).suffix().toLower();
    if (ext == "py") setLanguage(Language::Python);
    else if (ext == "cpp" || ext == "cc" || ext == "cxx") setLanguage(Language::Cpp);
    else if (ext == "lua") setLanguage(Language::Lua);
    editor_->setPlainText(QString::fromUtf8(f.readAll()));
    path_ = p;
    return true;
}

void ScriptDock::append(const QString& s) {
    console_->moveCursor(QTextCursor::End);
    console_->insertPlainText(s);
    console_->moveCursor(QTextCursor::End);
}

void ScriptDock::consumeOutput(const QByteArray& chunk, bool flush) {
    static const QByteArray kMarker = "@@FASTCAE_SHOW@@";
    lineBuf_ += chunk;
    int nl;
    while ((nl = lineBuf_.indexOf('\n')) >= 0) {
        QByteArray line = lineBuf_.left(nl);
        lineBuf_.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith(kMarker)) {
            const std::string path = QString::fromUtf8(line.mid(kMarker.size())).trimmed().toStdString();
            if (showFile_) showFile_(path);
            else append(tr("(fastcae.show: chưa có trình hiển thị)\n"));
            append(tr("▣ Hiển thị trên khung 3D: %1\n").arg(QString::fromStdString(path)));
        } else {
            append(QString::fromUtf8(line) + "\n");
        }
    }
    if (flush && !lineBuf_.isEmpty()) {
        append(QString::fromUtf8(lineBuf_));
        lineBuf_.clear();
    }
}

void ScriptDock::setRunning(bool on) {
    running_ = on;
    actRun_->setEnabled(!on);
    actStop_->setEnabled(on);
}

void ScriptDock::run() {
    if (running_) return;
    if (lang_ == Language::Lua) runLua();
    else runProcess();
}

void ScriptDock::runLua() {
    const QString code = editor_->toPlainText();
    const QString name = path_.isEmpty() ? QStringLiteral("script") : QFileInfo(path_).fileName();
    // thư mục làm việc: nơi chứa script, nếu chưa lưu thì Tài liệu/FastCAE (để các file CSV/VTK có chỗ ghi quen thuộc)
    QString dir = path_.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/FastCAE" : QFileInfo(path_).absolutePath();
    QDir().mkpath(dir);
    QDir::setCurrent(dir);
    console_->appendPlainText(tr("▶ %1   (thư mục làm việc: %2)").arg(name, dir));
    cancel_ = false;
    setRunning(true);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    thread_ = QThread::create([this, code, name, timer] {
        ScriptEngine eng;
        eng.setCancelFlag(&cancel_);
        eng.setPrinter([this](const std::string& s) {
            const QString q = QString::fromStdString(s);
            QMetaObject::invokeMethod(this, [this, q] { append(q); }, Qt::QueuedConnection);
        });
        eng.setShowHandler([this](const RunReport& r) {
            QMetaObject::invokeMethod(this, [this, r] { if (show_) show_(r); }, Qt::QueuedConnection);
        });
        eng.setOpenCaseHandler([this](const std::string& t) {
            QMetaObject::invokeMethod(this, [this, t] { if (openCase_) openCase_(t); }, Qt::QueuedConnection);
        });
        const ScriptOutcome out = eng.runString(code.toStdString(), name.toStdString());
        const QString msg = out.ok ? tr("✓ Hoàn tất (%1 s)").arg(timer->elapsed() / 1000.0, 0, 'f', 2)
                                   : tr("✗ Lỗi: %1").arg(QString::fromStdString(out.error));
        QMetaObject::invokeMethod(this, [this, msg] { append("\n" + msg + "\n"); }, Qt::QueuedConnection);
    });
    connect(thread_, &QThread::finished, this, [this] {
        thread_->deleteLater();
        thread_ = nullptr;
        setRunning(false);
    });
    thread_->start();
}

void ScriptDock::stop() {
    if (proc_) {
        proc_->kill();
        append(tr("\n■ Đã dừng.\n"));
        return;
    }
    cancel_ = true;
    append(tr("\n■ Đã yêu cầu dừng (một lần giải đang chạy sẽ dừng ngay sau khi xong)...\n"));
}

void ScriptDock::openFile() {
    const QString p = QFileDialog::getOpenFileName(this, tr("Mở script"), path_.isEmpty() ? QString::fromStdString(fastcae::userDocumentsDir()) : path_,
                                                   tr("Script (*.lua *.py *.cpp *.cc *.cxx);;Lua (*.lua);;Python (*.py);;C++ (*.cpp *.cc *.cxx);;Tất cả (*)"));
    if (p.isEmpty()) return;
    if (!loadFile(p)) QMessageBox::warning(this, tr("Mở script"), tr("Không đọc được file."));
}

void ScriptDock::saveFile() {
    QString p = path_;
    if (p.isEmpty()) {
        const char* name = lang_ == Language::Python ? "script.py" : lang_ == Language::Cpp ? "script.cpp" : "script.lua";
        const QString filter = lang_ == Language::Python ? tr("Script Python (*.py)") : lang_ == Language::Cpp ? tr("Script C++ (*.cpp)") : tr("Script Lua (*.lua)");
        p = QFileDialog::getSaveFileName(this, tr("Lưu script"), QString::fromStdString(fastcae::userDocumentsDir()) + "/" + name, filter);
    }
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Lưu script"), tr("Không ghi được file. Hãy chọn thư mục khác (ví dụ Documents)."));
        return;
    }
    f.write(editor_->toPlainText().toUtf8());
    path_ = p;
    append(tr("Đã lưu: %1\n").arg(p));
}

namespace {

QString appDir() { return QCoreApplication::applicationDirPath(); }

QString exeName(const QString& base) {
#ifdef _WIN32
    return base + ".exe";
#else
    return base;
#endif
}

QString findCliExe() {
    const QString env = qEnvironmentVariable("FASTCAE_CLI");
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    for (const QString& d : {appDir(), appDir() + "/..", appDir() + "/../bin", appDir() + "/../.."}) {
        const QString c = QDir::cleanPath(d + "/" + exeName("fastcae-cli"));
        if (QFileInfo::exists(c)) return c;
    }
    return QStandardPaths::findExecutable("fastcae-cli");
}

QString findPythonDir() {
    for (const QString& d : {appDir() + "/python", appDir() + "/../python", appDir() + "/../share/doc/fastcae/python",
                             appDir() + "/../../python", appDir() + "/../../../python"
#ifdef FASTCAE_DEV_PYTHON
                             , QString(FASTCAE_DEV_PYTHON)
#endif
         }) {
        if (QFileInfo::exists(d + "/fastcae.py")) return QDir::cleanPath(d);
    }
    return {};
}

// Python chạy được thật? (loại "python.exe"/"python3.exe" giả của Microsoft Store: nó thoát với mã 9009 khi chưa cài Python)
bool pythonRuns(const QString& exe) {
    QProcess p;
    p.start(exe, {"-c", "import sys; sys.exit(0)"});
    if (!p.waitForStarted(3000)) return false;
    if (!p.waitForFinished(10000)) { p.kill(); p.waitForFinished(1000); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QString findPythonExe() {
    const QString env = qEnvironmentVariable("FASTCAE_PYTHON");
    if (!env.isEmpty()) return env;
    for (const QString& d : {appDir() + "/python/" + exeName("python"), appDir() + "/python-embed/" + exeName("python")})
        if (QFileInfo::exists(d)) return d;
    QStringList cands;
    for (const QString& dir : qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts))
        for (const char* n : {"python", "python3", "py"}) {
            const QString p = QDir(dir).filePath(exeName(n));
            if (QFileInfo::exists(p)) cands << p;
        }
#ifdef Q_OS_WIN
    // Bản cài python.org cho riêng người dùng nhưng chưa tick "Add to PATH"
    QDir pd(qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Python");
    for (const QString& e : pd.entryList({"Python3*"}, QDir::Dirs, QDir::Name | QDir::Reversed))
        cands << pd.filePath(e + "/python.exe");
#endif
    for (const QString& c : cands)
        if (QFileInfo::exists(c) && pythonRuns(c)) return c;
    return {};
}

}  // namespace

void ScriptDock::runProcess() {
    const bool py = lang_ == Language::Python;
    const QString cli = findCliExe();
    if (cli.isEmpty()) {
        append(tr("✗ Không tìm thấy fastcae-cli cạnh chương trình (cần cho Python và C++). Đặt biến môi trường FASTCAE_CLI.\n"));
        return;
    }
    QString program;
    QStringList args;
    if (py) {
        program = findPythonExe();
        if (program.isEmpty()) {
            append(tr("✗ Không tìm thấy Python. Cài Python 3 từ python.org (chọn \"Add to PATH\"; \"python.exe\" của Microsoft Store chỉ là lối tắt, chưa phải Python), rồi pip install numpy scipy; hoặc đặt biến FASTCAE_PYTHON trỏ tới python.exe thật.\n"));
            return;
        }
    } else {
        program = cli;
    }

    const QString dir = path_.isEmpty() ? QString::fromStdString(fastcae::userDocumentsDir()) : QFileInfo(path_).absolutePath();
    QDir().mkpath(dir);
    const QString file = path_.isEmpty() ? dir + (py ? "/script_tmp.py" : "/script_tmp.cpp") : path_;
    {
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            append(tr("✗ Không ghi được %1 (hãy bấm Lưu và chọn thư mục như Documents)\n").arg(file));
            return;
        }
        f.write(editor_->toPlainText().toUtf8());
    }
    if (py) args << "-u" << file;
    else args << "cxx" << file;

    proc_ = new QProcess(this);
    proc_->setWorkingDirectory(dir);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("FASTCAE_CLI", cli);
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    const QString pyDir = findPythonDir();
    if (!pyDir.isEmpty()) {
        const QString old = env.value("PYTHONPATH");
        env.insert("PYTHONPATH", old.isEmpty() ? pyDir : pyDir + QDir::listSeparator() + old);
    }
    proc_->setProcessEnvironment(env);
    lineBuf_.clear();
    connect(proc_, &QProcess::readyRead, this, [this] { consumeOutput(proc_->readAll()); });
    connect(proc_, &QProcess::errorOccurred, this, [this, py](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            append(py ? tr("✗ Không chạy được Python.\n") : tr("✗ Không chạy được fastcae-cli.\n"));
    });
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    connect(proc_, &QProcess::finished, this, [this, timer](int code, QProcess::ExitStatus st) {
        consumeOutput(proc_->readAll(), true);
        append(code == 0 && st == QProcess::NormalExit ? tr("\n✓ Hoàn tất (%1 s)\n").arg(timer->elapsed() / 1000.0, 0, 'f', 2)
                                                        : tr("\n✗ Kết thúc với mã %1\n").arg(code));
        proc_->deleteLater();
        proc_ = nullptr;
        setRunning(false);
    });
    console_->appendPlainText(tr("▶ %1 %2   (thư mục: %3)").arg(QFileInfo(program).fileName(), args.join(' '), dir));
    if (!py) console_->appendPlainText(tr("  (biên dịch với FastCAE SDK, lần đầu có thể mất vài chục giây)"));
    append("\n");
    setRunning(true);
    proc_->start(program, args);
}

void ScriptDock::showHelp() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Hàm lập trình (Lua / Python / C++)"));
    dlg.resize(900, 640);
    auto* lay = new QVBoxLayout(&dlg);
    QString help = QString::fromUtf8(scriptApiHelp());
    if (lang_ == Language::Python) help = QString::fromUtf8(pythonApiHelp());
    else if (lang_ == Language::Cpp) help = QString::fromUtf8(cppApiHelp());
    auto* t = new QPlainTextEdit(help);
    t->setReadOnly(true);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    t->setFont(mono);
    lay->addWidget(t);
    auto* ok = new QPushButton(tr("Đóng"));
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(ok);
    dlg.exec();
}

}  // namespace fastcae::gui
