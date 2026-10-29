#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Trần Minh Tâm
"""FEM tự lập trình (numpy + scipy) chạy trên mô hình do FastCAE dựng.

Hợp đồng "mô hình vào - kết quả ra":
    my_fem.py <job.fcmodel.json> <job_result.vtk>
Đọc lưới, vật liệu, liên kết, tải từ JSON; giải; ghi VTK (điểm i = nút i). FastCAE hiển thị kết quả lên khung 3D.

Phần tử: tứ diện 4 nút (order = 1) hoặc 10 nút (order = 2, thứ tự nút kiểu Abaqus/CalculiX).
Bài toán: elastic (đàn hồi tuyến tính 3D) và thermal (dẫn nhiệt dừng). Đây là chỗ để bạn thay bằng thuật toán của mình.
"""
import json
import sys

import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

A_G, B_G = 0.5854101966249685, 0.1381966011250105
GAUSS = {4: (np.array([[0.25, 0.25, 0.25]]), np.array([1 / 6])),
         10: (np.array([[B_G, B_G, B_G], [A_G, B_G, B_G], [B_G, A_G, B_G], [B_G, B_G, A_G]]), np.full(4, 1 / 24))}
NODE_XI = {4: np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], float)}
NODE_XI[10] = np.vstack([NODE_XI[4], [(NODE_XI[4][a] + NODE_XI[4][b]) / 2 for a, b in
                                      [(0, 1), (1, 2), (2, 0), (0, 3), (1, 3), (2, 3)]]])


def shape(nn, xi):
    """Hàm dạng N (nn,) và đạo hàm theo (xi, eta, zeta) dN (nn, 3) tại điểm xi."""
    L = np.array([1 - xi.sum(), xi[0], xi[1], xi[2]])
    dL = np.array([[-1, -1, -1], [1, 0, 0], [0, 1, 0], [0, 0, 1]], float)
    if nn == 4:
        return L, dL
    N = np.empty(10)
    dN = np.empty((10, 3))
    for i in range(4):
        N[i] = L[i] * (2 * L[i] - 1)
        dN[i] = (4 * L[i] - 1) * dL[i]
    for k, (a, b) in enumerate([(0, 1), (1, 2), (2, 0), (0, 3), (1, 3), (2, 3)]):
        N[4 + k] = 4 * L[a] * L[b]
        dN[4 + k] = 4 * (L[a] * dL[b] + L[b] * dL[a])
    return N, dN


def physical_grads(X, dN):
    """X (ne, nn, 3), dN (nn, 3) -> gradient theo x (ne, nn, 3) và detJ (ne,)."""
    J = np.einsum("ena,nb->eab", X, dN)          # J[e,a,b] = d x_a / d xi_b
    det = np.linalg.det(J)
    G = np.einsum("nb,ebc->enc", dN, np.linalg.inv(J))   # dN/dx_c
    return G, det


