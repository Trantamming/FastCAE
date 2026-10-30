-- 15. Quét tham số trên mô hình CAD tham số (.fcad): đổi chiều cao H của dầm, dựng lại hình học,
-- chia lưới, giải đàn hồi và so với lý thuyết dầm console  w = F L^3 / (3 E I).
-- Chạy:  fastcae-cli script scripts/15_quet_tham_so_cad.lua <đường_dẫn_dam_tham_so.fcad>   (cần gmsh)
local model = arg[1] or "examples/param/dam_tham_so.fcad"
local mat, L, W, F = fastcae.materials.steel, 0.30, 0.03, 500.0
print(string.format("%8s %14s %14s %9s", "H (mm)", "võng FE (mm)", "lý thuyết", "lệch %"))
for _, H in ipairs({0.015, 0.020, 0.025, 0.030}) do
  local res = fastcae.solve(fastcae.case{
    type = "elastic", geometry = "param " .. model,
    param_set = { "H=" .. H }, mesh_size = H / 2, order = 2,
    E = mat.E, nu = mat.nu, fix = "xmin", load = string.format("xmax 0 0 %g", -F),
  })
  local I = W * H^3 / 12
  local exact = F * L^3 / (3 * mat.E * I)
  local fe = res.values.max_displacement
  print(string.format("%8.1f %14.4f %14.4f %9.2f", H * 1000, fe * 1000, exact * 1000, 100 * math.abs(fe - exact) / exact))
end
