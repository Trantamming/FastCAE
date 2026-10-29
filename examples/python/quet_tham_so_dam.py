# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Trần Minh Tâm
"""Quét chiều cao dầm đơn giản bằng Python; so với lý thuyết 5qL^4/(384EI). Cần fastcae-cli (biến FASTCAE_CLI hoặc PATH).
Chạy:  python quet_tham_so_dam.py      (nếu có matplotlib sẽ vẽ biểu đồ)"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))   # tìm python/fastcae.py khi chạy từ kho mã
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "python"))
import fastcae

L, q, b, n, E = 6.0, 12000.0, 0.15, 8, 210e9
rows = []
for k in range(7):
    h = 0.20 + 0.05 * k
    text = fastcae.case(
        type="frame", E=E, nu=0.3, rho=7850, yield_=235e6,
        section=f"S rect {b} {h}",
        node=[f"{i + 1} {L * i / n} 0 0" for i in range(n + 1)],
        member=[f"{i + 1} {i + 2} S beam" for i in range(n)],
        udl=[f"{i + 1} 0 0 {-q}" for i in range(n)],
        support=["1 x y z rx", f"{n + 1} y z"],
    ).replace("yield_", "yield")
    res = fastcae.run(text).raise_for_status()
    exact = 5 * q * L ** 4 / (384 * E * b * h ** 3 / 12)
    fe = res.values["max_displacement"]
    rows.append((h, fe, exact))
    print(f"h = {h:.2f} m   võng FE = {fe * 1000:8.3f} mm   lý thuyết = {exact * 1000:8.3f} mm   lệch {100 * abs(fe - exact) / exact:.4f} %")

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.plot([r[0] for r in rows], [r[1] * 1000 for r in rows], "o-", label="FastCAE (khung 1D)")
    plt.plot([r[0] for r in rows], [r[2] * 1000 for r in rows], "--", label="lý thuyết")
    plt.xlabel("h (m)"); plt.ylabel("độ võng giữa nhịp (mm)"); plt.legend(); plt.grid(True)
    plt.savefig("quet_tham_so_dam.png", dpi=120)
    print("Đã vẽ quet_tham_so_dam.png")
except ImportError:
    print("(Không có matplotlib: bỏ qua phần vẽ biểu đồ)")
