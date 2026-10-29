# Thư viện cấu kiện kết cấu

Tác giả: Trần Minh Tâm · MIT. Mở bằng thẻ **Cấu kiện** (hoặc Mô hình ▸ Thư viện cấu kiện), hoặc dòng lệnh:

```bash
fastcae-cli templates                 # liệt kê mọi mẫu và tham số mặc định
fastcae-cli template ibeam 3 0.15 0.3 0.0107 0.0071 > dam.fcae
fastcae-cli run dam.fcae
```

## Mỗi loại cấu kiện được mô hình thế nào

| Loại cấu kiện | Cách mô hình trong FastCAE | Solver |
|---|---|---|
| 1. Thanh | Thanh dàn `truss` (chỉ chịu dọc trục) hoặc dầm | Khung 1D tự viết |
| 2. Dầm | Dầm 3D Euler-Bernoulli 12 bậc tự do (`beam`), hoặc khối đặc 3D cho dầm chữ I/U/L/T/ống | Khung 1D / khối đặc |
| 3. Dàn | Các thanh `truss` nối tại nút: Warren, Pratt, dàn mái | Khung 1D |
| 4. Tấm | Tấm phẳng: vỏ S4 (mặt trung bình + chiều dày) hoặc khối đặc (tấm chữ nhật, tấm có lỗ vuông) | CalculiX / khối đặc |
| 5. Vỏ | Vỏ trụ (bồn chứa) bằng phần tử vỏ S4 | CalculiX |
| 6. Khối đặc | Hộp, trụ, cầu, STEP/IGES, mọi cấu kiện ghép khối | Khối đặc T4/T10 |
| 7. Tường, vách, móng | Tường, móng đơn + cổ cột, tường chắn chữ T ngược bằng khối đặc | Khối đặc |
| 8. Liên kết | Ngàm, gối cố định, gối con lăn, **bản lề đầu thanh**, gối đàn hồi (`ksupport`), lò xo nối nút | Khung 1D |
| 9. Đặc biệt | Lò xo (`spring`), cáp chỉ chịu kéo (`cable`, bỏ cáp chùng bằng lặp) | Khung 1D |

## Khung/dàn 1D trong file case

```
type = frame
E = 210e9
nu = 0.3
rho = 7850
yield = 235e6                       # tuỳ chọn: tính hệ số sử dụng sigma/fy
node = 1 0 0 0                      # id x y z (m), Z hướng lên
node = 2 6 0 0
section = IPE ibeam 0.15 0.30 0.0107 0.0071   # rect b h | circle d | pipe d t | ibeam b h tf tw | box b h t | general A Iy Iz J
member = 1 2 IPE beam               # beam | truss | cable | spring k ; thêm pin1 pin2 (hoặc pinned) cho bản lề
support = 1 fixed                   # fixed | pinned | liệt kê x y z rx ry rz
ksupport = 2 0 0 5e6                # gối đàn hồi (N/m) tại nút (nền Winkler rời rạc)
nload = 2 0 0 -5000                 # Fx Fy Fz [Mx My Mz]
udl = 1 0 0 -10000                  # thanh số 1 (đếm từ 1): tải phân bố đều qx qy qz (N/m), hệ chung
gravity = 9.81                      # trọng lượng bản thân, hướng -Z
report = noi_luc.csv                # tuỳ chọn: bảng nội lực từng thanh
nmass = 2 500                       # khối lượng tập trung 500 kg tại nút 2 (cho dao động riêng)
analysis = static                   # static (mặc định) | modal | buckling
modes = 6                           # số dạng cần tính (modal, buckling)
mode = 1                            # dạng hiển thị trong khung nhìn 3D
```

**Dao động riêng** (`analysis = modal`): ma trận khối lượng nhất quán của dầm và thanh dàn (kèm quán tính xoay) cộng khối lượng tập
trung `nmass`; kết quả là tần số (Hz), chu kỳ và dạng dao động (chuẩn hoá chuyển vị tịnh tiến lớn nhất bằng 1). Lò xo không có khối lượng.

**Mất ổn định** (`analysis = buckling`): dùng độ cứng hình học tuyến tính hoá theo lực dọc của lời giải tĩnh; kết quả là các hệ số
tải tới hạn λ (tải tới hạn = λ × tải đã đặt) và dạng mất ổn định. Chỉ dầm (`beam`) mới có mất ổn định uốn; thanh dàn lý tưởng thì không.
Nếu không có thanh nào chịu nén, chương trình báo không có nguy cơ mất ổn định.

Trục địa phương: x dọc thanh; thanh nằm ngang thì y = Y chung, z = Z chung (hướng lên); thanh thẳng đứng dùng X chung làm
vector tham chiếu. Kết quả: chuyển vị, phản lực, nội lực (N, V, T, M), ứng suất pháp lớn nhất, hệ số sử dụng, cáp chùng.
Thanh được vẽ bằng lăng trụ theo tiết diện bao (tiết diện quá mỏng được phóng to để nhìn thấy, chỉ để hiển thị).

## Tấm/vỏ trong file case (cần CalculiX)

