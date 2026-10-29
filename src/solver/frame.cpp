// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/frame.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/case_file.hpp"
#include "solver/sparse.hpp"

namespace fastcae {

namespace {

constexpr double kPi = 3.14159265358979323846;

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }
Vec3 unit(const Vec3& a) { const double n = norm(a); return n > 0 ? Vec3{a.x / n, a.y / n, a.z / n} : a; }

using M12 = std::array<std::array<double, 12>, 12>;

struct Axes {
    Vec3 ex, ey, ez;
    double L = 0;
};

Axes memberAxes(const FrameModel& m, const FrameMember& mb) {
    Axes a;
    const Vec3 d = sub(m.nodes[mb.n2], m.nodes[mb.n1]);
    a.L = norm(d);
    a.ex = unit(d);
    // y = Zc x x (ngang theo X: y = Y chung); thanh gần thẳng đứng: dùng Xc làm tham chiếu
    Vec3 ref = std::abs(a.ex.z) > 0.999 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
    a.ey = unit(cross(ref, a.ex));
    a.ez = cross(a.ex, a.ey);
    return a;
}

// Ma trận độ cứng địa phương của dầm 12x12.
M12 beamLocal(double E, double G, const FrameSection& s, double L) {
    M12 k{};
    const double EA = E * s.A / L, GJ = G * s.J / L;
    k[0][0] = k[6][6] = EA;
    k[0][6] = k[6][0] = -EA;
    k[3][3] = k[9][9] = GJ;
    k[3][9] = k[9][3] = -GJ;
    // uốn trong mặt xy: v, thz (quanh z) dùng Iz
    const double a1 = E * s.Iz / (L * L * L);
    const int vz[4] = {1, 5, 7, 11};
    const double bz[4][4] = {{12, 6 * L, -12, 6 * L}, {6 * L, 4 * L * L, -6 * L, 2 * L * L},
                             {-12, -6 * L, 12, -6 * L}, {6 * L, 2 * L * L, -6 * L, 4 * L * L}};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) k[vz[i]][vz[j]] += a1 * bz[i][j];
    // uốn trong mặt xz: w, thy (quanh y, thy = -dw/dx) dùng Iy
    const double a2 = E * s.Iy / (L * L * L);
    const int wy[4] = {2, 4, 8, 10};
    const double by[4][4] = {{12, -6 * L, -12, -6 * L}, {-6 * L, 4 * L * L, 6 * L, 2 * L * L},
                             {-12, 6 * L, 12, 6 * L}, {-6 * L, 2 * L * L, 6 * L, 4 * L * L}};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) k[wy[i]][wy[j]] += a2 * by[i][j];
    return k;
}

// Lực nút tương đương của tải phân bố đều địa phương (qx qy qz), 12 thành phần địa phương.
std::array<double, 12> fixedEndForces(const Vec3& q, double L) {
    std::array<double, 12> f{};
    f[0] = f[6] = q.x * L / 2;
    f[1] = f[7] = q.y * L / 2;
    f[5] = q.y * L * L / 12;
    f[11] = -q.y * L * L / 12;
    f[2] = f[8] = q.z * L / 2;
    f[4] = -q.z * L * L / 12;
    f[10] = q.z * L * L / 12;
    return f;
}

// Chuyển vị 12 thành phần: toàn cục -> địa phương (R^T áp từng khối 3).
void toLocal(const Axes& a, const double* g, double* l) {
    for (int blk = 0; blk < 4; ++blk) {
        const Vec3 v{g[3 * blk], g[3 * blk + 1], g[3 * blk + 2]};
        l[3 * blk] = dot(a.ex, v);
        l[3 * blk + 1] = dot(a.ey, v);
        l[3 * blk + 2] = dot(a.ez, v);
    }
}
void toGlobal(const Axes& a, const double* l, double* g) {
    for (int blk = 0; blk < 4; ++blk) {
        const double x = l[3 * blk], y = l[3 * blk + 1], z = l[3 * blk + 2];
        g[3 * blk] = a.ex.x * x + a.ey.x * y + a.ez.x * z;
        g[3 * blk + 1] = a.ex.y * x + a.ey.y * y + a.ez.y * z;
        g[3 * blk + 2] = a.ex.z * x + a.ey.z * y + a.ez.z * z;
    }
}

// Độ cứng toàn cục 12x12 = T^T k T, T = diag(R,R,R,R) với R có hàng là ex, ey, ez.
M12 toGlobalMatrix(const Axes& a, const M12& kl) {
    M12 kg{};
    // kg = T^T kl T: tính cột theo cột bằng cách chuyển các vector cơ sở toàn cục sang địa phương
    for (int j = 0; j < 12; ++j) {
        double e[12] = {0}, el[12], col[12], cg[12];
        e[j] = 1;
        toLocal(a, e, el);
        for (int i = 0; i < 12; ++i) {
            double s = 0;
            for (int c = 0; c < 12; ++c) s += kl[i][c] * el[c];
            col[i] = s;
        }
        toGlobal(a, col, cg);
        for (int i = 0; i < 12; ++i) kg[i][j] = cg[i];
    }
    return kg;
}

// Ngưng tụ tĩnh các bậc tự do địa phương r (giải phóng mômen ở đầu thanh): K' = K - K[:,r] Krr^-1 K[r,:]
void condenseReleases(M12& k, std::array<double, 12>& f, const std::vector<int>& r) {
    const int m = static_cast<int>(r.size());
    if (m == 0) return;
    std::vector<std::vector<double>> a(m, std::vector<double>(m));
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < m; ++j) a[i][j] = k[r[i]][r[j]];
    // nghịch đảo ma trận nhỏ bằng Gauss-Jordan
    std::vector<std::vector<double>> inv(m, std::vector<double>(m, 0.0));
    for (int i = 0; i < m; ++i) inv[i][i] = 1;
    for (int col = 0; col < m; ++col) {
        int piv = col;
        for (int i = col + 1; i < m; ++i)
            if (std::abs(a[i][col]) > std::abs(a[piv][col])) piv = i;
        std::swap(a[col], a[piv]);
        std::swap(inv[col], inv[piv]);
        const double d = a[col][col];
        if (std::abs(d) < 1e-300) return;
        for (int j = 0; j < m; ++j) { a[col][j] /= d; inv[col][j] /= d; }
        for (int i = 0; i < m; ++i) {
            if (i == col) continue;
            const double s = a[i][col];
            if (s == 0) continue;
            for (int j = 0; j < m; ++j) { a[i][j] -= s * a[col][j]; inv[i][j] -= s * inv[col][j]; }
        }
    }
    M12 kn = k;
    std::array<double, 12> fn = f;
    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 12; ++j) {
            double s = 0;
            for (int p = 0; p < m; ++p)
                for (int q = 0; q < m; ++q) s += k[i][r[p]] * inv[p][q] * k[r[q]][j];
            kn[i][j] = k[i][j] - s;
        }
        double s = 0;
        for (int p = 0; p < m; ++p)
            for (int q = 0; q < m; ++q) s += k[i][r[p]] * inv[p][q] * f[r[q]];
        fn[i] = f[i] - s;
    }
    for (int p : r) {
        for (int j = 0; j < 12; ++j) kn[p][j] = kn[j][p] = 0;
        fn[p] = 0;
    }
    k = kn;
    f = fn;
}

