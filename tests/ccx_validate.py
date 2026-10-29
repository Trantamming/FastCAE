#!/usr/bin/env python3
"""Kiểm chứng với CalculiX thật: dao động riêng và động lực của dầm console 1 x 0,1 x 0,1 m (thép), đối chiếu lý thuyết dầm.
Mỗi dạng riêng / mốc thời gian phải thành một khung trong .fcres. Dùng: python3 ccx_validate.py <fastcae-cli>"""
import math, os, re, subprocess, sys, tempfile
cli = sys.argv[1]
E, rho, L, b = 210e9, 7850.0, 1.0, 0.1
I, A = b**4 / 12, b * b
f1 = 1.8751**2 / (2 * math.pi) * math.sqrt(E * I / (rho * A * L**4))
dst = 1000 / (3 * E * I / L**3)
base = "type = elastic\nsolver = calculix\ngeometry = box 1.0 0.1 0.1\ndivisions = 20 3 3\norder = 2\nE = 210e9\nnu = 0.3\nrho = 7850\nfix = xmin\n"
fails = 0
def run(name, extra):
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, "c.fcae"), "w").write(base + extra + f"output = o.vtk\nresult_file = o.fcres\nwork_dir = w\n")
        p = subprocess.run([cli, "run", os.path.join(d, "c.fcae")], capture_output=True, text=True, timeout=250)
        if p.returncode != 0: return None, p.stdout + p.stderr
        i = subprocess.run([cli, "result", "info", os.path.join(d, "o.fcres")], capture_output=True, text=True).stdout
        n = len([x for x in os.listdir(d) if re.match(r"o_\d{4}\.vtk", x)])
        return (i, n, p.stdout), ""
r, err = run("freq", "analysis = frequency\nmodes = 5\n")
if r is None: print("FAIL dao dong rieng:", err[-300:]); fails += 1
else:
    info, nvtk, _ = r
    m = re.search(r"frequency_1_hz = ([0-9.eE+-]+)", info); got = float(m.group(1))
    ok = abs(got - f1) / f1 < 0.005 and "chuoi buoc: 5 khung" in info and nvtk == 5
    print(f"{'ok  ' if ok else 'FAIL'} dao dong rieng: f1 = {got:.3f} Hz, ly thuyet {f1:.3f} Hz ({abs(got-f1)/f1*100:.2f}%), khung = 5, file vtk = {nvtk}"); fails += not ok
r, err = run("dyn", "analysis = dynamic\ntime = 0.036\nincrements = 36\nload = xmax 0 0 -1000\n")
if r is None: print("FAIL dong luc:", err[-300:]); fails += 1
else:
    info, nvtk, out36 = r
    warn36 = "CANH BAO: buoc thoi gian qua tho" in out36
    print(f"{'ok  ' if warn36 else 'FAIL'} dong luc 36 buoc (12 buoc/chu ky): phai canh bao buoc thoi gian tho"); fails += not warn36
    peak = float(re.search(r"max_displacement = ([0-9.eE+-]+)", info).group(1))
    ok = abs(peak - 2 * dst) / (2 * dst) < 0.05 and "chuoi buoc: 36 khung" in info and nvtk == 36
    vm = float(re.search(r"max_von_mises = ([0-9.eE+-]+)", info).group(1))
    sst = 1000 * L * (b / 2) / I          # ứng suất uốn tĩnh tại ngàm = 6 MPa (lý thuyết dầm)
    ok_vm = 1.8 * sst < vm < 2.6 * sst and "day so max_von_mises_history: 36 diem" in info and "day so times: 36 diem" in info
    print(f"{'ok  ' if ok_vm else 'FAIL'} dong luc: dinh von Mises TAI NGAM (diem ky di, chi kiem tra co xuat ung suat) = {vm/1e6:.2f} MPa, tinh ly thuyet {sst/1e6:.2f} MPa, he so {vm/sst:.2f} (cho phep 1.8-2.6), lich su 36 diem")
    fails += not ok_vm
    print(f"{'ok  ' if ok else 'FAIL'} dong luc: dinh = {peak:.4e} m, 2 x tinh = {2*dst:.4e} m ({abs(peak-2*dst)/(2*dst)*100:.2f}%), khung = 36, file vtk = {nvtk}"); fails += not ok
r, err = run("dyn144", "analysis = dynamic\ntime = 0.036\nincrements = 144\nload = xmax 0 0 -1000\n")
if r is None: print("FAIL dong luc 144 buoc:", err[-300:]); fails += 1
else:
    nowarn = "CANH BAO: buoc thoi gian" not in r[2] and "steps_per_period" in r[0]
    print(f"{'ok  ' if nowarn else 'FAIL'} dong luc 144 buoc (48 buoc/chu ky): khong canh bao, co ghi steps_per_period"); fails += not nowarn
sys.exit(1 if fails else 0)
