-- 07. Hội tụ lưới của tấm vuông tựa đơn (cần CalculiX): độ võng giữa tấm so với lý thuyết Kirchhoff
local mat = fastcae.materials.steel
local a, t, q = 1.0, 0.01, 1000.0
local D = mat.E * t^3 / (12 * (1 - mat.nu^2))
local exact = 0.00406 * q * a^4 / D
print(string.format("Kirchhoff: w = %.4e m", exact))
for _, n in ipairs({ 8, 16, 32 }) do
  local res = fastcae.run(fastcae.shell_template("plate_simply_supported", { a, t, q, n }) .. "\nwork_dir = fastcae_shell_tmp\n")
  if not res.ok then
    print("Không chạy được CalculiX: " .. res.message)
    break
  end
  local w = res.values.max_displacement
  print(string.format("  lưới %2dx%-2d  w = %.4e m   lệch %+.2f %%", n, n, w, 100 * (w - exact) / exact))
end
