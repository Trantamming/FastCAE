// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDialog>
#include <map>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QListWidget;

namespace fastcae::gui {

class PlotWidget;

// Đồ thị các dãy số của kết quả: lịch sử hội tụ (dư lượng), lực nâng/cản, chuỗi thời gian...
class SeriesPlotDialog : public QDialog {
    Q_OBJECT
public:
    SeriesPlotDialog(QWidget* parent, const std::map<std::string, std::vector<double>>& series, const QString& title);
    QPixmap grabPlot() const;

private:
    void refresh();
    void exportPng();
    std::map<std::string, std::vector<double>> series_;
    QListWidget* list_ = nullptr;
    QComboBox* xBox_ = nullptr;
    QCheckBox* log_ = nullptr;
    QCheckBox* norm_ = nullptr;
    bool normAutoDone_ = false;
    PlotWidget* plot_ = nullptr;
};

}  // namespace fastcae::gui