std::vector<int> releaseDofs(const FrameMember& mb) {
    std::vector<int> r;
    if (mb.pin1) { r.push_back(4); r.push_back(5); }
    if (mb.pin2) { r.push_back(10); r.push_back(11); }
    return r;
}

M12 trussLocal(double EAoverL) {
    M12 k{};
    k[0][0] = k[6][6] = EAoverL;
    k[0][6] = k[6][0] = -EAoverL;
    return k;
}

double sectionStressFactorY(const FrameSection& s) { return 0.5 * s.width; }
double sectionStressFactorZ(const FrameSection& s) { return 0.5 * s.height; }

struct Assembled {
    CsrMatrix K;
    std::vector<double> F;
};

}  // namespace

bool makeSection(const std::string& name, const std::string& kind, const std::vector<double>& p, FrameSection& s, std::string& err) {
    s = {};
    s.name = name;
    s.kind = kind;
    auto need = [&](std::size_t n) {
        if (p.size() < n) {
            err = "Tiet dien '" + kind + "' can " + std::to_string(n) + " tham so";
            return false;
        }
        for (std::size_t i = 0; i < n; ++i)
            if (!(p[i] > 0)) {
                err = "Tham so tiet dien phai duong";
                return false;
            }
        return true;
    };
    if (kind == "rect") {
        if (!need(2)) return false;
        const double b = p[0], h = p[1];
        s.A = b * h;
        s.Iy = b * h * h * h / 12;
        s.Iz = h * b * b * b / 12;
        const double a = std::max(b, h), c = std::min(b, h);
        s.J = a * c * c * c * (1.0 / 3 - 0.21 * (c / a) * (1 - std::pow(c, 4) / (12 * std::pow(a, 4))));
        s.width = b;
        s.height = h;
    } else if (kind == "circle") {
        if (!need(1)) return false;
        const double d = p[0];
        s.A = kPi * d * d / 4;
        s.Iy = s.Iz = kPi * std::pow(d, 4) / 64;
        s.J = kPi * std::pow(d, 4) / 32;
        s.width = s.height = d;
    } else if (kind == "pipe") {
        if (!need(2)) return false;
        const double d = p[0], t = p[1], di = d - 2 * t;
        if (di <= 0) { err = "Day ong qua lon"; return false; }
        s.A = kPi * (d * d - di * di) / 4;
        s.Iy = s.Iz = kPi * (std::pow(d, 4) - std::pow(di, 4)) / 64;
        s.J = kPi * (std::pow(d, 4) - std::pow(di, 4)) / 32;
        s.width = s.height = d;
    } else if (kind == "ibeam") {
        if (!need(4)) return false;
        const double b = p[0], h = p[1], tf = p[2], tw = p[3], hw = h - 2 * tf;
        if (hw <= 0 || tw >= b) { err = "Kich thuoc tiet dien chu I khong hop le"; return false; }
        s.A = 2 * b * tf + hw * tw;
        s.Iy = (b * h * h * h - (b - tw) * hw * hw * hw) / 12;
        s.Iz = (2 * tf * b * b * b + hw * tw * tw * tw) / 12;
        s.J = (2 * b * tf * tf * tf + hw * tw * tw * tw) / 3;
        s.width = b;
        s.height = h;
    } else if (kind == "box") {
        if (!need(3)) return false;
        const double b = p[0], h = p[1], t = p[2];
        if (2 * t >= b || 2 * t >= h) { err = "Day qua lon"; return false; }
        s.A = b * h - (b - 2 * t) * (h - 2 * t);
        s.Iy = (b * h * h * h - (b - 2 * t) * std::pow(h - 2 * t, 3)) / 12;
        s.Iz = (h * b * b * b - (h - 2 * t) * std::pow(b - 2 * t, 3)) / 12;
        const double Am = (b - t) * (h - t), per = 2 * ((b - t) + (h - t));
        s.J = 4 * Am * Am * t / per;
        s.width = b;
        s.height = h;
    } else if (kind == "general") {
        if (!need(4)) return false;
        s.A = p[0];
        s.Iy = p[1];
        s.Iz = p[2];
        s.J = p[3];
        s.width = s.height = std::sqrt(s.A);
    } else {
        err = "Loai tiet dien khong ho tro: " + kind + " (rect, circle, pipe, ibeam, box, general)";
        return false;
    }
    return true;
}

std::array<double, 6> memberForceAt(const FrameMemberResult& r, double x, const Vec3& q) {
    const auto& f = r.f1;
    std::array<double, 6> o{};
    o[0] = -f[0] - q.x * x;
    o[1] = -f[1] - q.y * x;
    o[2] = -f[2] - q.z * x;
    o[3] = -f[3];
    o[4] = -f[4] - x * f[2] - q.z * x * x / 2;
    o[5] = -f[5] + x * f[1] + q.y * x * x / 2;
    return o;
}

