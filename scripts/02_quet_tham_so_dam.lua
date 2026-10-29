-- 02. Quét tham số: chiều cao dầm chữ nhật đơn giản chịu tải phân bố
-- So sánh khung 1D (FastCAE) với lý thuyết 5qL^4/(384 EI) và mômen qL^2/8; ghi CSV.
local mat, L, q, b = fastcae.materials.steel, 6.0, 12000.0, 0.15
local n = 8

local function beamCase(h)
  local nodes, members, udl = {}, {}, {}
  for i = 0, n do nodes[#nodes + 1] = string.format("%d %g 0 0", i + 1, L * i / n) end
  for i = 1, n do
    members[#members + 1] = string.format("%d %d S beam", i, i + 1)
    udl[#udl + 1] = string.format("%d 0 0 %g", i, -q)
  end
  return fastcae.case{
    type = "frame", E = mat.E, nu = mat.nu, rho = mat.rho, yield = mat.fy,
    section = string.format("S rect %g %g", b, h),
    node = nodes, member = members, udl = udl,
    support = { "1 x y z rx", string.format("%d y z", n + 1) },
  }
end

local rows = {}
print(string.format("%8s %12s %12s %9s %12s %12s %8s", "h (m)", "võng FE (mm)", "lý thuyết", "lệch %", "M max (kN.m)", "qL2/8", "σ/fy"))
for h = 0.20, 0.50, 0.05 do
  local res = fastcae.solve(beamCase(h))
  local I = b * h^3 / 12
  local exact = 5 * q * L^4 / (384 * mat.E * I)
  local fe = res.values.max_displacement
  print(string.format("%8.2f %12.3f %12.3f %9.4f %12.2f %12.2f %8.3f", h, fe * 1000, exact * 1000, 100 * math.abs(fe - exact) / exact,
        res.values.max_moment / 1000, q * L^2 / 8 / 1000, res.values.max_utilization))
  rows[#rows + 1] = { h, fe, exact, res.values.max_moment, res.values.max_utilization }
end
if fastcae.write_csv("quet_tham_so_dam.csv", rows, { "h_m", "vong_FE_m", "vong_ly_thuyet_m", "M_max_Nm", "sigma_over_fy" }) then
  print("\nĐã ghi quet_tham_so_dam.csv")
end
