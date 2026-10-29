// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "Icons.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <cmath>

namespace fastcae::gui {

namespace {

constexpr int S = 64;   // vẽ trên lưới 64x64, thu nhỏ khi hiển thị

QPolygonF poly(std::initializer_list<QPointF> pts) { return QPolygonF(QList<QPointF>(pts)); }

// Hình hộp đẳng cự; hl: 0 không nhấn, 1 mặt trước, 2 mặt phải, 3 mặt trên, 4 sau, 5 trái, 6 dưới, 7 tất cả
void cube(QPainter& p, const QColor& ink, const QColor& acc, int hl, bool wire = false) {
    const QPointF top[4] = {{32, 8}, {54, 20}, {32, 32}, {10, 20}};
    const QPointF left[4] = {{10, 20}, {32, 32}, {32, 56}, {10, 44}};
    const QPointF right[4] = {{32, 32}, {54, 20}, {54, 44}, {32, 56}};
    auto face = [&](const QPointF* q, bool on, int alpha) {
        QColor c = on ? acc : ink;
        c.setAlpha(on ? 235 : (wire ? 0 : alpha));
        p.setBrush(c);
        p.drawPolygon(q, 4);
    };
    p.setPen(QPen(ink, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    face(top, hl == 3 || hl == 6 || hl == 7, 70);
    face(left, hl == 1 || hl == 5 || hl == 7, 130);
    face(right, hl == 2 || hl == 4 || hl == 7, 190);
}

}  // namespace

QIcon makeIcon(const QString& kind, const QColor& ink, const QColor& accent) {
    QPixmap pm(S * 2, S * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(1.0, 1.0);
    const QPen line(ink, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QPen accLine(accent, 3.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);

    if (kind == "play") {
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawPolygon(poly({{18, 10}, {54, 32}, {18, 54}}));
    } else if (kind == "stop") {
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawRoundedRect(QRectF(14, 14, 36, 36), 6, 6);
    } else if (kind == "new") {
        p.drawPolygon(poly({{16, 8}, {38, 8}, {50, 20}, {50, 56}, {16, 56}}));
        p.setPen(accLine);
        p.drawLine(33, 28, 33, 46);
        p.drawLine(24, 37, 42, 37);
    } else if (kind == "open") {
        p.drawPolygon(poly({{8, 18}, {26, 18}, {31, 24}, {54, 24}, {54, 52}, {8, 52}}));
        p.setPen(accLine);
        p.drawLine(8, 52, 16, 34);
        p.drawLine(16, 34, 60, 34);
    } else if (kind == "save") {
        p.drawRoundedRect(QRectF(10, 10, 44, 44), 5, 5);
        p.drawRect(QRectF(20, 10, 22, 14));
        p.setPen(accLine);
        p.drawRect(QRectF(18, 36, 28, 18));
    } else if (kind == "import" || kind == "export") {
        const bool in = kind == "import";
        p.drawLine(10, 40, 10, 54);
        p.drawLine(10, 54, 54, 54);
        p.drawLine(54, 54, 54, 40);
        p.setPen(accLine);
        p.drawLine(32, in ? 8 : 44, 32, in ? 44 : 8);
        p.drawLine(32, in ? 44 : 8, 22, in ? 34 : 18);
        p.drawLine(32, in ? 44 : 8, 42, in ? 34 : 18);
    } else if (kind == "cube") {
        cube(p, ink, accent, 7);
    } else if (kind == "sketch") {
        p.setPen(QPen(ink, 3.2, Qt::DashLine, Qt::RoundCap));
        p.drawRect(QRectF(10, 18, 36, 34));
        p.setPen(accLine);
        p.drawLine(34, 36, 56, 12);
        p.drawLine(56, 12, 60, 18);
        p.drawLine(60, 18, 40, 42);
    } else if (kind == "mesh") {
        p.setPen(QPen(ink, 2.6, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i <= 3; ++i) {
            p.drawLine(10 + i * 15, 10, 10 + i * 15, 54);
            p.drawLine(10, 10 + i * 15, 55, 10 + i * 15);
        }
        p.setPen(QPen(accent, 2.6));
        p.drawLine(10, 10, 55, 55);
        p.drawLine(25, 10, 55, 40);
        p.drawLine(10, 25, 40, 55);
    } else if (kind == "fix") {
        p.drawLine(8, 10, 8, 54);
        p.setPen(accLine);
        p.drawPolygon(poly({{16, 32}, {44, 14}, {44, 50}}));
        p.setPen(QPen(ink, 2.6));
        for (int y = 12; y <= 52; y += 8) p.drawLine(8, y, 2, y + 6);
    } else if (kind == "force") {
        p.setPen(accLine);
        p.drawLine(10, 54, 50, 14);
        p.drawLine(50, 14, 34, 16);
        p.drawLine(50, 14, 48, 30);
        p.setPen(line);
        p.drawLine(8, 58, 56, 58);
    } else if (kind == "heat") {
        QPainterPath fl;
        fl.moveTo(32, 6);
        fl.cubicTo(40, 18, 52, 26, 50, 40);
        fl.cubicTo(48, 54, 38, 58, 32, 58);
        fl.cubicTo(24, 58, 14, 52, 14, 40);
        fl.cubicTo(14, 30, 22, 26, 24, 16);
        fl.cubicTo(28, 20, 28, 26, 30, 30);
        fl.cubicTo(34, 24, 34, 14, 32, 6);
        p.setPen(line);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 90));
        p.drawPath(fl);
    } else if (kind == "wave") {
        for (int k = 0; k < 3; ++k) {
            QPainterPath w;
            const double y0 = 16 + k * 16;
            w.moveTo(6, y0);
            for (int x = 6; x <= 58; ++x) w.lineTo(x, y0 + 6 * std::sin((x - 6) / 52.0 * 6.2832 * 1.5 + k));
            p.setPen(k == 1 ? accLine : line);
            p.drawPath(w);
        }
    } else if (kind == "link") {
        p.drawRoundedRect(QRectF(6, 20, 30, 24), 12, 12);
        p.setPen(accLine);
        p.drawRoundedRect(QRectF(28, 20, 30, 24), 12, 12);
    } else if (kind == "boom") {
        QPolygonF st;
        for (int i = 0; i < 16; ++i) {
            const double a = i * 3.14159265 / 8, r = i % 2 ? 12 : 27;
            st << QPointF(32 + r * std::cos(a), 32 + r * std::sin(a));
        }
        p.setPen(line);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 110));
        p.drawPolygon(st);
    } else if (kind == "gear") {
        p.setPen(line);
        for (int i = 0; i < 8; ++i) {
            const double a = i * 3.14159265 / 4;
            p.drawLine(QPointF(32 + 17 * std::cos(a), 32 + 17 * std::sin(a)),
                       QPointF(32 + 25 * std::cos(a), 32 + 25 * std::sin(a)));
        }
        p.drawEllipse(QPointF(32, 32), 17, 17);
        p.setPen(accLine);
        p.drawEllipse(QPointF(32, 32), 7, 7);
    } else if (kind == "info") {
        p.drawEllipse(QPointF(32, 32), 24, 24);
        p.setPen(accLine);
        p.drawLine(32, 28, 32, 44);
        p.drawPoint(32, 20);
    } else if (kind == "sun") {
        p.setPen(accLine);
        p.setBrush(accent);
        p.drawEllipse(QPointF(32, 32), 10, 10);
        for (int i = 0; i < 8; ++i) {
            const double a = i * 3.14159265 / 4;
            p.drawLine(QPointF(32 + 17 * std::cos(a), 32 + 17 * std::sin(a)),
                       QPointF(32 + 25 * std::cos(a), 32 + 25 * std::sin(a)));
        }
    } else if (kind == "moon") {
        QPainterPath m;
        m.addEllipse(QPointF(30, 32), 22, 22);
        QPainterPath cut;
        cut.addEllipse(QPointF(41, 26), 18, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawPath(m.subtracted(cut));
    } else if (kind == "fit") {
        p.setPen(line);
        for (int sx : {0, 1})
            for (int sy : {0, 1}) {
                const double x = sx ? 54 : 10, y = sy ? 54 : 10;
                p.drawLine(QPointF(x, y), QPointF(x + (sx ? -14 : 14), y));
                p.drawLine(QPointF(x, y), QPointF(x, y + (sy ? -14 : 14)));
            }
        p.setPen(accLine);
        p.drawRect(QRectF(24, 24, 16, 16));
    } else if (kind == "iso") {
        cube(p, ink, accent, 7);
    } else if (kind == "front") {
        cube(p, ink, accent, 1);
    } else if (kind == "right") {
        cube(p, ink, accent, 2);
    } else if (kind == "top") {
        cube(p, ink, accent, 3);
    } else if (kind == "back") {
        cube(p, ink, accent, 4);
    } else if (kind == "left") {
        cube(p, ink, accent, 5);
    } else if (kind == "bottom") {
        cube(p, ink, accent, 6);
    } else if (kind == "pick") {
        cube(p, ink, accent, 1);
        p.setPen(QPen(ink, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawPolygon(poly({{38, 34}, {38, 58}, {44, 52}, {49, 62}, {54, 60}, {49, 50}, {57, 50}}));
    } else if (kind == "beam") {   // dầm chữ I nhìn ngang
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawRect(QRectF(8, 14, 48, 8));
        p.drawRect(QRectF(8, 42, 48, 8));
        p.setBrush(accent);
        p.drawRect(QRectF(28, 22, 8, 20));
    } else if (kind == "truss") {
        p.setPen(line);
        p.drawLine(6, 46, 58, 46);
        p.drawLine(6, 46, 32, 14);
        p.drawLine(58, 46, 32, 14);
        p.setPen(accLine);
        p.drawLine(19, 46, 32, 14);
        p.drawLine(32, 14, 45, 46);
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        for (QPointF c : {QPointF(6, 46), QPointF(58, 46), QPointF(32, 14), QPointF(19, 46), QPointF(45, 46)}) p.drawEllipse(c, 3.4, 3.4);
    } else if (kind == "shell") {   // vỏ cong
        QPainterPath s;
        s.moveTo(8, 40);
        s.cubicTo(18, 14, 46, 14, 56, 40);
        s.lineTo(56, 50);
        s.cubicTo(46, 24, 18, 24, 8, 50);
        s.closeSubpath();
        p.setPen(line);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 90));
        p.drawPath(s);
    } else if (kind == "node") {
        p.setPen(accLine);
        p.setBrush(accent);
        p.drawEllipse(QPointF(32, 32), 8, 8);
        p.setPen(line);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(32, 32), 18, 18);
    } else if (kind == "member") {
        p.setPen(line);
        p.setBrush(ink);
        p.drawEllipse(QPointF(12, 48), 5, 5);
        p.drawEllipse(QPointF(52, 16), 5, 5);
        p.setPen(accLine);
        p.drawLine(12, 48, 52, 16);
    } else if (kind == "udl") {
        p.setPen(line);
        p.drawLine(6, 46, 58, 46);
        p.setPen(accLine);
        for (int x = 10; x <= 54; x += 11) {
            p.drawLine(x, 12, x, 40);
            p.drawLine(x, 40, x - 4, 33);
            p.drawLine(x, 40, x + 4, 33);
        }
        p.drawLine(10, 12, 54, 12);
    } else if (kind == "library") {
        p.setPen(line);
        p.drawRect(QRectF(8, 10, 22, 22));
        p.drawRect(QRectF(34, 10, 22, 22));
        p.drawRect(QRectF(8, 36, 22, 18));
        p.setPen(accLine);
        p.drawRect(QRectF(34, 36, 22, 18));
    } else if (kind == "wire") {
        cube(p, ink, accent, 0, true);
    } else if (kind == "camera") {
        p.drawRoundedRect(QRectF(6, 18, 52, 36), 6, 6);
        p.drawRect(QRectF(22, 11, 20, 7));
        p.setPen(accLine);
        p.drawEllipse(QPointF(32, 36), 10, 10);
    } else if (kind == "probe") {
        p.setPen(line);
        p.drawEllipse(QPointF(32, 32), 14, 14);
        p.setPen(accLine);
        p.drawLine(32, 4, 32, 18);
        p.drawLine(32, 46, 32, 60);
        p.drawLine(4, 32, 18, 32);
        p.drawLine(46, 32, 60, 32);
    } else if (kind == "chart") {
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawRect(QRectF(10, 34, 11, 20));
        p.setBrush(accent);
        p.drawRect(QRectF(26, 18, 11, 36));
        p.setBrush(ink);
        p.drawRect(QRectF(42, 26, 11, 28));
    } else if (kind == "layers") {
        for (int k = 0; k < 3; ++k) {
            p.setPen(k == 0 ? accLine : line);
            const double y = 14 + k * 14;
            p.drawPolygon(poly({{32, y}, {56, y + 10}, {32, y + 20}, {8, y + 10}}));
        }
    } else if (kind == "doc") {
        p.drawPolygon(poly({{14, 6}, {38, 6}, {52, 20}, {52, 58}, {14, 58}}));
        p.setPen(accLine);
        p.drawLine(22, 28, 44, 28);
        p.drawLine(22, 38, 44, 38);
        p.drawLine(22, 48, 36, 48);
    } else if (kind == "chip") {
        p.drawRoundedRect(QRectF(16, 16, 32, 32), 5, 5);
        p.setPen(accLine);
        p.drawRect(QRectF(26, 26, 12, 12));
        p.setPen(line);
        for (int i = 0; i < 3; ++i) {
            const double t = 22 + i * 10;
            p.drawLine(QPointF(t, 6), QPointF(t, 16));
            p.drawLine(QPointF(t, 48), QPointF(t, 58));
            p.drawLine(QPointF(6, t), QPointF(16, t));
            p.drawLine(QPointF(48, t), QPointF(58, t));
        }
    } else {
        p.drawEllipse(QPointF(32, 32), 20, 20);
    }
    p.end();
    return QIcon(pm);
}

}  // namespace fastcae::gui