FrameResult solveFrame(const FrameModel& model) {
    FrameResult res;
    const int nn = static_cast<int>(model.nodes.size());
    const int nm = static_cast<int>(model.members.size());
    if (nn < 2 || nm < 1) {
        res.message = "Can it nhat 2 nut va 1 thanh";
        return res;
    }
    const int ndof = 6 * nn;
    if (model.supports.empty() && model.springSupports.empty()) {
        res.message = "Thieu lien ket (support): ket cau chuyen dong tu do";
        return res;
    }
    const double G = model.E / (2 * (1 + model.nu));

    std::vector<Axes> axes(static_cast<std::size_t>(nm));
    std::vector<Vec3> qLocal(static_cast<std::size_t>(nm));
    std::vector<char> active(static_cast<std::size_t>(nm), 1);
    for (int e = 0; e < nm; ++e) {
        const auto& mb = model.members[e];
        if (mb.n1 < 0 || mb.n2 < 0 || mb.n1 >= nn || mb.n2 >= nn || mb.n1 == mb.n2) {
            res.message = "Thanh " + std::to_string(e + 1) + " co chi so nut khong hop le";
            return res;
        }
        if (mb.section < 0 || mb.section >= static_cast<int>(model.sections.size())) {
            res.message = "Thanh " + std::to_string(e + 1) + " thieu tiet dien";
            return res;
        }
        axes[e] = memberAxes(model, mb);
        if (axes[e].L < 1e-12) {
            res.message = "Thanh " + std::to_string(e + 1) + " co chieu dai bang 0";
            return res;
        }
    }

    // tải phân bố tổng hợp từng thanh (hệ chung) = tải người dùng + trọng lượng bản thân
    std::vector<Vec3> qGlobal(static_cast<std::size_t>(nm));
    for (const auto& ml : model.memberLoads) {
        if (ml.member < 0 || ml.member >= nm) { res.message = "udl tham chieu thanh khong ton tai"; return res; }
        qGlobal[ml.member].x += ml.q.x;
        qGlobal[ml.member].y += ml.q.y;
        qGlobal[ml.member].z += ml.q.z;
    }
    if (model.gravity != 0)
        for (int e = 0; e < nm; ++e) {
            const auto& mb = model.members[e];
            if (mb.kind == MemberKind::Spring) continue;
            qGlobal[e].z -= model.rho * model.sections[mb.section].A * model.gravity;
        }
    for (int e = 0; e < nm; ++e) {
        qLocal[e] = {dot(qGlobal[e], axes[e].ex), dot(qGlobal[e], axes[e].ey), dot(qGlobal[e], axes[e].ez)};
        // thanh dàn / lò xo / cáp không chịu uốn: tải phân bố quy thành lực nút (nửa nửa), không có mômen
    }

    std::vector<std::array<double, 6>> disp(static_cast<std::size_t>(nn));
    std::vector<double> u;
    CsrMatrix Kfull;
    std::vector<double> Fext;
    std::vector<char> fixedDof(static_cast<std::size_t>(ndof), 0);
    int iterations = 0;
    bool converged = false;

    const std::string prevSolver = linearSolverName();
    setLinearSolver("direct");   // khung nhỏ/vừa: giải trực tiếp (chắc chắn hơn CG với ma trận nhiều thang đo)

    for (iterations = 1; iterations <= 30; ++iterations) {
        std::vector<Triplet> tr;
        Fext.assign(static_cast<std::size_t>(ndof), 0.0);
        std::vector<char> hasBeam(static_cast<std::size_t>(nn), 0);
        for (int e = 0; e < nm; ++e) {
            const auto& mb = model.members[e];
            if (!active[e]) continue;
            const auto& sec = model.sections[mb.section];
            const Axes& a = axes[e];
            M12 kl{};
            if (mb.kind == MemberKind::Beam) {
                kl = beamLocal(model.E, G, sec, a.L);
                hasBeam[mb.n1] = hasBeam[mb.n2] = 1;
            } else if (mb.kind == MemberKind::Spring) {
                kl = trussLocal(mb.k);
            } else {
                kl = trussLocal(model.E * sec.A / a.L);
            }
            std::array<double, 12> flc = fixedEndForces(qLocal[e], a.L);
            if (mb.kind != MemberKind::Beam)
                for (int i : {4, 5, 10, 11}) flc[i] = 0;
            if (mb.kind == MemberKind::Beam) condenseReleases(kl, flc, releaseDofs(mb));
            const M12 kg = toGlobalMatrix(a, kl);
            int dofs[12];
            for (int i = 0; i < 6; ++i) {
                dofs[i] = 6 * mb.n1 + i;
                dofs[6 + i] = 6 * mb.n2 + i;
            }
            for (int i = 0; i < 12; ++i)
                for (int j = 0; j < 12; ++j)
                    if (kg[i][j] != 0.0) tr.push_back({dofs[i], dofs[j], kg[i][j]});
            // lực nút tương đương của tải phân bố (đã ngưng tụ nếu có khớp bản lề)
            double fg[12];
            toGlobal(a, flc.data(), fg);
            for (int i = 0; i < 12; ++i) Fext[dofs[i]] += fg[i];
        }
        for (const auto& sp : model.springSupports)
            for (int i = 0; i < 6; ++i)
                if (sp.k[i] > 0) tr.push_back({6 * sp.node + i, 6 * sp.node + i, sp.k[i]});
        for (const auto& nl : model.nodalLoads)
            for (int i = 0; i < 6; ++i) Fext[6 * nl.node + i] += nl.f[i];

        // bậc tự do bị khoá: liên kết + bậc quay của nút không có dầm + bậc không có độ cứng
        std::fill(fixedDof.begin(), fixedDof.end(), 0);
        for (const auto& s : model.supports)
            for (int i = 0; i < 6; ++i)
                if (s.fixed[i]) fixedDof[6 * s.node + i] = 1;
        for (int n = 0; n < nn; ++n)
            if (!hasBeam[n])
                for (int i = 3; i < 6; ++i) fixedDof[6 * n + i] = 1;
        Kfull = buildCsr(ndof, tr);
        const auto diag = Kfull.diagonal();
        double dmax = 0;
        for (double d : diag) dmax = std::max(dmax, std::abs(d));
        for (int i = 0; i < ndof; ++i)
            if (std::abs(diag[i]) < 1e-12 * dmax) fixedDof[i] = 1;   // thanh không nối vào bậc này (nút tự do hoặc cáp chùng)

        CsrMatrix K = Kfull;
        std::vector<double> rhs = Fext;
        std::vector<int> fd;
        for (int i = 0; i < ndof; ++i)
            if (fixedDof[i]) fd.push_back(i);
        applyDirichlet(K, rhs, fd, std::vector<double>(fd.size(), 0.0));
        std::vector<double> x(static_cast<std::size_t>(ndof), 0.0);
        const CgResult cg = solveCg(K, rhs, x, 1e-12, 20000);
        if (!cg.converged) {
            setLinearSolver(prevSolver);
            res.message = "Khong giai duoc: ket cau co the la co cau (thieu lien ket hoac thanh) hoac ma tran suy bien";
            return res;
        }
        u = x;

        // cáp: bỏ các cáp đang chịu nén rồi giải lại
        bool changed = false;
        for (int e = 0; e < nm; ++e) {
            const auto& mb = model.members[e];
            if (mb.kind != MemberKind::Cable || !active[e]) continue;
            const auto& sec = model.sections[mb.section];
            const Vec3 d{u[6 * mb.n2] - u[6 * mb.n1], u[6 * mb.n2 + 1] - u[6 * mb.n1 + 1], u[6 * mb.n2 + 2] - u[6 * mb.n1 + 2]};
            const double elong = dot(d, axes[e].ex);
            if (model.E * sec.A / axes[e].L * elong < -1e-9) {
                active[e] = 0;
                changed = true;
            }
        }
        if (!changed) {
            converged = true;
            break;
        }
    }
    setLinearSolver(prevSolver);
    if (!converged) {
        res.message = "Cap chung khong hoi tu sau 30 vong lap";
        return res;
    }
    res.cableIterations = iterations;

    // chuyển vị, phản lực
    res.disp.assign(static_cast<std::size_t>(nn), {});
    for (int n = 0; n < nn; ++n)
        for (int i = 0; i < 6; ++i) res.disp[n][i] = u[6 * n + i];
    std::vector<double> Ku(static_cast<std::size_t>(ndof));
    Kfull.multiply(u, Ku);
    res.reactions.assign(static_cast<std::size_t>(nn), {});
    for (int i = 0; i < ndof; ++i)
        if (fixedDof[i]) res.reactions[i / 6][i % 6] = Ku[i] - Fext[i];

    // nội lực thanh
    res.members.resize(static_cast<std::size_t>(nm));
    for (int e = 0; e < nm; ++e) {
        const auto& mb = model.members[e];
        const auto& sec = model.sections[mb.section];
        FrameMemberResult& mr = res.members[e];
        const Axes& a = axes[e];
        mr.length = a.L;
        mr.ex = a.ex;
        mr.ey = a.ey;
        mr.ez = a.ez;
        mr.active = active[e] != 0;
        if (!mr.active) continue;
        double dg[12], dl[12];
        for (int i = 0; i < 6; ++i) {
            dg[i] = u[6 * mb.n1 + i];
            dg[6 + i] = u[6 * mb.n2 + i];
        }
        toLocal(a, dg, dl);
        M12 kl{};
        if (mb.kind == MemberKind::Beam) kl = beamLocal(model.E, G, sec, a.L);
        else if (mb.kind == MemberKind::Spring) kl = trussLocal(mb.k);
        else kl = trussLocal(model.E * sec.A / a.L);
        std::array<double, 12> fef = fixedEndForces(qLocal[e], a.L);
        if (mb.kind != MemberKind::Beam)
            for (int i : {4, 5, 10, 11}) fef[i] = 0;
        if (mb.kind == MemberKind::Beam) condenseReleases(kl, fef, releaseDofs(mb));
        double f[12];
        for (int i = 0; i < 12; ++i) {
            double s = 0;
            for (int j = 0; j < 12; ++j) s += kl[i][j] * dl[j];
            f[i] = s - fef[i];
        }
        for (int i = 0; i < 6; ++i) {
            mr.f1[i] = f[i];
            mr.f2[i] = f[6 + i];
        }
        // cực trị dọc thanh (21 điểm) và ứng suất pháp
        const int ns = 20;
        for (int s = 0; s <= ns; ++s) {
            const double x = a.L * s / ns;
            const auto fo = memberForceAt(mr, x, qLocal[e]);
            const double Mres = std::hypot(fo[4], fo[5]);
            const double Vres = std::hypot(fo[1], fo[2]);
            mr.maxN = std::max(mr.maxN, std::abs(fo[0]));
            mr.maxM = std::max(mr.maxM, Mres);
            mr.maxV = std::max(mr.maxV, Vres);
            double sig = std::abs(fo[0]) / std::max(sec.A, 1e-300);
            if (mb.kind == MemberKind::Beam)
                sig = std::abs(fo[0]) / sec.A + std::abs(fo[4]) * sectionStressFactorZ(sec) / std::max(sec.Iy, 1e-300) +
                      std::abs(fo[5]) * sectionStressFactorY(sec) / std::max(sec.Iz, 1e-300);
            mr.maxStress = std::max(mr.maxStress, sig);
        }
        res.maxMoment = std::max(res.maxMoment, mr.maxM);
        res.maxAxial = std::max(res.maxAxial, mr.maxN);
        res.maxStress = std::max(res.maxStress, mr.maxStress);
    }
    for (int n = 0; n < nn; ++n) {
        double d = std::sqrt(res.disp[n][0] * res.disp[n][0] + res.disp[n][1] * res.disp[n][1] + res.disp[n][2] * res.disp[n][2]);
        res.maxDisp = std::max(res.maxDisp, d);
    }
    if (model.yield > 0) res.maxUtilization = res.maxStress / model.yield;
    res.ok = true;
    res.message = "Hoi tu";
    return res;
}


