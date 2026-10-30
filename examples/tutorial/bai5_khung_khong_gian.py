# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Trần Minh Tâm
"""BÀI 5 - Kết cấu phức tạp: khung thép không gian 5 tầng, FEM dầm 3D tự lập trình trong Python.

Mô hình (dựng bằng chương trình): 3 nhịp 6 m theo X, 2 nhịp 5 m theo Y, 4 tầng cao 3,5 m, tầng mái thu nhỏ (chỉ 2 nhịp đầu),
cột hộp, dầm chữ I hai cỡ, giằng chéo chữ X (thanh dàn) ở hai khung biên; ~150 thanh, ~360 bậc tự do.
Tải: tải sàn phân bố trên dầm, tải gió ngang tại mép X = 0 (kèm thành phần Y gây xoắn), khối lượng tập trung mỗi nút sàn.

Thuật toán tự viết (hàm `giai_tinh`, `giai_dao_dong`): phần tử dầm Euler-Bernoulli 12 bậc tự do + thanh dàn, ghép ma trận thưa,
giải tĩnh, dao động riêng K phi = w^2 M phi. KIỂM CHỨNG: (1) cân bằng phản lực tổng = tổng tải đặt; (2) đối chiếu chuyển vị
mọi nút, lực dọc thanh và tần số riêng với solver khung có sẵn của FastCAE (chạy cùng văn bản case); (3) hiển thị biến dạng
lên khung 3D (nội suy Hermite dọc thanh, tiết diện xoay theo góc xoắn).

Chạy: bảng Lập trình (Python, F6) hoặc `python bai5_khung_khong_gian.py [--khong-hien-thi]`. Cần numpy, scipy.
"""
import os
import sys

import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (os.path.join(HERE, "..", "..", "python"),):
    sys.path.insert(0, p)
import fastcae

E, NU, RHO = 210e9, 0.3, 7850.0
G = E / (2 * (1 + NU))


# ------------------------------------------------------------------------------------------ 1. Dựng mô hình
def dung_mo_hinh():
    XS, YS, ZS = [0, 6, 12, 18], [0, 5, 10], [0, 3.5, 7, 10.5, 14]
    nodes, idx = [], {}
    for k, z in enumerate(ZS):
        for j, y in enumerate(YS):
            for i, x in enumerate(XS):
                if k == 4 and i == 3:          # tầng mái thu nhỏ: bỏ cột trục X = 18
                    continue
                idx[(i, j, k)] = len(nodes)
                nodes.append((x, y, z))
    members = []                                # (n1, n2, tiết diện, loại)
    for (i, j, k), n in idx.items():            # cột
        if k < 4 and (i, j, k + 1) in idx:
            members.append((n, idx[(i, j, k + 1)], "COT", "beam"))
    for k in range(1, 5):                       # dầm sàn
        for j in range(3):
            for i in range(3):
                if (i, j, k) in idx and (i + 1, j, k) in idx:
                    members.append((idx[(i, j, k)], idx[(i + 1, j, k)], "DAM_X", "beam"))
        for i in range(4):
            for j in range(2):
                if (i, j, k) in idx and (i, j + 1, k) in idx:
                    members.append((idx[(i, j, k)], idx[(i, j + 1, k)], "DAM_Y", "beam"))
    for j in (0, 2):                            # giằng chéo chữ X ở hai khung biên (nhịp 1 và 3)
        for k in range(4):
            for i in (0, 2):
                if all(key in idx for key in ((i, j, k), (i + 1, j, k), (i, j, k + 1), (i + 1, j, k + 1))):
                    members.append((idx[(i, j, k)], idx[(i + 1, j, k + 1)], "GIANG", "truss"))
                    members.append((idx[(i + 1, j, k)], idx[(i, j, k + 1)], "GIANG", "truss"))
    supports = [n for (i, j, k), n in idx.items() if k == 0]
    udl = {}                                    # thanh -> qz (N/m)
    for m, (n1, n2, sec, kind) in enumerate(members):
        if sec in ("DAM_X", "DAM_Y"):
            udl[m] = -20e3 if nodes[n1][2] > 14 - 1e-9 else -30e3
    loads = {}                                  # nút -> (Fx, Fy, Fz)
    for (i, j, k), n in idx.items():
        if i == 0 and k >= 1:
            loads[n] = (8e3 * (1 + 0.25 * k), 2e3 if j == 0 else 0.0, 0.0)
    masses = {n: 15000.0 for (i, j, k), n in idx.items() if k >= 1}
    return nodes, members, supports, udl, loads, masses