def solve(model):
    nn = 10 if "tet10" in model["mesh"] else 4
    P = np.asarray(model["mesh"]["nodes"], float).reshape(-1, 3)
    conn = np.asarray(model["mesh"]["tet10" if nn == 10 else "tet4"], int).reshape(-1, nn)
    X = P[conn]
    ne, npts = len(conn), len(P)
    gp, gw = GAUSS[nn]
    mat = model["material"]
    kind = model["analysis"]
    ndof = 3 if kind == "elastic" else 1
    nd = nn * ndof

    if kind == "elastic":
        E, nu = mat["E"], mat["nu"]
        lam, mu = E * nu / ((1 + nu) * (1 - 2 * nu)), E / (2 * (1 + nu))
        D = np.zeros((6, 6))
        D[:3, :3] = lam
        D[0, 0] = D[1, 1] = D[2, 2] = lam + 2 * mu
        D[3, 3] = D[4, 4] = D[5, 5] = mu

        def Bmat(G):
            B = np.zeros((G.shape[0], 6, nd))
            for n in range(nn):
                gx, gy, gz = G[:, n, 0], G[:, n, 1], G[:, n, 2]
                B[:, 0, 3 * n] = gx
                B[:, 1, 3 * n + 1] = gy
                B[:, 2, 3 * n + 2] = gz
                B[:, 3, 3 * n], B[:, 3, 3 * n + 1] = gy, gx
                B[:, 4, 3 * n + 1], B[:, 4, 3 * n + 2] = gz, gy
                B[:, 5, 3 * n], B[:, 5, 3 * n + 2] = gz, gx
            return B
    Ke = np.zeros((ne, nd, nd))
    for xi, w in zip(gp, gw):
        G, det = physical_grads(X, shape(nn, xi)[1])
        wd = w * np.abs(det)
        if kind == "elastic":
            B = Bmat(G)
            Ke += np.einsum("e,eki,kl,elj->eij", wd, B, D, B)
        else:
            Ke += mat["k"] * np.einsum("e,enc,emc->enm", wd, G, G)

    dofs = (conn[:, :, None] * ndof + np.arange(ndof)).reshape(ne, nd)
    rows = np.repeat(dofs, nd, axis=1).ravel()
    cols = np.tile(dofs, (1, nd)).ravel()
    ntot = npts * ndof
    K = sp.coo_matrix((Ke.ravel(), (rows, cols)), shape=(ntot, ntot)).tocsr()

    f = np.zeros(ntot)
    for ld in model["loads"]:
        for rec in ld["nodal"]:
            if kind == "elastic":
                f[int(rec[0]) * 3:int(rec[0]) * 3 + 3] += rec[1:4]
            else:
                f[int(rec[0])] += rec[1]
    fixed = {}
    for bc in model["dirichlet"]:
        for n in bc["nodes"]:
            if kind == "elastic":
                for d in bc["dofs"]:
                    fixed[n * 3 + d] = bc["value"]
            else:
                fixed[n] = bc["value"]
    fx = np.fromiter(fixed.keys(), int)
    fv = np.fromiter(fixed.values(), float)
    free = np.setdiff1d(np.arange(ntot), fx)
    u = np.zeros(ntot)
    u[fx] = fv
    rhs = f[free] - K[free][:, fx] @ fv
    Kff = K[free][:, free].tocsc()
    if len(free) < 200000:
        u[free] = spla.spsolve(Kff, rhs)
    else:
        M = sp.diags(1.0 / Kff.diagonal())
        u[free], info = spla.cg(Kff, rhs, rtol=1e-10, M=M, maxiter=20000)
        if info != 0:
            raise RuntimeError("CG khong hoi tu")
    out = {}
    if kind == "thermal":
        out["temperature"] = u
        return P, conn, {}, {"temperature": u}

    # ứng suất tại các nút phần tử, trung bình theo nút
    ue = u[dofs]
    acc = np.zeros((npts, 6))
    cnt = np.zeros(npts)
    for k in range(nn):
        G, _ = physical_grads(X, shape(nn, NODE_XI[nn][k])[1])
        sig = np.einsum("kl,elj,ej->ek", D, Bmat(G), ue)
        np.add.at(acc, conn[:, k], sig)
        np.add.at(cnt, conn[:, k], 1)
    acc /= cnt[:, None]
    s = acc
    vm = np.sqrt(0.5 * ((s[:, 0] - s[:, 1]) ** 2 + (s[:, 1] - s[:, 2]) ** 2 + (s[:, 2] - s[:, 0]) ** 2) + 3 * (s[:, 3] ** 2 + s[:, 4] ** 2 + s[:, 5] ** 2))
    return P, conn, {"displacement": u.reshape(-1, 3)}, {"von_mises_nodal": vm}


def write_vtk(path, P, conn, vectors, scalars):
    tets = conn[:, :4]
    with open(path, "w") as f:
        f.write("# vtk DataFile Version 3.0\nmy_fem (Python)\nASCII\nDATASET UNSTRUCTURED_GRID\n")
        f.write(f"POINTS {len(P)} double\n")
        np.savetxt(f, P, fmt="%.12g")
        f.write(f"CELLS {len(tets)} {5 * len(tets)}\n")
        np.savetxt(f, np.hstack([np.full((len(tets), 1), 4), tets]), fmt="%d")
        f.write(f"CELL_TYPES {len(tets)}\n")
        f.write("10\n" * len(tets))
        f.write(f"POINT_DATA {len(P)}\n")
        for name, v in vectors.items():
            f.write(f"VECTORS {name} double\n")
            np.savetxt(f, v, fmt="%.12g")
        for name, v in scalars.items():
            f.write(f"SCALARS {name} double 1\nLOOKUP_TABLE default\n")
            np.savetxt(f, v, fmt="%.12g")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    with open(sys.argv[1], encoding="utf-8") as fh:
        model = json.load(fh)
    P, conn, vec, sca = solve(model)
    write_vtk(sys.argv[2], P, conn, vec, sca)
    print(f"my_fem: {model['analysis']}, {len(P)} nut, {len(conn)} phan tu T{conn.shape[1]} -> {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