// ---------------------------------------------------------------- dao động riêng và mất ổn định
namespace {

M12 beamMassLocal(const FrameSection& s, double rho, double L) {
    M12 m{};
    const double mA = rho * s.A * L;
    m[0][0] = m[6][6] = mA / 3;
    m[0][6] = m[6][0] = mA / 6;
    const double mt = rho * (s.Iy + s.Iz) * L;   // quán tính xoay quanh trục thanh
    m[3][3] = m[9][9] = mt / 3;
    m[3][9] = m[9][3] = mt / 6;
    const double c = mA / 420, l = L, l2 = L * L;
    const double bz[4][4] = {{156, 22 * l, 54, -13 * l}, {22 * l, 4 * l2, 13 * l, -3 * l2}, {54, 13 * l, 156, -22 * l}, {-13 * l, -3 * l2, -22 * l, 4 * l2}};
    const double by[4][4] = {{156, -22 * l, 54, 13 * l}, {-22 * l, 4 * l2, -13 * l, -3 * l2}, {54, -13 * l, 156, 22 * l}, {13 * l, -3 * l2, 22 * l, 4 * l2}};
    const int vz[4] = {1, 5, 7, 11}, wy[4] = {2, 4, 8, 10};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            m[vz[i]][vz[j]] += c * bz[i][j];
            m[wy[i]][wy[j]] += c * by[i][j];
        }
    return m;
}

M12 trussMassLocal(double rho, double A, double L) {
    M12 m{};
    const double mA = rho * A * L;
    for (int d = 0; d < 3; ++d) {
        m[d][d] = m[6 + d][6 + d] = mA / 3;
        m[d][6 + d] = m[6 + d][d] = mA / 6;
    }
    return m;
}

// Độ cứng hình học tuyến tính hoá theo lực dọc N (kéo dương)
M12 beamGeomLocal(double N, double L) {
    M12 g{};
    const double c = N / (30 * L), l = L, l2 = L * L;
    const double gz[4][4] = {{36, 3 * l, -36, 3 * l}, {3 * l, 4 * l2, -3 * l, -l2}, {-36, -3 * l, 36, -3 * l}, {3 * l, -l2, -3 * l, 4 * l2}};
    const double gy[4][4] = {{36, -3 * l, -36, -3 * l}, {-3 * l, 4 * l2, 3 * l, -l2}, {-36, 3 * l, 36, 3 * l}, {-3 * l, -l2, 3 * l, 4 * l2}};
    const int vz[4] = {1, 5, 7, 11}, wy[4] = {2, 4, 8, 10};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            g[vz[i]][vz[j]] += c * gz[i][j];
            g[wy[i]][wy[j]] += c * gy[i][j];
        }
    return g;
}

M12 trussGeomLocal(double N, double L) {
    M12 g{};
    for (int d : {1, 2}) {
        g[d][d] = g[6 + d][6 + d] = N / L;
        g[d][6 + d] = g[6 + d][d] = -N / L;
    }
    return g;
}

using Dense = std::vector<std::vector<double>>;

// Jacobi cyclic cho ma trận đối xứng nhỏ: A -> trị riêng w, vectơ riêng là các cột của V.
void jacobiEigen(Dense A, std::vector<double>& w, Dense& V) {
    const int n = static_cast<int>(A.size());
    V.assign(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) V[i][i] = 1;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) off += A[i][j] * A[i][j];
        if (off < 1e-300) break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                if (std::abs(A[p][q]) < 1e-300) continue;
                const double theta = (A[q][q] - A[p][p]) / (2 * A[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double akp = A[k][p], akq = A[k][q];
                    A[k][p] = c * akp - s * akq;
                    A[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = A[p][k], aqk = A[q][k];
                    A[p][k] = c * apk - s * aqk;
                    A[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const double vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c * vkp - s * vkq;
                    V[k][q] = s * vkp + c * vkq;
                }
            }
    }
    w.resize(n);
    for (int i = 0; i < n; ++i) w[i] = A[i][i];
}

