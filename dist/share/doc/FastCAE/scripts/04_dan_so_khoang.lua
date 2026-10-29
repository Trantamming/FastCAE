-- 04. So sánh dàn Warren theo số khoang: lực dọc lớn nhất, độ võng và khối lượng (cùng tiết diện ống)
local rows = {}
print(string.format("%8s %14s %14s %12s", "số khoang", "N max (kN)", "võng (mm)", "khối lượng"))
for n = 4, 12, 2 do
  local text = fastcae.frame_template("warren_truss", { 12.0, 2.0, n, 10000.0 })
  local res = fastcae.solve(text)
  local s = fastcae.section("pipe", 0.1, 0.005)
  local length = 0
  for _, l in ipairs(res.series.member_length) do length = length + l end
  local mass = length * s.A * fastcae.materials.steel.rho
  print(string.format("%8d %14.2f %14.3f %10.0f kg", n, res.values.max_axial / 1000, res.values.max_displacement * 1000, mass))
  rows[#rows + 1] = { n, res.values.max_axial, res.values.max_displacement, mass }
end
fastcae.write_csv("dan_warren.csv", rows, { "n_khoang", "N_max_N", "vong_m", "khoi_luong_kg" })
