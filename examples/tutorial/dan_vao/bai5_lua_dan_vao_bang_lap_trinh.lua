-- SPDX-License-Identifier: MIT
-- Tác giả: Trần Minh Tâm
-- BÀI 5 (bản DÁN THẲNG vào bảng Lập trình, ngôn ngữ LUA - không cần cài thêm gì):
-- khung thép không gian 5 tầng (57 nút, 136 thanh, có giằng và tầng thu nhỏ), FEM dầm 3D tự lập trình bằng Lua.
-- Cách dùng: ở bảng Lập trình để ngôn ngữ LUA -> dán toàn bộ -> bấm Chạy (F6). Chạy khoảng vài giây.
-- Thuật toán tự viết: phần tử dầm Euler-Bernoulli 12 bậc tự do + thanh dàn, ma trận đầy, giải bằng phân rã Cholesky.
-- Kiểm chứng: dầm công xôn giải tích, cân bằng phản lực, đối chiếu chuyển vị với solver khung có sẵn của FastCAE.
-- Lưu ý: Lua không đẩy được kết quả CỦA THUẬT TOÁN TỰ VIẾT lên khung 3D; phần hiển thị cuối là kết quả của solver khung tích hợp
-- trên cùng mô hình. Muốn xem kết quả của chính thuật toán tự viết trên 3D, dùng bản Python.

local E, NU, RHO = 210e9, 0.3, 7850.0
local G = E / (2 * (1 + NU))

---------------------------------------------------------------- 1. Dựng mô hình
local SECTIONS = {
  COT   = {"box", 0.30, 0.30, 0.016},
  DAM_X = {"ibeam", 0.15, 0.30, 0.0107, 0.0071},
  DAM_Y = {"ibeam", 0.10, 0.20, 0.0085, 0.0056},
  GIANG = {"pipe", 0.10, 0.005},
}
local props = {}
for name, s in pairs(SECTIONS) do props[name] = fastcae.section(table.unpack(s)) end

