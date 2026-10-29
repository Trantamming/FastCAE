# CAD tham số (file `.fcad`)

Mô hình = **bảng tham số + cây tính năng**. Đổi một tham số thì toàn bộ mô hình dựng lại. FastCAE biên dịch `.fcad` thành
script Gmsh (nhân OpenCASCADE) rồi chia lưới khối bằng `gmsh` ngoài (đặt `FASTCAE_GMSH` hoặc đưa `gmsh` vào PATH).
Đơn vị: mét. Chú thích bắt đầu bằng `#`. Đối số là biểu thức **không chứa khoảng trắng** (`L/2`, `(L+5)*2`);
riêng `param` cho phép khoảng trắng. Hàm: sin cos tan asin acos atan sqrt abs exp log rad deg min max pow, hằng `pi`.

| Lệnh | Ý nghĩa |
|---|---|
| `param tên = biểu_thức` | tham số (có thể dùng tham số khai báo trước) |
| `box tên x y z dx dy dz` | khối hộp |
| `cylinder tên x y z dx dy dz r` | trụ; (dx,dy,dz) là trục, độ dài = chiều cao |
| `sphere tên x y z r` · `cone tên x y z dx dy dz r1 r2` · `torus tên x y z R r` | cầu, nón/nón cụt, xuyến |
| `sketch tên xy\|yz\|xz [độ_dời]` … `end` | phác thảo phẳng: `rect x y rộng cao`, `polygon x1 y1 x2 y2 …`, `circle cx cy r`. Vòng đầu là biên ngoài, các vòng sau là **lỗ** |
| `csketch tên xy\|yz\|xz [độ_dời]` … `end` | **phác thảo có ràng buộc**: tọa độ chỉ là ước đoán ban đầu, bộ giải 2D tìm vị trí thỏa mọi ràng buộc. Xem bảng bên dưới |
| `extrude tên phác_thảo d` | đùn theo pháp tuyến mặt phác thảo |
| `revolve tên phác_thảo góc_độ px py pz ax ay az` | xoay quanh trục qua p hướng a |
| `cut tên a b [c…]` · `fuse tên a b […]` · `intersect tên a b […]` | phép toán Boolean (các thân đầu vào bị dùng hết) |
| `translate tên thân dx dy dz` · `rotate tên thân px py pz ax ay az góc` | di chuyển / xoay |
| `linear_pattern tên thân n dx dy dz` · `circular_pattern tên thân n px py pz ax ay az góc_tổng` | mảng thẳng / mảng tròn (hợp nhất) |
| `mirror tên thân a b c d` | thân + ảnh đối xứng qua mặt ax+by+cz+d=0 |
| `fillet r xmin ymin zmin xmax ymax zmax` · `chamfer d …` | bo góc / vát cạnh mọi cạnh nằm **trọn trong hộp** chọn; lệnh có thể lặp. **Áp dụng sau cùng** lên thân kết quả (không xen kẽ giữa các tính năng). Cần Python có gói `gmsh` (`pip install gmsh`, đặt `FASTCAE_PYTHON` nếu cần); chạy như tiến trình riêng nên FastCAE vẫn MIT |
| `result tên` | chọn thân kết quả (mặc định: thân duy nhất còn lại) |

## Phác thảo có ràng buộc (`csketch`)
Khai báo: `pt A x y` (điểm), `line l A B`, `circle c P r` (tâm là điểm P, r ban đầu), `arc c O P Q` (cung **ngược chiều kim đồng hồ** tâm O từ P đến Q; dùng được như đường tròn trong
`radius`, `equal`, `tangent`). Biên: `loop A B C D` (đa giác theo thứ tự), `wire e1 e2 …` (vòng kín gồm đường thẳng và cung nối tiếp, chung đầu mút), `loopcircle c`;
vòng đầu = biên ngoài, các vòng sau = lỗ. Giá trị kích thước là biểu thức của tham số nên phác thảo **được điều khiển bởi tham số**.

