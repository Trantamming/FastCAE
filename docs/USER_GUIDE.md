# Hướng dẫn sử dụng

> Bản đầy đủ, có mục lục và tìm kiếm, nằm ngay trong phần mềm: **Trợ giúp ▸ Hướng dẫn sử dụng (F1)**. Nguồn: [help/huong_dan.html](help/huong_dan.html). Tệp này là bản tóm tắt cú pháp case.

## File case (`.fcae`)

Dạng `khoá = giá trị`, `#` bắt đầu chú thích; khoá không phân biệt hoa/thường. Đơn vị SI.

| Khoá | Áp dụng | Ý nghĩa |
|---|---|---|
| `type` | tất cả | `elastic`, `thermal`, `cavity` |
| `geometry` | elastic/thermal | `box lx ly lz` · `cylinder R L` · `sphere R` · `file duong_dan` (.step/.stp/.iges/.igs/.brep/.xao/.stl/.msh/.inp/.unv/.bdf/.med/.mesh/.FCStd) |
| `divisions` | box | số chia `nx ny nz` |
| `mesh_size` | trụ/cầu/file CAD | cỡ phần tử tối đa **tính bằng mét** (0 = tự động) |
| `unit_scale` | file CAD | hệ số quy đổi sang mét (STEP mm → `0.001`) |
| `order` | elastic/thermal | `1` = T4 (mặc định), `2` = T10 bậc 2 (khuyên dùng cho uốn) |
| `solver` | elastic | `builtin` (mặc định) hoặc `calculix` (cần `ccx`) |
| `E`, `nu`, `k`, `rho` | | mô-đun Young, Poisson, độ dẫn nhiệt, khối lượng riêng |
| `fix` | elastic | `fix = xmin` (3 phương) hoặc `fix = ymin y` — lặp được |
| `load` | elastic | `load = xmax 0 0 -1000` — **tổng** lực (N), phân bố theo diện tích phụ trách |
| `temp` | thermal | `temp = xmin 100` |
| `heat` | thermal | `heat = xmax 50` — tổng công suất (W) |
| `output` | | file `.vtk` (CLI; đường dẫn tương đối tính từ thư mục file case) |
| `n`, `Re`, `lid_speed`, `max_steps` | cavity | lưới, Reynolds, vận tốc nắp, số bước |

Tập nút: `xmin xmax ymin ymax zmin zmax all` (theo hộp bao của lưới).

## Giao diện

- **Tệp ▸ Ví dụ** nạp bài mẫu; **F5** giải. Chuột trái xoay, giữa/Shift+trái dịch, lăn chuột zoom, **bấm** vào mô hình để đọc giá trị tại điểm đó.
- Thanh công cụ: các hướng nhìn (iso / trước / phải / trên), vừa khung, hiện/ẩn cạnh lưới.
- Bảng **Kết quả**: chọn trường (chuyển vị, von Mises, nhiệt độ…), thanh phóng đại biến dạng.
- **Mô hình ▸ Thiết kế hình học tham số**: khối hộp/trụ/cầu → ghi dòng `geometry` vào case.
- **Tệp ▸ Nhập CAD**: chọn file, đơn vị (mm/cm/m/inch) và cỡ lưới (mm); FastCAE tự ghi `geometry`, `unit_scale`, `order = 2`, `mesh_size`. Quá nhiều phần tử (>4 triệu) sẽ bị từ chối kèm gợi ý tăng cỡ lưới.
- **Tệp ▸ Xuất** kết quả `.vtk` nếu cần mở bằng phần mềm khác (không bắt buộc).

## Nhập CAD

Ba đường (tự chọn theo cái sẵn có): thư viện Gmsh liên kết · `gmsh` ngoài (PATH hoặc `FASTCAE_GMSH`) · đọc thẳng lưới `.msh` / `.inp`. Dùng `fastcae-cli formats` để xem máy bạn hỗ trợ gì.

| Đuôi file | Loại | Ghi chú |
|---|---|---|
| `.step .stp .iges .igs .brep .brp .xao` | hình học B-Rep | chia lưới khối bằng Gmsh; đặt `mesh_size` (mét) hoặc để 0 = tự động |
| `.stl` | lưới bề mặt | phải kín; Gmsh dựng khối rồi chia lưới. Mô hình có cạnh sắc cho kết quả tốt nhất |
| `.msh .inp` | lưới khối | đọc thẳng, không chia lại lưới, không cần Gmsh. `.inp` = Abaqus/CalculiX (`*NODE`, `*ELEMENT` loại C3D4/C3D10/C3D8/C3D20/C3D6/C3D15; vỏ/dầm bị bỏ qua; `*INCLUDE` không được theo) |
| `.unv .bdf .nas .med .mesh` | lưới khối | đọc qua Gmsh |
| `.FCStd` | FreeCAD | gọi `freecadcmd` để đổi sang STEP |

