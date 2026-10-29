-- 09. Kiểm chứng cầu nối OpenFOAM 3D (simpleFoam) với lời giải giải tích / tương quan thực nghiệm.
-- Cần OpenFOAM (etc/bashrc tự tìm, hoặc đặt FASTCAE_OPENFOAM_BASHRC). Thoát với lỗi nếu có chỉ tiêu vượt dung sai.
local fails = 0
local function check(name, got, want, tol)
  local err = math.abs(got - want) / math.abs(want)
  local ok = err <= tol
  if not ok then fails = fails + 1 end
  print(string.format("%-40s FE=%-12.6g chuẩn=%-12.6g lệch=%6.2f%% (dung sai %g%%) %s",
        name, got, want, 100 * err, 100 * tol, ok and "ĐẠT" or "KHÔNG ĐẠT"))
end

-- áp suất trung bình trên mặt cắt x = x0 (các nút có |x - x0| < tol)
local function section_mean(res, field, x0, tol)
  local f = res:field(field)
  local s, n = 0, 0
  for i = 1, #f do
    local x = res:node(i)
    if math.abs(x - x0) < tol then s = s + f[i]; n = n + 1 end
  end
  return s / n
end

local function channel(job, L, nu, model, iters, nx)
  return fastcae.solve(fastcae.case{
    type = "cfd3d", solver = "openfoam", job_name = job, work_dir = "foam_bench",
    geometry = string.format("box %g 1 0.5", L), divisions = string.format("%d 20 2", nx), nu_kin = nu,
    inlet_velocity = "1 0 0", bc_xmin = "inlet", bc_xmax = "outlet", bc_ymin = "wall", bc_ymax = "wall",
    bc_zmin = "symmetry", bc_zmax = "symmetry", model = model, max_iter = iters })
end

-- 1) Kênh phẳng tầng lưu (Poiseuille), Re = U H / nu = 10, chiều cao H = 1, vận tốc trung bình U = 1:
--    u_max = 1.5 U;  dp/dx = 12 nu U / H^2 = 1.2 (áp suất động học) ở vùng phát triển đầy đủ.
do
  local r = channel("lam", 10, 0.1, "laminar", 2000, 100)
  check("tang luu: u_max = 1.5 U", r.values.max_speed, 1.5, 0.02)
  local dx = 8.0 - 4.0
  local dpdx = (section_mean(r, "pressure", 4.0, 1e-6) - section_mean(r, "pressure", 8.0, 1e-6)) / dx
  check("tang luu: dp/dx = 12 nu U / H^2", dpdx, 12 * 0.1 * 1 / 1, 0.03)
  check("tang luu: bao toan khoi luong (<1.5%)", 1 + r.values.mass_imbalance, 1, 0.015)
end

-- 2) Kênh phẳng rối k-epsilon, Re = U H / nu = 1e5: hệ số ma sát Dean Cf = 0.073 Re^-0.25,
--    dp/dx = 2 tau_w / H = Cf U^2 / H (áp suất động học). Dung sai rộng (RANS + hàm tường, lưới thô).
do
  local Re = 1e5
  local r = channel("turb", 20, 1 / Re, "kepsilon", 3000, 100)
  local Cf = 0.073 * Re ^ -0.25
  local dpdx = (section_mean(r, "pressure", 12.0, 1e-6) - section_mean(r, "pressure", 18.0, 1e-6)) / 6.0
  check("roi k-epsilon: dp/dx (Dean)", dpdx, Cf * 1.0 * 1.0 / 1.0, 0.20)
  check("roi: bao toan khoi luong (<1.5%)", 1 + r.values.mass_imbalance, 1, 0.015)
end

-- 3) Ống tròn từ CAD (STEP, lưới tứ diện Gmsh -> polyMesh trực tiếp), tầng lưu Re = U D / nu = 20:
--    Hagen-Poiseuille: u_max = 2 U;  dp/dx = 32 nu U / D^2 = 1.6 (áp suất động học). Cần đường dẫn STEP: tham số thứ nhất.
local step = arg and arg[1]
if step and step ~= "" then
  local r = fastcae.solve(fastcae.case{ type = "cfd3d", solver = "openfoam", job_name = "pipe", work_dir = "foam_bench",
    geometry = "file " .. step, mesh_size = 0.12, nu_kin = 0.05, inlet_velocity = "1 0 0",
    bc_xmin = "inlet", bc_xmax = "outlet", model = "laminar", max_iter = 1500 })
  -- trung bình các nút trong lát |x - x0| < 0.2 (lưới không có nút đúng trên mặt cắt)
  local function slab(x0)
    local f = r:field("pressure"); local s, n = 0, 0
    for i = 1, #f do local x = r:node(i); if math.abs(x - x0) < 0.2 then s = s + f[i]; n = n + 1 end end
    return s / n
  end
  check("ong CAD: u_max = 2 U", r.values.max_speed, 2.0, 0.05)
  -- ĐÃ SỬA (v0.3.x): lược đồ gradient "Gauss linear" không nhất quán trên lưới tứ diện lệch nên trước đây dp/dx cao hơn lý thuyết
  -- ~15% và không giảm khi làm mịn lưới (h 0,25 -> 0,08: 1,82 / 1,85 / 1,86 / 1,85). Với gradSchemes = pointCellsLeastSquares
  -- (mặc định cho lưới CAD) sai số còn trong khoảng +-2% và hội tụ về 1,6 khi mịn dần (xem docs/SOLVERS.md).
  check("ong CAD: dp/dx = 32 nu U / D^2", (slab(4.0) - slab(8.0)) / 4.0, 32 * 0.05 * 1 / 1, 0.05)
  check("ong CAD: bao toan khoi luong (<3%)", 1 + r.values.mass_imbalance, 1, 0.03)
else
  print("(bo qua kiem chung ong CAD: khong co tham so duong dan STEP)")
end

if fails > 0 then error(string.format("%d chỉ tiêu kiểm chứng KHÔNG ĐẠT", fails)) end
print("Tất cả chỉ tiêu kiểm chứng OpenFOAM ĐẠT")
