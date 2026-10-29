#!/usr/bin/env python3
"""Ứng suất T10 phải có giá trị ở CẢ nút giữa cạnh (trước đây = 0) và đúng lý thuyết dầm giữa nhịp (3,0 MPa).
Dùng: python3 stress_midnodes.py <fastcae-cli>   (không cần solver ngoài)"""
import csv, os, subprocess, sys, tempfile
cli = sys.argv[1]
case = """type = elastic
geometry = box 1.0 0.1 0.1
divisions = 20 3 3
order = 2
E = 210e9
nu = 0.3
fix = xmin
load = xmax 0 0 -1000
result_file = r.fcres
"""
with tempfile.TemporaryDirectory() as d:
    open(os.path.join(d, "c.fcae"), "w").write(case)
    assert subprocess.run([cli, "run", os.path.join(d, "c.fcae")], capture_output=True, text=True, timeout=120).returncode == 0
    assert subprocess.run([cli, "result", "csv", os.path.join(d, "r.fcres"), os.path.join(d, "r.csv")], capture_output=True).returncode == 0
    rows = list(csv.DictReader(open(os.path.join(d, "r.csv"))))
zeros = sum(1 for r in rows if float(r["von_mises_nodal"]) == 0.0)
top = [float(r["von_mises_nodal"]) for r in rows if abs(float(r["x"]) - 0.5) < 0.026 and abs(float(r["z"]) - 0.1) < 1e-9]
mean = sum(top) / len(top)
th = 1000 * 0.5 * 0.05 / (0.1**4 / 12)
ok = zeros == 0 and abs(mean - th) / th < 0.03
print(f"{'ok  ' if ok else 'FAIL'} nut co von Mises = 0: {zeros} (phai la 0); trung binh mat tren giua nhip (ca nut giua canh) = {mean/1e6:.3f} MPa, ly thuyet {th/1e6:.3f} MPa")
sys.exit(0 if ok else 1)