// A z = w B z với B đối xứng xác định dương (Cholesky + Jacobi). Trả về trị riêng tăng dần và Z (cột = vectơ riêng, z^T B z = 1).
bool smallGeneralizedEigen(const Dense& A, const Dense& B, std::vector<double>& w, Dense& Z) {
    const int n = static_cast<int>(A.size());
    Dense L(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j <= i; ++j) {
            double s = B[i][j];
            for (int k = 0; k < j; ++k) s -= L[i][k] * L[j][k];
            if (i == j) {
                if (!(s > 0)) return false;
                L[i][i] = std::sqrt(s);
            } else {
                L[i][j] = s / L[j][j];
            }
        }
    // C = L^-1 A L^-T
    Dense T(n, std::vector<double>(n, 0.0)), C(n, std::vector<double>(n, 0.0));
    for (int col = 0; col < n; ++col)   // T = L^-1 A
        for (int i = 0; i < n; ++i) {
            double s = A[i][col];
            for (int k = 0; k < i; ++k) s -= L[i][k] * T[k][col];
            T[i][col] = s / L[i][i];
        }
    for (int row = 0; row < n; ++row)   // C = T L^-T  (resolve C L^T = T)
        for (int j = 0; j < n; ++j) {
            double s = T[row][j];
            for (int k = 0; k < j; ++k) s -= C[row][k] * L[j][k];
            C[row][j] = s / L[j][j];
        }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) C[i][j] = C[j][i] = 0.5 * (C[i][j] + C[j][i]);
    std::vector<double> ww;
    Dense Y;
    jacobiEigen(C, ww, Y);
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return ww[a] < ww[b]; });
    w.resize(n);
    Z.assign(n, std::vector<double>(n, 0.0));
    for (int c = 0; c < n; ++c) {
        const int src = idx[c];
        w[c] = ww[src];
        // z = L^-T y
        for (int i = n - 1; i >= 0; --i) {
            double s = Y[i][src];
            for (int k = i + 1; k < n; ++k) s -= L[k][i] * Z[k][c];
            Z[i][c] = s / L[i][i];
        }
    }
    return true;
}

}  // namespace