| Ràng buộc | Ý nghĩa |
|---|---|
| `fix A [x y]` | cố định điểm (không có x y: giữ tại vị trí ban đầu) |
| `coincident A B` · `midpoint P l` | trùng nhau · trung điểm |
| `horizontal l` hay `horizontal A B` · `vertical …` | nằm ngang · thẳng đứng |
| `dist A B d` · `length l d` | khoảng cách · độ dài đoạn |
| `parallel l1 l2` · `perpendicular l1 l2` · `equal l1 l2` · `angle l1 l2 độ` | song song · vuông góc · bằng chiều dài (hoặc `equal c1 c2`: bằng bán kính) · góc từ l1 đến l2 |
| `radius c r` · `on P l` / `on P c` · `tangent l c` | bán kính (tròn hoặc cung) · điểm nằm trên đường/tròn · đường tiếp xúc tròn/cung (đường và cung phải chung đầu mút) |

Bộ giải (Levenberg–Marquardt) báo: **mâu thuẫn** (lỗi, kèm dòng), **chưa đủ ràng buộc** (cảnh báo: còn n bậc tự do) và **dư ràng buộc**
(cảnh báo: nhất quán nhưng thừa). Ví dụ: `examples/param/tam_rang_buoc.fcad` (tấm có lỗ), `examples/param/ranh_cung.fcad` (rãnh bầu dục: đường + cung tiếp xúc). Cung lớn hơn 120° được tự tách thành nhiều mảnh khi sinh script Gmsh.

## Dùng

```
fastcae-cli param examples/param/bracket.fcad --set T=0.02 --size 0.01 --out m.vtk --geo m.geo
```
Trong case: `geometry = param bracket.fcad`, `param_set = T=0.015` (lặp được; quét tham số = đổi `param_set` rồi chạy lại),
`mesh_size = 0.01`. Xem `examples/param/bracket.fcae`.

## Lập trình: quét tham số bằng Lua
`scripts/15_quet_tham_so_cad.lua` đổi `H` của `examples/param/dam_tham_so.fcad`, dựng lại hình học, chia lưới, giải và so với
lý thuyết dầm console `F·L³/(3EI)`: lệch 0,12–0,55% cho H = 15…30 mm. Cùng cách này dùng cho tối ưu, quét nhiều tham số, tổ hợp tải.

## Kiểm chứng
`tests/param/validate.py`: 31 mô hình (gồm 10 phác thảo ràng buộc: đa giác sai số 0, có cung/lỗ tròn ≤ 0,5%) (gồm bo một cạnh sai số 0,04%, vát một cạnh 0,00%) so thể tích lưới với công thức (hộp/Boolean/mảng/đối xứng: sai số 0; trụ, cầu, nón,
xoay: ≤ 2% do xấp xỉ mặt cong bằng tứ diện) và 6 trường hợp lỗi phải báo đúng dòng.

## Chưa có (nói thẳng)
Đây **chưa phải** SolidWorks. Chưa có: spline và ràng buộc đối xứng trong phác thảo, phác thảo trực quan bằng chuột,
bo góc xen kẽ giữa các tính năng (hiện chỉ áp dụng sau cùng; lệnh `Fillet` không có trong ngôn ngữ .geo của Gmsh 4.15 nên dùng API Python), vỏ mỏng (shell), sweep/loft, đường cong spline, lắp ráp có ràng buộc, giao diện phác thảo
bằng chuột. Các mục này cần nhân hình học riêng hoặc liên kết trực tiếp OpenCASCADE (xem ROADMAP 1.0).

# Lưu kết quả (`.fcres`)
Case thêm `result_file = ten.fcres` để lưu lưới + mọi trường + đại lượng kỹ thuật (độ chính xác đầy đủ).
`fastcae-cli result info|vtk|csv ten.fcres [đầu_ra]` xem, xuất VTK (mở bằng ParaView) hoặc CSV theo nút.
