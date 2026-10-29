-- 05. Tần số riêng dầm công xôn theo chiều dài, đối chiếu công thức f1 = (1.8751^2 / 2pi) * sqrt(EI / (rho A L^4))
local mat = fastcae.materials.steel
local s = fastcae.section("rect", 0.05, 0.10)
print(string.format("%8s %14s %14s %9s", "L (m)", "f1 FE (Hz)", "f1 công thức", "lệch %"))
for L = 1.0, 4.0, 0.5 do
  local nodes, members = {}, {}
  for i = 0, 10 do nodes[#nodes + 1] = string.format("%d %g 0 0", i + 1, L * i / 10) end
  for i = 1, 10 do members[#members + 1] = string.format("%d %d S beam", i, i + 1) end
  local res = fastcae.solve(fastcae.case{
    type = "frame", analysis = "modal", modes = 3, E = mat.E, nu = mat.nu, rho = mat.rho,
    section = "S rect 0.05 0.10", node = nodes, member = members, support = "1 fixed" })
  local f1 = res.values.frequency_1_hz
  -- trục yếu (Iz) cho tần số thấp nhất
  local exact = (1.875104069^2 / (2 * math.pi)) * math.sqrt(mat.E * s.Iz / (mat.rho * s.A * L^4))
  print(string.format("%8.1f %14.4f %14.4f %9.4f", L, f1, exact, 100 * math.abs(f1 - exact) / exact))
end
