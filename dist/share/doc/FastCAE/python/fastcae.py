# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Trần Minh Tâm
"""
FastCAE cho Python: dựng mô hình, chạy bài toán và đọc kết quả bằng Python (numpy, scipy, matplotlib...).

Module chỉ cần thư viện chuẩn: nó gọi chương trình dòng lệnh `fastcae-cli` (lệnh `run --json`,
`script`, `templates`) nên không cần biên dịch hay cài thêm gì. Đặt biến môi trường FASTCAE_CLI nếu
`fastcae-cli` không nằm trong PATH.

    import fastcae
    text = fastcae.template("cantilever_beam", 3.0, 5000, 6)
    res = fastcae.run(text)
    print(res.values["max_displacement"], res.series["node_uz"])
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from typing import Any, Dict, List, Optional, Sequence

__all__ = ["FastCAEError", "Result", "run", "run_file", "run_lua", "case", "template", "templates", "section", "read_vtk", "find_cli",
           "SolverRun", "solvers", "solver_info", "run_solver", "run_program", "code_aster", "openradioss", "su2", "openfoam",
           "calculix", "elmer", "list_files", "show", "model", "model_arrays", "write_vtk"]
__author__ = "Trần Minh Tâm"
__license__ = "MIT"


class FastCAEError(RuntimeError):
    """Lỗi khi gọi fastcae-cli hoặc khi giải bài toán thất bại."""


def find_cli(explicit: Optional[str] = None) -> str:
    """Tìm fastcae-cli: tham số, biến FASTCAE_CLI, PATH, rồi thư mục build/ cạnh module."""
    cands: List[Optional[str]] = [explicit, os.environ.get("FASTCAE_CLI"), shutil.which("fastcae-cli"), shutil.which("fastcae-cli.exe")]
    here = os.path.dirname(os.path.abspath(__file__))
    for rel in ("../build/fastcae-cli", "../build/Release/fastcae-cli.exe", "../bin/fastcae-cli.exe", "../bin/fastcae-cli"):
        cands.append(os.path.normpath(os.path.join(here, rel)))
    for c in cands:
        if c and os.path.isfile(c):
            return c
    raise FastCAEError("Khong tim thay fastcae-cli. Dat bien moi truong FASTCAE_CLI hoac them vao PATH.")


class Result:
    """Kết quả một lần giải (đọc từ JSON của `fastcae-cli run --json`)."""

    def __init__(self, data: Dict[str, Any]):
        self.data = data
        self.ok: bool = bool(data.get("ok"))
        self.message: str = data.get("message", "")
        self.type: str = data.get("type", "")
        self.summary: str = data.get("summary", "")
        self.log: str = data.get("log", "")
        self.nodes: int = int(data.get("nodes", 0))
        self.cells: int = int(data.get("cells", 0))
        self.iterations: int = int(data.get("iterations", 0))
        self.values: Dict[str, float] = {k: (float("nan") if v is None else v) for k, v in data.get("values", {}).items()}
        self.series: Dict[str, List[float]] = data.get("series", {})
        self.fields: Dict[str, Dict[str, float]] = data.get("fields", {})   # min/max/mean của từng trường vô hướng
        self.vector_fields: List[str] = data.get("vector_fields", [])

    def raise_for_status(self) -> "Result":
        if not self.ok:
            raise FastCAEError("Giai that bai: " + self.message)
        return self

    def __repr__(self) -> str:
        return f"<Result ok={self.ok} type={self.type!r} message={self.message!r} values={len(self.values)}>"


def _call(args: Sequence[str], cli: Optional[str], cwd: Optional[str] = None) -> subprocess.CompletedProcess:
    exe = find_cli(cli)
    return subprocess.run([exe, *args], capture_output=True, text=True, encoding="utf-8", cwd=cwd)


def run_file(path: str, cli: Optional[str] = None) -> Result:
    """Chạy một file case (*.fcae). File kết quả `output = ...` được ghi cạnh file case."""
    p = _call(["run", os.path.abspath(path), "--json"], cli)
    out = p.stdout.strip()
    if not out.startswith("{"):
        raise FastCAEError(f"fastcae-cli khong tra ve JSON (ma {p.returncode}): {p.stderr.strip() or out[:200]}")
    return Result(json.loads(out))


def run(case_text: str, workdir: Optional[str] = None, cli: Optional[str] = None) -> Result:
    """Chạy một bài toán cho dưới dạng văn bản case. workdir: nơi đặt file case tạm và file kết quả (mặc định thư mục tạm)."""
    if workdir is None:
        with tempfile.TemporaryDirectory(prefix="fastcae_") as d:
            return _run_in(case_text, d, cli)
    os.makedirs(workdir, exist_ok=True)
    return _run_in(case_text, workdir, cli)


def _run_in(case_text: str, d: str, cli: Optional[str]) -> Result:
    path = os.path.join(d, "case.fcae")
    with open(path, "w", encoding="utf-8") as f:
        f.write(case_text)
    return run_file(path, cli)


def run_lua(code: str, cli: Optional[str] = None) -> str:
    """Chạy một đoạn Lua bằng bộ nhúng của FastCAE và trả về đầu ra (stdout)."""
    p = _call(["script", "-e", code], cli)
    if p.returncode != 0:
        raise FastCAEError(p.stderr.strip() or "script that bai")
    return p.stdout


def case(**kw: Any) -> str:
    """Dựng văn bản case từ tham số; giá trị là danh sách thì lặp khoá (node=[...], member=[...]).
    Khoá `type` luôn đứng đầu."""
    lines = ["# Tao boi Python (fastcae.py) - Tran Minh Tam"]
    keys = sorted(kw, key=lambda k: (k != "type", k))
    for k in keys:
        v = kw[k]
        for item in (v if isinstance(v, (list, tuple)) else [v]):
            lines.append(f"{k} = {item}")
    return "\n".join(lines) + "\n"


def templates(cli: Optional[str] = None) -> List[Dict[str, Any]]:
    """Danh sách cấu kiện trong thư viện: [{name, category, label, params: {key: mặc định}}]."""
    p = _call(["templates"], cli)
    out = []
    for line in p.stdout.splitlines():
        parts = [s.strip() for s in line.split("|")]
        if len(parts) < 4:
            continue
        params = {}
        for tok in parts[3].split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                params[k] = float(v)
        out.append({"name": parts[0], "category": parts[1], "label": parts[2], "params": params})
    return out


def template(name: str, *params: float, cli: Optional[str] = None) -> str:
    """Văn bản case của một cấu kiện/khung/tấm trong thư viện (bỏ trống tham số = mặc định)."""
    p = _call(["template", name, *[repr(float(x)) for x in params]], cli)
    if p.returncode != 0:
        raise FastCAEError(p.stderr.strip() or f"khong tao duoc mau {name}")
    return p.stdout


def section(kind: str, *params: float, cli: Optional[str] = None) -> Dict[str, float]:
    """Đặc trưng tiết diện: kind = rect | circle | pipe | ibeam | box | general. Trả về A, Iy, Iz, J, width, height."""
    args = ", ".join(repr(float(x)) for x in params)
    out = run_lua(f'local s = fastcae.section("{kind}", {args}) print(s.A, s.Iy, s.Iz, s.J, s.width, s.height)', cli)
    a = [float(x) for x in out.split()]
    return dict(zip(("A", "Iy", "Iz", "J", "width", "height"), a))


def read_vtk(path: str) -> Dict[str, Any]:
    """Đọc file .vtk (ASCII, lưới tứ diện) do FastCAE ghi: {points, cells, scalars{tên: [..]}, vectors{tên: [(x,y,z),..]}}.
    Nếu có numpy, dùng np.array(...) trên kết quả để xử lý tiếp."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        tok = f.read().split()
    i = 0
    res: Dict[str, Any] = {"points": [], "cells": [], "scalars": {}, "vectors": {}}
    n = len(tok)
    while i < n:
        t = tok[i]
        if t == "POINTS":
            cnt = int(tok[i + 1])
            i += 3
            vals = [float(x) for x in tok[i:i + 3 * cnt]]
            res["points"] = [tuple(vals[3 * k:3 * k + 3]) for k in range(cnt)]
            i += 3 * cnt
        elif t == "CELLS":
            cnt = int(tok[i + 1])
            i += 3
            cells = []
            for _ in range(cnt):
                m = int(tok[i])
                cells.append(tuple(int(x) for x in tok[i + 1:i + 1 + m]))
                i += 1 + m
            res["cells"] = cells
        elif t == "SCALARS":
            name, cnt = tok[i + 1], len(res["points"])
            while i < n and tok[i] != "LOOKUP_TABLE":   # SCALARS tên kiểu [số thành phần] LOOKUP_TABLE default
                i += 1
            i += 2
            res["scalars"][name] = [float(x) for x in tok[i:i + cnt]]
            i += cnt
        elif t == "VECTORS":
            name, cnt = tok[i + 1], len(res["points"])
            i += 3
            vals = [float(x) for x in tok[i:i + 3 * cnt]]
            res["vectors"][name] = [tuple(vals[3 * k:3 * k + 3]) for k in range(cnt)]
            i += 3 * cnt
        else:
            i += 1
    return res