Với lưới có sẵn, nhớ đặt `unit_scale` (mm → `0.001`) vì tọa độ trong file được hiểu theo đơn vị của file. Nhóm nút theo mặt (`xmin`, `xmax`…) và chọn mặt bằng chuột dùng được như với lưới từ STEP. Hexa/lăng trụ/tháp được cắt thành tứ diện; lưới hỗn hợp tứ diện + hexa không liên tục tại mặt chung của hai loại.

Từ SolidWorks/Inventor/AutoCAD/Fusion/Creo/CATIA/NX: **Save As / Export → STEP (AP214/AP242)**. Định dạng gốc của các phần mềm này không có đặc tả mở nên FastCAE không đọc trực tiếp; nếu bạn có công cụ chuyển đổi riêng, đặt `FASTCAE_CAD_CONVERTER` (lệnh có `{in}` và `{out}`, `{out}` là file `.step`) thì mở thẳng được `.sldprt`, `.ipt`, `.CATPart`… (FastCAE chỉ gọi công cụ đó, không kèm theo). FreeCAD: mở thẳng `.FCStd` nếu có `freecadcmd`, hoặc `freecadcmd tools/fcstd_to_step.py a.FCStd a.step`.

Ví dụ: `examples/plate_hole.fcae` (tấm 100×40×20 mm, lỗ Ø16, kéo 10 kN, T10).


## Chọn mặt bằng chuột để đặt liên kết, lực, nhiệt

1. Bấm **Chọn mặt bằng chuột** (thẻ Kết cấu, Nhiệt hoặc Kết quả).
2. Bấm lên một mặt phẳng của vật thể: mọi tam giác biên cùng mặt phẳng được tô cam; thanh trạng thái hiện pháp tuyến, số tam giác và diện tích. Bấm ra ngoài vật thể hoặc nhấn **Esc** để bỏ chọn.
3. Bấm **Liên kết**, **Lực**, **Nhiệt độ** hoặc **Nguồn nhiệt**; mục đầu của ô "Mặt / tập nút" là mặt đã chọn.

FastCAE ghi mặt đó vào file case dưới dạng tập nút hình học, không phụ thuộc lưới nên đổi cỡ lưới hoặc mở lại file vẫn đúng:

```
nodeset = face1 plane nx ny nz d tol     # mặt phẳng n·x = d, dung sai tuyệt đối tol (m)
fix  = face1
load = face1 0 0 -2000
```

Giới hạn: chỉ chọn được **mặt phẳng** (chưa chọn mặt trụ/cong, cạnh hay điểm). Mọi mặt biên cùng nằm trên mặt phẳng đó đều được chọn, kể cả các phần tách rời. Mặt phẳng chỉ cắt xuyên bên trong vật thể thì không chọn nút nào và bài toán báo "Tập nút không hợp lệ".


## Chạy nền, OpenFOAM và PETSc

- Bấm **Chạy** (F5): bài toán giải ở luồng nền, thanh tiến trình hiện ở góc phải thanh trạng thái, vẫn xoay/zoom được. Yêu cầu giải mới trong lúc đang chạy sẽ được xếp hàng. Quá nhiều phần tử → thông báo "Hết bộ nhớ" kèm gợi ý, không làm sập chương trình.
- **CFD ▸ OpenFOAM khoang kín**: hỏi số ô và Reynolds, ghi case vào thư mục tạm, chạy OpenFOAM thật và hiện vận tốc, áp suất, xoáy. Log của OpenFOAM nằm trong thư mục case (`log.blockMesh`, `log.icoFoam`). Trong file case: `type = cavity`, `solver = openfoam`, `n`, `Re`, `end_time`, `work_dir`, `openfoam_bashrc` (xem `examples/cavity_openfoam.fcae`).
- **Kết cấu/Nhiệt ▸ Giải bằng PETSc**: chuyển bộ giải tuyến tính sang PETSc (`linear_solver = petsc`); bản không build PETSc sẽ báo rõ.


## Thư viện cấu kiện và khung/dàn 1D

Thẻ **Cấu kiện**: bấm *Thư viện cấu kiện* (hoặc một trong bốn nút nhóm), chọn loại, nhập kích thước, bấm *Tạo mô hình*.
FastCAE tạo file case hoàn chỉnh (hình học, vật liệu, liên kết, tải mặc định) và giải ngay; bạn sửa tiếp trong khung "Bài toán".

Với khung/dàn 1D, nhóm *Dựng khung / dàn 1D* cho thêm nút, tiết diện, thanh (dầm, thanh dàn, cáp, lò xo, có bản lề),
liên kết, tải tại nút và tải phân bố. Biểu đồ nội lực: chọn trường `bending_moment`, `axial_force`, `shear_force`,
`normal_stress` hoặc `utilization` (cần `yield`) ở bảng Kết quả. Chi tiết và giới hạn: [STRUCTURAL_LIBRARY.md](STRUCTURAL_LIBRARY.md).


## Lập trình tùy biến

Thẻ **Lập trình**: soạn và chạy script Lua ngay trong phần mềm (F6), có sẵn 8 ví dụ; `fastcae.show(res)` đưa kết quả của script lên
khung 3D. Ngoài ra có module Python và thư viện C++. Hướng dẫn đầy đủ: [SCRIPTING.md](SCRIPTING.md).
