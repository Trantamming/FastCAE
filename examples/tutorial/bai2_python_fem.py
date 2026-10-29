# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Trần Minh Tâm
"""BÀI 2 - Lập trình FEM trong Python: dựng mô hình bằng FastCAE -> thuật toán của bạn -> hiển thị trên khung 3D.

Cách chạy: mở file này ở bảng "Lập trình" (chọn Python) rồi bấm Chạy (F6); hoặc `python bai2_python_fem.py` (không hiển thị 3D).
Cần Python 3 có numpy và scipy. Thuật toán nằm ở hàm `giai()`: thay bằng thuật toán của bạn.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (os.path.join(HERE, "..", "..", "python"), os.path.join(HERE, "..", "solver_plugin"),      # chạy từ kho mã
          os.path.join(HERE, "..", "..", "solvers.d")):                                           # bản cài: share/fastcae/solvers.d
    sys.path.insert(0, p)
import fastcae

CASE = """type = elastic
geometry = box 1.0 0.1 0.1
divisions = 20 3 3
order = 2
E = 210e9
nu = 0.3
fix = xmin
load = xmax 0 0 -1000
"""


def giai(a):
    """a = mô hình dạng mảng numpy (a['nodes'], a['tet10'], a['material'], a['dirichlet'], a['loads']).
    Trả về (chuyển vị (n,3), trường vô hướng). Ở đây dùng sẵn FEM T10 của my_fem; thay bằng thuật toán của bạn."""
    import my_fem
    P, conn, vec, sca = my_fem.solve({"mesh": {"nodes": a["nodes"].ravel().tolist(), "tet10": a["tet10"].ravel().tolist()},
                                      "material": a["material"], "analysis": a["analysis"],
                                      "dirichlet": a["dirichlet"], "loads": a["loads"]})
    return vec["displacement"], sca


m = fastcae.model(CASE)                 # 1) FastCAE dựng mô hình (lưới, tập nút, liên kết, tải) - chưa giải
a = fastcae.model_arrays(m)
print("Mô hình:", len(a["nodes"]), "nút,", len(a["tet10"]), "phần tử T10")
u, scal = giai(a)                       # 2) thuật toán của bạn
L, P, E, I = 1.0, 1000.0, 210e9, 0.1 ** 4 / 12
print("Độ võng đầu tự do: %.6g m   (lý thuyết dầm %.6g m)" % (-u[:, 2].min(), P * L ** 3 / (3 * E * I)))
out = os.path.join(os.path.expanduser("~"), "bai2_ket_qua.vtk") if not os.access(HERE, os.W_OK) else os.path.join(HERE, "bai2_ket_qua.vtk")
fastcae.write_vtk(out, a["nodes"], a["tet4"], point_vectors={"displacement": u}, point_scalars=scal)
fastcae.show(out)                       # 3) hiện lên khung 3D (chỉ khi chạy trong bảng Lập trình)