def show(target: Any) -> str:
    """Hiển thị kết quả lên khung 3D của giao diện FastCAE (bảng Lập trình).

    `target` là đường dẫn file .vtk ASCII (lưới thể tích: tứ diện, lục diện, lăng trụ, chóp), hoặc một SolverRun
    (lấy file .vtk mới nhất do solver sinh ra), hoặc một list đường dẫn (lấy file cuối). Trả về đường dẫn đã gửi.
    Ngoài giao diện (chạy bằng python thường), hàm chỉ in dòng thông báo và không làm gì khác.
        r = fastcae.openfoam("cavity", ["blockMesh", "icoFoam", "foamToVTK -ascii"]); fastcae.show(r)
    Với .frd / .rmed / .med: chuyển sang .vtk bằng meshio/ccx2paraview rồi gọi show (xem docs/SCRIPTING.md).
    """
    if isinstance(target, SolverRun):
        cands = target.find(".vtk")
        if not cands:
            raise FastCAEError("Solver không sinh file .vtk (có: %s). Với OpenFOAM thêm lệnh 'foamToVTK -ascii'." %
                               (", ".join(os.path.basename(x) for x in target.files[:8]) or "không có file nào"))
        path = max(cands, key=lambda f: os.path.getmtime(f) if os.path.exists(f) else 0)
    elif isinstance(target, (list, tuple)):
        if not target:
            raise FastCAEError("Danh sách file rỗng")
        path = str(target[-1])
    else:
        path = str(target)
    path = os.path.abspath(path)
    if not os.path.isfile(path):
        raise FastCAEError("Không thấy file kết quả: " + path)
    print("@@FASTCAE_SHOW@@" + path, flush=True)
    return path