```
type = shell
geometry = shell_plate Lx Ly t nx ny          # hoặc shell_cylinder R L t nc nl
fix = xmin x y z                              # không ghi bậc tự do = khoá cả 6; x y z = chỉ khoá chuyển vị (tựa đơn)
pressure = 1000                               # Pa; tấm: hướng -Z; vỏ trụ: áp suất trong, hướng ra ngoài
load = xmax 0 0 -1000                         # hoặc lực tổng chia đều lên tập nút
```

## Kiểm chứng (Linux, 10/2026)

| Bài toán | Kết quả FastCAE | Lời giải tham chiếu | Lệch |
|---|---|---|---|
| Dầm công xôn, lực đầu tự do (1D) | PL³/3EI | giải tích | khớp đến 1e-9 |
| Dầm đơn giản chịu tải đều, 4 phần tử (1D) | độ võng giữa, phản lực, mômen | 5qL⁴/384EI, qL/2, qL²/8 | khớp đến 1e-6 |
| Dàn tam giác (1D) | lực thanh 6009 N nén, 3333 N kéo | cân bằng nút | khớp đến 1e-6 |
| Xoắn thanh tròn, lò xo, cáp chùng (1D) | TL/GJ, F/k, 1250 N | giải tích | khớp đến 1e-6 |
| Dầm công xôn có gối (bản lề đầu thanh) | R_B = 3qL/8, M_A = qL²/8 | giải tích | khớp đến 1e-6 |
| Dầm I công xôn, khối đặc T10, 2400 tứ diện | 6,285e-4 m | Timoshenko 6,281e-4 m | 0,06% |
| Dầm công xôn IPE300 L = 3 m, dao động riêng (12 phần tử) | f₁ = 10,961 Hz (trục yếu), 39,93 Hz (trục mạnh), dạng xoắn 11,38 Hz | 1,8751²/2π·√(EI/ρAL⁴) = 10,961 Hz; xoắn (1/4L)√(GJ/ρI_p) = 11,38 Hz | khớp 4-5 chữ số |
| Dầm công xôn và dầm đơn giản 10 phần tử, dao động riêng | các dạng 1-3 | β²√(EI/ρAL⁴) và (nπ)²√(EI/ρAL⁴) | dưới 0,3% |
| Cột Euler công xôn / hai đầu bản lề (8 phần tử) | tải tới hạn | π²EI/4L², π²EI/L² | dưới 0,2% (IPE300: 346,994 kN, trùng) |
| Tấm vuông tựa đơn, vỏ S4, lưới 64x64 | w = 2,122e-4 m | Kirchhoff 2,111e-4 m | +0,5% |
| Tấm vuông ngàm bốn cạnh, vỏ S4, lưới 64x64 | w = 6,588e-5 m | Kirchhoff 6,552e-5 m | +0,5% |
| Bồn trụ chịu áp suất trong, giữa chiều cao | σ vòng 10,02 MPa; chuyển vị hướng tâm 4,772e-5 m | pR/t = 10,00 MPa; 4,762e-5 m | +0,2% |

## Giới hạn cần biết

- **Chưa mô hình** tiếp xúc, bu lông có dự ứng lực, mối hàn chi tiết, vật liệu cao su phi tuyến (hyperelastic), composite nhiều lớp,
  phi tuyến hình học/vật liệu, động lực học theo thời gian (chỉ có dao động riêng), nền đất liên tục (chỉ có gối lò xo rời rạc `ksupport`).
  Mất ổn định và dao động riêng chỉ có cho khung/dàn 1D (chưa có cho khối đặc và tấm/vỏ).
  Bu lông và liên kết có thể xấp xỉ bằng `spring` hoặc `truss` nối hai nút.
- Cáp: không xét độ võng do trọng lượng bản thân và không có lực căng trước; thuật toán bỏ cáp chịu nén rồi giải lại.
  Cáp hoặc mạng cáp tạo cơ cấu (không đủ ràng buộc) sẽ báo lỗi.
- Mất ổn định: tuyến tính (Euler), bỏ qua xoắn trong độ cứng hình học; không xét khuyết tật ban đầu, phi tuyến sau mất ổn định, mất ổn định cục bộ của cánh/bụng. Cáp coi như thanh dàn chịu kéo khi phân tích riêng.
- Bản lề đầu thanh chỉ giải phóng mômen uốn (giữ xoắn); hình dạng biến dạng vẽ ở đầu bản lề là gần đúng.
- Khung 1D: giải trực tiếp bằng ma trận đầy đủ rút gọn qua bộ giải Cholesky dải; phù hợp đến vài nghìn nút.
- Tấm/vỏ: chỉ có tấm chữ nhật và vỏ trụ (lưới cấu trúc), phần tử S4. Ứng suất tại **biên ngàm** hội tụ chậm theo lưới
  (tấm ngàm 64x64 còn thấp hơn Kirchhoff khoảng 17%); dùng chuyển vị làm thước đo chính, hoặc làm mịn lưới.
  Lưới quá thô (8x8) cho độ võng sai 26%; các mẫu mặc định dùng 24 ô mỗi cạnh.
- Khối đặc từ thư viện: ít nhất một phần tử qua bề dày, nên dùng phần tử bậc 2 (`order = 2`). Lỗ tròn cần Gmsh;
  lỗ vuông không cần.
- Khối đặc, khung, vỏ đều là các mô hình tuyến tính, biến dạng nhỏ.
