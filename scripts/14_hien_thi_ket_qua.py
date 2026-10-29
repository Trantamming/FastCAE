# FastCAE - Python: day ket qua len khung 3D bang fastcae.show().
# Vi du nay tu tao mot luoi luc dien 20x4x4 (thanh dam) kem truong nhiet do va vecto chuyen vi, ghi .vtk (ASCII)
# roi goi fastcae.show(). Voi solver that: r = fastcae.openfoam(case, ["blockMesh", "simpleFoam", "foamToVTK -ascii"]); fastcae.show(r)
import os
import fastcae

nx, ny, nz, L, H = 20, 4, 4, 5.0, 0.5
pts = [(L * i / nx, H * j / ny, H * k / nz) for k in range(nz + 1) for j in range(ny + 1) for i in range(nx + 1)]
idx = lambda i, j, k: (k * (ny + 1) + j) * (nx + 1) + i
cells = [(idx(i, j, k), idx(i + 1, j, k), idx(i + 1, j + 1, k), idx(i, j + 1, k),
          idx(i, j, k + 1), idx(i + 1, j, k + 1), idx(i + 1, j + 1, k + 1), idx(i, j + 1, k + 1))
         for k in range(nz) for j in range(ny) for i in range(nx)]

path = os.path.join(os.path.expanduser("~"), "FastCAE_runs", "demo_hex.vtk")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w") as f:
    f.write("# vtk DataFile Version 3.0\nFastCAE demo\nASCII\nDATASET UNSTRUCTURED_GRID\n")
    f.write("POINTS %d double\n" % len(pts))
    f.writelines("%g %g %g\n" % p for p in pts)
    f.write("CELLS %d %d\n" % (len(cells), 9 * len(cells)))
    f.writelines("8 " + " ".join(map(str, c)) + "\n" for c in cells)
    f.write("CELL_TYPES %d\n" % len(cells))
    f.writelines("12\n" for _ in cells)
    f.write("POINT_DATA %d\nSCALARS temperature double 1\nLOOKUP_TABLE default\n" % len(pts))
    f.writelines("%g\n" % (20 + 80 * p[0] / L) for p in pts)
    f.write("VECTORS displacement double\n")
    f.writelines("0 0 %g\n" % (-0.01 * (p[0] / L) ** 2) for p in pts)

print("Da ghi", path)
fastcae.show(path)      # chon truong mau o hop 'Truong' tren khung 3D: temperature, displacement_magnitude