SECTIONS = {"COT": ("box", 0.30, 0.30, 0.016), "DAM_X": ("ibeam", 0.15, 0.30, 0.0107, 0.0071),
            "DAM_Y": ("ibeam", 0.10, 0.20, 0.0085, 0.0056), "GIANG": ("pipe", 0.10, 0.005)}


def van_ban_case(nodes, members, supports, udl, loads, masses):
    """Cùng mô hình ở dạng file case của FastCAE (dùng để đối chiếu và để mở trong giao diện)."""
    t = ["type = frame", "E = %g" % E, "nu = %g" % NU, "rho = %g" % RHO]
    for name, s in SECTIONS.items():
        t.append("section = %s %s %s" % (name, s[0], " ".join("%g" % v for v in s[1:])))
    t += ["node = %d %g %g %g" % ((i + 1,) + p) for i, p in enumerate(nodes)]
    t += ["member = %d %d %s %s" % (a + 1, b + 1, s, k) for a, b, s, k in members]
    t += ["support = %d fixed" % (n + 1) for n in supports]
    t += ["udl = %d 0 0 %g" % (m + 1, q) for m, q in udl.items()]
    t += ["nload = %d %g %g %g" % ((n + 1,) + f) for n, f in loads.items()]
    t += ["nmass = %d %g" % (n + 1, m) for n, m in masses.items()]
    return "\n".join(t) + "\n"


# ------------------------------------------------------------------------------------------ 2. FEM tự viết
def truc_dia_phuong(p1, p2):
    d = np.array(p2) - np.array(p1)
    L = np.linalg.norm(d)
    ex = d / L
    ref = np.array([1.0, 0, 0]) if abs(ex[2]) > 0.999 else np.array([0, 0, 1.0])
    ey = np.cross(ref, ex)
    ey /= np.linalg.norm(ey)
    return L, np.vstack([ex, ey, np.cross(ex, ey)])


def k_dam(A, Iy, Iz, J, L):
    k = np.zeros((12, 12))
    k[0, 0] = k[6, 6] = E * A / L
    k[0, 6] = k[6, 0] = -E * A / L
    k[3, 3] = k[9, 9] = G * J / L
    k[3, 9] = k[9, 3] = -G * J / L
    for I, dofs, sg in ((Iz, (1, 5, 7, 11), 1.0), (Iy, (2, 4, 8, 10), -1.0)):   # uốn trong mặt xy (Iz) và xz (Iy)
        a = E * I / L**3
        b = np.array([[12, 6 * L, -12, 6 * L], [6 * L, 4 * L**2, -6 * L, 2 * L**2],
                      [-12, -6 * L, 12, -6 * L], [6 * L, 2 * L**2, -6 * L, 4 * L**2]])
        s = np.array([1, sg, 1, sg])                      # đổi dấu bậc xoay khi uốn quanh y (thy = -dw/dx)
        k[np.ix_(dofs, dofs)] += a * b * np.outer(s, s)
    return k


def k_dan(A, L):
    k = np.zeros((12, 12))
    k[0, 0] = k[6, 6] = E * A / L
    k[0, 6] = k[6, 0] = -E * A / L
    return k


def m_dam(A, Iy, Iz, L):
    m = np.zeros((12, 12))
    mA = RHO * A * L
    m[0, 0] = m[6, 6] = mA / 3
    m[0, 6] = m[6, 0] = mA / 6
    mt = RHO * (Iy + Iz) * L
    m[3, 3] = m[9, 9] = mt / 3
    m[3, 9] = m[9, 3] = mt / 6
    c, l = mA / 420, L
    b = np.array([[156, 22 * l, 54, -13 * l], [22 * l, 4 * l * l, 13 * l, -3 * l * l],
                  [54, 13 * l, 156, -22 * l], [-13 * l, -3 * l * l, -22 * l, 4 * l * l]])
    for dofs, sg in (((1, 5, 7, 11), 1.0), ((2, 4, 8, 10), -1.0)):
        s = np.array([1, sg, 1, sg])
        m[np.ix_(dofs, dofs)] += c * b * np.outer(s, s)
    return m


