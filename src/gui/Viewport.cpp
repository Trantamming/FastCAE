// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include <QKeyEvent>
#include "Viewport.hpp"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace fastcae::gui {

namespace {

constexpr float kFovDeg = 35.0f;

const char* kVert = R"(#version 120
attribute vec3 aPos;
attribute vec3 aDisp;
attribute float aVal;
uniform mat4 uMV;
uniform mat4 uP;
uniform float uWarp;
varying vec3 vView;
varying float vVal;
void main() {
    vec4 v = uMV * vec4(aPos + uWarp * aDisp, 1.0);
    vView = v.xyz;
    vVal = aVal;
    gl_Position = uP * v;
}
)";

// "turbo" colour map (polynomial approximation, A. Mikhailov) - identical to
// the CPU version used for the colour bar below.
const char* kFrag = R"(#version 120
uniform vec3 uRange;      // lo, 1/(hi-lo), useScalar
uniform vec3 uBase;
uniform float uEdge;
varying vec3 vView;
varying float vVal;
vec3 turbo(float x) {
    x = clamp(x, 0.0, 1.0);
    vec4 v4 = vec4(1.0, x, x * x, x * x * x);
    vec2 v2 = v4.zw * v4.z;
    return vec3(
        dot(v4, vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234)) + dot(v2, vec2(-152.94239396, 59.28637943)),
        dot(v4, vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333)) + dot(v2, vec2(4.27729857, 2.82956604)),
        dot(v4, vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771)) + dot(v2, vec2(-89.90310912, 27.34824973)));
}
void main() {
    if (uEdge > 0.5) { gl_FragColor = vec4(0.05, 0.06, 0.08, 1.0); return; }
    vec3 n = normalize(cross(dFdx(vView), dFdy(vView)));
    vec3 L = normalize(vec3(0.35, 0.55, 1.0));
    float lit = 0.38 + 0.62 * abs(dot(n, L));
    vec3 base = (uRange.z > 0.5) ? turbo((vVal - uRange.x) * uRange.y) : uBase;
    gl_FragColor = vec4(base * lit, 1.0);
}
)";

QColor turboColor(double x) {
    x = std::clamp(x, 0.0, 1.0);
    const double x2 = x * x, x3 = x2 * x, x4 = x2 * x2, x5 = x3 * x2;
    const double r = 0.13572138 + 4.61539260 * x - 42.66032258 * x2 + 132.13108234 * x3 - 152.94239396 * x4 + 59.28637943 * x5;
    const double g = 0.09140261 + 2.19418839 * x + 4.84296658 * x2 - 14.18503333 * x3 + 4.27729857 * x4 + 2.82956604 * x5;
    const double b = 0.10667330 + 12.64194608 * x - 60.58204836 * x2 + 110.36276771 * x3 - 89.90310912 * x4 + 27.34824973 * x5;
    return QColor::fromRgbF(std::clamp(r, 0.0, 1.0), std::clamp(g, 0.0, 1.0), std::clamp(b, 0.0, 1.0));
}

// Transparent child widget painted with the normal raster engine on top of the GL surface.
// (QPainter directly on a QOpenGLWidget does not render with some software GL drivers.)
class OverlayWidget : public QWidget {
public:
    explicit OverlayWidget(Viewport* vp) : QWidget(vp), vp_(vp) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        vp_->paintOverlay(p);
    }
private:
    Viewport* vp_;
};

}  // namespace

Viewport::Viewport(QWidget* parent) : QOpenGLWidget(parent) {
    overlay_ = new OverlayWidget(this);
    setMinimumSize(400, 300);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    resetView();
}

Viewport::~Viewport() {
    if (glReady_) {
        makeCurrent();
        triVbo_.destroy();
        lineVbo_.destroy();
        doneCurrent();
    }
}

