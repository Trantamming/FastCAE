#!/usr/bin/env python3
"""Hội tụ theo bước thời gian của hệ số động (tải bước nhảy ở đầu dầm console, CalculiX thật).
Dùng: python3 dynamic_time_convergence.py <fastcae-cli> (chạy trong thư mục làm việc tùy ý; cần ccx)."""
import sys
cli = sys.argv[1] if len(sys.argv) > 1 else "fastcae-cli"
L=1.0; b=0.1; I=b**4/12; F=1000.0; E=210e9
st_mid = F*(L/2)*(b/2)/I; st_tip = F*L**3/(3*E*I)
print(f"{'buoc':>5s} {'dt (ms)':>8s} {'he so dong ung suat':>20s} {'he so dong vong':>16s}")
for ns in (18, 36, 72, 144):
    tag=f"dt_{ns}"
    open(tag+".fcae","w").write(f"type = elastic\nsolver = calculix\nanalysis = dynamic\ntime = 0.036\nincrements = {ns}\ngeometry = box 1.0 0.1 0.1\ndivisions = 20 3 3\norder = 2\nE = 210e9\nnu = 0.3\nrho = 7850\nfix = xmin\nload = xmax 0 0 -1000\nresult_file = {tag}.fcres\nwork_dir = w_{tag}\n")
    p = subprocess.run([cli,"run",tag+".fcae"],capture_output=True,text=True,timeout=280)
    if p.returncode: print(tag,"LOI"); continue
    bm=0; bu=0
    for k in range(1,ns+1):
        subprocess.run([cli,"result","csv",tag+".fcres",tag+".csv","--frame",str(k)],capture_output=True)
        rows=list(csv.DictReader(open(tag+".csv")))
        mid=[float(r["von_mises_nodal"]) for r in rows if abs(float(r["x"])-0.5)<0.026 and abs(float(r["z"])-0.1)<1e-9]
        bm=max(bm,sum(mid)/len(mid)); bu=max(bu,max(float(r["displacement_magnitude"]) for r in rows))
    print(f"{ns:5d} {36.0/ns:8.2f} {bm/st_mid:20.3f} {bu/st_tip:16.3f}")
