// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "ParamEditDialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace fastcae::gui {

ParamEditDialog::ParamEditDialog(QWidget* parent, const QString& title, const std::vector<Item>& items)
    : QDialog(parent), items_(items) {
    setWindowTitle(tr("Tham số mô hình — %1").arg(title));
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Đổi giá trị (mét) rồi bấm Áp dụng: hình học được dựng lại, chia lưới và giải lại.")));
    auto* form = new QFormLayout;
    for (const Item& it : items_) {
        auto* b = new QDoubleSpinBox;
        b->setDecimals(6);
        b->setRange(-1e9, 1e9);
        b->setSingleStep(std::max(std::fabs(it.defaultValue) * 0.05, 1e-6));
        b->setValue(it.value);
        const QString tip = tr("Mặc định trong file: %1").arg(it.defaultValue, 0, 'g', 8);
        b->setToolTip(tip);
        auto* lab = new QLabel(it.name);
        lab->setToolTip(tip);
        auto mark = [lab, b, def = it.defaultValue] {
            QFont f = lab->font();
            f.setBold(std::fabs(b->value() - def) > 1e-12 * std::max(1.0, std::fabs(def)));
            lab->setFont(f);
        };
        mark();
        connect(b, qOverload<double>(&QDoubleSpinBox::valueChanged), this, mark);
        form->addRow(lab, b);
        boxes_.push_back(b);
    }
    lay->addLayout(form);
    auto* bb = new QDialogButtonBox;
    bb->addButton(tr("Hủy"), QDialogButtonBox::RejectRole);
    auto* apply = bb->addButton(tr("Áp dụng"), QDialogButtonBox::AcceptRole);
    apply->setDefault(true);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    setMinimumWidth(380);
}

QMap<QString, double> ParamEditDialog::changed() const {
    QMap<QString, double> out;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const double v = boxes_[i]->value();
        if (std::fabs(v - items_[i].defaultValue) > 1e-12 * std::max(1.0, std::fabs(items_[i].defaultValue))) out[items_[i].name] = v;
    }
    return out;
}

}  // namespace fastcae::gui