// ---------------------------------------------------------------- model data
void Viewport::setModel(const Mesh& mesh, const VtkFields& fields) {
    mesh_ = mesh;
    fields_ = fields;
    current_.clear();
    hasScalar_ = false;
    warp_ = 0;
    probeNode_ = -1;
    const auto it = fields_.pointVectors.find("displacement");
    dispField_ = (it != fields_.pointVectors.end() && it->second.size() == mesh_.nodes.size() * 3) ? &it->second : nullptr;

    Vec3 lo, hi;
    if (mesh_.nodes.empty()) { lo = hi = {0, 0, 0}; } else { mesh_.bounds(lo, hi); }
    center_ = QVector3D(float((lo.x + hi.x) / 2), float((lo.y + hi.y) / 2), float((lo.z + hi.z) / 2));
    radius_ = std::max(0.5f * float(std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) +
                                              (hi.z - lo.z) * (hi.z - lo.z))), 1e-12f);
    extractSurface();
    dirty_ = true;
    resetView();
}

void Viewport::extractSurface() {
    struct Face { int a, b, c; };
    std::vector<Face> faces;
    faces.reserve(mesh_.tets.size() * 4);
    static const int idx[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    for (const auto& t : mesh_.tets)
        for (const auto& f : idx) {
            int v[3] = {t[f[0]], t[f[1]], t[f[2]]};
            std::sort(v, v + 3);
            faces.push_back({v[0], v[1], v[2]});
        }
    std::sort(faces.begin(), faces.end(), [](const Face& x, const Face& y) {
        return x.a != y.a ? x.a < y.a : (x.b != y.b ? x.b < y.b : x.c < y.c);
    });
    surfTris_.clear();
    for (std::size_t i = 0; i < faces.size();) {
        std::size_t j = i + 1;
        while (j < faces.size() && faces[j].a == faces[i].a && faces[j].b == faces[i].b && faces[j].c == faces[i].c) ++j;
        if (j - i == 1) surfTris_.push_back({faces[i].a, faces[i].b, faces[i].c});   // boundary face appears once
        i = j;
    }
    std::vector<char> used(mesh_.nodes.size(), 0);
    std::vector<std::array<int, 2>> edges;
    for (const auto& t : surfTris_) {
        for (int k = 0; k < 3; ++k) {
            used[t[k]] = 1;
            int a = t[k], b = t[(k + 1) % 3];
            if (a > b) std::swap(a, b);
            edges.push_back({a, b});
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    surfEdges_ = std::move(edges);
    surfNodes_.clear();
    for (std::size_t i = 0; i < used.size(); ++i) if (used[i]) surfNodes_.push_back(int(i));
    recomputeSelection();
}

QStringList Viewport::scalarNames() const {
    QStringList l;
    for (const auto& kv : fields_.pointScalars) l << QString::fromStdString(kv.first);
    return l;
}

void Viewport::setColorRangeProvider(std::function<bool(const QString&, double&, double&)> fn) { rangeFn_ = std::move(fn); }

void Viewport::setFields(const VtkFields& fields) {
    fields_ = fields;
    const auto it = fields_.pointVectors.find("displacement");
    dispField_ = (it != fields_.pointVectors.end() && it->second.size() == mesh_.nodes.size() * 3) ? &it->second : nullptr;
    showScalar(current_);   // tính lại khoảng màu theo trường mới, giữ camera
}

void Viewport::showScalar(const QString& name) {
    current_ = name;
    hasScalar_ = false;
    if (!name.isEmpty()) {
        auto it = fields_.pointScalars.find(name.toStdString());
        if (it != fields_.pointScalars.end() && !it->second.empty()) {
            auto mm = std::minmax_element(it->second.begin(), it->second.end());
            lo_ = *mm.first;
            hi_ = *mm.second;
            double flo, fhi;
            if (rangeFn_ && rangeFn_(name, flo, fhi)) { lo_ = flo; hi_ = fhi; }
            if (hi_ - lo_ < 1e-300 * std::max(1.0, std::abs(hi_))) hi_ = lo_ + std::max(1e-30, std::abs(lo_) * 1e-6);
            hasScalar_ = true;
        }
    }
    dirty_ = true;
    refresh();
}

void Viewport::refresh() {
    update();
    if (overlay_) overlay_->update();
}

void Viewport::resizeEvent(QResizeEvent* e) {
    QOpenGLWidget::resizeEvent(e);
    if (overlay_) overlay_->setGeometry(rect());
}

void Viewport::setDeformationScale(double s) { warp_ = s; refresh(); }
void Viewport::setEdgesVisible(bool on) { edges_ = on; refresh(); }

QVector3D Viewport::nodePos(int i) const {
    QVector3D p(float(mesh_.nodes[i].x), float(mesh_.nodes[i].y), float(mesh_.nodes[i].z));
    if (dispField_ && warp_ != 0)
        p += float(warp_) * QVector3D(float((*dispField_)[3 * i]), float((*dispField_)[3 * i + 1]), float((*dispField_)[3 * i + 2]));
    return p;
}

void Viewport::fillBuffers() {
    const std::vector<double>* sc = nullptr;
    if (hasScalar_) sc = &fields_.pointScalars.at(current_.toStdString());
    auto push = [&](std::vector<float>& out, int n) {
        out.push_back(float(mesh_.nodes[n].x));
        out.push_back(float(mesh_.nodes[n].y));
        out.push_back(float(mesh_.nodes[n].z));
        if (dispField_) {
            out.push_back(float((*dispField_)[3 * n]));
            out.push_back(float((*dispField_)[3 * n + 1]));
            out.push_back(float((*dispField_)[3 * n + 2]));
        } else {
            out.insert(out.end(), {0.f, 0.f, 0.f});
        }
        out.push_back(sc && std::size_t(n) < sc->size() ? float((*sc)[n]) : 0.f);
    };
    triVerts_.clear();
    triVerts_.reserve(surfTris_.size() * 21);
    for (const auto& t : surfTris_) for (int k = 0; k < 3; ++k) push(triVerts_, t[k]);
    lineVerts_.clear();
    lineVerts_.reserve(surfEdges_.size() * 14);
    for (const auto& e : surfEdges_) { push(lineVerts_, e[0]); push(lineVerts_, e[1]); }

    triVbo_.bind();
    triVbo_.allocate(triVerts_.data(), int(triVerts_.size() * sizeof(float)));
    lineVbo_.bind();
    lineVbo_.allocate(lineVerts_.data(), int(lineVerts_.size() * sizeof(float)));
    lineVbo_.release();
    dirty_ = false;
}

// ---------------------------------------------------------------- camera
int Viewport::viewW() const { return std::max(50, width() - (hasScalar_ ? 130 : 0)); }

float Viewport::distance() const { return 2.5f * radius_ / zoom_; }

QMatrix4x4 Viewport::viewMatrix() const {
    QMatrix4x4 v;
    v.translate(pan_.x(), pan_.y(), -distance());
    v.rotate(rot_);
    v.translate(-center_);
    return v;
}

QMatrix4x4 Viewport::projMatrix() const {
    const float d = distance();
    const float nearP = std::max(0.01f * radius_, d - 1.6f * radius_);
    const float farP = d + 2.0f * radius_;
    QMatrix4x4 p;
    p.perspective(kFovDeg, height() > 0 ? float(viewW()) / float(height()) : 1.0f, nearP, farP);
    return p;
}

void Viewport::resetView() { setStandardView(0); }

void Viewport::fitAll() {
    zoom_ = 1;
    pan_ = QVector2D(0, 0);
    refresh();
}

void Viewport::setDarkTheme(bool dark) {
    dark_ = dark;
    refresh();
}

void Viewport::setWatermark(const QString& text) {
    watermark_ = text;
    refresh();
}

void Viewport::setStandardView(int which) {
    const auto qx = [](float a) { return QQuaternion::fromAxisAndAngle(1, 0, 0, a); };
    const auto qz = [](float a) { return QQuaternion::fromAxisAndAngle(0, 0, 1, a); };
    switch (which) {
        case 1: rot_ = qx(-90); break;                    // front (looking along +Y), Z up
        case 2: rot_ = qx(-90) * qz(-90); break;          // right (looking along -X)
        case 3: rot_ = QQuaternion(); break;              // top (looking down -Z)
        case 4: rot_ = qx(-90) * qz(180); break;          // back
        case 5: rot_ = qx(-90) * qz(90); break;           // left
        case 6: rot_ = qx(180); break;                    // bottom
        default: rot_ = qx(-62) * qz(-35); break;         // isometric-like, Z up
    }
    zoom_ = 1;
    pan_ = QVector2D(0, 0);
    refresh();
}

Viewport::Projected Viewport::project(const QVector3D& p, const QMatrix4x4& mvp) const {
    const QVector4D c = mvp * QVector4D(p, 1.0f);
    if (c.w() <= 1e-9f) return {{}, 0, false};
    const float nx = c.x() / c.w(), ny = c.y() / c.w(), nz = c.z() / c.w();
    return {QPointF((nx * 0.5f + 0.5f) * viewW(), (1.0f - (ny * 0.5f + 0.5f)) * height()), nz, true};
}

// ---------------------------------------------------------------- GL
void Viewport::initializeGL() {
    initializeOpenGLFunctions();
    prog_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVert);
    prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFrag);
    prog_.bindAttributeLocation("aPos", 0);
    prog_.bindAttributeLocation("aDisp", 1);
    prog_.bindAttributeLocation("aVal", 2);
    if (!prog_.link()) qWarning("FastCAE: shader link failed: %s", qPrintable(prog_.log()));
    triVbo_.create();
    lineVbo_.create();
    glReady_ = true;
    dirty_ = true;
}

void Viewport::resizeGL(int, int) {}

void Viewport::paintGL() {
    if (dark_) glClearColor(0.075f, 0.085f, 0.11f, 1.0f);
    else glClearColor(0.93f, 0.95f, 0.98f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!glReady_ || surfTris_.empty() || !prog_.isLinked()) return;
    if (dirty_) fillBuffers();

    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, GLsizei(viewW() * dpr), GLsizei(height() * dpr));
    glEnable(GL_DEPTH_TEST);
    prog_.bind();
    prog_.setUniformValue("uMV", viewMatrix());
    prog_.setUniformValue("uP", projMatrix());
    prog_.setUniformValue("uWarp", float(dispField_ ? warp_ : 0.0));
    prog_.setUniformValue("uRange", QVector3D(float(lo_), float(1.0 / (hi_ - lo_)), hasScalar_ ? 1.0f : 0.0f));
    prog_.setUniformValue("uBase", QVector3D(0.55f, 0.70f, 0.92f));

    const int stride = 7 * int(sizeof(float));
    auto bindAttribs = [&](QOpenGLBuffer& vbo) {
        vbo.bind();
        prog_.enableAttributeArray(0);
        prog_.enableAttributeArray(1);
        prog_.enableAttributeArray(2);
        prog_.setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);
        prog_.setAttributeBuffer(1, GL_FLOAT, 3 * int(sizeof(float)), 3, stride);
        prog_.setAttributeBuffer(2, GL_FLOAT, 6 * int(sizeof(float)), 1, stride);
    };

    prog_.setUniformValue("uEdge", 0.0f);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    bindAttribs(triVbo_);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(triVerts_.size() / 7));
    glDisable(GL_POLYGON_OFFSET_FILL);

    if (edges_) {
        prog_.setUniformValue("uEdge", 1.0f);
        bindAttribs(lineVbo_);
        glDrawArrays(GL_LINES, 0, GLsizei(lineVerts_.size() / 7));
    }
    prog_.disableAttributeArray(0);
    prog_.disableAttributeArray(1);
    prog_.disableAttributeArray(2);
    triVbo_.release();
    prog_.release();
    glDisable(GL_DEPTH_TEST);
}

