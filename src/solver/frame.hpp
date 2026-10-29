// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Solver khung/dàn 3D tuyến tính: dầm Euler-Bernoulli (12 bậc tự do), thanh dàn, lò xo, cáp chỉ chịu kéo.
// Đơn vị SI. Trục địa phương của thanh: x dọc thanh; y = Z_chung x x; z = x x y
// (thanh nằm ngang theo X thì y = Y chung, z = Z chung hướng lên). Thanh thẳng đứng dùng X chung làm vector tham chiếu.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "core/mesh.hpp"
#include "core/vtk_writer.hpp"

namespace fastcae {

enum class MemberKind { Beam, Truss, Spring, Cable };

struct FrameSection {
    std::string name;
    std::string kind = "general";   // rect circle pipe ibeam box general
    double A = 0, Iy = 0, Iz = 0, J = 0;   // Iy: uốn quanh trục y địa phương (võng theo z)
    double width = 0, height = 0;          // kích thước bao (y, z) cho hiển thị và ứng suất
};

// kind: rect b h | circle d | pipe d t | ibeam b h tf tw | box b h t | general A Iy Iz J
bool makeSection(const std::string& name, const std::string& kind, const std::vector<double>& p, FrameSection& out,
                 std::string& err);

struct FrameMember {
    int n1 = 0, n2 = 0;             // chỉ số nút (0-based)
    int section = 0;
    MemberKind kind = MemberKind::Beam;
    double k = 0;                   // độ cứng lò xo [N/m] khi kind = Spring
    bool pin1 = false, pin2 = false;   // khớp bản lề (giải phóng mômen uốn) ở đầu 1 / đầu 2 của dầm
};

struct FrameSupport {
    int node = 0;
    std::array<bool, 6> fixed{};    // ux uy uz rx ry rz
};

struct FrameSpringSupport {
    int node = 0;
    std::array<double, 6> k{};      // N/m, N·m/rad (gối đàn hồi / nền Winkler rời rạc)
};

struct FrameNodalLoad {
    int node = 0;
    std::array<double, 6> f{};      // Fx Fy Fz Mx My Mz (hệ chung)
};

struct FrameMemberLoad {
    int member = 0;
    Vec3 q;                         // tải phân bố đều theo hệ chung [N/m]
};

struct FrameNodalMass {
    int node = 0;
    double m = 0;                   // kg, khối lượng tập trung (ba phương tịnh tiến)
};

struct FrameModel {
    std::vector<Vec3> nodes;
    std::vector<FrameSection> sections;
    std::vector<FrameMember> members;
    std::vector<FrameSupport> supports;
    std::vector<FrameSpringSupport> springSupports;
    std::vector<FrameNodalLoad> nodalLoads;
    std::vector<FrameMemberLoad> memberLoads;
    std::vector<FrameNodalMass> masses;
    double E = 210e9, nu = 0.3, rho = 7850;
    double gravity = 0;             // m/s^2, tác dụng theo -Z (trọng lượng bản thân)
    double yield = 0;               // giới hạn chảy [Pa]; > 0 thì tính hệ số sử dụng
};

struct FrameMemberResult {
    // Nội lực đầu thanh trong hệ địa phương: N Vy Vz T My Mz (lực nút lên phần tử), đầu 1 và đầu 2.
    std::array<double, 6> f1{}, f2{};
    double length = 0;
    Vec3 ex, ey, ez;                // trục địa phương
    double maxN = 0, maxM = 0, maxV = 0;   // giá trị tuyệt đối lớn nhất dọc thanh (N, mômen hợp, cắt hợp)
    double maxStress = 0;           // ứng suất pháp lớn nhất N/A + |My| z/Iy + |Mz| y/Iz [Pa]
    bool active = true;             // cáp chùng (không chịu nén) -> false
};

struct FrameResult {
    bool ok = false;
    std::string message;
    std::vector<std::array<double, 6>> disp;       // theo nút
    std::vector<std::array<double, 6>> reactions;  // tại nút có liên kết (0 ở nơi khác)
    std::vector<FrameMemberResult> members;
    double maxDisp = 0, maxMoment = 0, maxAxial = 0, maxStress = 0, maxUtilization = 0;
    int cableIterations = 0;
};

FrameResult solveFrame(const FrameModel& m);

// Phân tích trị riêng tuyến tính:
//   buckling = false: dao động riêng K φ = ω² M φ (khối lượng nhất quán của dầm/thanh, cộng khối lượng tập trung `nmass`);
//                     values = ω² [rad²/s²] tăng dần.
//   buckling = true : mất ổn định tuyến tính K φ = λ (-K_G) φ với K_G dựng từ lực dọc của lời giải tĩnh; values = hệ số tải tới hạn
//                     λ > 0 tăng dần (tải tới hạn = λ x tải đã đặt). Chỉ dầm (beam) mới mất ổn định uốn; thanh dàn thì không.
// shapes[k][nút] = 6 thành phần, chuẩn hoá để chuyển vị tịnh tiến lớn nhất = 1.
struct FrameEigenResult {
    bool ok = false;
    std::string message;
    bool buckling = false;
    std::vector<double> values;
    std::vector<std::vector<std::array<double, 6>>> shapes;
};
FrameEigenResult solveFrameEigen(const FrameModel& m, int nModes, bool buckling);

// Nội lực tại vị trí x (0..L) dọc thanh: {N, Vy, Vz, T, My, Mz}; q là tải phân bố địa phương (qx, qy, qz).
std::array<double, 6> memberForceAt(const FrameMemberResult& r, double x, const Vec3& qLocal);

// Dựng lưới 3D để hiển thị (mỗi thanh là lăng trụ theo tiết diện bao) và các trường: displacement,
// displacement_magnitude, axial_force, bending_moment, shear_force, normal_stress, utilization.
struct FrameRender {
    Mesh mesh;
    VtkFields fields;
};
FrameRender buildFrameRender(const FrameModel& m, const FrameResult& r, double deformScale = 0);

// Khớp bản lề: member = n1 n2 section beam pin1 pin2 (từ khoá pin1, pin2 hoặc pinned = cả hai đầu).
// Đọc mô hình từ các dòng case: node / section / member / support / ksupport / nload / udl / gravity / E / nu / rho / yield.
class CaseFile;
bool readFrameModel(const CaseFile& cf, FrameModel& out, std::string& err);

}  // namespace fastcae
