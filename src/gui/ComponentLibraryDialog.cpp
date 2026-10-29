// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "ComponentLibraryDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

#include "cad/frame_templates.hpp"
#include "cad/shape_library.hpp"

namespace fastcae::gui {

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
bool isCount(const std::string& key) { return key == "n" || key == "nb" || key == "ns"; }
}  // namespace

ComponentLibraryDialog::ComponentLibraryDialog(QWidget* parent, const QString& preferCategory) : QDialog(parent) {
    setWindowTitle(tr("Thư viện cấu kiện kết cấu"));
    resize(980, 620);
    auto* lay = new QVBoxLayout(this);
    auto* split = new QSplitter;
    lay->addWidget(split, 1);

    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    split->addWidget(tree_);

    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    note_ = new QLabel(tr("Chọn một cấu kiện ở danh sách bên trái."));
    note_->setWordWrap(true);
    analysis_ = new QLabel;
    analysis_->setWordWrap(true);
    form_ = new QFormLayout;
    rl->addWidget(note_);
    rl->addWidget(analysis_);
    rl->addLayout(form_);
    rl->addStretch(1);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);

    // nhóm theo category, giữ thứ tự xuất hiện
    std::map<QString, QTreeWidgetItem*> groups;
    auto group = [&](const QString& name) {
        auto it = groups.find(name);
        if (it != groups.end()) return it->second;
        auto* g = new QTreeWidgetItem(tree_, QStringList(name));
        g->setFlags(g->flags() & ~Qt::ItemIsSelectable);
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        groups[name] = g;
        return g;
    };
    auto add = [&](const ShapeInfo& s, Kind kind) {
        auto* it = new QTreeWidgetItem(group(q(s.category)), QStringList(q(s.label)));
        it->setData(0, Qt::UserRole, int(kind));
        it->setData(0, Qt::UserRole + 1, q(s.name));
    };
    for (const auto& s : shapeCatalog()) add(s, Solid);
    for (const auto& s : frameTemplateCatalog()) add(s, Frame);
    for (const auto& s : shellTemplateCatalog()) add(s, Shell);
    tree_->expandAll();

    auto* bb = new QDialogButtonBox;
    auto* ok = bb->addButton(tr("Tạo mô hình"), QDialogButtonBox::AcceptRole);
    ok->setDefault(true);
    bb->addButton(tr("Đóng"), QDialogButtonBox::RejectRole);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &ComponentLibraryDialog::onSelect);

    // chọn mục đầu của nhóm ưu tiên (hoặc mục đầu tiên)
    QTreeWidgetItem* pick = nullptr;
    for (auto& [name, g] : groups)
        if (!preferCategory.isEmpty() && name.contains(preferCategory, Qt::CaseInsensitive) && g->childCount() > 0) { pick = g->child(0); break; }
    if (!pick && tree_->topLevelItemCount() > 0 && tree_->topLevelItem(0)->childCount() > 0) pick = tree_->topLevelItem(0)->child(0);
    if (pick) tree_->setCurrentItem(pick);
}

void ComponentLibraryDialog::clearForm() {
    spins_.clear();
    material_ = nullptr;
    materialLabel_ = nullptr;
    while (form_->rowCount() > 0) form_->removeRow(0);
}

void ComponentLibraryDialog::onSelect() {
    auto items = tree_->selectedItems();
    if (items.isEmpty() || items.first()->parent() == nullptr) return;
    auto* it = items.first();
    current_.kind = static_cast<Kind>(it->data(0, Qt::UserRole).toInt());
    current_.name = it->data(0, Qt::UserRole + 1).toString();
    const ShapeInfo* info = nullptr;
    QString analysis;
    if (current_.kind == Solid) {
        info = findShape(current_.name.toStdString());
        analysis = tr("<b>Phân tích:</b> khối đặc 3D (tứ diện bậc 2). Giải bằng solver nội bộ, CalculiX hoặc PETSc.");
    } else if (current_.kind == Frame) {
        info = findFrameTemplate(current_.name.toStdString());
        analysis = tr("<b>Phân tích:</b> phần tử 1D (dầm Euler-Bernoulli 12 bậc tự do, thanh dàn). Giải trực tiếp, tức thì.");
    } else {
        info = findShellTemplate(current_.name.toStdString());
        analysis = tr("<b>Phân tích:</b> phần tử vỏ S4 giải bằng CalculiX (cần cài ccx).");
    }
    clearForm();
    if (!info) return;
    note_->setText(q(info->note));
    analysis_->setText(analysis);
    for (const auto& p : info->params) {
        auto* sp = new QDoubleSpinBox;
        sp->setRange(isCount(p.key) ? 1 : 1e-6, 1e9);
        sp->setDecimals(isCount(p.key) ? 0 : 5);
        sp->setValue(p.def);
        sp->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        form_->addRow(q(p.label), sp);
        spins_.push_back(sp);
    }
    if (current_.kind == Solid) {
        material_ = new QComboBox;
        material_->addItem(tr("Thép (E = 210 GPa)"), "steel");
        material_->addItem(tr("Bê tông (E = 30 GPa)"), "concrete");
        material_->addItem(tr("Nhôm (E = 70 GPa)"), "aluminium");
        if (current_.name == "wall" || current_.name == "pad_footing" || current_.name == "retaining_wall") material_->setCurrentIndex(1);
        form_->addRow(tr("Vật liệu"), material_);
    }
}

void ComponentLibraryDialog::accept() {
    if (current_.name.isEmpty()) return;
    std::vector<double> p;
    for (auto* s : spins_) p.push_back(s->value());
    std::string err;
    if (current_.kind == Solid) {
        const std::string mat = material_ ? material_->currentData().toString().toStdString() : "steel";
        text_ = q(shapeCaseText(current_.name.toStdString(), p, mat));
        // kiểm tra nhanh hình học hợp lệ để báo lỗi ngay trong hộp thoại
        Mesh m;
        if (!buildShapeMesh(current_.name.toStdString(), p, 0, m, err)) {
            QMessageBox::warning(this, tr("Kích thước không hợp lệ"), q(err));
            return;
        }
    } else if (current_.kind == Frame) {
        text_ = q(frameTemplateText(current_.name.toStdString(), p, err));
    } else {
        text_ = q(shellTemplateText(current_.name.toStdString(), p, err));
    }
    if (text_.isEmpty()) {
        QMessageBox::warning(this, tr("Kích thước không hợp lệ"), q(err));
        return;
    }
    QDialog::accept();
}

}  // namespace fastcae::gui