def m_dan(A, L):
    m = np.zeros((12, 12))
    mA = RHO * A * L
    for d in range(3):
        m[d, d] = m[6 + d, 6 + d] = mA / 3
        m[d, 6 + d] = m[6 + d, d] = mA / 6
    return m


def lap_ma_tran(nodes, members, props, udl, masses=None):
    """Trả K, M (CSR), vectơ tải tương đương của tải phân bố, danh sách (L, R, kl) từng thanh."""
    n = 6 * len(nodes)
    rows, cols, kv, mv = [], [], [], []
    f = np.zeros(n)
    info = []
    for m, (a, b, sec, kind) in enumerate(members):
        pr = props[sec]
        L, R = truc_dia_phuong(nodes[a], nodes[b])
        if kind == "truss":
            kl, ml = k_dan(pr["A"], L), m_dan(pr["A"], L)
        else:
            kl, ml = k_dam(pr["A"], pr["Iy"], pr["Iz"], pr["J"], L), m_dam(pr["A"], pr["Iy"], pr["Iz"], L)
        T = np.zeros((12, 12))
        for q in range(4):
            T[3 * q:3 * q + 3, 3 * q:3 * q + 3] = R
        dofs = np.r_[6 * a:6 * a + 6, 6 * b:6 * b + 6]
        kg, mg = T.T @ kl @ T, T.T @ ml @ T
        rows.append(np.repeat(dofs, 12)); cols.append(np.tile(dofs, 12)); kv.append(kg.ravel()); mv.append(mg.ravel())
        if m in udl and kind == "beam":
            q = R @ np.array([0, 0, udl[m]])              # tải phân bố về hệ địa phương
            fl = np.zeros(12)
            fl[[0, 6]] = q[0] * L / 2
            fl[[1, 7]] = q[1] * L / 2
            fl[5], fl[11] = q[1] * L**2 / 12, -q[1] * L**2 / 12
            fl[[2, 8]] = q[2] * L / 2
            fl[4], fl[10] = -q[2] * L**2 / 12, q[2] * L**2 / 12
            f[dofs] += T.T @ fl
        info.append((L, R, kl, T, dofs))
    r, c = np.concatenate(rows), np.concatenate(cols)
    K = sp.csr_matrix((np.concatenate(kv), (r, c)), shape=(n, n))
    M = sp.csr_matrix((np.concatenate(mv), (r, c)), shape=(n, n))
    if masses:
        ex = np.zeros(n)
        for nd, mm in masses.items():
            ex[6 * nd:6 * nd + 3] = mm
        M = M + sp.diags(ex)
    return K, M, f, info


def giai_tinh(K, f_udl, nodes, supports, loads):
    n = K.shape[0]
    F = f_udl.copy()
    for nd, (fx, fy, fz) in loads.items():
        F[6 * nd:6 * nd + 3] += (fx, fy, fz)
    fixed = np.concatenate([np.arange(6 * s, 6 * s + 6) for s in supports])
    free = np.setdiff1d(np.arange(n), fixed)
    u = np.zeros(n)
    u[free] = spla.spsolve(K[free][:, free].tocsc(), F[free])
    reac = K @ u - F
    return u, reac, fixed, F


def giai_dao_dong(K, M, fixed, so_dang=6):
    n = K.shape[0]
    free = np.setdiff1d(np.arange(n), fixed)
    w2, V = spla.eigsh(K[free][:, free].tocsc(), k=so_dang, M=M[free][:, free].tocsc(), sigma=0, which="LM")
    o = np.argsort(w2)
    return np.sqrt(w2[o]) / (2 * np.pi)


