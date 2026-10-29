// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Bộ giải ràng buộc hình học 2D cho phác thảo (điểm, đường thẳng, đường tròn).
// Giải hệ phương trình phi tuyến bằng Levenberg-Marquardt (Jacobi sai phân trung tâm), đếm bậc tự do bằng hạng của Jacobi,
// phát hiện hệ mâu thuẫn (không hội tụ) và ràng buộc dư (hệ nhất quán nhưng hạng < số phương trình).
#pragma once
#include <string>
#include <vector>

namespace fastcae {

enum class SketchCon {
    Fix,            // điểm a cố định tại (value, value2)
    Coincident,     // điểm a trùng điểm b
    Horizontal,     // điểm a, b cùng y
    Vertical,       // điểm a, b cùng x
    Distance,       // |ab| = value
    Parallel,       // đường a // đường b
    Perpendicular,  // đường a vuông góc đường b
    EqualLength,    // |đường a| = |đường b|
    Angle,          // góc từ đường a đến đường b = value (radian)
    Radius,         // bán kính tròn a = value
    PointOnLine,    // điểm a nằm trên đường (vô hạn) b
    PointOnCircle,  // điểm a nằm trên đường tròn b
    Tangent,        // đường a tiếp xúc đường tròn b (khoảng cách từ tâm đến đường = bán kính)
    TangentAtPoint, // như Tangent nhưng đường đi qua điểm tiếp xúc (đầu mút chung, chỉ số điểm = value): vuông góc với bán kính tại điểm đó
    Midpoint,       // điểm a là trung điểm đường b
    EqualRadius,    // tròn a và tròn b cùng bán kính
};

struct SketchConstraint {
    SketchCon type;
    int a = -1, b = -1;
    double value = 0, value2 = 0;
    std::string label;   // để báo lỗi
};

struct SketchSolveResult {
    bool ok = false;
    std::string message;
    int dof = 0;          // số bậc tự do còn lại
    int redundant = 0;    // số phương trình dư (nhất quán)
    double residual = 0;
    int iterations = 0;
};

class SketchSystem {
public:
    int addPoint(double x, double y);
    int addLine(int p, int q);
    int addCircle(int centerPoint, double radius);
    void add(const SketchConstraint& c) { cons_.push_back(c); }

    SketchSolveResult solve();   // sửa tọa độ tại chỗ
    // Trước khi solve() trả tọa độ ban đầu; sau đó trả nghiệm.
    double px(int p) const { return laidOut_ ? x_[2 * p] : pts_[2 * p]; }
    double py(int p) const { return laidOut_ ? x_[2 * p + 1] : pts_[2 * p + 1]; }
    double radius(int c) const { return laidOut_ ? x_[radiusIndex_[c]] : radii0_[c]; }
    int numPoints() const { return static_cast<int>(x_.size() ? pointCount_ : 0); }

private:
    struct Line { int p, q; };
    struct Circle { int c; };
    std::vector<double> x_;           // [x0 y0 x1 y1 ...] rồi các bán kính
    std::vector<double> radii0_;
    std::vector<Line> lines_;
    std::vector<Circle> circles_;
    std::vector<int> radiusIndex_;
    std::vector<SketchConstraint> cons_;
    int pointCount_ = 0;
    std::vector<double> pts_;         // tọa độ ban đầu của điểm

    void residuals(const std::vector<double>& x, std::vector<double>& r) const;
    int rowsOf(const SketchConstraint& c) const;
    void layout();
    bool laidOut_ = false;
};

}  // namespace fastcae
