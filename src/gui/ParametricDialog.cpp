// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "ParametricDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace fastcae::gui {

static QDoubleSpinBox* dim(double v) {
    auto* s = new QDoubleSpinBox;
    s->setDecimals(6);
    s->setRange(1e-6, 1e6);
    s->setValue(v);
    return s;
}

ParametricDialog::ParametricDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Thiết kế hình học tham số"));
    auto* form = new QFormLayout;
    kind_ = new QComboBox;
    kind_->addItem(tr("Khối hộp (box)"), "box");
    kind_->addItem(tr("Trụ tròn (cylinder) - cần Gmsh/OpenCASCADE"), "cylinder");
    kind_->addItem(tr("Cầu (sphere) - cần Gmsh/OpenCASCADE"), "sphere");
    form->addRow(tr("Loại"), kind_);
    a_ = dim(1.0); b_ = dim(0.1); c_ = dim(0.1);
    la_ = new QLabel; lb_ = new QLabel; lc_ = new QLabel;
    form->addRow(la_, a_);
    form->addRow(lb_, b_);
    form->addRow(lc_, c_);
    size_ = dim(0.05);
    form->addRow(tr("Kích thước lưới (m, 0 = tự động)"), size_);
    size_->setMinimum(0);
    dx_ = new QSpinBox; dy_ = new QSpinBox; dz_ = new QSpinBox;
    for (auto* s : {dx_, dy_, dz_}) { s->setRange(1, 500); s->setValue(10); }
    dx_->setValue(20); dy_->setValue(3); dz_->setValue(3);
    form->addRow(tr("Số chia lưới X (box)"), dx_);
    form->addRow(tr("Số chia lưới Y (box)"), dy_);
    form->addRow(tr("Số chia lưới Z (box)"), dz_);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(box);
    connect(kind_, &QComboBox::currentIndexChanged, this, &ParametricDialog::updateVisibility);
    updateVisibility();
}

void ParametricDialog::updateVisibility() {
    const QString k = kind_->currentData().toString();
    const bool isBox = k == "box", isCyl = k == "cylinder";
    la_->setText(isBox ? tr("Dài X (m)") : tr("Bán kính (m)"));
    lb_->setText(isBox ? tr("Rộng Y (m)") : tr("Chiều dài trụ (m)"));
    lc_->setText(tr("Cao Z (m)"));
    b_->setVisible(isBox || isCyl); lb_->setVisible(isBox || isCyl);
    c_->setVisible(isBox); lc_->setVisible(isBox);
    for (auto* w : {static_cast<QWidget*>(dx_), static_cast<QWidget*>(dy_), static_cast<QWidget*>(dz_)})
        w->setEnabled(isBox);
}

QString ParametricDialog::geometryLines() const {
    const QString k = kind_->currentData().toString();
    QString g = "geometry = " + k + " " + QString::number(a_->value(), 'g', 10);
    if (k == "box") g += QString(" %1 %2").arg(b_->value(), 0, 'g', 10).arg(c_->value(), 0, 'g', 10);
    if (k == "cylinder") g += QString(" %1").arg(b_->value(), 0, 'g', 10);
    QString out = g + "\n";
    if (k == "box") out += QString("divisions = %1 %2 %3\n").arg(dx_->value()).arg(dy_->value()).arg(dz_->value());
    else out += QString("mesh_size = %1\n").arg(size_->value(), 0, 'g', 10);
    return out;
}

}  // namespace fastcae::gui