// ---------------------------------------------------------------- overlay
void Viewport::paintOverlay(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f = font();
    f.setPointSizeF(9.5);
    p.setFont(f);
    const QFontMetrics fm(f);
    const QColor ink = dark_ ? QColor(226, 231, 240) : QColor(31, 41, 55);
    const QColor frame = dark_ ? QColor(90, 100, 118) : QColor(150, 160, 175);

    // Colour bar
    if (hasScalar_) {
        const int bw = 16, bh = std::clamp(height() / 2, 120, 320);
        const int x = width() - 118, y = (height() - bh) / 2 + 8;   // inside the reserved 130 px strip
        for (int i = 0; i < bh; ++i) p.fillRect(x, y + i, bw, 1, turboColor(1.0 - double(i) / (bh - 1)));
        p.setPen(frame);
        p.drawRect(x, y, bw, bh);
        p.setPen(ink);
        const int nt = 6;
        for (int k = 0; k < nt; ++k) {
            const double t = double(k) / (nt - 1);
            const int yy = y + int((1.0 - t) * (bh - 1));
            p.drawLine(x + bw, yy, x + bw + 4, yy);
            p.drawText(x + bw + 7, yy + fm.ascent() / 2 - 1, QString::number(lo_ + t * (hi_ - lo_), 'g', 4));
        }
        p.drawText(x, y - 12, fm.elidedText(current_, Qt::ElideRight, 112));
    }

    // Axis triad (follows the camera rotation)
    {
        const QPointF o(48, height() - 48);
        QMatrix4x4 m;
        m.rotate(rot_);
        struct Ax { QVector3D d; QColor c; const char* n; };
        const Ax axes[3] = {{{1, 0, 0}, QColor(240, 90, 90), "X"}, {{0, 1, 0}, QColor(100, 210, 120), "Y"}, {{0, 0, 1}, QColor(90, 150, 245), "Z"}};
        for (const auto& a : axes) {
            const QVector3D v = m.map(a.d);
            const QPointF e = o + QPointF(v.x() * 30, -v.y() * 30);
            p.setPen(QPen(a.c, 2.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(o, e);
            p.drawText(e + QPointF(v.x() * 8 - 4, -v.y() * 8 + 4), a.n);
        }
    }

    // Mặt đã chọn (tô cam bán trong suốt)
    if (hasSel_ && !selTris_.empty()) {
        const QMatrix4x4 mvp = projMatrix() * viewMatrix();
        p.setPen(QPen(QColor(255, 140, 0, 230), 1.2));
        p.setBrush(QColor(255, 140, 0, 110));
        for (int ti : selTris_) {
            const auto& t = surfTris_[ti];
            const auto a = project(nodePos(t[0]), mvp), b = project(nodePos(t[1]), mvp), c = project(nodePos(t[2]), mvp);
            if (!a.ok || !b.ok || !c.ok) continue;
            const QPointF pts[3] = {a.pt, b.pt, c.pt};
            p.drawPolygon(pts, 3);
        }
    }

    // Probe marker + info box
    if (probeNode_ >= 0 && probeNode_ < int(mesh_.nodes.size())) {
        const auto pr = project(nodePos(probeNode_), projMatrix() * viewMatrix());
        if (pr.ok) {
            p.setPen(QPen(QColor(255, 255, 255), 2));
            p.setBrush(QColor(255, 190, 40));
            p.drawEllipse(pr.pt, 5.0, 5.0);
            const QStringList lines = probeText_.split('\n');
            int w = 0;
            for (const auto& l : lines) w = std::max(w, fm.horizontalAdvance(l));
            const int h = int(lines.size()) * fm.height() + 10;
            QRectF box(pr.pt.x() + 12, pr.pt.y() - h - 6, w + 16, h);
            if (box.right() > width() - 4) box.moveRight(pr.pt.x() - 12);
            if (box.top() < 4) box.moveTop(pr.pt.y() + 12);
            p.setPen(dark_ ? QColor(70, 80, 98) : QColor(170, 180, 195));
            p.setBrush(dark_ ? QColor(22, 27, 36, 235) : QColor(255, 255, 255, 240));
            p.drawRoundedRect(box, 6, 6);
            p.setPen(ink);
            for (int i = 0; i < lines.size(); ++i)
                p.drawText(QPointF(box.left() + 8, box.top() + 5 + fm.ascent() + i * fm.height()), lines[i]);
        }
    }
    if (!watermark_.isEmpty()) {
        QFont wf = f;
        wf.setPointSizeF(8.5);
        p.setFont(wf);
        p.setPen(dark_ ? QColor(140, 150, 170, 170) : QColor(90, 100, 115, 190));
        const QFontMetrics wfm(wf);
        p.drawText(QPointF(width() - 130 - wfm.horizontalAdvance(watermark_) - 12, height() - 12), watermark_);
        p.setFont(f);
    }
    if (surfTris_.empty()) {
        p.setPen(dark_ ? QColor(130, 140, 158) : QColor(100, 110, 125));
        p.drawText(rect(), Qt::AlignCenter, tr("Chưa có mô hình - chọn Tệp ▸ Ví dụ, rồi bấm Chạy"));
    }
}

// ---------------------------------------------------------------- input
void Viewport::mousePressEvent(QMouseEvent* e) {
    lastPos_ = pressPos_ = e->pos();
    setCursor(e->button() == Qt::LeftButton ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    const QPoint d = e->pos() - lastPos_;
    lastPos_ = e->pos();
    if (e->buttons() & Qt::LeftButton) {
        const auto qx = QQuaternion::fromAxisAndAngle(1, 0, 0, d.y() * 0.45f);
        const auto qy = QQuaternion::fromAxisAndAngle(0, 1, 0, d.x() * 0.45f);
        rot_ = (qy * qx * rot_).normalized();
        refresh();
    } else if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) {
        const float s = 2.0f * distance() * std::tan(kFovDeg * 3.14159265f / 360.0f) / std::max(1, height());
        pan_ += QVector2D(d.x() * s, -d.y() * s);
        refresh();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    setCursor(Qt::OpenHandCursor);
    if (e->button() == Qt::LeftButton && (e->pos() - pressPos_).manhattanLength() < 4) {
        if (pickMode_) pickFaceAt(e->pos());
        else pickAt(e->pos());
    }
}

void Viewport::mouseDoubleClickEvent(QMouseEvent*) {
    resetView();
}

void Viewport::wheelEvent(QWheelEvent* e) {
    zoom_ = std::clamp(zoom_ * std::pow(1.0015f, float(e->angleDelta().y())), 0.05f, 200.0f);
    refresh();
}

void Viewport::setPickMode(bool on) {
    pickMode_ = on;
    setCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    if (on) probeNode_ = -1;
    refresh();
}

void Viewport::clearFaceSelection() {
    if (!hasSel_) return;
    hasSel_ = false;
    selTris_.clear();
    selArea_ = 0;
    emit faceSelectionChanged(false);
    refresh();
}

void Viewport::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) clearFaceSelection();
    else QOpenGLWidget::keyPressEvent(e);
}

// Tìm các tam giác biên nằm trọn trên mặt phẳng đã chọn (dùng khi chọn mới và khi lưới được dựng lại).
void Viewport::recomputeSelection() {
    selTris_.clear();
    selArea_ = 0;
    if (!hasSel_) return;
    for (std::size_t i = 0; i < surfTris_.size(); ++i) {
        const auto& t = surfTris_[i];
        bool all = true;
        QVector3D p[3];
        for (int k = 0; k < 3; ++k) {
            const auto& nd = mesh_.nodes[t[k]];
            p[k] = QVector3D(float(nd.x), float(nd.y), float(nd.z));
            if (std::abs(double(QVector3D::dotProduct(selN_, p[k])) - selD_) > selTol_) { all = false; break; }
        }
        if (!all) continue;
        selTris_.push_back(int(i));
        selArea_ += 0.5 * double(QVector3D::crossProduct(p[1] - p[0], p[2] - p[0]).length());
    }
    if (selTris_.empty()) hasSel_ = false;
}

void Viewport::pickFaceAt(const QPoint& pos) {
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    int best = -1;
    float bestZ = 1e30f;
    const QPointF P(pos);
    for (std::size_t i = 0; i < surfTris_.size(); ++i) {
        const auto& t = surfTris_[i];
        const auto a = project(nodePos(t[0]), mvp), b = project(nodePos(t[1]), mvp), c = project(nodePos(t[2]), mvp);
        if (!a.ok || !b.ok || !c.ok) continue;
        const double det = (b.pt.y() - c.pt.y()) * (a.pt.x() - c.pt.x()) + (c.pt.x() - b.pt.x()) * (a.pt.y() - c.pt.y());
        if (std::abs(det) < 1e-9) continue;
        const double l1 = ((b.pt.y() - c.pt.y()) * (P.x() - c.pt.x()) + (c.pt.x() - b.pt.x()) * (P.y() - c.pt.y())) / det;
        const double l2 = ((c.pt.y() - a.pt.y()) * (P.x() - c.pt.x()) + (a.pt.x() - c.pt.x()) * (P.y() - c.pt.y())) / det;
        const double l3 = 1.0 - l1 - l2;
        if (l1 < -1e-6 || l2 < -1e-6 || l3 < -1e-6) continue;
        const float z = float(l1 * a.depth + l2 * b.depth + l3 * c.depth);
        if (z < bestZ) { bestZ = z; best = int(i); }
    }
    if (best < 0) {   // bấm ra ngoài vật thể = bỏ chọn
        clearFaceSelection();
        return;
    }
    const auto& t = surfTris_[best];
    QVector3D p[3];
    for (int k = 0; k < 3; ++k) {
        const auto& nd = mesh_.nodes[t[k]];
        p[k] = QVector3D(float(nd.x), float(nd.y), float(nd.z));
    }
    QVector3D n = QVector3D::crossProduct(p[1] - p[0], p[2] - p[0]);
    if (n.length() < 1e-20f) return;
    n.normalize();
    Vec3 lo, hi;
    mesh_.bounds(lo, hi);
    const double diag = std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) + (hi.z - lo.z) * (hi.z - lo.z));
    selN_ = n;
    selD_ = double(QVector3D::dotProduct(n, p[0]));
    selTol_ = std::max(1e-6 * diag, 1e-12);
    hasSel_ = true;
    recomputeSelection();
    emit faceSelectionChanged(hasSel_);
    refresh();
}

