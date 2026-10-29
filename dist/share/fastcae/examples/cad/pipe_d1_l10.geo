// Nguồn của pipe_d1_l10.step: ống trụ D = 1 m, dài 10 m, trục x (gmsh pipe_d1_l10.geo -0 -format step -o pipe_d1_l10.step).
SetFactory("OpenCASCADE");
Cylinder(1) = {0, 0, 0, 10, 0, 0, 0.5};