FrameEigenResult solveFrameEigen(const FrameModel& model, int nev, bool buckling) {
    FrameEigenResult out;
    out.buckling = buckling;
    const int nn = static_cast<int>(model.nodes.size());
    const int nm = static_cast<int>(model.members.size());
    if (nn < 2 || nm < 1) { out.message = "Can it nhat 2 nut va 1 thanh"; return out; }
    if (model.supports.empty() && model.springSupports.empty()) { out.message = "Thieu lien ket (support)"; return out; }
    nev = std::max(1, nev);
    const double G = model.E / (2 * (1 + model.nu));
    const int ndof = 6 * nn;

    // lực dọc từng thanh từ lời giải tĩnh (cho mất ổn định)
    std::vector<double> axial(static_cast<std::size_t>(nm), 0.0);
    std::vector<char> active(static_cast<std::size_t>(nm), 1);
    if (buckling) {
        const FrameResult st = solveFrame(model);
        if (!st.ok) { out.message = "Khong giai duoc bai toan tinh: " + st.message; return out; }
        bool anyCompression = false;
        for (int e = 0; e < nm; ++e) {
            active[e] = st.members[e].active;
            axial[e] = 0.5 * (-st.members[e].f1[0] + st.members[e].f2[0]);
            if (axial[e] < -1e-9) anyCompression = true;
        }
        if (!anyCompression) { out.message = "Khong co thanh nao chiu nen: khong co nguy co mat on dinh tuyen tinh"; return out; }
    }

    std::vector<Triplet> tk, tm, tg;
    std::vector<char> hasBeam(static_cast<std::size_t>(nn), 0);
    for (int e = 0; e < nm; ++e) {
        const auto& mb = model.members[e];
        if (mb.n1 < 0 || mb.n2 < 0 || mb.n1 >= nn || mb.n2 >= nn || mb.n1 == mb.n2) { out.message = "Chi so nut thanh khong hop le"; return out; }
        if (!active[e]) continue;
        const auto& sec = model.sections[mb.section];
        const Axes a = memberAxes(model, mb);
        if (a.L < 1e-12) { out.message = "Thanh co chieu dai bang 0"; return out; }
        M12 kl{}, ml{}, gl{};
        if (mb.kind == MemberKind::Beam) {
            kl = beamLocal(model.E, G, sec, a.L);
            ml = beamMassLocal(sec, model.rho, a.L);
            gl = beamGeomLocal(axial[e], a.L);
            hasBeam[mb.n1] = hasBeam[mb.n2] = 1;
            std::array<double, 12> dummy{};
            const auto rel = releaseDofs(mb);
            if (!rel.empty()) {   // bản lề: ngưng tụ cả độ cứng, khối lượng (xấp xỉ) và độ cứng hình học
                condenseReleases(kl, dummy, rel);
                condenseReleases(gl, dummy, rel);
            }
        } else if (mb.kind == MemberKind::Spring) {
            kl = trussLocal(mb.k);
        } else {
            kl = trussLocal(model.E * sec.A / a.L);
            ml = trussMassLocal(model.rho, sec.A, a.L);
            gl = trussGeomLocal(axial[e], a.L);
        }
        const M12 kg = toGlobalMatrix(a, kl), mg = toGlobalMatrix(a, ml), gg = toGlobalMatrix(a, gl);
        int dofs[12];
        for (int i = 0; i < 6; ++i) { dofs[i] = 6 * mb.n1 + i; dofs[6 + i] = 6 * mb.n2 + i; }
        for (int i = 0; i < 12; ++i)
            for (int j = 0; j < 12; ++j) {
                if (kg[i][j] != 0.0) tk.push_back({dofs[i], dofs[j], kg[i][j]});
                if (mg[i][j] != 0.0) tm.push_back({dofs[i], dofs[j], mg[i][j]});
                if (gg[i][j] != 0.0) tg.push_back({dofs[i], dofs[j], gg[i][j]});
            }
    }
    for (const auto& s : model.springSupports)
        for (int i = 0; i < 6; ++i)
            if (s.k[i] > 0) tk.push_back({6 * s.node + i, 6 * s.node + i, s.k[i]});
    for (const auto& ms : model.masses)
        if (ms.node >= 0 && ms.node < nn)
            for (int d = 0; d < 3; ++d) tm.push_back({6 * ms.node + d, 6 * ms.node + d, ms.m});

    // bậc tự do bị khoá: liên kết, bậc quay nút không có dầm, bậc không có độ cứng
    std::vector<char> fixedDof(static_cast<std::size_t>(ndof), 0);
    for (const auto& s : model.supports)
        for (int i = 0; i < 6; ++i)
            if (s.fixed[i]) fixedDof[6 * s.node + i] = 1;
    for (int n = 0; n < nn; ++n)
        if (!hasBeam[n])
            for (int i = 3; i < 6; ++i) fixedDof[6 * n + i] = 1;
    {
        std::vector<Triplet> copy = tk;
        const CsrMatrix Kf = buildCsr(ndof, copy);
        const auto diag = Kf.diagonal();
        double dmax = 0;
        for (double d : diag) dmax = std::max(dmax, std::abs(d));
        for (int i = 0; i < ndof; ++i)
            if (std::abs(diag[i]) < 1e-12 * dmax) fixedDof[i] = 1;
    }
    std::vector<int> red(static_cast<std::size_t>(ndof), -1);
    int n = 0;
    for (int i = 0; i < ndof; ++i)
        if (!fixedDof[i]) red[i] = n++;
    if (n == 0) { out.message = "Khong con bac tu do tu do"; return out; }
    auto reduce = [&](const std::vector<Triplet>& t) {
        std::vector<Triplet> r;
        for (const auto& x : t)
            if (red[x.r] >= 0 && red[x.c] >= 0) r.push_back({red[x.r], red[x.c], x.v});
        return buildCsr(n, r);
    };
    // chuẩn hoá dạng riêng: lớn nhất của (chuyển vị tịnh tiến, góc xoay x nửa kích thước tiết diện lớn nhất) bằng 1
    double href = 0;
    for (const auto& s : model.sections) href = std::max(href, 0.5 * std::max(s.width, s.height));
    if (href <= 0) href = 0.05;
    auto normalizeShape = [&](std::vector<std::array<double, 6>>& shape) {
        double mx = 0;
        for (const auto& s : shape) {
            mx = std::max(mx, std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]));
            mx = std::max(mx, href * std::sqrt(s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
        }
        if (mx > 0)
            for (auto& s : shape)
                for (double& v : s) v /= mx;
    };
    CsrMatrix K = reduce(tk), M = reduce(tm), Gm = reduce(tg);
    BandedCholesky chol;
    if (!chol.factor(K)) { out.message = "Ma tran do cung khong xac dinh duong (co cau hoac thieu lien ket)"; return out; }

    // toán tử lặp: dao động: M ; mất ổn định: B = -K_G
    CsrMatrix Aop = buckling ? Gm : M;
    if (buckling)
        for (auto& v : Aop.val) v = -v;
    const int nev2 = std::min(nev, n);
    const int q = std::min(n, std::max(2 * nev2, nev2 + 8));
    std::vector<std::vector<double>> X(q, std::vector<double>(static_cast<std::size_t>(n)));
    for (int j = 0; j < q; ++j)
        for (int i = 0; i < n; ++i) X[j][i] = std::sin(12.9898 * (i + 1) * (j + 1) + 78.233 * (j + 1)) + 0.1 * std::cos(0.37 * i * (j + 1));
    std::vector<double> prev(static_cast<std::size_t>(nev2), 0.0), vals;
    Dense Z;
    std::vector<std::vector<double>> Y(q, std::vector<double>(static_cast<std::size_t>(n)));
    bool converged = false;
    for (int it = 0; it < 400; ++it) {
        std::vector<std::vector<double>> R(q, std::vector<double>(static_cast<std::size_t>(n)));
        for (int j = 0; j < q; ++j) {
            Aop.multiply(X[j], R[j]);
            chol.solve(R[j], Y[j]);   // Y = K^-1 A X
        }
        // Rayleigh-Ritz: K Y = R
        std::vector<double> tmp(static_cast<std::size_t>(n));
        Dense Asub(q, std::vector<double>(q)), Bsub(q, std::vector<double>(q));
        if (!buckling) {
            for (int a = 0; a < q; ++a)
                for (int b = 0; b < q; ++b) {
                    double s = 0;
                    for (int i = 0; i < n; ++i) s += Y[a][i] * R[b][i];   // Y^T K Y
                    Asub[a][b] = s;
                }
            std::vector<std::vector<double>> MY(q, std::vector<double>(static_cast<std::size_t>(n)));
            for (int j = 0; j < q; ++j) M.multiply(Y[j], MY[j]);
            for (int a = 0; a < q; ++a)
                for (int b = 0; b < q; ++b) {
                    double s = 0;
                    for (int i = 0; i < n; ++i) s += Y[a][i] * MY[b][i];   // Y^T M Y
                    Bsub[a][b] = s;
                }
        } else {
            std::vector<std::vector<double>> BY(q, std::vector<double>(static_cast<std::size_t>(n)));
            for (int j = 0; j < q; ++j) Aop.multiply(Y[j], BY[j]);
            for (int a = 0; a < q; ++a)
                for (int b = 0; b < q; ++b) {
                    double s1 = 0, s2 = 0;
                    for (int i = 0; i < n; ++i) {
                        s1 += Y[a][i] * BY[b][i];   // Y^T B Y
                        s2 += Y[a][i] * R[b][i];    // Y^T K Y
                    }
                    Asub[a][b] = s1;
                    Bsub[a][b] = s2;
                }
        }
        for (int a = 0; a < q; ++a)
            for (int b = a + 1; b < q; ++b) {
                Asub[a][b] = Asub[b][a] = 0.5 * (Asub[a][b] + Asub[b][a]);
                Bsub[a][b] = Bsub[b][a] = 0.5 * (Bsub[a][b] + Bsub[b][a]);
            }
        std::vector<double> w;
        if (!smallGeneralizedEigen(Asub, Bsub, w, Z)) { out.message = "Khong giai duoc bai toan rieng thu gon"; return out; }
        // dao động: λ = w nhỏ nhất; mất ổn định: μ lớn nhất (λ = 1/μ). Sắp xếp cột theo thứ tự cần lấy.
        std::vector<int> pick(q);
        for (int i = 0; i < q; ++i) pick[i] = buckling ? q - 1 - i : i;
        vals.assign(static_cast<std::size_t>(nev2), 0.0);
        for (int i = 0; i < nev2; ++i) vals[i] = buckling ? (w[pick[i]] > 0 ? 1.0 / w[pick[i]] : 1e300) : w[pick[i]];
        // X = Y Z (cột đã chọn xếp trước)
        std::vector<std::vector<double>> Xn(q, std::vector<double>(static_cast<std::size_t>(n), 0.0));
        for (int c = 0; c < q; ++c)
            for (int a = 0; a < q; ++a) {
                const double z = Z[a][pick[c]];
                if (z == 0) continue;
                for (int i = 0; i < n; ++i) Xn[c][i] += Y[a][i] * z;
            }
        X.swap(Xn);
        double change = 0;
        for (int i = 0; i < nev2; ++i) change = std::max(change, std::abs(vals[i] - prev[i]) / std::max(std::abs(vals[i]), 1e-300));
        prev = vals;
        if (it >= 2 && change < 1e-11) { converged = true; break; }
    }
    (void)converged;
    if (buckling) {   // chỉ giữ hệ số tải dương (trị riêng μ dương)
        std::vector<double> keep;
        std::vector<int> idx;
        for (int i = 0; i < nev2; ++i)
            if (vals[i] > 0 && vals[i] < 1e299) { keep.push_back(vals[i]); idx.push_back(i); }
        if (keep.empty()) { out.message = "Khong tim thay he so tai toi han duong"; return out; }
        out.values = keep;
        for (int k : idx) {
            std::vector<std::array<double, 6>> shape(static_cast<std::size_t>(nn), std::array<double, 6>{});
            for (int i = 0; i < ndof; ++i)
                if (red[i] >= 0) shape[i / 6][i % 6] = X[k][red[i]];
            normalizeShape(shape);
            out.shapes.push_back(shape);
        }
    } else {
        out.values.assign(vals.begin(), vals.end());
        for (int k = 0; k < nev2; ++k) {
            std::vector<std::array<double, 6>> shape(static_cast<std::size_t>(nn), std::array<double, 6>{});
            for (int i = 0; i < ndof; ++i)
                if (red[i] >= 0) shape[i / 6][i % 6] = X[k][red[i]];
            normalizeShape(shape);
            out.shapes.push_back(shape);
        }
    }
    out.ok = true;
    out.message = "Hoi tu";
    return out;
}