void Viewport::pickAt(const QPoint& pos) {
    if (surfNodes_.empty()) return;
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    int best = -1;
    float bestZ = 1e30f;
    for (int n : surfNodes_) {
        const auto pr = project(nodePos(n), mvp);
        if (!pr.ok) continue;
        const QPointF dlt = pr.pt - QPointF(pos);
        if (dlt.x() * dlt.x() + dlt.y() * dlt.y() > 14.0 * 14.0) continue;
        if (pr.depth < bestZ) { bestZ = pr.depth; best = n; }   // prefer the node nearest the camera
    }
    probeNode_ = best;
    if (best < 0) {
        probeText_.clear();
        refresh();
        return;
    }
    const auto& nd = mesh_.nodes[best];
    QString s = tr("Nút %1\nx=%2  y=%3  z=%4").arg(best).arg(nd.x, 0, 'g', 6).arg(nd.y, 0, 'g', 6).arg(nd.z, 0, 'g', 6);
    for (const auto& [name, v] : fields_.pointScalars)
        if (std::size_t(best) < v.size()) s += QString("\n%1 = %2").arg(QString::fromStdString(name)).arg(v[best], 0, 'g', 6);
    probeText_ = s;
    emit probed(QString(s).replace('\n', "   "));
    refresh();
}

}  // namespace fastcae::gui
