#!/usr/bin/env python3
"""Kiểm chứng CAD tham số: thể tích lưới so với công thức giải tích. Cần gmsh trong PATH.
Dùng:  python3 validate.py <đường_dẫn_fastcae-cli>"""
import math, subprocess, sys, tempfile, os, re
cli = sys.argv[1]
pi = math.pi
cases = []
def case(name, text, expect, tol, sets=None, size=0.0):
    cases.append((name, text, expect, tol, sets or [], size))

case("box", "box a 0 0 0 0.2 0.1 0.05\n", 0.2*0.1*0.05, 1e-6, size=0.03)
case("cylinder", "cylinder a 0 0 0 0 0 0.3 0.05\n", pi*0.05**2*0.3, 0.02, size=0.01)
case("sphere", "sphere a 0 0 0 0.1\n", 4/3*pi*0.1**3, 0.03, size=0.015)
case("cone", "cone a 0 0 0 0 0 0.2 0.08 0.02\n", pi*0.2/3*(0.08**2+0.08*0.02+0.02**2), 0.03, size=0.01)
case("cut", "box a 0 0 0 0.2 0.1 0.05\ncylinder h 0.1 0.05 -0.01 0 0 0.07 0.02\ncut r a h\n",
     0.2*0.1*0.05 - pi*0.02**2*0.05, 0.02, size=0.01)
case("fuse", "box a 0 0 0 0.1 0.1 0.1\nbox b 0.05 0 0 0.1 0.1 0.1\nfuse r a b\n", 0.15*0.1*0.1, 1e-6, size=0.03)
case("intersect", "box a 0 0 0 0.1 0.1 0.1\nbox b 0.05 0 0 0.1 0.1 0.1\nintersect r a b\n", 0.05*0.1*0.1, 1e-6, size=0.03)
case("extrude_polygon", "sketch s xy 0\n polygon 0 0 0.1 0 0.1 0.05 0.05 0.05 0.05 0.1 0 0.1\nend\nextrude r s 0.02\n",
     (0.1*0.05+0.05*0.05)*0.02, 1e-6, size=0.02)
case("extrude_hole", "sketch s xy 0\n rect 0 0 0.1 0.1\n circle 0.05 0.05 0.02\nend\nextrude r s 0.03\n",
     (0.01-pi*0.02**2)*0.03, 0.03, size=0.008)
case("extrude_yz_circle", "sketch s yz 0.1\n circle 0.02 0.03 0.015\nend\nextrude r s 0.2\n", pi*0.015**2*0.2, 0.03, size=0.006)
case("revolve_washer", "sketch s xz 0\n rect 0.03 0 0.02 0.01\nend\nrevolve r s 360 0 0 0 0 0 1\n",
     pi*(0.05**2-0.03**2)*0.01, 0.03, size=0.005)
case("revolve_half", "sketch s xz 0\n rect 0.03 0 0.02 0.01\nend\nrevolve r s 180 0 0 0 0 0 1\n",
     0.5*pi*(0.05**2-0.03**2)*0.01, 0.03, size=0.005)
case("linear_pattern", "box a 0 0 0 0.02 0.02 0.02\nlinear_pattern r a 4 0.05 0 0\n", 4*0.02**3, 1e-6, size=0.01)
case("circular_pattern", "box a 0.05 -0.01 0 0.02 0.02 0.02\ncircular_pattern r a 6 0 0 0 0 0 1 360\n", 6*0.02**3, 1e-6, size=0.01)
case("mirror", "box a 0.01 0 0 0.05 0.04 0.03\nmirror r a 1 0 0 0\n", 2*0.05*0.04*0.03, 1e-6, size=0.01)
case("translate_rotate", "box a 0 0 0 0.1 0.02 0.02\ntranslate b a 0.5 0 0\nrotate r b 0.5 0 0 0 0 1 90\n", 0.1*0.02*0.02, 1e-6, size=0.01)
# bo góc / vát cạnh MỘT cạnh (cần Python + gói gmsh): thể tích bị bớt theo công thức hình học
a, b, c, r = 0.1, 0.06, 0.04, 0.005
case("fillet_one_edge", "box a 0 0 0 0.1 0.06 0.04\nfillet 0.005 -0.001 -0.001 -0.001 0.001 0.001 0.041\n",
     a*b*c - (1-pi/4)*r*r*c, 0.002, size=0.008)
case("chamfer_one_edge", "box a 0 0 0 0.1 0.06 0.04\nchamfer 0.004 -0.001 -0.001 0.039 0.001 0.061 0.041\n",
     a*b*c - 0.5*0.004**2*b, 0.002, size=0.008)
# phác thảo có ràng buộc (csketch): hình dựng từ ràng buộc, KHÔNG nhập tọa độ đúng; điểm đầu cố ý lệch
case("csk_rect", "param L = 0.12\nparam W = 0.05\ncsketch s xy 0\n pt A 0 0\n pt B 0.1 0.01\n pt C 0.11 0.06\n pt D -0.01 0.04\n"
     " line ab A B\n line bc B C\n line cd C D\n line da D A\n fix A 0 0\n horizontal ab\n vertical bc\n horizontal cd\n vertical da\n"
     " length ab L\n length bc W\n loop A B C D\nend\nextrude r s 0.01\n", 0.12*0.05*0.01, 1e-6, size=0.02)
