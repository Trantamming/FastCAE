-- 01. Đặc trưng hình học tiết diện và so sánh các phương án
-- Tác giả: Trần Minh Tâm (MIT). Chạy: Lập trình ▸ Chạy, hoặc  fastcae-cli script 01_tiet_dien.lua
local E = fastcae.materials.steel.E
local ds = {
  { "Chu I 150x300x10.7x7.1", "ibeam", 0.15, 0.30, 0.0107, 0.0071 },
  { "Hop 150x300x8",           "box",   0.15, 0.30, 0.008 },
  { "Chu nhat 100x300",        "rect",  0.10, 0.30 },
  { "Ong tron D200x6",         "pipe",  0.20, 0.006 },
}
print(string.format("%-26s %10s %12s %12s %10s", "Tiết diện", "A (cm2)", "Iy (cm4)", "Wy (cm3)", "kg/m"))
for _, d in ipairs(ds) do
  local s = fastcae.section(table.unpack(d, 2))
  local W = s.Iy / (s.height / 2)
  print(string.format("%-26s %10.2f %12.1f %12.1f %10.2f", d[1], s.A * 1e4, s.Iy * 1e8, W * 1e6, s.A * fastcae.materials.steel.rho))
end
-- độ cứng uốn EI và độ võng công xôn L = 3 m, P = 5 kN của từng tiết diện
print("\nĐộ võng đầu công xôn L = 3 m, P = 5 kN:")
for _, d in ipairs(ds) do
  local s = fastcae.section(table.unpack(d, 2))
  print(string.format("  %-26s %8.2f mm", d[1], 1000 * 5000 * 3^3 / (3 * E * s.Iy)))
end
