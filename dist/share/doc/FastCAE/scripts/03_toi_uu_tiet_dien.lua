-- 03. Tối ưu tiết diện: chiều cao nhỏ nhất của dầm chữ nhật thoả đồng thời
--     độ võng <= L/250 và ứng suất <= fy/1.5, bằng phương pháp chia đôi; mỗi bước giải bằng khung 1D.
local mat, L, q, b = fastcae.materials.steel, 8.0, 15000.0, 0.20
local n = 8
local function solveFor(h)
  local nodes, members, udl = {}, {}, {}
  for i = 0, n do nodes[#nodes + 1] = string.format("%d %g 0 0", i + 1, L * i / n) end
  for i = 1, n do
    members[#members + 1] = string.format("%d %d S beam", i, i + 1)
    udl[#udl + 1] = string.format("%d 0 0 %g", i, -q)
  end
  return fastcae.solve(fastcae.case{
    type = "frame", E = mat.E, nu = mat.nu, rho = mat.rho, gravity = 9.81,
    section = string.format("S rect %g %g", b, h), node = nodes, member = members, udl = udl,
    support = { "1 x y z rx", string.format("%d y z", n + 1) } })
end
local limitU, limitS = L / 250, mat.fy / 1.5
local function ok(h)
  local r = solveFor(h)
  return r.values.max_displacement <= limitU and r.values.max_stress <= limitS, r
end
local lo, hi = 0.1, 2.0
assert(ok(hi), "ngay ca h = 2 m cung khong dat")
for it = 1, 40 do
  local mid = 0.5 * (lo + hi)
  if ok(mid) then hi = mid else lo = mid end
  if hi - lo < 1e-4 then break end
end
local _, r = ok(hi)
print(string.format("Chiều cao nhỏ nhất h = %.4f m (b = %.2f m)", hi, b))
print(string.format("  độ võng lớn nhất = %.2f mm (giới hạn L/250 = %.2f mm)", r.values.max_displacement * 1000, limitU * 1000))
print(string.format("  ứng suất lớn nhất = %.1f MPa (giới hạn fy/1.5 = %.1f MPa)", r.values.max_stress / 1e6, limitS / 1e6))
print(string.format("  khối lượng dầm = %.0f kg", b * hi * L * mat.rho))
