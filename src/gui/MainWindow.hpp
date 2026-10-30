// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <vector>
#include <map>
#include <QMap>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPoint>
#include <QStringList>

#include "app/case_runner.hpp"

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QProcess;
class QSlider;
class QTabWidget;
class QPushButton;
class QTimer;
class QTreeWidget;

namespace fastcae::gui {

class HelpDialog;
class Ribbon;
class ScriptDock;
class Viewport;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    // Dùng bởi dòng lệnh (fastcae file.fcae, --demo, --screenshot, --theme).
    bool loadCaseFile(const QString& path);
    bool showResultFile(const QString& path);
    void setFrameIndex(int i);                 // chọn bước thời gian (dòng lệnh --frame, kiểm tra)
    void setLockRange(bool on);                // khóa thang màu theo toàn chuỗi
    int frameCount() const { return int(frameFields_.size()); }
    QDialog* createPlotDialog();               // (dùng cho --dialog plot và kiểm tra)
    bool applyParamOverrides(const QMap<QString, double>& values);   // ghi param_set vào case rồi dựng lại + giải (dòng lệnh --set-param)
    bool openAnyFile(const QString& path);      // mở theo đuôi: case, CAD/lưới, kết quả .vtk, script Lua/Python/C++   // mở kết quả .vtk (OpenFOAM/SU2/Code_Aster/...) lên khung 3D
    void loadDemo(const QString& name) { newFromDemo(name); }
    void selectScalar(const QString& name);
    void setWarpPercent(int v);
    void setEdges(bool on);
    void setView(int which);
    void setTheme(bool dark);
    void setPickMode(bool on);
    bool runScriptFile(const QString& path);   // mở bảng lập trình, nạp script và chạy (dòng lệnh --script)
    void waitForScript();
    void waitForRun();   // chặn (xử lý sự kiện) cho tới khi lần giải đang chạy nền kết thúc
    // Gán điều kiện lên mặt đang chọn không qua hộp thoại, ví dụ "fix", "load 0 0 -1000", "temp 100" (dùng cho kiểm tra tự động).
    bool applyPickedSpec(const QString& spec);
    QString caseText() const;
    bool isDark() const { return dark_; }
    void showRibbonTab(int index);
    void clickViewport(QPoint viewportPos);   // mô phỏng một cú bấm chuột trái (chụp ảnh tự động)

signals:
    void runFinished();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void newFromDemo(const QString& name);
    void openCase();
    void openResultDialog();
    void saveCase();
    void importCad();
    void importCadFile(const QString& path);   // CAD/lưới (xem cad_import): hỏi đơn vị + cỡ lưới rồi chia lưới và giải
    void designParametric();
    void runSolver();                       // chạy nền (luồng riêng); kết quả hiện khi xong
    void exportVtk();
    void showSeriesPlot();                 // đồ thị các dãy số của kết quả hiện tại (hội tụ, lực, thời gian)
    void editParamModel();                 // hộp thoại sửa tham số của geometry = param ...
    void saveResultFile();                  // lưu trọn kết quả (lưới + trường + đại lượng) ra .fcres
    bool showStoredResult(const QString& path);   // mở .fcres đã lưu, không cần giải lại
    bool openParamModel(const QString& path);     // mở mô hình tham số .fcad: tạo case geometry = param ...
    void about(int tab = 0);
    void showCapability();                       // Năng lực tính toán CAE (cửa sổ riêng, chỉ một chương)
    void showHelp(const QString& anchor = {});   // Hướng dẫn sử dụng (F1); anchor = chương cần mở, ví dụ "nang-luc"
    void scalarChanged(const QString& name);

private:
    struct Job {
        QString program;
        QStringList args;
        QString workDir;
        QString label;
    };

    QAction* makeAction(const QString& text, const QString& icon, const QString& tip = {});
    void createActions();
    void buildRibbon();
    void buildMenus();
    void buildDocks();
    void log(const QString& s);
    void finishRun(const RunReport& rep, const QString& logText);
    void refreshTree();
    void refreshTitle();
    void applyIcons();
    QAction* act(const char* id) const { return acts_.value(QString::fromLatin1(id)); }

    // case-file helpers
    QString caseValue(const QString& key) const;
    void setCaseKey(const QString& key, const QString& value);
    void addCaseLine(const QString& line);
    bool ensureType(const QString& type, const QString& demo);   // true nếu đã nạp ví dụ (và đã chạy)

    // solvers
    void openLibrary(const QString& category = {});
    QStringList caseValues(const QString& key) const;   // giá trị của mọi dòng `key = ...`
    bool requireFrame();                                // báo nếu bài toán hiện tại không phải khung/dàn
    void frameSetAnalysis(const QString& kind);
    void frameChooseMode();
    void frameAddNode();
    void frameAddSection();
    void frameAddMember();
    void frameAddSupport();
    void frameAddNodeLoad();
    void frameAddUdl();
    void runInternal();
    void runCalculix();
    void addCondition(int kind);
    QString pickedFaceName(QString* planeLine) const;   // tên tập nút của mặt đang chọn (dùng lại nếu đã có)
    void editMaterial();
    void setElementOrder(int order);
    void meshInfo();
    void openSolverManager(const QString& selectId = {});
    void runOpenFoamCavity();
    void runExternal(const QString& solverId, const QString& inputFile);
    void startJobs(const QList<Job>& jobs);
    void nextJob();
    void stopJobs();
    void saveScreenshot();

    Viewport* view_ = nullptr;
    Ribbon* ribbon_ = nullptr;
    ScriptDock* scriptDock_ = nullptr;
    HelpDialog* helpDlg_ = nullptr;
    HelpDialog* capabilityDlg_ = nullptr;   // chỉ chương "Năng lực tính toán CAE"
    QPlainTextEdit* editor_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QPlainTextEdit* extLog_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QComboBox* scalarBox_ = nullptr;
    QSlider* warpSlider_ = nullptr;
    // chuỗi bước thời gian (kết quả solver ngoài nhiều file / .fcres có khung)
    std::vector<VtkFields> frameFields_;
    QStringList frameLabels_;
    QWidget* framePanel_ = nullptr;
    QSlider* frameSlider_ = nullptr;
    QLabel* frameLabel_ = nullptr;
    QPushButton* playBtn_ = nullptr;
    QTimer* playTimer_ = nullptr;
    QCheckBox* lockRange_ = nullptr;
    std::map<QString, std::pair<double, double>> rangeCache_;
    void setupFrames(std::vector<VtkFields> frames, const QStringList& labels, int current);
    void clearFrames();
    void applyWarp();
    QCheckBox* edges_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* credit_ = nullptr;

    QHash<QString, QAction*> acts_;
    QList<QPair<QAction*, QString>> iconActions_;
    QAction *actRun_ = nullptr, *actStop_ = nullptr, *actEdges_ = nullptr, *actLight_ = nullptr, *actDark_ = nullptr;

    QProcess* proc_ = nullptr;
    QList<Job> queue_;

    bool running_ = false;
    bool pendingRun_ = false;   // có yêu cầu giải mới trong lúc đang chạy: chạy lại ngay khi xong
    QProgressBar* busy_ = nullptr;
    QString casePath_;
    RunReport last_;
    bool dark_ = true;
};

}  // namespace fastcae::gui
