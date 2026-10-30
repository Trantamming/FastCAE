#!/usr/bin/env python3
"""Tạo lưới 2D tam giác quanh trụ tròn (bán kính 0,5 m trong hộp 10 x 10 m) ra cylinder.su2. Cần: pip install gmsh."""
import gmsh
gmsh.initialize(); gmsh.option.setNumber("General.Terminal", 0)
gmsh.model.add("cyl"); occ = gmsh.model.occ
box = occ.addRectangle(-5, -5, 0, 10, 10); disk = occ.addDisk(0, 0, 0, 0.5, 0.5)
occ.cut([(2, box)], [(2, disk)]); occ.synchronize()
curves = [t for d, t in gmsh.model.getEntities(1)]
wall = [t for t in curves if max(abs(v) for v in gmsh.model.getBoundingBox(1, t)[:6]) < 0.6]
far = [t for t in curves if t not in wall]
gmsh.model.addPhysicalGroup(1, wall, name="wall"); gmsh.model.addPhysicalGroup(1, far, name="farfield")
gmsh.model.addPhysicalGroup(2, [t for d, t in gmsh.model.getEntities(2)], name="fluid")
f = gmsh.model.mesh.field; f.add("Distance", 1); f.setNumbers(1, "CurvesList", wall); f.setNumber(1, "Sampling", 100)
f.add("Threshold", 2); f.setNumber(2, "InField", 1)
for k, v in (("SizeMin", 0.04), ("SizeMax", 1.0), ("DistMin", 0.3), ("DistMax", 4)): f.setNumber(2, k, v)
f.setAsBackgroundMesh(2)
gmsh.option.setNumber("Mesh.MeshSizeExtendFromBoundary", 0); gmsh.option.setNumber("Mesh.MeshSizeFromPoints", 0)
gmsh.model.mesh.generate(2); gmsh.write("cylinder.su2")