local function build()
  local XS, YS, ZS = {0, 6, 12, 18}, {0, 5, 10}, {0, 3.5, 7, 10.5, 14}
  local nodes, idx = {}, {}
  local function key(i, j, k) return i .. "," .. j .. "," .. k end
  for k = 0, 4 do for j = 0, 2 do for i = 0, 3 do
    if not (k == 4 and i == 3) then            -- tầng mái thu nhỏ: bỏ cột trục X = 18
      nodes[#nodes + 1] = {XS[i + 1], YS[j + 1], ZS[k + 1]}
      idx[key(i, j, k)] = #nodes
    end
  end end end
  local members = {}                            -- {n1, n2, tiết diện, loại}
  local function add(a, b, sec, kind) members[#members + 1] = {a, b, sec, kind} end
  for k = 0, 3 do for j = 0, 2 do for i = 0, 3 do
    local a, b = idx[key(i, j, k)], idx[key(i, j, k + 1)]
    if a and b then add(a, b, "COT", "beam") end
  end end end
  for k = 1, 4 do
    for j = 0, 2 do for i = 0, 2 do
      local a, b = idx[key(i, j, k)], idx[key(i + 1, j, k)]
      if a and b then add(a, b, "DAM_X", "beam") end
    end end
    for i = 0, 3 do for j = 0, 1 do
      local a, b = idx[key(i, j, k)], idx[key(i, j + 1, k)]
      if a and b then add(a, b, "DAM_Y", "beam") end
    end end
  end
  for _, j in ipairs({0, 2}) do for k = 0, 3 do for _, i in ipairs({0, 2}) do   -- giằng chữ X ở hai khung biên
    local a, b = idx[key(i, j, k)], idx[key(i + 1, j, k + 1)]
    local c, d = idx[key(i + 1, j, k)], idx[key(i, j, k + 1)]
    if a and b and c and d then add(a, b, "GIANG", "truss"); add(c, d, "GIANG", "truss") end
  end end end
  local supports, udl, loads, masses = {}, {}, {}, {}
  for k = 0, 4 do for j = 0, 2 do for i = 0, 3 do
    local n = idx[key(i, j, k)]
    if n then
      if k == 0 then supports[#supports + 1] = n end
      if k >= 1 then masses[n] = 15000.0 end
      if i == 0 and k >= 1 then loads[n] = {8e3 * (1 + 0.25 * k), (j == 0) and 2e3 or 0.0, 0.0} end
    end
  end end end
  for m, mb in ipairs(members) do
    if mb[3] == "DAM_X" or mb[3] == "DAM_Y" then udl[m] = (nodes[mb[1]][3] > 14 - 1e-9) and -20e3 or -30e3 end
  end
  return nodes, members, supports, udl, loads, masses
end

local function case_text(nodes, members, supports, udl, loads, masses)
  local t = {"type = frame", string.format("E = %g", E), string.format("nu = %g", NU), string.format("rho = %g", RHO)}
  for name, s in pairs(SECTIONS) do
    local p = {}
    for i = 2, #s do p[#p + 1] = string.format("%g", s[i]) end
    t[#t + 1] = "section = " .. name .. " " .. s[1] .. " " .. table.concat(p, " ")
  end
  for i, p in ipairs(nodes) do t[#t + 1] = string.format("node = %d %g %g %g", i, p[1], p[2], p[3]) end
  for _, m in ipairs(members) do t[#t + 1] = string.format("member = %d %d %s %s", m[1], m[2], m[3], m[4]) end
  for _, n in ipairs(supports) do t[#t + 1] = string.format("support = %d fixed", n) end
  for m, q in pairs(udl) do t[#t + 1] = string.format("udl = %d 0 0 %g", m, q) end
  for n, f in pairs(loads) do t[#t + 1] = string.format("nload = %d %g %g %g", n, f[1], f[2], f[3]) end
  return table.concat(t, "\n") .. "\n"
end

---------------------------------------------------------------- 2. FEM tự viết
local function axes(p1, p2)
  local d = {p2[1] - p1[1], p2[2] - p1[2], p2[3] - p1[3]}
  local L = math.sqrt(d[1]^2 + d[2]^2 + d[3]^2)
  local ex = {d[1] / L, d[2] / L, d[3] / L}
  local ref = (math.abs(ex[3]) > 0.999) and {1, 0, 0} or {0, 0, 1}
  local ey = {ref[2] * ex[3] - ref[3] * ex[2], ref[3] * ex[1] - ref[1] * ex[3], ref[1] * ex[2] - ref[2] * ex[1]}
  local ny = math.sqrt(ey[1]^2 + ey[2]^2 + ey[3]^2)
  ey = {ey[1] / ny, ey[2] / ny, ey[3] / ny}
  local ez = {ex[2] * ey[3] - ex[3] * ey[2], ex[3] * ey[1] - ex[1] * ey[3], ex[1] * ey[2] - ex[2] * ey[1]}
  return L, {ex, ey, ez}
end

local function zeros(n, m) local a = {} for i = 1, n do local r = {} for j = 1, m do r[j] = 0.0 end a[i] = r end return a end

local function k_local(kind, pr, L)
  local k = zeros(12, 12)
  local EA = E * pr.A / L
  k[1][1], k[7][7], k[1][7], k[7][1] = EA, EA, -EA, -EA
  if kind == "truss" then return k end
  local GJ = G * pr.J / L
  k[4][4], k[10][10], k[4][10], k[10][4] = GJ, GJ, -GJ, -GJ
  local function bend(I, dofs, sg)             -- dofs: 4 chỉ số (1-based); sg = đổi dấu bậc xoay
    local a = E * I / L^3
    local b = {{12, 6 * L, -12, 6 * L}, {6 * L, 4 * L^2, -6 * L, 2 * L^2}, {-12, -6 * L, 12, -6 * L}, {6 * L, 2 * L^2, -6 * L, 4 * L^2}}
    local s = {1, sg, 1, sg}
    for i = 1, 4 do for j = 1, 4 do k[dofs[i]][dofs[j]] = k[dofs[i]][dofs[j]] + a * b[i][j] * s[i] * s[j] end end
  end
  bend(pr.Iz, {2, 6, 8, 12}, 1)               -- uốn trong mặt xy (Iz)
  bend(pr.Iy, {3, 5, 9, 11}, -1)              -- uốn trong mặt xz (Iy), thy = -dw/dx
  return k
end

-- ma trận cứng toàn cục phần tử kg = T' kl T và vectơ tải tương đương của tải phân bố (hệ chung)
local function element(nodes, mb, q_z)
  local pr = props[mb[3]]
  local L, R = axes(nodes[mb[1]], nodes[mb[2]])
  local kl = k_local(mb[4], pr, L)
  local function T(i, j) local bi, bj = (i - 1) // 3, (j - 1) // 3
    if bi ~= bj then return 0.0 end
    return R[(i - 1) % 3 + 1][(j - 1) % 3 + 1] end
  local tmp = zeros(12, 12)                     -- tmp = kl * T
  for i = 1, 12 do for j = 1, 12 do
    local s = 0.0
    for m = 1, 12 do if kl[i][m] ~= 0 then s = s + kl[i][m] * T(m, j) end end
    tmp[i][j] = s
  end end
  local kg = zeros(12, 12)                      -- kg = T' * tmp
  for i = 1, 12 do for j = 1, 12 do
    local s = 0.0
    for m = 1, 12 do local t = T(m, i) if t ~= 0 then s = s + t * tmp[m][j] end end
    kg[i][j] = s
  end end
  local fg = nil
  if q_z and mb[4] == "beam" then
    local q = {R[1][3] * q_z, R[2][3] * q_z, R[3][3] * q_z}   -- q địa phương = R * (0,0,qz)
    local fl = {}
    for i = 1, 12 do fl[i] = 0.0 end
    fl[1], fl[7] = q[1] * L / 2, q[1] * L / 2
    fl[2], fl[8] = q[2] * L / 2, q[2] * L / 2
    fl[6], fl[12] = q[2] * L^2 / 12, -q[2] * L^2 / 12
    fl[3], fl[9] = q[3] * L / 2, q[3] * L / 2
    fl[5], fl[11] = -q[3] * L^2 / 12, q[3] * L^2 / 12
    fg = {}
    for i = 1, 12 do local s = 0.0 for m = 1, 12 do s = s + T(m, i) * fl[m] end fg[i] = s end
  end
  return kg, fg, L
end

local function solve_static(nodes, members, supports, udl, loads)
  local n = 6 * #nodes
  local K, F = zeros(n, n), {}
  for i = 1, n do F[i] = 0.0 end
  local total_vertical = 0.0
  for m, mb in ipairs(members) do
    local kg, fg, L = element(nodes, mb, udl[m])
    local dofs = {}
    for a = 0, 5 do dofs[#dofs + 1] = 6 * (mb[1] - 1) + a + 1 end
    for a = 0, 5 do dofs[#dofs + 1] = 6 * (mb[2] - 1) + a + 1 end
    for i = 1, 12 do
      for j = 1, 12 do K[dofs[i]][dofs[j]] = K[dofs[i]][dofs[j]] + kg[i][j] end
      if fg then F[dofs[i]] = F[dofs[i]] + fg[i] end
    end
    if udl[m] and mb[4] == "beam" then total_vertical = total_vertical - udl[m] * L end
  end
  for nd, f in pairs(loads) do for a = 1, 3 do F[6 * (nd - 1) + a] = F[6 * (nd - 1) + a] + f[a] end end
  local isfixed, free = {}, {}
  for _, s in ipairs(supports) do for a = 1, 6 do isfixed[6 * (s - 1) + a] = true end end
  for i = 1, n do if not isfixed[i] then free[#free + 1] = i end end
  local nf = #free
  -- Cholesky A = L L' trên ma trận rút gọn (các bậc tự do tự do)
  local A = zeros(nf, nf)
  for i = 1, nf do for j = 1, i do A[i][j] = K[free[i]][free[j]] end end
  for j = 1, nf do
    local s = A[j][j]
    for k = 1, j - 1 do s = s - A[j][k] * A[j][k] end
    if s <= 0 then error("Ma trận cứng không xác định dương: thiếu liên kết hoặc cơ cấu") end
    A[j][j] = math.sqrt(s)
    for i = j + 1, nf do
      local t = A[i][j]
      local Ai, Aj = A[i], A[j]
      for k = 1, j - 1 do t = t - Ai[k] * Aj[k] end
      A[i][j] = t / A[j][j]
    end
  end
  local y = {}
  for i = 1, nf do
    local t = F[free[i]]
    for k = 1, i - 1 do t = t - A[i][k] * y[k] end
    y[i] = t / A[i][i]
  end
  local x = {}
  for i = nf, 1, -1 do
    local t = y[i]
    for k = i + 1, nf do t = t - A[k][i] * x[k] end
    x[i] = t / A[i][i]
  end
  local u = {}
  for i = 1, n do u[i] = 0.0 end
  for i = 1, nf do u[free[i]] = x[i] end
  local Rz, Rx, Ry = 0.0, 0.0, 0.0                -- phản lực = K u - F tại bậc tự do bị chặn
  for _, s in ipairs(supports) do
    for a = 1, 3 do
      local row, sum = 6 * (s - 1) + a, 0.0
      for j = 1, n do sum = sum + K[row][j] * u[j] end
      sum = sum - F[row]
      if a == 1 then Rx = Rx + sum elseif a == 2 then Ry = Ry + sum else Rz = Rz + sum end
    end
  end
  return u, Rx, Ry, Rz, total_vertical
end

---------------------------------------------------------------- 3. Kiểm chứng và chạy
-- (a) dầm công xôn giải tích: độ võng P L^3 / (3 E I)
do
  local s = props.DAM_X
  local nodes, members = {}, {}
  local L, P, n = 3.0, 5000.0, 6
  for i = 0, n do nodes[#nodes + 1] = {L * i / n, 0.0, 0.0} end
  for i = 1, n do members[#members + 1] = {i, i + 1, "DAM_X", "beam"} end
  local u = solve_static(nodes, members, {1}, {}, {[n + 1] = {0, 0, -P}})
  local vong, lt = -u[6 * n + 3], P * L^3 / (3 * E * s.Iy)
  print(string.format("Kiểm tra dầm công xôn: võng %.6g m, lý thuyết %.6g m, lệch %.4f%%", vong, lt, 100 * math.abs(vong / lt - 1)))
end

local t0 = os.clock()
local nodes, members, supports, udl, loads, masses = build()
local nb, nt = 0, 0
for _, m in ipairs(members) do if m[4] == "beam" then nb = nb + 1 else nt = nt + 1 end end
print(string.format("Mô hình: %d nút, %d thanh (%d dầm/cột, %d giằng), %d bậc tự do", #nodes, #members, nb, nt, 6 * #nodes))
local u, Rx, Ry, Rz, Pv = solve_static(nodes, members, supports, udl, loads)
local wind = 0.0
for _, f in pairs(loads) do wind = wind + f[1] end
print(string.format("Cân bằng: tổng phản lực Rz = %.1f N, tổng tải đứng = %.1f N", Rz, Pv))
print(string.format("          tổng phản lực Rx = %.1f N, tổng tải gió X = %.1f N", Rx, -wind))
local umax, top = 0.0, 1
for i = 1, #nodes do local ux = math.abs(u[6 * (i - 1) + 1]) if ux > umax then umax, top = ux, i end end
print(string.format("Chuyển vị ngang lớn nhất: %.3f mm tại nút %d (z = %g m)   [tính trong %.1f s]", umax * 1e3, top, nodes[top][3], os.clock() - t0))

-- (b) đối chiếu với solver khung có sẵn của FastCAE (cùng văn bản case)
local text = case_text(nodes, members, supports, udl, loads, masses)
local res = fastcae.run(text)
if res.ok then
  local names, dmax = {"ux", "uy", "uz", "rx", "ry", "rz"}, 0.0
  for d = 1, 6 do
    local ref = res.series["node_" .. names[d]]
    for i = 1, #nodes do dmax = math.max(dmax, math.abs(u[6 * (i - 1) + d] - ref[i])) end
  end
  print(string.format("Đối chiếu FastCAE (solver khung có sẵn): sai khác chuyển vị lớn nhất = %.2e (|u|max = %.4f m)", dmax, res.values.max_displacement))
  fastcae.show(res)      -- hiển thị khung 3D: kết quả của solver khung tích hợp trên cùng mô hình
else
  print("Không chạy được solver khung của FastCAE để đối chiếu: " .. tostring(res.message))
end
