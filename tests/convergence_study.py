#!/usr/bin/env python3
"""Khảo sát hội tụ lưới của dầm console (chuyển vị đầu, ứng suất ngàm, ứng suất giữa nhịp) với solver nội và CalculiX thật.
Chạy trong thư mục làm việc tùy ý: python3 convergence_study.py <fastcae-cli>   (cần ccx cho nửa sau)"""
import subprocess, os, csv, re, sys
cli = sys.argv[1] if len(sys.argv) > 1 else "fastcae-cli"
E=210e9; L=1.0; b=0.1; I=b**4/12; F=1000.0
th_tip = F*L**3/(3*E*I); th_root = F*L*(b/2)/I; th_mid = F*(L/2)*(b/2)/I
print(f"ly thuyet: vong dau = {th_tip:.4e} m, ung suat ngam = {th_root/1e6:.2f} MPa, giua nhip = {th_mid/1e6:.2f} MPa")
print(f"{'solver':8s} {'luoi':10s} {'nut':>7s} {'vong (m)':>11s} {'sai %':>6s} {'max vM (MPa)':>13s} {'giua nhip (MPa)':>16s} {'sai %':>6s}")
for solver in ("builtin", "calculix"):
    for div in ("10 2 2", "20 3 3", "40 5 5", "60 7 7"):
        tag = f"{solver}_{div.replace(' ','x')}"
        case = f"type = elastic\nsolver = {solver}\ngeometry = box 1.0 0.1 0.1\ndivisions = {div}\norder = 2\nE = 210e9\nnu = 0.3\nfix = xmin\nload = xmax 0 0 -1000\nresult_file = {tag}.fcres\nwork_dir = w_{tag}\n"
        open(f"{tag}.fcae", "w").write(case)
        p = subprocess.run([cli, "run", f"{tag}.fcae"], capture_output=True, text=True, timeout=280)
        if p.returncode != 0: print(tag, "LOI", p.stdout[-200:], p.stderr[-200:]); continue
        subprocess.run([cli, "result", "csv", f"{tag}.fcres", f"{tag}.csv"], capture_output=True)
        rows = list(csv.DictReader(open(f"{tag}.csv")))
        u = max(float(r["displacement_magnitude"]) for r in rows)
        vm = max(float(r["von_mises_nodal"]) for r in rows)
        mid = [float(r["von_mises_nodal"]) for r in rows if abs(float(r["x"])-0.5) < 0.026 and abs(float(r["z"])-0.1) < 1e-9]   # mặt trên (kéo) quanh giữa nhịp
        mid_vm = sum(mid)/len(mid) if mid else float("nan")
        print(f"{solver:8s} {div:10s} {len(rows):7d} {u:11.4e} {100*(u-th_tip)/th_tip:6.2f} {vm/1e6:13.2f} {mid_vm/1e6:16.2f} {100*(mid_vm-th_mid)/th_mid:6.1f}")
