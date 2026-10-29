// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// FastCAE graphical front end.
//
//   fastcae                         open the application (starts with the cantilever demo)
//   fastcae model.fcae              open and solve a case file
//   fastcae --demo cavity           start with a built-in example
//   fastcae --screenshot out.png    render the window to a PNG and exit (headless checks / docs)
//           [--theme light|dark] [--tab 0..9] [--script file.lua] [--scalar name] [--warp 0..100] [--edges] [--pick] [--view 0..3] [--size 1400x850] [--click x,y]
#include <QApplication>
#include <QFileInfo>
#include <cstdio>
#include <QIcon>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTimer>

#include "AboutDialog.hpp"
#include "ComponentLibraryDialog.hpp"
#include "MainWindow.hpp"
#include "ParamEditDialog.hpp"
#include "SolverManagerDialog.hpp"
#include "Theme.hpp"
#include "core/about.hpp"

int main(int argc, char** argv) {
    // OpenGL 2.1 is enough for the viewport (GLSL 1.20), so it runs on old drivers,
    // remote desktops (via the bundled software renderer) and virtual machines.
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    fmt.setVersion(2, 1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    // Máy không tạo được ngữ cảnh OpenGL (driver thiếu, máy ảo, Remote Desktop...): báo rõ thay vì để cửa sổ 3D sập.
    {
        QOpenGLContext probe;
        probe.setFormat(QSurfaceFormat::defaultFormat());
        if (!probe.create()) {
            const QString msg = QStringLiteral(
                "Không tạo được ngữ cảnh OpenGL (cần OpenGL 2.1 trở lên).\n"
                "Hãy cập nhật driver đồ họa; trên máy ảo/Remote Desktop đặt biến môi trường QT_OPENGL=software "
                "(Windows) hoặc LIBGL_ALWAYS_SOFTWARE=1 (Linux) rồi chạy lại.\n"
                "Có thể dùng bản dòng lệnh fastcae-cli không cần đồ họa.");
            std::fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
            if (QGuiApplication::platformName() != QLatin1String("offscreen") && QGuiApplication::platformName() != QLatin1String("minimal"))
                QMessageBox::critical(nullptr, QStringLiteral("FastCAE"), msg);
            return 3;
        }
    }
    QApplication::setApplicationName("FastCAE");
    QApplication::setOrganizationName(QString::fromUtf8(fastcae::about::kAuthor));
    QApplication::setApplicationVersion(FASTCAE_VERSION);
    app.setWindowIcon(QIcon(":/fastcae.png"));
    fastcae::gui::SolverManagerDialog::applySavedPaths();   // đường dẫn solver đã đặt ở Quản lý solver có hiệu lực cho mọi bài toán (kể cả tấm/vỏ)

    QString frameArg; bool lockArg = false; QString setParamArg, caseFile, demo, shot, scalar, sizeArg, clickArg, theme, dialogArg, applyArg, scriptArg;
    int warp = -1, view = -1, tab = -1;
    bool edges = false, pick = false;
    const QStringList args = QApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&]() { return i + 1 < args.size() ? args[++i] : QString(); };
        if (a == "--demo") demo = next();
        else if (a == "--screenshot") shot = next();
        else if (a == "--scalar") scalar = next();
        else if (a == "--warp") warp = next().toInt();
        else if (a == "--view") view = next().toInt();
        else if (a == "--size") sizeArg = next();
        else if (a == "--click") clickArg = next();
        else if (a == "--theme") theme = next();
        else if (a == "--dialog") dialogArg = next();
        else if (a == "--tab") tab = next().toInt();
        else if (a == "--edges") edges = true;
        else if (a == "--pick") pick = true;
        else if (a == "--apply") applyArg = next();
        else if (a == "--frame") frameArg = next();
        else if (a == "--lock-range") lockArg = true;
        else if (a == "--set-param") setParamArg = next();   // "H=0.02,T=0.015": ghi param_set rồi giải lại (kiểm tra tự động)
        else if (a == "--script") scriptArg = next();
        else if (!a.startsWith("--")) caseFile = a;
    }

    fastcae::gui::MainWindow w;
    if (theme == "light") w.setTheme(false);
    else if (theme == "dark") w.setTheme(true);
    if (tab >= 0) w.showRibbonTab(tab);
    if (!sizeArg.isEmpty()) {
        const auto wh = sizeArg.split('x');
        if (wh.size() == 2) w.resize(wh[0].toInt(), wh[1].toInt());
    }
    if (!caseFile.isEmpty()) {
        const QString ext = QFileInfo(caseFile).suffix().toLower();
        if (ext == "fcae" || ext == "txt") w.loadCaseFile(caseFile); else { w.waitForRun(); w.openAnyFile(caseFile); }
    }
    else if (!demo.isEmpty()) w.loadDemo(demo);
    w.waitForRun();   // lần giải khởi động/chạy nền: chờ xong trước (tránh ghi đè kết quả của script)
    if (!scriptArg.isEmpty()) {
        w.runScriptFile(scriptArg);
        w.waitForScript();
    }
    if (!setParamArg.isEmpty()) {
        QMap<QString, double> pv;
        for (const QString& kv : setParamArg.split(',', Qt::SkipEmptyParts)) { const auto p = kv.split('='); if (p.size() == 2) pv[p[0]] = p[1].toDouble(); }
        w.applyParamOverrides(pv);
    }
    if (lockArg) w.setLockRange(true);
    if (!frameArg.isEmpty()) w.setFrameIndex(frameArg.toInt());
    w.waitForRun();   // lần giải chạy nền: chờ xong trước khi chọn trường, chụp ảnh
    if (!scalar.isEmpty()) w.selectScalar(scalar);
    if (warp >= 0) w.setWarpPercent(warp);
    if (edges) w.setEdges(true);
    if (pick) w.setPickMode(true);
    if (view >= 0) w.setView(view);
    w.show();
    if (!clickArg.isEmpty()) {
        const auto xy = clickArg.split(',');
        if (xy.size() == 2) QTimer::singleShot(500, [&w, xy]() { w.clickViewport(QPoint(xy[0].toInt(), xy[1].toInt())); });
    }

    // --dialog solvers|about|license|notices: chụp hộp thoại thay vì cửa sổ chính (kiểm tra tự động)
    QWidget* dlg = nullptr;
    if (dialogArg == "solvers") dlg = new fastcae::gui::SolverManagerDialog(&w, dialogArg == "solvers" ? "calculix" : "");
    else if (dialogArg == "library") dlg = new fastcae::gui::ComponentLibraryDialog(&w, "khung 1D");
    else if (dialogArg == "param") dlg = new fastcae::gui::ParamEditDialog(&w, "bracket.fcad", {{"D", 0.016, 0.016}, {"E", 0.025, 0.025}, {"L", 0.2, 0.2}, {"T", 0.02, 0.012}, {"W", 0.08, 0.08}});
    else if (dialogArg == "plot") dlg = w.createPlotDialog();
    else if (dialogArg == "about") dlg = new fastcae::gui::AboutDialog(&w, 0);
    else if (dialogArg == "license") dlg = new fastcae::gui::AboutDialog(&w, 1);
    else if (dialogArg == "thirdparty") dlg = new fastcae::gui::AboutDialog(&w, 2);
    if (dlg) dlg->show();

    if (!applyArg.isEmpty())   // --apply "load 0 0 -1000": gán lên mặt vừa chọn rồi in nội dung case
        QTimer::singleShot(900, [&w, applyArg]() {
            const bool ok = w.applyPickedSpec(applyArg);
            fprintf(stdout, "APPLY %s\n%s\n", ok ? "OK" : "NO-SELECTION", qPrintable(w.caseText()));
            fflush(stdout);
        });

    if (!shot.isEmpty()) {
        QTimer::singleShot(applyArg.isEmpty() ? 900 : 2200, [&]() {
            const bool ok = (dlg ? dlg->grab() : w.grab()).save(shot);
            app.exit(ok ? 0 : 3);
        });
    }
    return app.exec();
}
