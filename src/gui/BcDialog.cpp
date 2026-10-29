// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "BcDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace fastcae::gui {

namespace {
QDoubleSpinBox* spin(double lo, double hi, int dec = 4) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setValue(0);
    s->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    return s;
}
}  // namespace

BcDialog::BcDialog(Kind kind, QWidget* parent, const QString& pickedName, const QString& pickedDesc)
    : QDialog(parent), kind_(kind), pickedName_(pickedName) {
    static const char* titles[] = {"Thêm liên kết (ngàm)", "Thêm lực tác dụng", "Thêm nhiệt độ cưỡng bức", "Thêm nguồn nhiệt"};
    setWindowTitle(tr(titles[kind]));
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    lay->addLayout(form);

    set_ = new QComboBox;
    set_->setEditable(true);
    if (!pickedName.isEmpty()) set_->addItem(tr("Mặt đã chọn bằng chuột: %1").arg(pickedDesc), pickedName);
    set_->addItems({"xmin", "xmax", "ymin", "ymax", "zmin", "zmax", "all"});
    form->addRow(tr("Mặt / tập nút"), set_);

    const double big = 1e12;
    switch (kind) {
        case Fix:
            cx_ = new QCheckBox(tr("Khoá chuyển vị theo X"));
            cy_ = new QCheckBox(tr("Khoá chuyển vị theo Y"));
            cz_ = new QCheckBox(tr("Khoá chuyển vị theo Z"));
            for (auto* c : {cx_, cy_, cz_}) { c->setChecked(true); form->addRow(c); }
            lay->addWidget(new QLabel(tr("Chọn cả ba trục = ngàm cứng (chuyển vị bằng 0).")));
            break;
        case Load:
            v1_ = spin(-big, big, 3); v2_ = spin(-big, big, 3); v3_ = spin(-big, big, 3);
            form->addRow(tr("Fx tổng (N)"), v1_);
            form->addRow(tr("Fy tổng (N)"), v2_);
            form->addRow(tr("Fz tổng (N)"), v3_);
            v3_->setValue(-1000);
            lay->addWidget(new QLabel(tr("Lực tổng được phân bố đều theo diện tích mặt.")));
            break;
        case Temp:
            v1_ = spin(-1e6, 1e6, 3);
            form->addRow(tr("Nhiệt độ"), v1_);
            v1_->setValue(100);
            break;
        case Heat:
            v1_ = spin(-big, big, 3);
            form->addRow(tr("Công suất tổng (W)"), v1_);
            v1_->setValue(100);
            break;
    }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

bool BcDialog::usesPicked() const {
    const int i = set_->currentIndex();
    return !pickedName_.isEmpty() && i == 0 && set_->itemText(0) == set_->currentText();
}

QString BcDialog::caseLine() const {
    const QString s = usesPicked() ? pickedName_ : set_->currentText().trimmed();
    auto n = [](QDoubleSpinBox* b) { return QString::number(b->value(), 'g', 10); };
    switch (kind_) {
        case Fix: {
            if (cx_->isChecked() && cy_->isChecked() && cz_->isChecked()) return QString("fix = %1").arg(s);
            QString axes;
            if (cx_->isChecked()) axes += " x";
            if (cy_->isChecked()) axes += " y";
            if (cz_->isChecked()) axes += " z";
            return axes.isEmpty() ? QString() : QString("fix = %1%2").arg(s, axes);
        }
        case Load: return QString("load = %1 %2 %3 %4").arg(s, n(v1_), n(v2_), n(v3_));
        case Temp: return QString("temp = %1 %2").arg(s, n(v1_));
        case Heat: return QString("heat = %1 %2").arg(s, n(v1_));
    }
    return {};
}

}  // namespace fastcae::gui