# ----------------------------------------------------------------------------------------------
# FastCAE làm nơi DỰNG MÔ HÌNH; thuật toán FEM/CFD tự lập trình hoặc thư viện bên ngoài làm phần GIẢI.
#   m = fastcae.model("dam.fcae")            # lưới + tập nút + vật liệu + liên kết + tải (không giải)
#   a = fastcae.model_arrays(m)              # numpy: a["nodes"] (n,3), a["tet4"], a["tet10"], ...
#   ... giải bằng numpy/scipy/FEniCSx/scikit-fem/MFEM ...
#   fastcae.write_vtk("kq.vtk", a["nodes"], a["tet4"], point_vectors={"displacement": u})
#   fastcae.show("kq.vtk")                   # hiện lên khung 3D của FastCAE
# Hoặc đóng gói thuật toán thành solver (*.fsolver) để dùng bằng `solver = <id>` trong case: xem docs/ARCHITECTURE.md.
# ----------------------------------------------------------------------------------------------
def model(case: str, cli: Optional[str] = None) -> Dict[str, Any]:
    """Dựng mô hình từ file case (đường dẫn *.fcae) hoặc văn bản case, trả về dict theo định dạng fastcae-model (JSON).
    Chỉ hỗ trợ type = elastic | thermal. Không giải bài toán."""
    with tempfile.TemporaryDirectory(prefix="fastcae_model_") as d:
        path = case
        if "\n" in case or not os.path.isfile(case):
            path = os.path.join(d, "case.fcae")
            with open(path, "w", encoding="utf-8") as f:
                f.write(case)
        out = os.path.join(d, "model.fcmodel.json")
        p = _call(["model", os.path.abspath(path), out], cli)
        if p.returncode != 0 or not os.path.isfile(out):
            raise FastCAEError("Khong dung duoc mo hinh: " + (p.stderr.strip() or p.stdout.strip()[-300:]))
        with open(out, "r", encoding="utf-8") as f:
            return json.load(f)


def model_arrays(m: Dict[str, Any]) -> Dict[str, Any]:
    """Đổi mô hình sang mảng numpy: nodes (n,3), tet4 (m,4), tet10 (m,10) nếu order = 2, cùng material, dirichlet, loads, node_sets."""
    import numpy as np
    mesh = m["mesh"]
    out: Dict[str, Any] = {"nodes": np.asarray(mesh["nodes"], float).reshape(-1, 3), "tet4": np.asarray(mesh["tet4"], int).reshape(-1, 4),
                           "material": m["material"], "analysis": m["analysis"], "order": m["order"], "dirichlet": m["dirichlet"],
                           "loads": m["loads"], "node_sets": m["node_sets"]}
    if "tet10" in mesh:
        out["tet10"] = np.asarray(mesh["tet10"], int).reshape(-1, 10)
    return out


