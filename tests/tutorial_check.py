#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Chạy các bài thực hành trong examples/tutorial và kiểm tra số liệu. Dùng: tutorial_check.py <fastcae-cli>
import os, re, subprocess, sys, tempfile, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
env = dict(os.environ, FASTCAE_CLI=os.path.abspath(sys.argv[1]), PYTHONUTF8="1", PYTHONIOENCODING="utf-8")   # Windows: ép con in UTF-8
fails = []
with tempfile.TemporaryDirectory() as d:
    tut = os.path.join(d, "examples", "tutorial")        # bản sao để file .vtk không ghi vào kho mã
    shutil.copytree(os.path.join(ROOT, "examples"), os.path.join(d, "examples"))
    shutil.copytree(os.path.join(ROOT, "python"), os.path.join(d, "python"))
    for name, pat, lo, hi in (("bai3_cfd_python.py", r"u tại tâm = (-?[\d.]+)", -0.2078, -0.2038),
                              ("bai2_python_fem.py", r"Độ võng đầu tự do: ([\d.e-]+) m", 1.88e-4, 1.92e-4)):
        r = subprocess.run([sys.executable, os.path.join(tut, name)], capture_output=True, text=True, env=env, encoding="utf-8", errors="replace")
        m = re.search(pat, r.stdout)
        ok = r.returncode == 0 and m and lo <= float(m.group(1)) <= hi and "@@FASTCAE_SHOW@@" in r.stdout
        print(("OK   " if ok else "LOI  ") + name, m.group(1) if m else (r.stderr or "")[-300:])
        if not ok:
            fails.append(name)
    # bài 1 với solver tự viết
    text = open(os.path.join(tut, "bai1_dam_fem_tu_viet.fcae"), encoding="utf-8", errors="replace").read().replace("#solver = my_fem", "solver = my_fem")
    p = os.path.join(tut, "b1.fcae")
    open(p, "w", encoding="utf-8", errors="replace").write(text)
    e2 = dict(env, FASTCAE_SOLVER_PLUGINS=os.path.join(d, "examples", "solver_plugin"))
    r = subprocess.run([env["FASTCAE_CLI"], "run", p], capture_output=True, text=True, env=e2, encoding="utf-8", errors="replace")
    m = re.search(r"\|u\|max = ([\d.e-]+)", r.stdout)
    ok = r.returncode == 0 and m and abs(float(m.group(1)) - 1.90524e-4) < 1e-8
    print(("OK   " if ok else "LOI  ") + "bai1", m.group(1) if m else (r.stdout or "")[-300:])
    if not ok:
        fails.append("bai1")
sys.exit(1 if fails else 0)
