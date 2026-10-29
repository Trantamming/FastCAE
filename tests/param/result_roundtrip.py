#!/usr/bin/env python3
"""Lưu kết quả .fcres rồi xuất VTK/CSV: VTK xuất lại phải trùng từng byte với VTK ghi trực tiếp."""
import subprocess, sys, tempfile, os
cli = sys.argv[1]
case = """type = elastic
geometry = box 1.0 0.1 0.1
divisions = 10 2 2
order = 2
E = 210e9
nu = 0.3
fix = xmin
load = xmax 0 0 -1000
output = a.vtk
result_file = a.fcres
"""
with tempfile.TemporaryDirectory() as d:
    open(os.path.join(d, "c.fcae"), "w").write(case)
    r = subprocess.run([cli, "run", os.path.join(d, "c.fcae")], capture_output=True, text=True); assert r.returncode == 0, r.stdout + r.stderr
    for sub, out in (("vtk", "b.vtk"), ("csv", "b.csv")):
        r = subprocess.run([cli, "result", sub, os.path.join(d, "a.fcres"), os.path.join(d, out)], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
    a = open(os.path.join(d, "a.vtk")).read().split("\n", 2)[2]; b = open(os.path.join(d, "b.vtk")).read().split("\n", 2)[2]
    assert a == b, "VTK xuất lại khác bản gốc"
    assert open(os.path.join(d, "b.csv")).readline().startswith("node_id,x,y,z,displacement_magnitude")
    # file hỏng phải bị từ chối, không sập
    t = open(os.path.join(d, "a.fcres")).read(); open(os.path.join(d, "bad.fcres"), "w").write(t[: len(t)//2])
    r = subprocess.run([cli, "result", "info", os.path.join(d, "bad.fcres")], capture_output=True, text=True)
    assert r.returncode != 0, "file cụt phải báo lỗi"
print("result_roundtrip OK")