def write_vtk(path: str, points: Any, tets: Any, point_scalars: Optional[Dict[str, Any]] = None,
              point_vectors: Optional[Dict[str, Any]] = None, cell_scalars: Optional[Dict[str, Any]] = None) -> str:
    """Ghi file VTK legacy ASCII (lưới tứ diện 4 nút) để hiển thị bằng fastcae.show() hoặc ParaView.
    points (n,3); tets (m,4) — với T10 chỉ truyền 4 đỉnh đầu; mỗi trường có đúng n (nút) hoặc m (ô) giá trị."""
    pts = [tuple(float(c) for c in p) for p in points]
    tt = [tuple(int(i) for i in t[:4]) for t in tets]
    with open(path, "w", encoding="utf-8") as f:
        f.write("# vtk DataFile Version 3.0\nFastCAE python\nASCII\nDATASET UNSTRUCTURED_GRID\n")
        f.write("POINTS %d double\n" % len(pts))
        f.writelines("%.12g %.12g %.12g\n" % p for p in pts)
        f.write("CELLS %d %d\n" % (len(tt), 5 * len(tt)))
        f.writelines("4 %d %d %d %d\n" % t for t in tt)
        f.write("CELL_TYPES %d\n" % len(tt))
        f.write("10\n" * len(tt))
        if cell_scalars:
            f.write("CELL_DATA %d\n" % len(tt))
            for name, v in cell_scalars.items():
                if len(v) != len(tt):
                    raise FastCAEError("cell_scalars '%s': can %d gia tri" % (name, len(tt)))
                f.write("SCALARS %s double 1\nLOOKUP_TABLE default\n" % name)
                f.writelines("%.12g\n" % float(x) for x in v)
        if point_scalars or point_vectors:
            f.write("POINT_DATA %d\n" % len(pts))
            for name, v in (point_vectors or {}).items():
                if len(v) != len(pts):
                    raise FastCAEError("point_vectors '%s': can %d vector" % (name, len(pts)))
                f.write("VECTORS %s double\n" % name)
                f.writelines("%.12g %.12g %.12g\n" % tuple(float(c) for c in x) for x in v)
            for name, v in (point_scalars or {}).items():
                if len(v) != len(pts):
                    raise FastCAEError("point_scalars '%s': can %d gia tri" % (name, len(pts)))
                f.write("SCALARS %s double 1\nLOOKUP_TABLE default\n" % name)
                f.writelines("%.12g\n" % float(x) for x in v)
    return path


# ----------------------------------------------------------------------------------------------
# Solver mã nguồn mở bên ngoài: Code_Aster, OpenRadioss/OpenCourant, SU2, OpenFOAM, CalculiX, Elmer...
# FastCAE chạy chúng như TIẾN TRÌNH RIÊNG (giữ nguyên giấy phép của từng solver); đầu ra hiện ngay khi chạy.
# Chương trình được tìm theo: tham số exe=, biến môi trường (FASTCAE_ASTER_BIN, FASTCAE_SU2_BIN, ...), PATH.
# ----------------------------------------------------------------------------------------------
class SolverRun:
    """Kết quả một lần chạy solver ngoài."""

    def __init__(self, ok: bool, exit_code: int, message: str, log: str, files: List[str], workdir: str, commands: List[str]):
        self.ok, self.exit_code, self.message, self.log = ok, exit_code, message, log
        self.files, self.workdir, self.commands = files, workdir, commands

    def find(self, *suffixes: str) -> List[str]:
        """File kết quả có đuôi khớp, ví dụ r.find('.vtk', '.frd', '.rmed')."""
        return [f for f in self.files if f.lower().endswith(tuple(x.lower() for x in suffixes))]

    def __bool__(self) -> bool:
        return self.ok

    def __repr__(self) -> str:
        return "SolverRun(ok=%r, exit_code=%r, message=%r, files=%d)" % (self.ok, self.exit_code, self.message, len(self.files))


def solvers(cli: Optional[str] = None) -> List[Dict[str, Any]]:
    """Danh mục solver: id, tên, giấy phép, đã cài chưa, đường dẫn, mẫu lệnh."""
    p = _call(["solvers", "--json"], cli)
    if p.returncode != 0:
        raise FastCAEError(p.stderr.strip() or "fastcae-cli solvers that bai")
    return json.loads(p.stdout)


def solver_info(solver_id: str, cli: Optional[str] = None) -> Dict[str, Any]:
    for s in solvers(cli):
        if s["id"] == solver_id:
            return s
    raise FastCAEError("Khong biet solver '%s'. Co: %s" % (solver_id, ", ".join(s["id"] for s in solvers(cli))))