# ------------------------------------------------------------------------------------------ 3. Hiển thị
def dung_luoi_hien_thi(nodes, members, props, u, info, so_doan=6):
    """Mỗi thanh là lăng trụ tiết diện chữ nhật bao; chuyển vị điểm góc = chuyển vị trục (Hermite) + góc xoay x vị trí."""
    pts, tets, disp, ncell = [], [], [], []
    for m, (a, b, sec, kind) in enumerate(members):
        L, R, kl, T, dofs = info[m]
        d = T @ u[dofs]                                    # chuyển vị đầu thanh trong hệ địa phương
        w_, h_ = props[sec]["width"], props[sec]["height"]
        axial = props[sec]["A"] * E / L * (d[6] - d[0])
        base = len(pts)
        for s in range(so_doan + 1):
            x = s / so_doan
            xi = x * L
            h1, h2, h3, h4 = 1 - 3 * x**2 + 2 * x**3, xi * (1 - x)**2, 3 * x**2 - 2 * x**3, xi * (x**2 - x)
            dh1, dh2, dh3, dh4 = (-6 * x + 6 * x**2) / L, (1 - x) * (1 - 3 * x), (6 * x - 6 * x**2) / L, x * (3 * x - 2)
            ul = d[0] * (1 - x) + d[6] * x
            vl = h1 * d[1] + h2 * d[5] + h3 * d[7] + h4 * d[11]
            wl = h1 * d[2] - h2 * d[4] + h3 * d[8] - h4 * d[10]
            thz = dh1 * d[1] + dh2 * d[5] + dh3 * d[7] + dh4 * d[11]
            thy = -(dh1 * d[2] - dh2 * d[4] + dh3 * d[8] - dh4 * d[10])
            thx = d[3] * (1 - x) + d[9] * x
            c0 = np.array(nodes[a]) + (np.array(nodes[b]) - np.array(nodes[a])) * x
            for (yy, zz) in ((-w_ / 2, -h_ / 2), (w_ / 2, -h_ / 2), (w_ / 2, h_ / 2), (-w_ / 2, h_ / 2)):
                pts.append(tuple(c0 + R[1] * yy + R[2] * zz))
                dl = np.array([ul + thy * zz - thz * yy, vl - thx * zz, wl + thx * yy])
                if kind == "truss":
                    dl = np.array([ul, vl, wl])
                disp.append(R.T @ dl)
        for s in range(so_doan):
            c = [base + 4 * s + q for q in range(4)] + [base + 4 * (s + 1) + q for q in range(4)]
            for t in [(0, 1, 2, 6), (0, 2, 3, 6), (0, 3, 7, 6), (0, 7, 4, 6), (0, 4, 5, 6), (0, 5, 1, 6)]:
                tets.append(tuple(c[i] for i in t))
                ncell.append(axial)
    return pts, tets, np.array(disp), np.array(ncell)


def kiem_tra_dam_cong_xon():
    """Kiểm tra bộ ghép ma trận bằng lời giải giải tích: dầm công xôn L = 3 m, lực P = 5 kN: võng PL^3/(3EI), tần số dạng 1 (uốn quanh trục yếu) 1,875^2/(2pi) sqrt(EI/(rho A L^4))."""
    s = fastcae.section("ibeam", 0.15, 0.30, 0.0107, 0.0071)
    L, P, n = 3.0, 5000.0, 6
    nodes = [(L * i / n, 0.0, 0.0) for i in range(n + 1)]
    members = [(i, i + 1, "S", "beam") for i in range(n)]
    K, M, f, _ = lap_ma_tran(nodes, members, {"S": s}, {})
    u, _, fixed, _ = giai_tinh(K, f, nodes, [0], {n: (0, 0, -P)})
    fe = giai_dao_dong(K, M, fixed, 2)[0]
    vong, vong_lt = -u[6 * n + 2], P * L**3 / (3 * E * s["Iy"])
    fe_lt = 1.8751**2 / (2 * np.pi) * np.sqrt(E * min(s["Iy"], s["Iz"]) / (RHO * s["A"] * L**4))   # dạng 1 uốn quanh trục yếu
    print("Kiểm tra dầm công xôn: võng %.6g m (lý thuyết %.6g, lệch %.3f%%); tần số %.4f Hz (lý thuyết %.4f, lệch %.3f%%)" %
          (vong, vong_lt, abs(vong / vong_lt - 1) * 100, fe, fe_lt, abs(fe / fe_lt - 1) * 100))


