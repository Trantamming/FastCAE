// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDockWidget>
#include <QRect>
#include <atomic>
#include <functional>

#include "app/case_runner.hpp"

class QAction;
class QComboBox;
class QProcess;
class QPlainTextEdit;
class QSyntaxHighlighter;
class QThread;

namespace fastcae::gui {

// Ngôn ngữ của bảng lập trình: Lua nhúng sẵn; Python chạy bằng trình thông dịch Python trên máy (module fastcae);
// C++ được biên dịch với FastCAE SDK bằng g++/clang++/cl rồi chạy.
enum class Language { Lua, Python, Cpp };

// Bảng lập trình Lua / Python / C++: soạn script, chạy nền, xem đầu ra, mở/lưu, ví dụ và trợ giúp API.
class ScriptDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ScriptDock(QWidget* parent = nullptr);
    ~ScriptDock() override;

    // fastcae.show(res) và fastcae.open_case(text): được gọi trên luồng giao diện.
    void setShowHandler(std::function<void(const RunReport&)> h) { show_ = std::move(h); }
    void setOpenCaseHandler(std::function<void(const std::string&)> h) { openCase_ = std::move(h); }
    // Python: fastcae.show("ket_qua.vtk") in dòng "@@FASTCAE_SHOW@@<đường dẫn>"; bảng tách dòng đó ra và gọi handler này.
    void setShowFileHandler(std::function<void(const std::string&)> h) { showFile_ = std::move(h); }
    void setDarkTheme(bool dark);
    bool running() const { return running_; }

    static QStringList exampleNames();      // tên hiển thị các script Lua mẫu nhúng sẵn
    static QStringList exampleNames(Language lang);
    void loadExample(int index);            // ví dụ Lua
    void loadExample(Language lang, int index);
    Language language() const { return lang_; }
    void setLanguage(Language lang);
    bool loadFile(const QString& path);   // nạp script từ file (không chạy)

public slots:
    void run();
    void stop();
    void openFile();
    void saveFile();
    void showHelp();
    void toggleMaximize();   // phóng to bảng (chiếm ~85% cửa sổ) / khôi phục kích thước cũ (F7)
    void zoomText(int delta);   // đổi cỡ chữ soạn thảo và đầu ra (Ctrl+lăn chuột cũng được)

private:
    void append(const QString& s);
    void consumeOutput(const QByteArray& chunk, bool flush = false);   // tách dòng lệnh đặc biệt khỏi đầu ra tiến trình
    void setRunning(bool on);
    void runLua();
    void runProcess();   // Python và C++: chạy qua tiến trình con, đầu ra hiện trong bảng

    QPlainTextEdit* editor_ = nullptr;
    QPlainTextEdit* console_ = nullptr;
    QSyntaxHighlighter* highlighter_ = nullptr;
    QAction *actRun_ = nullptr, *actStop_ = nullptr, *actMax_ = nullptr;
    bool maximized_ = false, savedFloating_ = false;
    int savedHeight_ = 0;
    QRect savedGeom_;
    QThread* thread_ = nullptr;
    QProcess* proc_ = nullptr;
    QComboBox* langCombo_ = nullptr;
    Language lang_ = Language::Lua;
    QString texts_[3];
    QString paths_[3];
    std::atomic<bool> cancel_{false};
    bool running_ = false;
    QString path_;
    std::function<void(const RunReport&)> show_;
    std::function<void(const std::string&)> openCase_;
    std::function<void(const std::string&)> showFile_;
    QByteArray lineBuf_;
};

}  // namespace fastcae::gui
