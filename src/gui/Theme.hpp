// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Giao diện sáng và tối cho FastCAE (Qt style sheet + bảng màu dùng cho icon).
#pragma once
#include <QColor>
#include <QString>

namespace fastcae::gui {

struct Palette {
    QString bg, panel, panel2, border, text, dim, accent, accentHover, accentText, input, handle, hover, scroll;
    QColor ink() const { return QColor(text); }
    QColor accentColor() const { return QColor(accent); }
};

inline Palette darkPalette() {
    return {"#12151a", "#171b22", "#1b2028", "#2a323e", "#e6e9ef", "#9aa6b8", "#3da9fc", "#5bbcff",
            "#ffffff", "#1a1f27", "#12151a", "#232a35", "#2f3745"};
}

inline Palette lightPalette() {
    return {"#f3f5f8", "#ffffff", "#f7f9fc", "#cfd6e0", "#1f2937", "#5b6678", "#1f6feb", "#388bfd",
            "#ffffff", "#ffffff", "#e5e9ef", "#e3eaf5", "#b7c0cd"};
}

inline Palette paletteFor(bool dark) { return dark ? darkPalette() : lightPalette(); }

inline QString styleSheet(bool dark) {
    const Palette p = paletteFor(dark);
    QString s = R"(
* { font-family: "Segoe UI", "Inter", "Noto Sans", sans-serif; font-size: 13px; }
QMainWindow, QDialog { background: @bg@; color: @text@; }
QWidget { color: @text@; }
QToolTip { background: @panel2@; color: @text@; border: 1px solid @border@; padding: 4px; }
QMenuBar { background: @bg@; padding: 2px 6px; }
QMenuBar::item { padding: 6px 12px; border-radius: 6px; background: transparent; }
QMenuBar::item:selected { background: @hover@; }
QMenu { background: @panel2@; border: 1px solid @border@; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 7px 26px; border-radius: 6px; }
QMenu::item:selected { background: @accent@; color: @accentText@; }
QMenu::separator { height: 1px; background: @border@; margin: 5px 8px; }
QToolBar { background: @panel@; border: none; spacing: 6px; padding: 6px; }
QToolButton { background: transparent; border-radius: 8px; padding: 7px 12px; }
QToolButton:hover { background: @hover@; }
QToolButton:pressed, QToolButton:checked { background: @accent@; color: @accentText@; }
QDockWidget { titlebar-close-icon: none; font-weight: 600; }
QDockWidget::title { background: @panel@; padding: 8px 10px; border-bottom: 1px solid @border@; }
QTreeWidget, QTableWidget, QPlainTextEdit, QListWidget, QTextEdit, QTextBrowser, QLineEdit, QComboBox,
QDoubleSpinBox, QSpinBox {
    background: @input@; border: 1px solid @border@; border-radius: 8px; padding: 5px;
    selection-background-color: @accent@; selection-color: @accentText@; }
QTableWidget { gridline-color: @border@; }
QHeaderView::section { background: @panel2@; border: none; border-bottom: 1px solid @border@; padding: 6px; font-weight: 600; }
QPlainTextEdit { font-family: "JetBrains Mono", "Consolas", "DejaVu Sans Mono", monospace; font-size: 12px; }
QPushButton { background: @accent@; color: @accentText@; border: none; border-radius: 8px; padding: 8px 18px; font-weight: 600; }
QPushButton:hover { background: @accentHover@; }
QPushButton:disabled { background: @border@; color: @dim@; }
QPushButton[flat="true"] { background: @hover@; color: @text@; }
QStatusBar { background: @panel@; color: @dim@; }
QProgressBar { background: @input@; border-radius: 6px; text-align: center; height: 12px; }
QProgressBar::chunk { background: @accent@; border-radius: 6px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid @scroll@; background: @input@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }
QSplitter::handle { background: @handle@; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: @scroll@; border-radius: 5px; min-height: 30px; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle:horizontal { background: @scroll@; border-radius: 5px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QTabWidget::pane { border: 1px solid @border@; border-radius: 8px; top: -1px; background: @panel@; }
QTabBar::tab { background: transparent; padding: 7px 16px; margin-right: 2px; border-top-left-radius: 8px;
    border-top-right-radius: 8px; color: @dim@; }
QTabBar::tab:selected { background: @panel@; color: @text@; font-weight: 600; border: 1px solid @border@; border-bottom: none; }
QTabBar::tab:hover:!selected { background: @hover@; }

/* Ribbon kiểu SolidWorks CommandManager */
QTabWidget#ribbon::pane { border: none; border-top: 1px solid @border@; border-radius: 0; background: @panel@; }
QTabWidget#ribbon > QTabBar::tab { padding: 8px 12px; margin: 0; border-radius: 0; }
QTabWidget#ribbon > QTabBar::tab:selected { border: none; border-bottom: 3px solid @accent@; background: @panel@; }
QScrollArea#ribbonScroll, QScrollArea#ribbonScroll > QWidget > QWidget { background: @panel@; }
QFrame#ribbonGroup { background: transparent; border: none; border-right: 1px solid @border@; }
QLabel#ribbonGroupTitle { color: @dim@; font-size: 11px; }
QToolButton#ribbonBtn { padding: 4px 6px; min-width: 58px; border-radius: 6px; }
QToolButton#ribbonBtn::menu-indicator { image: none; }
QLabel#credit { color: @dim@; padding-right: 8px; }
)";
    const std::pair<const char*, QString> kv[] = {
        {"@bg@", p.bg}, {"@panel@", p.panel}, {"@panel2@", p.panel2}, {"@border@", p.border}, {"@text@", p.text},
        {"@dim@", p.dim}, {"@accentHover@", p.accentHover}, {"@accentText@", p.accentText}, {"@accent@", p.accent},
        {"@input@", p.input}, {"@handle@", p.handle}, {"@hover@", p.hover}, {"@scroll@", p.scroll}};
    for (const auto& [k, v] : kv) s.replace(k, v);
    return s;
}

}  // namespace fastcae::gui