// ---------------------------------------------------------------- hiển thị 3D
FrameRender buildFrameRender(const FrameModel& m, const FrameResult& r, double) {
    FrameRender out;
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const auto& p : m.nodes) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-9});
    const double minDim = 0.012 * size;   // tiết diện quá mỏng được phóng to để nhìn thấy (chỉ để hiển thị)
    std::vector<double> disp, mag, axial, moment, shear, stress, util;
    std::vector<double> vec;
    auto addNode = [&](const Vec3& p, const Vec3& d, double N, double M, double V, double sig) {
        out.mesh.nodes.push_back(p);
        vec.push_back(d.x);
        vec.push_back(d.y);
        vec.push_back(d.z);
        mag.push_back(norm(d));
        axial.push_back(N);
        moment.push_back(M);
        shear.push_back(V);
        stress.push_back(sig);
        util.push_back(m.yield > 0 ? sig / m.yield : 0.0);
    };
    static const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (std::size_t e = 0; e < m.members.size(); ++e) {
        const auto& mb = m.members[e];
        const auto& sec = m.sections[mb.section];
        const FrameMemberResult& mr = r.members[e];
        const Axes ax = memberAxes(m, mb);
        const int nseg = std::clamp(static_cast<int>(std::ceil(ax.L / (size / 20.0))), 2, 24);
        const double w = std::max(sec.width, minDim), h = std::max(sec.height, minDim);
        // tải phân bố địa phương để suy nội lực dọc thanh
        Vec3 q{0, 0, 0};
        for (const auto& ml : m.memberLoads)
            if (ml.member == static_cast<int>(e)) q = {q.x + dot(ml.q, ax.ex), q.y + dot(ml.q, ax.ey), q.z + dot(ml.q, ax.ez)};
        if (m.gravity != 0 && mb.kind != MemberKind::Spring) q.z += -m.rho * sec.A * m.gravity * ax.ez.z, q.x += -m.rho * sec.A * m.gravity * ax.ex.z,
                                                              q.y += -m.rho * sec.A * m.gravity * ax.ey.z;
        const int base = static_cast<int>(out.mesh.nodes.size());
        const Vec3 p1 = m.nodes[mb.n1];
        double dg[12] = {0};
        double dl[12] = {0};
        if (r.ok) {
            for (int i = 0; i < 6; ++i) {
                dg[i] = r.disp[mb.n1][i];
                dg[6 + i] = r.disp[mb.n2][i];
            }
            toLocal(ax, dg, dl);
        }
        for (int s = 0; s <= nseg; ++s) {
            const double xi = static_cast<double>(s) / nseg, x = xi * ax.L;
            // chuyển vị trục: nội suy Hermite (uốn) + tuyến tính (dọc trục)
            const double N1 = 1 - 3 * xi * xi + 2 * xi * xi * xi, N2 = (xi - 2 * xi * xi + xi * xi * xi) * ax.L;
            const double N3 = 3 * xi * xi - 2 * xi * xi * xi, N4 = (-xi * xi + xi * xi * xi) * ax.L;
            const double ul = dl[0] * (1 - xi) + dl[6] * xi;
            const double vl = N1 * dl[1] + N2 * dl[5] + N3 * dl[7] + N4 * dl[11];
            const double wl = N1 * dl[2] + N2 * (-dl[4]) + N3 * dl[8] + N4 * (-dl[10]);
            const Vec3 dglob{ul * ax.ex.x + vl * ax.ey.x + wl * ax.ez.x, ul * ax.ex.y + vl * ax.ey.y + wl * ax.ez.y,
                             ul * ax.ex.z + vl * ax.ey.z + wl * ax.ez.z};
            double N = 0, M = 0, V = 0, sig = 0;
            if (r.ok && mr.active) {
                const auto fo = memberForceAt(mr, x, q);
                N = fo[0];
                M = std::hypot(fo[4], fo[5]);
                V = std::hypot(fo[1], fo[2]);
                sig = std::abs(fo[0]) / std::max(sec.A, 1e-300);
                if (mb.kind == MemberKind::Beam)
                    sig += std::abs(fo[4]) * 0.5 * sec.height / std::max(sec.Iy, 1e-300) + std::abs(fo[5]) * 0.5 * sec.width / std::max(sec.Iz, 1e-300);
            }
            const Vec3 c{p1.x + ax.ex.x * x, p1.y + ax.ex.y * x, p1.z + ax.ex.z * x};
            // 4 góc tiết diện: (-w/2,-h/2) (w/2,-h/2) (w/2,h/2) (-w/2,h/2) theo (ey, ez)
            const double cy[4] = {-w / 2, w / 2, w / 2, -w / 2}, cz[4] = {-h / 2, -h / 2, h / 2, h / 2};
            const double twist = dl[3] * (1 - xi) + dl[9] * xi;   // góc xoắn quanh trục thanh (nội suy tuyến tính)
            for (int k = 0; k < 4; ++k) {
                const Vec3 off{ax.ey.x * cy[k] + ax.ez.x * cz[k], ax.ey.y * cy[k] + ax.ez.y * cz[k], ax.ey.z * cy[k] + ax.ez.z * cz[k]};
                const Vec3 tw = cross(ax.ex, off);   // dịch chuyển góc tiết diện khi xoắn (góc nhỏ)
                addNode({c.x + off.x, c.y + off.y, c.z + off.z}, {dglob.x + twist * tw.x, dglob.y + twist * tw.y, dglob.z + twist * tw.z}, N, M, V, sig);
            }
        }
        // mỗi đoạn là một hexa (8 nút) chia 6 tứ diện
        for (int s = 0; s < nseg; ++s) {
            int v[8];
            for (int k = 0; k < 4; ++k) {
                v[k] = base + 4 * s + k;
                v[4 + k] = base + 4 * (s + 1) + k;
            }
            // hexa theo lưới (i: ey, j: ez, k: dọc): v(i,j,k) với (i,j) -> góc: (0,0)=0 (1,0)=1 (1,1)=2 (0,1)=3
            auto id = [&](int i, int j, int k) {
                const int corner = (j == 0) ? i : (i == 0 ? 3 : 2);
                return v[(k ? 4 : 0) + corner];
            };
            for (const auto& p : perms) {
                std::array<int, 3> o{0, 0, 0};
                Tet t;
                t[0] = id(0, 0, 0);
                for (int q2 = 0; q2 < 3; ++q2) {
                    o[p[q2]] = 1;
                    t[q2 + 1] = id(o[0], o[1], o[2]);
                }
                out.mesh.tets.push_back(t);
            }
        }
    }
    out.mesh.fixOrientation();
    out.fields.pointVectors["displacement"] = vec;
    out.fields.pointScalars["displacement_magnitude"] = mag;
    out.fields.pointScalars["axial_force"] = axial;
    out.fields.pointScalars["bending_moment"] = moment;
    out.fields.pointScalars["shear_force"] = shear;
    out.fields.pointScalars["normal_stress"] = stress;
    out.fields.pointScalars["von_mises_nodal"] = stress;   // tên chung với solver khối đặc (ứng suất tương đương)
    if (m.yield > 0) out.fields.pointScalars["utilization"] = util;
    return out;
}

