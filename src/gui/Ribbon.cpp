// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "Ribbon.hpp"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace fastcae::gui {

Ribbon::Ribbon(QWidget* parent) : QTabWidget(parent) {
    setObjectName("ribbon");
    setDocumentMode(true);
    setFixedHeight(146);
}

QHBoxLayout* Ribbon::addPage(const QString& title) {
    auto* page = new QWidget;
    auto* lay = new QHBoxLayout(page);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    // cuộn ngang khi cửa sổ hẹp (các nhóm không bị cắt)
    auto* sc = new QScrollArea;
    sc->setObjectName("ribbonScroll");
    sc->setFrameShape(QFrame::NoFrame);
    sc->setWidgetResizable(true);
    sc->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sc->setWidget(page);
    addTab(sc, title);
    return lay;
}

QHBoxLayout* Ribbon::addGroup(QHBoxLayout* page, const QString& title) {
    auto* frame = new QFrame;
    frame->setObjectName("ribbonGroup");
    auto* v = new QVBoxLayout(frame);
    v->setContentsMargins(8, 0, 8, 0);
    v->setSpacing(1);
    auto* row = new QHBoxLayout;
    row->setSpacing(2);
    v->addLayout(row, 1);
    auto* label = new QLabel(title);
    label->setObjectName("ribbonGroupTitle");
    label->setAlignment(Qt::AlignHCenter);
    v->addWidget(label);
    // chèn trước stretch cuối (nếu có) để các nhóm dồn về bên trái
    if (page->count() > 0 && page->itemAt(page->count() - 1)->spacerItem()) page->insertWidget(page->count() - 1, frame);
    else {
        page->addWidget(frame);
        page->addStretch(1);
    }
    return row;
}

void Ribbon::addButton(QHBoxLayout* group, QAction* action, QMenu* menu) {
    auto* b = new QToolButton;
    b->setObjectName("ribbonBtn");
    b->setDefaultAction(action);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setIconSize(QSize(32, 32));
    b->setMinimumHeight(88);
    b->setAutoRaise(true);
    if (menu) {
        b->setMenu(menu);
        b->setPopupMode(QToolButton::InstantPopup);
    }
    group->addWidget(b);
}

}  // namespace fastcae::gui
