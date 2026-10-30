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
    # bài 5: khung không gian, FEM tự viết đối chiếu solver khung của FastCAE
    r = subprocess.run([sys.executable, os.path.join(tut, "bai5_khung_khong_gian.py"), "--khong-hien-thi"], capture_output=True, text=True, env=env, encoding="utf-8", errors="replace")
    o = r.stdout
    cm = re.search(r"lệch ([\d.]+)%\); tần số [\d.]+ Hz \(lý thuyết [\d.]+, lệch ([\d.]+)%", o)
    eq = re.search(r"tổng phản lực Rz = ([\d.]+) N, tổng tải đứng = ([\d.]+) N", o)
    du = re.search(r"sai khác chuyển vị lớn nhất = [\d.e+-]+ \(([\d.e+-]+) so với", o)
    df = re.search(r"sai khác tần số riêng lớn nhất = ([\d.e+-]+) Hz", o)
    ok = (r.returncode == 0 and cm and float(cm.group(1)) < 0.1 and float(cm.group(2)) < 1.0 and eq
          and abs(float(eq.group(1)) - float(eq.group(2))) < 1e-3 * float(eq.group(2))
          and du and float(du.group(1)) < 1e-6 and df and float(df.group(1)) < 1e-4
          and os.path.isfile(os.path.join(tut, "bai5_khung_khong_gian.vtk")))
    print(("OK   " if ok else "LOI  ") + "bai5", (r.stderr[-300:] if not ok else ""))
    if not ok:
        print(o[-800:])
        fails.append("bai5")
    # bản dán thẳng vào bảng Lập trình (Lua và Python), chạy từ thư mục khác
    r = subprocess.run([env["FASTCAE_CLI"], "script", os.path.join(tut, "dan_vao", "bai5_lua_dan_vao_bang_lap_trinh.lua")], capture_output=True, text=True, env=env, encoding="utf-8", cwd=d)
    m = re.search(r"sai khác chuyển vị lớn nhất = ([\d.e+-]+)", r.stdout)
    ok = r.returncode == 0 and m and float(m.group(1)) < 1e-9 and "lệch 0.0000%" in r.stdout
    print(("OK   " if ok else "LOI  ") + "dan_vao lua", r.stdout[-300:] if not ok else "")
    if not ok: fails.append("dan_vao_lua")
    env_py = dict(env, PYTHONPATH=os.path.join(d, "python"))
    r = subprocess.run([sys.executable, os.path.join(tut, "dan_vao", "bai5_python_dan_vao_bang_lap_trinh.py")], capture_output=True, text=True, env=env_py, encoding="utf-8", cwd=d)
    m = re.search(r"sai khác chuyển vị lớn nhất = ([\d.e+-]+)", r.stdout)
    ok = r.returncode == 0 and m and float(m.group(1)) < 1e-9 and "@@FASTCAE_SHOW@@" in r.stdout
    print(("OK   " if ok else "LOI  ") + "dan_vao python", r.stderr[-300:] if not ok else "")
    if not ok: fails.append("dan_vao_py")
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
