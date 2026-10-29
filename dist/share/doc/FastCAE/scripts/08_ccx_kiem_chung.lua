-- 08. Kiểm chứng cầu nối CalculiX (phi tuyến, dẻo, dao động riêng, mất ổn định, động lực học, nhiệt) với lời giải giải tích.
-- Cần `ccx` trong PATH (hoặc FASTCAE_CCX). Thoát với lỗi nếu có chỉ tiêu nào vượt dung sai.
local fails = 0
local function check(name, got, want, tol)
  local err = math.abs(got - want) / math.abs(want)
  local ok = err <= tol
  if not ok then fails = fails + 1 end
  print(string.format("%-34s FE=%-12.6g chuẩn=%-12.6g lệch=%6.2f%% (dung sai %g%%) %s",
        name, got, want, 100 * err, 100 * tol, ok and "ĐẠT" or "KHÔNG ĐẠT"))
end

local E, nu, rho = 210e9, 0.3, 7850
local pi = math.pi

-- 1) Dao động riêng dầm công xôn (Euler-Bernoulli)
do
  local b, L = 0.1, 1.0
  local I, A = b^4 / 12, b * b
  local r = fastcae.solve(fastcae.case{ type = "elastic", solver = "calculix", analysis = "frequency", job_name = "freq",
    work_dir = "ccx_bench", geometry = "box 1.0 0.1 0.1", divisions = "20 2 2", order = 2, E = E, nu = nu, rho = rho,
    fix = "xmin", modes = 4 })
  local f = r.series.frequencies_hz
  -- hai dạng đầu suy biến (uốn theo y và z)
  check("dao dong rieng f1", f[1], 1.875104^2 / (2*pi) * math.sqrt(E*I/(rho*A*L^4)), 0.02)
  -- dầm dày (h/L = 0.1): biến dạng cắt (Timoshenko) hạ f2 ~4%, nên dung sai 6% so với Euler-Bernoulli
  check("dao dong rieng f3", f[3], 4.694091^2 / (2*pi) * math.sqrt(E*I/(rho*A*L^4)), 0.06)
end

-- 2) Mất ổn định Euler, cột ngàm một đầu: Pcr = pi^2 EI / (4 L^2) (tải đơn vị => hệ số = Pcr)
do
  local b, L = 0.04, 1.0
  local I = b^4 / 12
  local r = fastcae.solve(fastcae.case{ type = "elastic", solver = "calculix", analysis = "buckle", job_name = "buckle",
    work_dir = "ccx_bench", geometry = "box 1.0 0.04 0.04", divisions = "20 2 2", order = 2, E = E, nu = nu, rho = rho,
    fix = "xmin", load = "xmax -1 0 0", modes = 2 })
  check("mat on dinh Euler Pcr", r.series.buckling_factors[1], pi^2 * E * I / (4 * L^2), 0.03)
end

-- 3) Dầm công xôn biến dạng lớn (đàn hồi dẻo ELASTICA): PL^2/EI = 1 => |u_tip| / L = 0.3069
do
  local b, L = 0.02, 1.0
  local I = b^4 / 12
  local P = 1.0 * E * I / L^2
  local r = fastcae.solve(fastcae.case{ type = "elastic", solver = "calculix", analysis = "nlstatic", job_name = "nl",
    work_dir = "ccx_bench", geometry = "box 1.0 0.02 0.02", divisions = "20 2 2", order = 2, E = E, nu = nu, rho = rho,
    fix = "xmin", load = string.format("xmax 0 0 %.6f", -P), increments = 10 })
  local lin = P * L^3 / (3 * E * I) -- tuyến tính (lớn hơn rõ rệt)
  check("cong xon bien dang lon |u|/L", r.max_value / L, math.sqrt(0.30172^2 + 0.05611^2), 0.04)
  print(string.format("   (tuyến tính sẽ cho %.4f, phi tuyến hình học cho %.4f)", lin / L, r.max_value / L))
end

-- 4) Dẻo: kéo thanh, hóa bền tuyến tính. sigma = 300 MPa, sy = 250 MPa, H = 2 GPa => eps_p = 0.025
do
  local A = 0.05 * 0.05
  local sig, sy, H, Es = 300e6, 250e6, 2e9, 200e9
  local r = fastcae.solve(fastcae.case{ type = "elastic", solver = "calculix", analysis = "plastic", job_name = "plastic",
    work_dir = "ccx_bench", geometry = "box 1.0 0.05 0.05", divisions = "10 2 2", order = 2, E = Es, nu = 0.3, rho = rho,
    yield = sy, hardening = H, fix = "xmin", load = string.format("xmax %.6f 0 0", sig * A), increments = 20 })
  check("keo deo: do dan dai", r.max_value, 1.0 * (sig / Es + (sig - sy) / H), 0.04)
end

-- 5) Động lực học: tải bậc thang lên công xôn => độ võng cực đại ~ 2 lần tĩnh (không cản)
do
  local common = { type = "elastic", E = E, nu = nu, rho = rho, geometry = "box 1.0 0.1 0.1", divisions = "20 2 2", order = 2,
                   fix = "xmin", load = "xmax 0 0 -1000" }
  local st = fastcae.solve(fastcae.case(common))
  local c = {}
  for k, v in pairs(common) do c[k] = v end
  c.solver, c.analysis, c.job_name, c.work_dir, c.time, c.increments = "calculix", "dynamic", "dyn", "ccx_bench", 0.03, 300
  local r = fastcae.solve(fastcae.case(c))
  check("dong luc: he so khuech dai", r.values.max_displacement / st.max_value, 2.0, 0.08)
