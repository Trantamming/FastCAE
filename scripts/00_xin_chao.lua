-- 00. Xin chào: bảng `fastcae`, đặc trưng tiết diện và một bài toán dầm công xôn giải trong script
-- Tác giả: Trần Minh Tâm (MIT). Bấm Chạy (F6). Trợ giúp ▸ API để xem mọi hàm.
print("FastCAE " .. fastcae.version .. " — tác giả " .. fastcae.author)
local s = fastcae.section("ibeam", 0.15, 0.30, 0.0107, 0.0071)
print(string.format("IPE300: A = %.4e m2, Iy = %.4e m4, Iz = %.4e m4", s.A, s.Iy, s.Iz))
-- dầm công xôn bằng khung 1D, đối chiếu PL^3/3EI
local L, P, E = 3.0, 5000.0, fastcae.materials.steel.E
local text = fastcae.case{
  type = "frame", E = E, nu = 0.3, rho = 7850,
  section = "IPE ibeam 0.15 0.30 0.0107 0.0071",
  node = { "1 0 0 0", "2 3 0 0" },
  member = "1 2 IPE beam",
  support = "1 fixed",
  nload = "2 0 0 -5000",
}
local res = fastcae.solve(text)
local exact = P * L^3 / (3 * E * s.Iy)
print(string.format("độ võng FE = %.6e m, lý thuyết = %.6e m, lệch %.2e %%", res.values.max_displacement, exact, 100*math.abs(res.values.max_displacement-exact)/exact))
-- hiển thị kết quả lên khung 3D (chỉ trong giao diện): fastcae.show(res)
fastcae.show(res)