case("csk_rect_driven", "param L = 0.12\nparam W = 0.05\ncsketch s xy 0\n pt A 0 0\n pt B 0.1 0.01\n pt C 0.11 0.06\n pt D -0.01 0.04\n"
     " line ab A B\n line bc B C\n line cd C D\n line da D A\n fix A 0 0\n horizontal ab\n vertical bc\n horizontal cd\n vertical da\n"
     " length ab L\n length bc W\n loop A B C D\nend\nextrude r s 0.01\n", 0.2*0.05*0.01, 1e-6, sets=["L=0.2"], size=0.02)
case("csk_triangle_345", "csketch s xy 0\n pt A 0 0\n pt B 0.05 0.002\n pt C 0.01 0.03\n line ab A B\n fix A 0 0\n horizontal ab\n"
     " dist A B 0.05\n dist A C 0.03\n dist B C 0.04\n loop A B C\nend\nextrude r s 0.01\n", 0.5*0.03*0.04*0.01, 1e-6, size=0.01)
case("csk_parallelogram_angle", "csketch s xy 0\n pt A 0 0\n pt B 0.04 0\n pt C 0.06 0.03\n pt D 0.02 0.03\n"
     " line ab A B\n line ad A D\n line bc B C\n line dc D C\n fix A 0 0\n horizontal ab\n length ab 0.04\n angle ab ad 60\n length ad 0.03\n"
     " parallel bc ad\n parallel dc ab\n loop A B C D\nend\nextrude r s 0.01\n", 0.04*0.03*math.sin(math.radians(60))*0.01, 1e-6, size=0.01)
case("csk_hole_by_constraints", "csketch s xy 0\n pt A 0 0\n pt B 0.1 0\n pt C 0.1 0.06\n pt D 0 0.06\n pt O 0.05 0.02\n pt F 0.05 0\n circle h O 0.01\n"
     " line ab A B\n line bc B C\n line cd C D\n line da D A\n line of O F\n fix A 0 0\n horizontal ab\n vertical bc\n horizontal cd\n vertical da\n length ab 0.1\n length bc 0.06\n"
     " on F ab\n vertical of\n length of 0.03\n radius h 0.012\n loop A B C D\n loopcircle h\nend\nextrude r s 0.01\n",
     (0.1*0.06 - pi*0.012**2)*0.01, 0.03, size=0.008)
# cung tròn trong phác thảo ràng buộc
slot = ("param L = 0.1\nparam R = 0.02\ncsketch s xy 0\n pt A 0 0\n pt B 0.1 0\n pt C 0.1 0.04\n pt D 0 0.04\n pt O2 0.1 0.02\n pt O1 0 0.02\n"
        " line ab A B\n line cd C D\n arc r2 O2 B C\n arc r1 O1 D A\n fix A 0 0\n horizontal ab\n length ab L\n radius r2 R\n radius r1 R\n"
        " tangent ab r2\n tangent cd r2\n tangent ab r1\n tangent cd r1\n wire ab r2 cd r1\nend\nextrude r s 0.01\n")
case("csk_slot_arcs", slot, (0.1*0.04 + pi*0.02**2)*0.01, 0.02, size=0.006)
case("csk_slot_driven", slot, (0.15*0.03 + pi*0.015**2)*0.01, 0.02, sets=["L=0.15", "R=0.015"], size=0.006)
case("csk_pacman_270", "param R = 0.03\ncsketch s xy 0\n pt O 0 0\n pt P 0.03 0\n pt Q 0 -0.03\n line l1 O P\n line l2 Q O\n arc a O P Q\n fix O 0 0\n fix P R 0\n fix Q 0 -R\n"
     " wire l1 a l2\nend\nextrude r s 0.01\n", 0.75*pi*0.03**2*0.01, 0.03, size=0.006)
case("csk_arc_on_yz", "csketch s yz 0.05\n pt O 0 0\n pt P 0.02 0\n pt Q 0 0.02\n line l1 O P\n line l2 Q O\n arc a O P Q\n fix O 0 0\n fix P 0.02 0\n fix Q 0 0.02\n"
     " wire l1 a l2\nend\nextrude r s 0.01\n", 0.25*pi*0.02**2*0.01, 0.03, size=0.004)
case("param_expr", "param R = 0.02\nparam H = 3*R + 0.01\ncylinder a 0 0 0 0 0 H R\n", pi*0.02**2*0.07, 0.02, size=0.006)
case("param_override", "param R = 0.02\ncylinder a 0 0 0 0 0 0.1 R\n", pi*0.03**2*0.1, 0.02, sets=["R=0.03"], size=0.008)

