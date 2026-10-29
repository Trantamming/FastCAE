// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "SeriesPlotDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace fastcae::gui {

class PlotWidget : public QWidget {
public:
    struct Line { QString name; std::vector<double> x, y; QColor color; };
    std::vector<Line> lines;
    bool logY = false;
    QString xLabel;
    PlotWidget() { setMinimumSize(520, 360); setAutoFillBackground(true); }

protected:
    static double niceStep(double range, int target) {
        if (range <= 0) return 1;
        const double raw = range / target, mag = std::pow(10.0, std::floor(std::log10(raw)));
        const double f = raw / mag;
        return (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * mag;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor bg("#12161d"), fg("#cfd6e4");   // khớp giao diện tối của chương trình (không dùng palette: nền Base có thể là trắng)
        p.fillRect(rect(), bg);
        const QRectF plot(70, 16, width() - 90, height() - 70);
        QColor grid = fg; grid.setAlpha(40);
        double xlo = 1e300, xhi = -1e300, ylo = 1e300, yhi = -1e300;
        auto ty = [&](double v) { return logY ? std::log10(v) : v; };
        for (const auto& l : lines)
            for (std::size_t i = 0; i < l.y.size(); ++i) {
                if (logY && l.y[i] <= 0) continue;
                xlo = std::min(xlo, l.x[i]); xhi = std::max(xhi, l.x[i]);
                ylo = std::min(ylo, ty(l.y[i])); yhi = std::max(yhi, ty(l.y[i]));
            }
        if (xlo > xhi) { p.setPen(fg); p.drawText(rect(), Qt::AlignCenter, tr("Chưa chọn dãy số nào")); return; }
        if (xhi == xlo) { xlo -= 0.5; xhi += 0.5; }
        if (yhi == ylo) { ylo -= 0.5; yhi += 0.5; }
        const double pad = 0.04 * (yhi - ylo);
        ylo -= pad; yhi += pad;
        auto X = [&](double v) { return plot.left() + (v - xlo) / (xhi - xlo) * plot.width(); };
        auto Y = [&](double v) { return plot.bottom() - (ty(v) - ylo) / (yhi - ylo) * plot.height(); };
        // lưới và nhãn trục
        p.setFont(font());
        const double sx = niceStep(xhi - xlo, 6), sy = niceStep(yhi - ylo, 6);
        for (double v = std::ceil(xlo / sx) * sx; v <= xhi + 1e-12 * sx; v += sx) {
            const double px = plot.left() + (v - xlo) / (xhi - xlo) * plot.width();
            p.setPen(grid); p.drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
            p.setPen(fg); p.drawText(QRectF(px - 40, plot.bottom() + 4, 80, 16), Qt::AlignCenter, QString::number(v, 'g', 6));
        }
        for (double v = std::ceil(ylo / sy) * sy; v <= yhi + 1e-12 * sy; v += sy) {
            const double py = plot.bottom() - (v - ylo) / (yhi - ylo) * plot.height();
            p.setPen(grid); p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
            p.setPen(fg);
            p.drawText(QRectF(2, py - 8, plot.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                       logY ? QString("1e%1").arg(v, 0, 'g', 4) : QString::number(v, 'g', 5));
        }
        p.setPen(fg);
        p.drawRect(plot);
        p.drawText(QRectF(plot.left(), height() - 22, plot.width(), 18), Qt::AlignCenter, xLabel);
        // đường
        p.setClipRect(plot);
        for (const auto& l : lines) {
            QPainterPath path;
            bool started = false;
            const std::size_t n = l.y.size(), stride = std::max<std::size_t>(1, n / 6000);
            for (std::size_t i = 0; i < n; i += stride) {
                if (logY && l.y[i] <= 0) { started = false; continue; }
                const QPointF pt(X(l.x[i]), Y(l.y[i]));
                if (!started) { path.moveTo(pt); started = true; } else path.lineTo(pt);
            }
            p.setPen(QPen(l.color, 1.6));
            p.drawPath(path);
        }
        p.setClipping(false);
        // chú thích
        double ly = plot.top() + 6;
        for (const auto& l : lines) {
            p.setPen(QPen(l.color, 3));
            p.drawLine(QPointF(plot.right() - 190, ly + 7), QPointF(plot.right() - 170, ly + 7));
            p.setPen(fg);
            p.drawText(QRectF(plot.right() - 165, ly, 160, 14), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(l.name, Qt::ElideRight, 160));
            ly += 16;
        }
    }
};

SeriesPlotDialog::SeriesPlotDialog(QWidget* parent, const std::map<std::string, std::vector<double>>& series, const QString& title)
    : QDialog(parent), series_(series) {
    setWindowTitle(tr("Đồ thị — %1").arg(title));
    resize(960, 540);
    auto* root = new QHBoxLayout(this);
    auto* left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Dãy số (chọn để vẽ):")));
    list_ = new QListWidget;
    list_->setMinimumWidth(220);
    // chọn sẵn: các dãy dư lượng (rms...) nếu có, nếu không ba dãy đầu không phải bộ đếm vòng lặp
    int preselect = 0;
    for (const auto& kv : series_) {
        auto* it = new QListWidgetItem(QString::fromStdString(kv.first), list_);
        const QString n = it->text().toLower();
        const bool counter = n.contains("iter") || n == "time" || n == "times" || n == "step";
        const bool resid = n.startsWith("rms") || n.contains("res");
        bool on = resid || (!counter && preselect < 3 && std::none_of(series_.begin(), series_.end(), [](const auto& e) {
                                const QString k = QString::fromStdString(e.first).toLower(); return k.startsWith("rms") || k.contains("res"); }));
        if (on) ++preselect;
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    left->addWidget(list_, 1);
    left->addWidget(new QLabel(tr("Trục x:")));
    xBox_ = new QComboBox;
    xBox_->addItem(tr("Chỉ số (vòng lặp / bước)"));
    for (const auto& kv : series_) xBox_->addItem(QString::fromStdString(kv.first));
    left->addWidget(xBox_);
    // có dãy thời gian cùng độ dài với dãy đang chọn thì lấy làm trục x
    for (const char* tn : {"times", "time"}) {
        const auto t = series_.find(tn);
        if (t == series_.end()) continue;
        bool same = false;
        for (int i = 0; i < list_->count(); ++i)
            if (list_->item(i)->checkState() == Qt::Checked && series_[list_->item(i)->text().toStdString()].size() == t->second.size()) same = true;
        if (same) { xBox_->setCurrentText(QString::fromLatin1(tn)); break; }
    }
    log_ = new QCheckBox(tr("Thang log (trục y, giá trị dương)"));
    left->addWidget(log_);
    norm_ = new QCheckBox(tr("Chuẩn hóa mỗi dãy về 0–1 (khác đơn vị)"));
    left->addWidget(norm_);
    auto* png = new QPushButton(tr("Xuất PNG…"));
    auto* close = new QPushButton(tr("Đóng"));
    auto* row = new QHBoxLayout;
    row->addWidget(png); row->addWidget(close);
    left->addLayout(row);
    root->addLayout(left);
    plot_ = new PlotWidget;
    root->addWidget(plot_, 1);
    connect(list_, &QListWidget::itemChanged, this, [this] { refresh(); });
    connect(xBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
    connect(log_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(norm_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(png, &QPushButton::clicked, this, [this] { exportPng(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    refresh();
}

void SeriesPlotDialog::refresh() {
    static const QColor pal[] = {QColor("#4da3ff"), QColor("#ff7a59"), QColor("#5bd48a"), QColor("#e6c14a"),
                                 QColor("#b48cff"), QColor("#ff6fb1"), QColor("#4fd1c5"), QColor("#a0aec0")};
    plot_->lines.clear();
    const std::vector<double>* xs = nullptr;
    if (xBox_->currentIndex() > 0) {
        const auto it = series_.find(xBox_->currentText().toStdString());
        if (it != series_.end()) xs = &it->second;
        plot_->xLabel = xBox_->currentText();
    } else plot_->xLabel = tr("Chỉ số");
    int k = 0;
    for (int i = 0; i < list_->count(); ++i) {
        auto* it = list_->item(i);
        if (it->checkState() != Qt::Checked) continue;
        const auto& y = series_[it->text().toStdString()];
        PlotWidget::Line l;
        l.name = it->text(); l.y = y; l.color = pal[k++ % 8];
        l.x.resize(y.size());
        for (std::size_t j = 0; j < y.size(); ++j) l.x[j] = (xs && j < xs->size()) ? (*xs)[j] : double(j);
        plot_->lines.push_back(std::move(l));
    }
    // tự bật chuẩn hóa khi các dãy đang chọn chênh nhau > 1000 lần về biên độ (vd. chuyển vị 1e-4 và ứng suất 1e7)
    if (!normAutoDone_) {
        normAutoDone_ = true;
        double lo = 1e300, hi = 0;
        for (const auto& l : plot_->lines) {
            double m = 0;
            for (double v : l.y) m = std::max(m, std::fabs(v));
            if (m > 0) { lo = std::min(lo, m); hi = std::max(hi, m); }
        }
        if (plot_->lines.size() >= 2 && hi > 1000 * lo) { norm_->blockSignals(true); norm_->setChecked(true); norm_->blockSignals(false); }
    }
    if (norm_->isChecked()) {
        log_->setChecked(false);
        for (auto& l : plot_->lines) {
            double mn = 1e300, mx = -1e300;
            for (double v : l.y) { mn = std::min(mn, v); mx = std::max(mx, v); }
            const double r = mx - mn > 0 ? mx - mn : 1;
            for (double& v : l.y) v = (v - mn) / r;
        }
    }
    log_->setEnabled(!norm_->isChecked());
    plot_->logY = log_->isChecked();
    plot_->update();
}

QPixmap SeriesPlotDialog::grabPlot() const { return plot_->grab(); }

void SeriesPlotDialog::exportPng() {
    const QString p = QFileDialog::getSaveFileName(this, tr("Xuất đồ thị"), "plot.png", tr("Ảnh PNG (*.png)"));
    if (!p.isEmpty()) plot_->grab().save(p, "PNG");
}

}  // namespace fastcae::gui
