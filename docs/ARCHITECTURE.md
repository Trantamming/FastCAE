# Kiến trúc: FastCAE là nơi dựng mô hình, phần giải là của bạn

Tác giả: Trần Minh Tâm · MIT.

## Định hướng

FastCAE **không** cố chứa mọi thư viện CAE và **không** dựng sẵn kho cấu kiện. FastCAE làm ba việc:

1. **Dựng mô hình**: hình học (CAD nhập vào hoặc CAD tham số) → lưới → tập nút → vật liệu → liên kết → tải.
2. **Là chỗ để lập trình thuật toán CAE** (FEM, CFD...): thuật toán nhận mô hình, trả kết quả. Viết bằng Python, C++, Lua hoặc bất kỳ ngôn ngữ nào.
3. **Gọi thư viện/solver mã nguồn mở bên ngoài** (CalculiX, OpenFOAM, SU2, Code_Aster, FEniCSx, scikit-fem, MFEM...) như **tiến trình riêng**, rồi **hiển thị kết quả trên giao diện GUI**.

```
        FastCAE (dựng mô hình + hiển thị)                       Bên ngoài (giải)
 ┌──────────────────────────────────────────┐   mô hình      ┌─────────────────────────────┐
 │ CAD/STEP/lưới → lưới → tập nút → vật liệu│ ─────────────► │ my_fem.py (tự lập trình)    │
 │ → liên kết → tải                         │  *.fcmodel.json│ CalculiX / OpenFOAM / SU2   │
 │                                          │                │ FEniCSx / scikit-fem / MFEM │
 │ Khung 3D: tô màu, biến dạng, đồ thị      │ ◄───────────── │ (chương trình bất kỳ)       │
 └──────────────────────────────────────────┘  <job>_result.vtk └───────────────────────────┘
```

Lợi ích: không phụ thuộc giấy phép của từng thư viện (solver là tiến trình riêng nên FastCAE vẫn là MIT), thay thuật toán không cần biên dịch lại
FastCAE, và mọi solver dùng chung một cách dựng mô hình và một khung hiển thị.

## Hợp đồng "mô hình vào – kết quả ra"

### 1. Mô hình: `<job>.fcmodel.json`

Tạo bằng `fastcae-cli model case.fcae ra.fcmodel.json`, Python `fastcae.model("case.fcae")`, hoặc tự động khi case có `solver = <id>`.
Hiện hỗ trợ `type = elastic | thermal`. SI (m, kg, s, Pa, K hoặc °C).

| Khoá | Nội dung |
|---|---|
| `format`, `version` | `"fastcae-model"`, `1` |
| `analysis` | `"elastic"` (đàn hồi tuyến tính 3D) hoặc `"thermal"` (dẫn nhiệt dừng) |
| `order` | `1` (tứ diện 4 nút) hoặc `2` (10 nút) |
| `material` | `E`, `nu`, `k`, `rho` |
| `mesh.nodes` | toạ độ phẳng `[x0,y0,z0,x1,...]` (reshape `(-1,3)`); với `order = 2` gồm cả nút giữa cạnh |
| `mesh.tet4` | 4 chỉ số đỉnh mỗi phần tử, phẳng |
| `mesh.tet10` | (chỉ `order = 2`) 10 chỉ số mỗi phần tử. Thứ tự giống CalculiX/Abaqus C3D10: 0–3 là đỉnh; 4=cạnh 0-1, 5=1-2, 6=2-0, 7=0-3, 8=1-3, 9=2-3 |
| `mesh.body_node_start` | đầu chỉ số nút của từng vật thể (rỗng nếu một vật thể) |
| `dirichlet` | elastic: `{set, dofs:[0,1,2], value, nodes}`; thermal: `{set, value, nodes}` |
| `loads` | elastic: `{set, total:[fx,fy,fz], nodal:[[nút,fx,fy,fz],...]}` (đã phân bố nhất quán theo diện tích phụ trách); thermal: `{set, total_watts, nodal:[[nút,q],...]}` |
| `node_sets` | các tập nút đã dùng, theo tên |

Chỉ số nút bắt đầu từ 0. Phần tử có thể có định hướng âm; dùng `abs(det J)`.

### 2. Kết quả: `<job>_result.vtk`

Solver ghi VTK legacy ASCII `UNSTRUCTURED_GRID` vào thư mục làm việc, tên `<job>_result.vtk`. **Điểm thứ i của VTK là nút i của mô hình.**

| Bài toán | Trường nút |
|---|---|
| elastic | `displacement` (VECTORS), tuỳ chọn `von_mises_nodal` (SCALARS) |
| thermal | `temperature` (SCALARS) |

Trường khác (SCALARS/VECTORS tuỳ ý, cả trường theo ô) đều hiển thị được. FastCAE tính thêm `displacement_magnitude`, và các giá trị
`max_<tên trường>` cho script/JSON. Nếu số điểm khác số nút mô hình, FastCAE hiển thị theo lưới trong file kết quả và cảnh báo.
Solver chạy xong mà không có file này → lỗi nêu rõ đường dẫn mong đợi.

## Khai báo solver: file `*.fsolver`

Không cần biên dịch lại FastCAE. FastCAE đọc mọi `*.fsolver` trong các thư mục của biến môi trường `FASTCAE_SOLVER_PLUGINS`
(nhiều thư mục, ngăn bằng `:` hoặc `;`) và thư mục `solvers.d` cạnh thư mục làm việc người dùng (`~/.local/share/FastCAE/solvers.d`,
`%LOCALAPPDATA%\FastCAE\solvers.d`), và `share/fastcae/solvers.d` trong thư mục cài đặt (chứa sẵn plugin `my_fem`). Plugin không được ghi đè solver có sẵn.