// ---------------------------------------------------------------- đọc mô hình từ case-file
bool readFrameModel(const CaseFile& cf, FrameModel& out, std::string& err) {
    out = {};
    std::map<long long, int> nodeIndex;
    auto num = [&](const std::string& s, double& v) {
        try {
            std::size_t pos = 0;
            v = std::stod(s, &pos);
            return pos == s.size();
        } catch (...) {
            return false;
        }
    };
    for (const auto& line : cf.getAll("node")) {
        const auto t = CaseFile::tokens(line);
        double id, x, y, z;
        if (t.size() < 4 || !num(t[0], id) || !num(t[1], x) || !num(t[2], y) || !num(t[3], z)) {
            err = "node = <id> x y z: dong khong hop le: " + line;
            return false;
        }
        if (nodeIndex.count(static_cast<long long>(id))) { err = "Trung id nut: " + t[0]; return false; }
        nodeIndex[static_cast<long long>(id)] = static_cast<int>(out.nodes.size());
        out.nodes.push_back({x, y, z});
    }
    auto findNode = [&](const std::string& s, int& idx) {
        double id;
        if (!num(s, id)) return false;
        auto it = nodeIndex.find(static_cast<long long>(id));
        if (it == nodeIndex.end()) return false;
        idx = it->second;
        return true;
    };
    std::map<std::string, int> secIndex;
    for (const auto& line : cf.getAll("section")) {
        const auto t = CaseFile::tokens(line);
        if (t.size() < 2) { err = "section = <ten> <loai> tham so...: " + line; return false; }
        std::vector<double> p;
        for (std::size_t i = 2; i < t.size(); ++i) {
            double v;
            if (!num(t[i], v)) { err = "Tham so tiet dien khong phai so: " + t[i]; return false; }
            p.push_back(v);
        }
        FrameSection s;
        if (!makeSection(t[0], t[1], p, s, err)) return false;
        secIndex[t[0]] = static_cast<int>(out.sections.size());
        out.sections.push_back(s);
    }
    for (const auto& line : cf.getAll("member")) {
        const auto t = CaseFile::tokens(line);
        FrameMember m;
        if (t.size() < 3 || !findNode(t[0], m.n1) || !findNode(t[1], m.n2)) {
            err = "member = <nut1> <nut2> <tiet dien> [beam|truss|spring k|cable]: nut khong ton tai: " + line;
            return false;
        }
        std::string kind = t.size() > 3 ? t[3] : "beam";
        if (kind == "spring") {
            m.kind = MemberKind::Spring;
            if (t.size() < 5 || !num(t[4], m.k) || !(m.k > 0)) { err = "spring can do cung k > 0: " + line; return false; }
            m.section = 0;
        } else {
            auto it = secIndex.find(t[2]);
            if (it == secIndex.end()) { err = "Khong co tiet dien '" + t[2] + "'"; return false; }
            m.section = it->second;
            if (kind == "beam") {
                m.kind = MemberKind::Beam;
                for (std::size_t i = 4; i < t.size(); ++i) {
                    if (t[i] == "pin1") m.pin1 = true;
                    else if (t[i] == "pin2") m.pin2 = true;
                    else if (t[i] == "pinned") m.pin1 = m.pin2 = true;
                    else { err = "member: tu khoa khong hop le: " + t[i] + " (pin1 pin2 pinned)"; return false; }
                }
            }
            else if (kind == "truss") m.kind = MemberKind::Truss;
            else if (kind == "cable") m.kind = MemberKind::Cable;
            else { err = "Loai thanh khong ho tro: " + kind; return false; }
        }
        out.members.push_back(m);
    }
    if (out.sections.empty() && !out.members.empty()) {
        bool anyNeeds = false;
        for (const auto& m : out.members) anyNeeds |= m.kind != MemberKind::Spring;
        if (anyNeeds) { err = "Thieu khai bao section"; return false; }
        out.sections.push_back({});
    }
    for (const auto& line : cf.getAll("support")) {
        const auto t = CaseFile::tokens(line);
        FrameSupport s;
        if (t.empty() || !findNode(t[0], s.node)) { err = "support = <nut> fixed|pinned|x y z rx ry rz: nut khong ton tai: " + line; return false; }
        for (std::size_t i = 1; i < t.size(); ++i) {
            const std::string& w = t[i];
            if (w == "fixed") s.fixed.fill(true);
            else if (w == "pinned") { s.fixed[0] = s.fixed[1] = s.fixed[2] = true; }
            else if (w == "x") s.fixed[0] = true;
            else if (w == "y") s.fixed[1] = true;
            else if (w == "z") s.fixed[2] = true;
            else if (w == "rx") s.fixed[3] = true;
            else if (w == "ry") s.fixed[4] = true;
            else if (w == "rz") s.fixed[5] = true;
            else { err = "support: tu khoa khong hop le: " + w; return false; }
        }
        out.supports.push_back(s);
    }
    for (const auto& line : cf.getAll("ksupport")) {
        const auto t = CaseFile::tokens(line);
        FrameSpringSupport s;
        if (t.size() < 4 || !findNode(t[0], s.node)) { err = "ksupport = <nut> kx ky kz [krx kry krz]: " + line; return false; }
        for (std::size_t i = 1; i < t.size() && i <= 6; ++i)
            if (!num(t[i], s.k[i - 1])) { err = "ksupport: gia tri khong phai so"; return false; }
        out.springSupports.push_back(s);
    }
    for (const auto& line : cf.getAll("nload")) {
        const auto t = CaseFile::tokens(line);
        FrameNodalLoad l;
        if (t.size() < 4 || !findNode(t[0], l.node)) { err = "nload = <nut> Fx Fy Fz [Mx My Mz]: " + line; return false; }
        for (std::size_t i = 1; i < t.size() && i <= 6; ++i)
            if (!num(t[i], l.f[i - 1])) { err = "nload: gia tri khong phai so"; return false; }
        out.nodalLoads.push_back(l);
    }
    for (const auto& line : cf.getAll("udl")) {
        const auto t = CaseFile::tokens(line);
        FrameMemberLoad l;
        double id, qx, qy, qz;
        if (t.size() < 4 || !num(t[0], id) || !num(t[1], qx) || !num(t[2], qy) || !num(t[3], qz) || !(id >= 1 && id <= 1.0e9)) {
            err = "udl = <thanh so> qx qy qz (N/m, he chung; thanh dem tu 1 theo thu tu khai bao): " + line;
            return false;
        }
        l.member = static_cast<int>(id) - 1;
        l.q = {qx, qy, qz};
        out.memberLoads.push_back(l);
    }
    for (const auto& line : cf.getAll("nmass")) {
        const auto t = CaseFile::tokens(line);
        FrameNodalMass nm;
        if (t.size() < 2 || !findNode(t[0], nm.node) || !num(t[1], nm.m) || !(nm.m >= 0)) { err = "nmass = <nut> <khoi luong kg>: " + line; return false; }
        out.masses.push_back(nm);
    }
    out.E = cf.getDouble("e", out.E);
    out.nu = cf.getDouble("nu", out.nu);
    out.rho = cf.getDouble("rho", out.rho);
    out.gravity = cf.getDouble("gravity", 0);
    out.yield = cf.getDouble("yield", 0);
    if (out.nodes.size() < 2 || out.members.empty()) { err = "Can khai bao it nhat 2 'node' va 1 'member'"; return false; }
    return true;
}

}  // namespace fastcae
