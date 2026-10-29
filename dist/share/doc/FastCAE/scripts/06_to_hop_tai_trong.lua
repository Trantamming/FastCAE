-- 06. Tổ hợp tải trọng cho khung cổng: duyệt các tổ hợp, giải từng tổ hợp và lấy đường bao hệ số sử dụng
local mat = fastcae.materials.steel
local G, Q, W = 12000.0, 8000.0, 15000.0          -- tĩnh tải, hoạt tải (N/m trên dầm) và gió (N ở đỉnh cột)
local combos = {
  { name = "1.35G",             g = 1.35, q = 0.0,  w = 0.0 },
  { name = "1.35G + 1.5Q",      g = 1.35, q = 1.5,  w = 0.0 },
  { name = "1.35G + 1.5W",      g = 1.35, q = 0.0,  w = 1.5 },
  { name = "1.35G + 1.5Q + 0.9W", g = 1.35, q = 1.5, w = 0.9 },
  { name = "1.0G + 1.5W",       g = 1.0,  q = 0.0,  w = 1.5 },
  { name = "1.0G - 1.5W",       g = 1.0,  q = 0.0,  w = -1.5 },
}
local function frameFor(c)
  local B, H = 6.0, 4.0
  local q = -(c.g * G + c.q * Q)
  return fastcae.case{
    type = "frame", E = mat.E, nu = mat.nu, rho = mat.rho, yield = mat.fy,
    section = "IPE ibeam 0.15 0.30 0.0107 0.0071",
    node = { "1 0 0 0", "2 0 0 4", "3 6 0 4", "4 6 0 0" },
    member = { "1 2 IPE beam", "2 3 IPE beam", "3 4 IPE beam" },
    support = { "1 fixed", "4 fixed" },
    udl = string.format("2 0 0 %g", q),
    nload = string.format("2 %g 0 0", c.w * W),
  }
end
local worst, worstName = 0, ""
print(string.format("%-26s %12s %12s %9s", "Tổ hợp", "M max (kN.m)", "võng (mm)", "σ/fy"))
for _, c in ipairs(combos) do
  local r = fastcae.solve(frameFor(c))
  print(string.format("%-26s %12.2f %12.2f %9.3f", c.name, r.values.max_moment / 1000, r.values.max_displacement * 1000, r.values.max_utilization))
  if r.values.max_utilization > worst then worst, worstName = r.values.max_utilization, c.name end
end
print(string.format("\nTổ hợp bất lợi nhất: %s, hệ số sử dụng σ/fy = %.3f %s", worstName, worst, worst > 1 and "(KHÔNG ĐẠT)" or "(đạt)"))
