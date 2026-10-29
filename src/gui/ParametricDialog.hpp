// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;

namespace fastcae::gui {

// Built-in parametric CAD designer: pick a primitive, set dimensions and mesh
// size; the dialog returns the matching `geometry = ...` case-file lines.
class ParametricDialog : public QDialog {
    Q_OBJECT
public:
    explicit ParametricDialog(QWidget* parent = nullptr);
    QString geometryLines() const;

private slots:
    void updateVisibility();

private:
    QComboBox* kind_;
    QDoubleSpinBox *a_, *b_, *c_, *size_;
    QSpinBox *dx_, *dy_, *dz_;
    QLabel *la_, *lb_, *lc_;
};

}  // namespace fastcae::gui
