// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
// FastCAE 3D viewport: pure Qt (QOpenGLWidget) - no VTK / ParaView needed.
#pragma once
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <functional>
#include <QPainter>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QVector2D>
#include <QVector3D>
#include <array>
#include <vector>

#include "core/mesh.hpp"
#include "core/vtk_writer.hpp"

namespace fastcae::gui {

// Renders the boundary surface of a tetrahedral mesh with scalar colouring,
// optional deformation, mesh edges, orbit/pan/zoom camera, colour bar, axis
// triad and a click-to-probe tool. Needs OpenGL 2.1 (GLSL 1.20) only.
class Viewport : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    void setModel(const Mesh& mesh, const VtkFields& fields);
    // Thay trường (cùng lưới) mà KHÔNG đặt lại camera/trường đang chọn: dùng cho chuỗi bước thời gian.
    void setFields(const VtkFields& fields);
    // Nếu có và trả true: dùng khoảng màu cố định [lo,hi] cho trường đó (so sánh các bước cùng một thang).
    void setColorRangeProvider(std::function<bool(const QString&, double&, double&)> fn);
    QStringList scalarNames() const;
    void showScalar(const QString& name);      // empty = uniform colour
    void setDeformationScale(double s);        // 0 = undeformed
    void setEdgesVisible(bool on);
    std::size_t surfaceEdgeCount() const { return surfEdges_.size(); }   // số cạnh bề mặt (để tự tắt cạnh khi lưới dày)
    bool hasDisplacement() const { return dispField_ != nullptr; }
    void resetView();
    void setStandardView(int which);           // 0 iso, 1 front, 2 right, 3 top, 4 back, 5 left, 6 bottom
    void fitAll();                             // keep orientation, reset zoom/pan
    void setDarkTheme(bool dark);              // viewport background + overlay colours
    void setWatermark(const QString& text);    // small credit line (author) in the corner

    // Chọn mặt phẳng bằng chuột (kiểu SolidWorks): bấm lên một mặt -> chọn mọi tam giác biên cùng mặt phẳng.
    void setPickMode(bool on);
    bool pickMode() const { return pickMode_; }
    void clearFaceSelection();
    bool hasFaceSelection() const { return hasSel_; }
    QVector3D selectionNormal() const { return selN_; }
    double selectionOffset() const { return selD_; }       // n·x = d
    double selectionTolerance() const { return selTol_; }  // dung sai tuyệt đối
    int selectionTriangles() const { return int(selTris_.size()); }
    double selectionArea() const { return selArea_; }
    void paintOverlay(QPainter& p);            // colour bar, axis triad, probe box (called by the overlay widget)

signals:
    void probed(const QString& text);
    void faceSelectionChanged(bool hasSelection);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void refresh();
    struct Projected { QPointF pt; float depth; bool ok; };

    void extractSurface();
    void fillBuffers();
    float distance() const;
    int viewW() const;   // width of the 3D area (a strip on the right is reserved for the colour bar)
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projMatrix() const;
    QVector3D nodePos(int i) const;
    Projected project(const QVector3D& p, const QMatrix4x4& mvp) const;
    void pickAt(const QPoint& pos);
    void pickFaceAt(const QPoint& pos);
    void recomputeSelection();

    Mesh mesh_;
    VtkFields fields_;
    QString current_;
    double warp_ = 0;
    bool edges_ = false;
    bool dirty_ = true;
    bool hasScalar_ = false;
    std::function<bool(const QString&, double&, double&)> rangeFn_;
    double lo_ = 0, hi_ = 1;

    std::vector<std::array<int, 3>> surfTris_;   // boundary triangles (node ids)
    std::vector<int> surfNodes_;                 // unique boundary nodes
    std::vector<std::array<int, 2>> surfEdges_;  // unique boundary edges
    std::vector<float> triVerts_, lineVerts_;    // pos(3) disp(3) value(1) per vertex
    const std::vector<double>* dispField_ = nullptr;

    QVector3D center_{0, 0, 0};
    float radius_ = 1;

    bool pickMode_ = false;
    bool hasSel_ = false;
    QVector3D selN_{1, 0, 0};
    double selD_ = 0, selTol_ = 1e-9, selArea_ = 0;
    std::vector<int> selTris_;                   // chỉ số trong surfTris_
    bool dark_ = true;
    QString watermark_;
    QWidget* overlay_ = nullptr;
    QOpenGLShaderProgram prog_;
    QOpenGLBuffer triVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer lineVbo_{QOpenGLBuffer::VertexBuffer};
    bool glReady_ = false;

    QQuaternion rot_;
    float zoom_ = 1;
    QVector2D pan_{0, 0};
    QPoint lastPos_, pressPos_;
    int probeNode_ = -1;
    QString probeText_;
};

}  // namespace fastcae::gui
