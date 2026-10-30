# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Trần Minh Tâm
"""BÀI 3 - Lập trình CFD trong Python: dòng chảy khoang kín nắp trượt (hàm dòng - xoáy), hiển thị trên khung 3D.

Chạy ở bảng "Lập trình" (Python, F6) hoặc `python bai3_cfd_python.py [N] [Re]`. Cần numpy và scipy.
Kiểm chứng: Re = 100 thì u tại tâm khoang (x = y = 0,5) của Ghia và cộng sự (1982) là -0,20581.
Thuật toán: xoáy chuyển tải-khuếch tán tường minh (sai phân trung tâm) + Poisson cho hàm dòng giải trực tiếp (LU).
"""
import os
import sys

import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (os.path.join(HERE, "..", "..", "python"), os.path.join(HERE, "..", "..", "..", "..", "python")):
    sys.path.insert(0, p)


def cavity(n=41, Re=100.0, lid=1.0, tol=1e-9, max_steps=60000):
    h = 1.0 / (n - 1)
    dt = min(0.2 * h * h * Re, 0.5 * h / lid) * 0.8
    m = n - 2
    I = sp.identity(m)
    T = sp.diags([1.0, -2.0, 1.0], [-1, 0, 1], shape=(m, m))
    Lap = (sp.kron(I, T) + sp.kron(T, I)) / (h * h)        # Laplace trên nút trong, psi = 0 ở biên
    lu = spla.splu(Lap.tocsc())
    psi = np.zeros((n, n))
    w = np.zeros((n, n))                                    # chỉ số [j (y), i (x)]
    for step in range(1, max_steps + 1):
        # xoáy trên tường: w = -2 (psi_kề) / h^2 - 2 U_tường / h
        w[0, :] = -2 * psi[1, :] / h**2
        w[-1, :] = -2 * psi[-2, :] / h**2 - 2 * lid / h
        w[:, 0] = -2 * psi[:, 1] / h**2
        w[:, -1] = -2 * psi[:, -2] / h**2
        u = np.zeros((n, n)); v = np.zeros((n, n))
        u[1:-1, 1:-1] = (psi[2:, 1:-1] - psi[:-2, 1:-1]) / (2 * h)
        v[1:-1, 1:-1] = -(psi[1:-1, 2:] - psi[1:-1, :-2]) / (2 * h)
        u[-1, :] = lid
        wi = w[1:-1, 1:-1]
        conv = u[1:-1, 1:-1] * (w[1:-1, 2:] - w[1:-1, :-2]) / (2 * h) + v[1:-1, 1:-1] * (w[2:, 1:-1] - w[:-2, 1:-1]) / (2 * h)
        lapw = (w[1:-1, 2:] + w[1:-1, :-2] + w[2:, 1:-1] + w[:-2, 1:-1] - 4 * wi) / h**2
        wnew = wi + dt * (-conv + lapw / Re)
        change = np.max(np.abs(wnew - wi))
        w[1:-1, 1:-1] = wnew
        psi[1:-1, 1:-1] = lu.solve(-w[1:-1, 1:-1].ravel()).reshape(m, m)
        if step % 50 == 0 and change < tol * dt:
            break
    u[1:-1, 1:-1] = (psi[2:, 1:-1] - psi[:-2, 1:-1]) / (2 * h)
    v[1:-1, 1:-1] = -(psi[1:-1, 2:] - psi[1:-1, :-2]) / (2 * h)
    u[-1, :] = lid
    u[0, :] = u[:, 0] = u[:, -1] = 0
    return psi, w, u, v, step


def slab_mesh(n, h):
    """Lưới tứ diện một lớp dày h để khung 3D hiển thị trường 2D (6 tứ diện mỗi ô)."""
    idx = lambda i, j, k: k * n * n + j * n + i
    pts = [(i * h, j * h, k * h) for k in range(2) for j in range(n) for i in range(n)]
    tets = []
    for j in range(n - 1):
        for i in range(n - 1):
            c = [idx(i, j, 0), idx(i + 1, j, 0), idx(i + 1, j + 1, 0), idx(i, j + 1, 0),
                 idx(i, j, 1), idx(i + 1, j, 1), idx(i + 1, j + 1, 1), idx(i, j + 1, 1)]
            for a, b, cc, d in [(0, 1, 2, 6), (0, 2, 3, 6), (0, 3, 7, 6), (0, 7, 4, 6), (0, 4, 5, 6), (0, 5, 1, 6)]:
                tets.append((c[a], c[b], c[cc], c[d]))
    return pts, tets


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 41
    Re = float(sys.argv[2]) if len(sys.argv) > 2 else 100.0
    psi, w, u, v, steps = cavity(n, Re)
    h = 1.0 / (n - 1)
    print("CFD khoang kín tự lập trình: lưới %dx%d, Re = %g, %d bước" % (n, n, Re, steps))
    if n % 2 == 1 and Re == 100.0 and n == 41:
        uc = u[n // 2, n // 2]
        print("u tại tâm = %.5f   (Ghia 1982: -0.20581, lệch %.2f%%)" % (uc, abs(uc + 0.20581) / 0.20581 * 100))
    import fastcae
    pts, tets = slab_mesh(n, h)
    rep = lambda f: np.concatenate([f.ravel(), f.ravel()])   # lớp z = 0 rồi z = h
    vel = np.stack([rep(u), rep(v), np.zeros(2 * n * n)], axis=1)
    out = os.path.join(HERE if os.access(HERE, os.W_OK) else os.path.expanduser("~"), "bai3_khoang_kin.vtk")
    fastcae.write_vtk(out, pts, tets, point_vectors={"velocity": vel},
                      point_scalars={"speed": np.hypot(rep(u), rep(v)), "streamfunction": rep(psi), "vorticity": rep(w)})
    fastcae.show(out)


if __name__ == "__main__":
    main()