def run_solver(solver_id: str, input_path: str, workdir: Optional[str] = None, args: str = "", commands: Sequence[str] = (),
               exe: Optional[str] = None, override: Optional[str] = None, echo: bool = True, cli: Optional[str] = None) -> SolverRun:
    """Chạy solver `solver_id` trên file input. Gọi qua `fastcae-cli solver-run`, đầu ra in ngay (echo=True)."""
    cmd = [find_cli(cli), "solver-run", solver_id, os.path.abspath(input_path)]
    if workdir:
        cmd += ["--dir", os.path.abspath(workdir)]
    if args:
        cmd += ["--args", args]
    if exe:
        cmd += ["--exe", exe]
    if override:
        cmd += ["--override", override]
    for c in commands:
        cmd += ["--cmd", c]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    lines: List[str] = []
    files: List[str] = []
    commands_run: List[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        if line.startswith("FILE "):
            files.append(line[5:].rstrip("\r\n"))
            continue
        if line.startswith("$ "):
            commands_run.append(line[2:].rstrip("\r\n"))
        lines.append(line)
        if echo:
            print(line, end="", flush=True)
    rc = proc.wait()
    log = "".join(lines)
    msg = ""
    for ln in reversed(lines):
        if ln.startswith("[OK]") or ln.startswith("[LOI]"):
            msg = ln.split("]", 1)[1].strip()
            break
    wd = os.path.abspath(workdir) if workdir else os.path.dirname(os.path.abspath(input_path))
    return SolverRun(rc == 0, rc, msg, log, files, wd, commands_run)


def run_program(command: str, workdir: Optional[str] = None, echo: bool = True) -> SolverRun:
    """Chạy lệnh bất kỳ (gmsh, paraview, python khác...) và lấy đầu ra."""
    proc = subprocess.Popen(command, shell=True, cwd=workdir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")
    out: List[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        out.append(line)
        if echo:
            print(line, end="", flush=True)
    rc = proc.wait()
    return SolverRun(rc == 0, rc, "Hoan tat" if rc == 0 else "Ma thoat %d" % rc, "".join(out), [], workdir or os.getcwd(), [command])


def list_files(directory: str, *suffixes: str) -> List[str]:
    """Liệt kê file (đệ quy) theo đuôi, vd list_files('run', '.vtk')."""
    res = []
    for root, _d, fs in os.walk(directory):
        for f in fs:
            if not suffixes or f.lower().endswith(tuple(x.lower() for x in suffixes)):
                res.append(os.path.join(root, f))
    return sorted(res)


def code_aster(export_file: str, workdir: Optional[str] = None, args: str = "", **kw: Any) -> SolverRun:
    """Code_Aster (GPL-3+): chạy file .export (run_aster). Kết quả thường là .med/.rmed/.resu trong thư mục chạy."""
    return run_solver("code_aster", export_file, workdir, args, **kw)


def openradioss(starter_file: str, workdir: Optional[str] = None, args: str = "", **kw: Any) -> SolverRun:
    """OpenRadioss / OpenCourant: chạy starter (*_0000.rad) rồi engine (*_0001.rad)."""
    return run_solver(kw.pop("solver_id", "openradioss"), starter_file, workdir, args, **kw)


def su2(cfg_file: str, workdir: Optional[str] = None, args: str = "", **kw: Any) -> SolverRun:
    """SU2 (LGPL-2.1): chạy SU2_CFD trên file .cfg (kèm .su2 lưới). Thêm args='-n 4' nếu dùng MPI wrapper."""
    return run_solver("su2", cfg_file, workdir, args, **kw)


def openfoam(case_dir: str, commands: Sequence[str] = (), workdir: Optional[str] = None, args: str = "", **kw: Any) -> SolverRun:
    """OpenFOAM (GPL-3+): chạy các lệnh trong thư mục case (tự nạp etc/bashrc). Để trống commands: blockMesh (nếu có
    blockMeshDict) rồi solver ghi trong controlDict. Ví dụ openfoam('cav', ['blockMesh', 'icoFoam', 'foamToVTK'])."""
    return run_solver("openfoam", case_dir, workdir, args, commands=commands, **kw)


def calculix(inp_file: str, workdir: Optional[str] = None, args: str = "", **kw: Any) -> SolverRun:
    """CalculiX (GPL-2+): chạy ccx trên deck .inp (kết quả .frd/.dat)."""
    return run_solver("calculix", inp_file, workdir, args, **kw)


def elmer(sif_file: str, workdir: Optional[str] = None, args: str = "", **kw: Any) -> SolverRun:
    """Elmer FEM: chạy ElmerSolver trên file .sif."""
    return run_solver("elmer", sif_file, workdir, args, **kw)
