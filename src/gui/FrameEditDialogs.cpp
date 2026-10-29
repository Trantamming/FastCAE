// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "FrameEditDialogs.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace fastcae::gui {

QStringList askFields(QWidget* parent, const QString& title, const QString& hint, const std::vector<FieldSpec>& fields) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    auto* lay = new QVBoxLayout(&dlg);
    if (!hint.isEmpty()) {
        auto* h = new QLabel(hint);
        h->setWordWrap(true);
        lay->addWidget(h);
    }
    auto* form = new QFormLayout;
    lay->addLayout(form);
    std::vector<QWidget*> w;
    for (const auto& f : fields) {
        QWidget* e = nullptr;
        if (f.type == FieldSpec::Text) {
            e = new QLineEdit(f.value);
            form->addRow(f.label, e);
        } else if (f.type == FieldSpec::Combo) {
            auto* c = new QComboBox;
            c->addItems(f.items);
            const int i = c->findText(f.value);
            if (i >= 0) c->setCurrentIndex(i);
            e = c;
            form->addRow(f.label, e);
        } else {
            auto* c = new QCheckBox(f.label);
            c->setChecked(f.value == "1");
            e = c;
            form->addRow(c);
        }
        w.push_back(e);
    }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return {};
    QStringList out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].type == FieldSpec::Text) out << static_cast<QLineEdit*>(w[i])->text().trimmed();
        else if (fields[i].type == FieldSpec::Combo) out << static_cast<QComboBox*>(w[i])->currentText();
        else out << (static_cast<QCheckBox*>(w[i])->isChecked() ? "1" : "");
    }
    return out;
}

}  // namespace fastcae::gui