end

-- 6) Dẫn nhiệt quá độ 1 chiều: thanh L = 1, T0 = 0, hai đầu giữ 100. Nhiệt độ giữa thanh tại t.
do
  local k, cp = 50, 500
  local al = k / (rho * cp)
  local t = 4000
  local s = 0
  for n = 1, 41, 2 do s = s + (1 / n) * math.sin(n * pi / 2) * math.exp(-n^2 * pi^2 * al * t) end
  local exact = 100 - 100 * (4 / pi) * s
  local r = fastcae.solve(fastcae.case{ type = "thermal", solver = "calculix", analysis = "heat_transient", job_name = "heat_tr",
    work_dir = "ccx_bench", geometry = "box 1.0 0.1 0.1", divisions = "20 2 2", order = 2, k = k, rho = rho, cp = cp,
    temp = { "xmin 100", "xmax 100" }, t0 = 0, time = t, increments = 40 })
  local T = r:field("temperature")
  local best, tm = 1e9, nil
  for i = 1, #T do
    local x, y, z = r:node(i)
    if math.abs(x - 0.5) < 1e-9 and math.abs(y - 0.05) < 1e-9 and math.abs(z - 0.05) < 1e-9 then tm = T[i] end
  end
  check("nhiet qua do: giua thanh", tm, exact, 0.03)
end

-- 7) Ứng suất nhiệt: thanh giãn nở tự do, DeltaT = 100 => u_tip = alpha dT L, ứng suất ~ 0
do
  local r = fastcae.solve(fastcae.case{ type = "elastic", solver = "calculix", analysis = "thermal_stress", job_name = "tstress",
    work_dir = "ccx_bench", geometry = "box 1.0 0.1 0.1", divisions = "10 2 2", order = 2, E = E, nu = nu, rho = rho,
    alpha = 1.2e-5, k = 50, cp = 500, t0 = 0, temp = "all 100",
    fix = { "xmin x", "ymin y", "zmin z" } })
  check("ung suat nhiet: gian no tu do", r.max_value, 1.2e-5 * 100 * 1.0, 0.03)
  print(string.format("   (ứng suất von Mises cực đại %.3g Pa, kỳ vọng ~0)", r.values.max_von_mises or 0))
end

-- 8) Áp suất theo mặt: thanh nén bằng áp suất 1 MPa trên mặt xmax => u = p L / E (dương = nén vào mặt)
do
  local r = fastcae.solve(fastcae.case{ type = "elastic", solver = "calculix", analysis = "static", job_name = "press",
    work_dir = "ccx_bench", geometry = "box 1.0 0.1 0.1", divisions = "10 2 2", order = 2, E = E, nu = nu, rho = rho,
    fix = { "xmin x", "ymin y", "zmin z" }, pressure = "xmax 1e6" })
  check("ap suat theo mat: u = pL/E", r.max_value, 1e6 * 1.0 / E, 0.02)
end

-- 9) Tiếp xúc không ma sát: hai khối chồng nhau (tiết diện 0.1 x 0.1, cao 0.5 mỗi khối), tải nén 100 kN.
--    Ứng xử của chồng khối nguyên: u_top = F (L1 + L2) / (E A) (cộng độ mềm phạt rất nhỏ).
--    Dùng đối xứng (xmin x, ymin y) để khử chuyển động cứng ngang (tiếp xúc không ma sát không giữ khối).
do
  local F, A = 1e5, 0.01
  local function stack(gap, adjust)
    local c = { type = "elastic", solver = "calculix", analysis = "contact", job_name = "contact" .. (adjust and "_adj" or "_0"),
      work_dir = "ccx_bench", geometry = "box 0.1 0.1 0.5", divisions = "2 2 8",
      body = string.format("box 0.1 0.1 0.5 at 0 0 %.6f div 2 2 8", 0.5 + gap), order = 2, E = E, nu = nu, rho = rho,
      nodeset = { "b1top plane 0 0 1 0.5 1e-6 body 1", string.format("b2bot plane 0 0 1 %.6f 1e-6 body 2", 0.5 + gap) },
      fix = { "zmin", "xmin x", "ymin y" }, load = string.format("zmax 0 0 %.1f", -F), increments = 5 }
    c.contact = adjust and string.format("b2bot b1top adjust %g", adjust) or "b2bot b1top"
    return fastcae.solve(fastcae.case(c))
  end
  local exact = F * 1.0 / (E * A)
  check("tiep xuc: hai khoi cham nhau", stack(0, nil).max_value, exact, 0.02)
  check("tiep xuc: khe ho 1 mm dong bang adjust", stack(0.001, 0.002).max_value, exact, 0.02)
  -- Áp suất tiếp xúc (CPRESS, *CONTACT FILE): hai khối chồng nhau chịu F -> p = F / A = 1e7 Pa trên mặt tiếp xúc.
  do
    local r = stack(0, nil)
    check("tiep xuc: ap suat tiep xuc max = F/A", r.values.max_contact_pressure, F / A, 0.02)
  end
end

if fails > 0 then error(string.format("%d chỉ tiêu kiểm chứng KHÔNG ĐẠT", fails)) end
print("Tất cả chỉ tiêu kiểm chứng CalculiX ĐẠT")