bad = [
 ("undeclared_param", "box a 0 0 0 X 1 1\n", "tham số chưa khai báo"),
 ("unknown_cmd", "foo a\n", "lệnh không có"),
 ("two_bodies", "box a 0 0 0 1 1 1\nbox b 3 0 0 1 1 1\n", "nhiều thân"),
 ("consumed", "box a 0 0 0 1 1 1\nbox b 0.5 0 0 1 1 1\nfuse c a b\ncut d a b\n", "đã bị dùng"),
 ("div_zero", "param A = 1/0\n", "chia cho 0"),
 ("fillet_no_edge", "box a 0 0 0 1 1 1\nfillet 0.01 5 5 5 6 6 6\n", "không có canh|khong co canh"),
 ("csk_conflict", "csketch s xy 0\n pt A 0 0\n pt B 0.1 0\n fix A 0 0\n dist A B 0.1\n dist A B 0.2\n pt C 0 0.1\n loop A B C\nend\nextrude r s 0.01\n", "mâu thuẫn"),
 ("csk_unknown_pt", "csketch s xy 0\n pt A 0 0\n dist A Z 0.1\nend\n", "không có trong phác thảo"),
 ("csk_open_wire", "csketch s xy 0\n pt A 0 0\n pt B 0.1 0\n pt C 0.1 0.1\n pt D 0 0.1\n line ab A B\n line bc B C\n line cd C D\n fix A 0 0\n fix B 0.1 0\n fix C 0.1 0.1\n fix D 0 0.1\n wire ab bc cd\nend\n", "không khép kín"),
 ("unclosed_sketch", "sketch s xy 0\n rect 0 0 1 1\n", "thiếu 'end'"),
]

try:
    import gmsh  # noqa: F401
except Exception:   # không có Gmsh Python: bỏ các ca bo góc/vát cạnh
    cases = [c for c in cases if "fillet" not in c[0] and "chamfer" not in c[0]]
    bad = [b for b in bad if not b[0].startswith("fillet")]
    print("(bỏ qua ca bo góc/vát cạnh: không có gói Python gmsh)")
fails = 0
for name, text, expect, tol, sets, size in cases:
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, "m.fcad"); open(f, "w", encoding="utf-8").write(text)
        cmd = [cli, "param", f, "--size", str(size)]
        for s in sets: cmd += ["--set", s]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        m = re.search(r"The tich = ([0-9.eE+-]+)", p.stdout)
        if p.returncode != 0 or not m:
            print(f"FAIL {name}: {p.stderr.strip()[:300]}"); fails += 1; continue
        v = float(m.group(1)); err = abs(v-expect)/expect
        ok = err <= tol
        print(f"{'ok  ' if ok else 'FAIL'} {name:18s} V={v:.6e} lý thuyết={expect:.6e} sai số={err*100:.3f}% (cho phép {tol*100:g}%)")
        fails += not ok
for name, text, needle in bad:
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, "m.fcad"); open(f, "w", encoding="utf-8").write(text)
        p = subprocess.run([cli, "param", f], capture_output=True, text=True, timeout=60)
        ok = p.returncode != 0 and any(n in p.stderr for n in needle.split("|"))
        print(f"{'ok  ' if ok else 'FAIL'} lỗi:{name:16s} -> {p.stderr.strip()[:90]}")
        fails += not ok
# cảnh báo: thiếu ràng buộc / dư ràng buộc (hình vẫn dựng được, nhưng phải báo)
def warn(name, text, needle):
    global fails
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, "m.fcad"); open(f, "w", encoding="utf-8").write(text)
        p = subprocess.run([cli, "param", f, "--size", "0.02"], capture_output=True, text=True, timeout=60)
        ok = p.returncode == 0 and needle in p.stderr
        print(f"{'ok  ' if ok else 'FAIL'} cảnh báo:{name:12s} -> {p.stderr.strip()[:110]}")
        fails += not ok
base = "csketch s xy 0\n pt A 0 0\n pt B 0.1 0\n pt C 0.1 0.05\n pt D 0 0.05\n line ab A B\n line bc B C\n line cd C D\n line da D A\n fix A 0 0\n horizontal ab\n vertical bc\n horizontal cd\n vertical da\n length ab 0.1\n"
warn("thieu_rang_buoc", base + " loop A B C D\nend\nextrude r s 0.01\n", "bậc tự do")
warn("du_rang_buoc", base + " length bc 0.05\n length da 0.05\n loop A B C D\nend\nextrude r s 0.01\n", "dư")
# phác thảo rãnh (đường + cung tiếp xúc) phải ràng buộc ĐỦ: không cảnh báo thiếu/dư
with tempfile.TemporaryDirectory() as d:
    f = os.path.join(d, "m.fcad"); open(f, "w", encoding="utf-8").write(slot)
    p = subprocess.run([cli, "param", f, "--size", "0.01"], capture_output=True, text=True, timeout=60)
    ok = p.returncode == 0 and "CANH BAO" not in p.stderr
    print(f"{'ok  ' if ok else 'FAIL'} rãnh cung: ràng buộc đủ, không cảnh báo -> {p.stderr.strip()[:100]}")
    fails += not ok
print("TỔNG: %d lỗi" % fails)
sys.exit(1 if fails else 0)
