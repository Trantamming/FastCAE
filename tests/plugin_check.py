#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Kiểm tra hợp đồng "mô hình vào - kết quả ra": FastCAE dựng mô hình, solver Python tự lập trình (plugin my_fem) giải,
# kết quả phải khớp solver nội và lý thuyết. Dùng: plugin_check.py <fastcae-cli>
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.abspath(sys.argv[1])
PLUG = os.path.join(ROOT, "examples", "solver_plugin")
os.environ["FASTCAE_CLI"] = CLI
os.environ["FASTCAE_SOLVER_PLUGINS"] = PLUG
sys.path.insert(0, os.path.join(ROOT, "python"))
sys.path.insert(0, PLUG)
import numpy as np
import fastcae
import my_fem

fails = []
def check(name, ok, info=""):
    print(("OK   " if ok else "LOI  ") + name + (" " + info if info else ""))
    if not ok:
        fails.append(name)

CANTI = """type = elastic
geometry = box 1.0 0.1 0.1
divisions = 20 3 3
order = %d
E = 210e9
nu = 0.3
fix = xmin
load = xmax 0 0 -1000
"""

def run_json(text, d, name):
    p = os.path.join(d, name + ".fcae")
    open(p, "w").write(text)
    r = subprocess.run([CLI, "run", p, "--json"], capture_output=True, text=True)
    return json.loads(r.stdout)

with tempfile.TemporaryDirectory() as d:
    beam = 1000 * 1.0 ** 3 / (3 * 210e9 * 0.1 ** 4 / 12)
    for order in (1, 2):
        ref = run_json(CANTI % order, d, "ref%d" % order)
        ext = run_json((CANTI % order) + "solver = my_fem\n", d, "ext%d" % order)
        check("T%d: solver ngoai chay duoc" % (4 if order == 1 else 10), ref.get("ok") and ext.get("ok"), str(ext.get("message")))
        if not (ref.get("ok") and ext.get("ok")):
            continue
        a, b = ref["values"]["max_displacement"], ext["values"]["max_displacement"]
        check("T%d: |u|max trung solver noi" % (4 if order == 1 else 10), abs(a - b) / a < 1e-6, "%.8g vs %.8g" % (a, b))
        if order == 2:
            check("T10: |u|max khop ly thuyet dam (<1%)", abs(b - beam) / beam < 0.01, "%.6g vs %.6g" % (b, beam))

    # thermal 1D: T tuyen tinh theo x
    th = "type = thermal\ngeometry = box 1.0 0.1 0.1\ndivisions = 10 2 2\nk = 50\ntemp = xmin 100\ntemp = xmax 0\nsolver = my_fem\n"
    t = run_json(th, d, "th")
    check("thermal: solver ngoai", t.get("ok") and abs(t["values"]["t_max"] - 100) < 1e-6 and abs(t["values"]["t_min"]) < 1e-6)

    # API Python: model -> thuat toan tu lap trinh -> write_vtk -> read_vtk
    m = fastcae.model(CANTI % 2)
    check("model(): co tet10 va tap nut", "tet10" in m["mesh"] and "xmin" in m["node_sets"] and m["order"] == 2)
    P, conn, vec, sca = my_fem.solve(m)
    out = fastcae.write_vtk(os.path.join(d, "k.vtk"), P, conn[:, :4], point_scalars=sca, point_vectors=vec)
    v = fastcae.read_vtk(out)
    check("write_vtk/read_vtk khu hoi", len(v["points"]) == len(P) and "displacement" in v["vectors"] and "von_mises_nodal" in v["scalars"])
    uz = np.array(v["vectors"]["displacement"])[:, 2].min()
    check("uz dau tu do ~ -1.905e-4", abs(uz + beam) / beam < 0.01, "%.6g" % uz)

    # loi phai duoc bao ro: ket qua thieu
    bad = os.path.join(d, "bad")
    os.makedirs(bad)
    open(os.path.join(bad, "bad.fsolver"), "w").write("id = bad_fem\nname = bad\nstep = true | -c x\n")
    env = dict(os.environ, FASTCAE_SOLVER_PLUGINS=bad)
    open(os.path.join(d, "bad.fcae"), "w").write((CANTI % 1) + "solver = bad_fem\n")
    r = subprocess.run([CLI, "run", os.path.join(d, "bad.fcae"), "--json"], capture_output=True, text=True, env=env)
    j = json.loads(r.stdout)
    check("solver khong ghi ket qua bi bao loi", (not j["ok"]) and "_result.vtk" in j["message"], j.get("message", ""))
    # solver la 'khong biet'
    open(os.path.join(d, "unk.fcae"), "w").write((CANTI % 1) + "solver = khong_co\n")
    r = subprocess.run([CLI, "run", os.path.join(d, "unk.fcae"), "--json"], capture_output=True, text=True)
    j = json.loads(r.stdout)
    check("solver khong biet bi bao loi", not j["ok"] and "khong_co" in j["message"])

print("\n%d that bai" % len(fails) if fails else "\nTat ca dat")
sys.exit(1 if fails else 0)