```
id = my_fem                               # chữ thường, số, _ -
name = FEM tự lập trình (Python)
category = fem                            # fem | cfd | dynamics | coupling | library
license = MIT
copyleft = permissive                     # permissive | weak | strong | network
capability = Mô tả ngắn
input = model                             # model: hợp đồng ở trên
env = FASTCAE_PYTHON                      # tuỳ chọn: biến môi trường trỏ tới file chạy chính (ưu tiên trước PATH)
step = python3, python | "{plugin_dir}/my_fem.py" {input} {result}
```

* `step` lặp được (nhiều bước nối tiếp); vế trái là các tên file chạy thử lần lượt (`env = BIEN_MOI_TRUONG` để chỉ thẳng).
* Mẫu tham số: `{plugin_dir}` (thư mục chứa `.fsolver`, thay khi nạp), `{input}` (file mô hình), `{result}` (file VTK kết quả), `{dir}`, `{base}`; các biến đã có dấu nháy.
* Dùng trong case: `solver = my_fem`; tuỳ chọn `job_name`, `work_dir`, `solver_path`, `solver_args`. Chạy từ GUI, `fastcae-cli run`, Lua `fastcae.run`, Python `fastcae.run`.
* Giấy phép do bạn tự khai báo và tự đối chiếu (`fastcae-cli solvers` hiển thị).

Ví dụ hoàn chỉnh: `examples/solver_plugin/` — `my_fem.py` là FEM 3D tự viết (đàn hồi + dẫn nhiệt, T4/T10) bằng numpy/scipy.
`cantilever_myfem.fcae` giải dầm console bằng nó.

```bash
export FASTCAE_SOLVER_PLUGINS=$PWD/examples/solver_plugin
fastcae-cli run examples/solver_plugin/cantilever_myfem.fcae
```

## Hai cách lập trình thuật toán

**A. Đóng gói thành solver** (`.fsolver` + chương trình đọc `.fcmodel.json`, ghi `_result.vtk`): dùng được từ GUI bằng `solver = ...`.
Thay `my_fem.py` bằng thuật toán của bạn; ngôn ngữ nào cũng được (C++, Julia, Fortran...).

**B. Lập trình trực tiếp trong Python**:

```python
import fastcae, numpy as np
m = fastcae.model("dam.fcae")            # dựng mô hình, chưa giải
a = fastcae.model_arrays(m)              # a["nodes"], a["tet4"], a["tet10"], a["dirichlet"], a["loads"]
u = my_algorithm(a)                      # thuật toán của bạn
fastcae.write_vtk("kq.vtk", a["nodes"], a["tet4"], point_vectors={"displacement": u.reshape(-1, 3)})
fastcae.show("kq.vtk")                   # hiện lên khung 3D (trong bảng Lập trình của GUI)
```

## Dùng thư viện mã nguồn mở bên ngoài

Giữ nguyên nguyên tắc: **không** nhúng vào FastCAE. Hai mức:

* **Cầu nối có sẵn** (ghi input → chạy → đọc kết quả): CalculiX (`solver = calculix`), OpenFOAM (`type = cfd3d`), SU2, Gmsh (CAD → lưới). Xem [SOLVERS.md](SOLVERS.md).
* **Bất kỳ thư viện khác** (FEniCSx, scikit-fem, MFEM, Kratos, Elmer...): viết một chương trình mỏng đọc `.fcmodel.json`, gọi thư viện, ghi `_result.vtk`, rồi khai báo bằng `.fsolver`.

## Trạng thái và giới hạn (đã kiểm chứng)

| Hạng mục | Trạng thái |
|---|---|
| Xuất mô hình elastic/thermal, T4 và T10 | ✅ test `solver_plugin_contract` |
| Plugin `.fsolver` chạy từ `fastcae-cli run`, Python | ✅ dầm console: \|u\|max = 1,90524e-4 m trùng solver nội (sai < 1e-6) và lý thuyết dầm (0,03%) |
| Dẫn nhiệt 1D qua plugin | ✅ Tmax = 100, Tmin = 0 |
| Báo lỗi: solver không ghi kết quả, solver không biết | ✅ |
| Von Mises nút của `my_fem.py` | trung bình ứng suất tại nút theo phần tử; lệch ~0,3% so với solver nội (khác cách làm trơn), không phải sai số giải |
| Chạy plugin từ GUI | ⚙️ đường chạy dùng chung `runCase` nên `solver = my_fem` trong trình soạn case phải chạy được; **chưa biên dịch lại / thử GUI Qt trong lần sửa này** |
| Hợp đồng cho CFD | ❌ chưa có (xem lộ trình): mô hình chưa xuất mặt biên theo tên/ vùng chất lỏng |
| Kết quả nhiều bước thời gian từ plugin | ❌ chưa (đọc chuỗi `.vtk` đã có cho solver ngoài khác) |
| Giá trị có tên từ plugin (`max_*` tự tính từ trường) | ⚙️ mới tự tính từ trường, chưa có file khai báo riêng |

## Bài thực hành

`examples/tutorial/` (cũng được cài vào `share/fastcae/examples/tutorial`, mô tả trong chương 18–19 của Hướng dẫn trong phần mềm, phím F1):
Bài 1 dầm + FEM Python tự viết qua `solver = my_fem`; Bài 2 lập trình FEM trong bảng Lập trình; Bài 3 CFD khoang kín tự lập trình (lệch Ghia 0,9%);
Bài 4 nối CalculiX/OpenFOAM/SU2 (cần cài riêng). Bài 1–3 chạy tự động trong test `tutorial_examples`.