# ------------------------------------------------------------------------------------------ chương trình chính
def main():
    kiem_tra_dam_cong_xon()
    nodes, members, supports, udl, loads, masses = dung_mo_hinh()
    text = van_ban_case(nodes, members, supports, udl, loads, masses)
    props = {}
    for name, s in SECTIONS.items():
        props[name] = fastcae.section(s[0], *s[1:])
    print("Mô hình: %d nút, %d thanh (%d dầm/cột, %d giằng), %d bậc tự do" %
          (len(nodes), len(members), sum(1 for m in members if m[3] == "beam"), sum(1 for m in members if m[3] == "truss"), 6 * len(nodes)))
    K, M, f_udl, info = lap_ma_tran(nodes, members, props, udl, masses)
    u, reac, fixed, F = giai_tinh(K, f_udl, nodes, supports, loads)
    tong_tai = sum(-q * info[m][0] for m, q in udl.items())
    print("Cân bằng: tổng phản lực Rz = %.1f N, tổng tải đứng = %.1f N" % (reac[fixed][2::6].sum(), tong_tai))
    print("          tổng phản lực Rx = %.1f N, tổng tải gió X = %.1f N" % (reac[fixed][0::6].sum(), -sum(f[0] for f in loads.values())))
    ux = u.reshape(-1, 6)
    top = max(range(len(nodes)), key=lambda i: abs(ux[i, 0]))
    print("Chuyển vị ngang lớn nhất: %.3f mm tại nút %d (z = %g m)" % (abs(ux[top, 0]) * 1e3, top + 1, nodes[top][2]))
    freqs = giai_dao_dong(K, M, fixed)
    print("Tần số riêng (Hz):", " ".join("%.3f" % f for f in freqs))

    # ---- đối chiếu với solver khung có sẵn của FastCAE (cùng văn bản case)
    ref = fastcae.run(text)
    if ref.ok:
        du = max(np.max(np.abs(ux[:, d] - np.array(ref.series["node_" + nm]))) for d, nm in enumerate(["ux", "uy", "uz", "rx", "ry", "rz"]))
        scale = np.max(np.abs(ux[:, :3]))
        print("Đối chiếu FastCAE (solver khung có sẵn): sai khác chuyển vị lớn nhất = %.2e (%.2e so với chuyển vị lớn nhất)" % (du, du / scale))
        nm = np.array([abs(props[members[m][2]]["A"] * E / info[m][0] * ((info[m][3] @ u[info[m][4]])[6] - (info[m][3] @ u[info[m][4]])[0])) for m in range(len(members))])
        dn = np.max(np.abs(nm - np.array(ref.series["member_n_max"])))
        print("                                         sai khác lực dọc thanh lớn nhất = %.3g N (lực dọc lớn nhất %.3g N)" % (dn, nm.max()))
        mod = fastcae.run(text + "analysis = modal\nmodes = 6\n")
        if mod.ok:
            fr = np.array(mod.series["frequencies_hz"])
            print("                                         sai khác tần số riêng lớn nhất = %.2e Hz (FastCAE: %s)" %
                  (np.max(np.abs(freqs - fr)), " ".join("%.3f" % v for v in fr)))
    else:
        print("Không chạy được solver khung của FastCAE để đối chiếu:", ref.message)

    # ---- hiển thị
    pts, tets, disp, ncell = dung_luoi_hien_thi(nodes, members, props, u, info)
    out = os.path.join(HERE if os.access(HERE, os.W_OK) else os.path.expanduser("~"), "bai5_khung_khong_gian.vtk")
    fastcae.write_vtk(out, pts, tets, point_vectors={"displacement": disp},
                      point_scalars={"displacement_magnitude": np.linalg.norm(disp, axis=1)}, cell_scalars={"axial_force": ncell})
    if "--khong-hien-thi" not in sys.argv:
        fastcae.show(out)
    with open(os.path.join(os.path.dirname(out), "bai5_khung_khong_gian.fcae"), "w", encoding="utf-8") as fh:
        fh.write("# Khung thép không gian 5 tầng (Bài 5). Mở trong giao diện rồi F5 để giải bằng solver khung có sẵn.\n" + text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
