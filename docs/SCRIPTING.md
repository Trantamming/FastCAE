# Lập trình tùy biến và tính toán kỹ thuật trong FastCAE

Tác giả: Trần Minh Tâm · MIT.

## Chọn ngôn ngữ nào?

FastCAE được viết bằng **C++20**, nhưng C++ không phù hợp để người dùng viết script nhanh (phải có trình biên dịch, liên kết,
biên dịch lại mỗi lần sửa). Vì vậy có ba lớp, dùng ngôn ngữ nào tuỳ nhu cầu:

| Lớp | Ngôn ngữ | Cần cài thêm | Hợp với |
|---|---|---|---|
| **Bảng lập trình trong phần mềm** | **Lua 5.4** (nhúng sẵn, MIT) | không | quét tham số, tối ưu, tổ hợp tải, tính tay, đưa kết quả lên khung 3D |
| **Module Python** `python/fastcae.py` | Python 3 | Python | phân tích số liệu (numpy, scipy, pandas), vẽ biểu đồ (matplotlib), tự động hoá hàng loạt |
| **Thư viện C++** `fastcae_core` | C++20 | trình biên dịch | nhúng solver vào chương trình khác, tốc độ, viết solver mới |

Cả ba dùng **cùng bộ giải** và cùng định dạng file case. Lua được chọn để nhúng vì nhỏ (vài trăm KB), giấy phép MIT, chạy được ngay
trên Windows lẫn Linux mà không cần Python hay trình biên dịch.

## 1. Lua trong phần mềm

Thẻ **Lập trình** (hoặc Công cụ ▸ Bảng lập trình): soạn script có tô màu cú pháp, **Chạy (F6)**, **Dừng**, Mở/Lưu, menu **Ví dụ**,
nút **API**. Script chạy ở luồng nền, đầu ra hiện ở khung bên dưới. Thư mục làm việc: thư mục chứa script (hoặc `Tài liệu/FastCAE`).

Dòng lệnh (không cần giao diện): `fastcae-cli script file.lua [tham số...]` hoặc `fastcae-cli script -e "print(fastcae.version)"`.
Tham số nằm trong bảng `arg`.

```lua
-- dầm công xôn bằng khung 1D, đối chiếu lý thuyết
local s = fastcae.section("ibeam", 0.15, 0.30, 0.0107, 0.0071)
local res = fastcae.solve(fastcae.case{
  type = "frame", E = 210e9, nu = 0.3, rho = 7850,
  section = "IPE ibeam 0.15 0.30 0.0107 0.0071",
  node = { "1 0 0 0", "2 3 0 0" }, member = "1 2 IPE beam",
  support = "1 fixed", nload = "2 0 0 -5000" })
print(res.values.max_displacement, 5000 * 3^3 / (3 * 210e9 * s.Iy))
fastcae.show(res)         -- hiển thị lên khung 3D (chỉ trong giao diện)
```

### Bảng `fastcae`

| Hàm | Mô tả |
|---|---|
| `fastcae.run(caseText [, {base_dir=, write_output=}])` | chạy bài toán, trả về bảng kết quả (không báo lỗi, kiểm tra `res.ok`) |
| `fastcae.solve(caseText)` | như `run` nhưng báo lỗi (có thông báo) nếu giải thất bại |
| `fastcae.case{...}` | dựng văn bản case từ bảng Lua; giá trị là mảng thì lặp khoá (`node = {...}`); `type` luôn đứng đầu |
| `fastcae.templates()` | danh sách cấu kiện: `{name, label, category, kind, params={ {key,label,default} }}` |
| `fastcae.shape(name, {p...} [, "steel"\|"concrete"\|"aluminium"])` | văn bản case của cấu kiện khối đặc (dầm I/U/L/T, tấm, tường, móng...) |
| `fastcae.frame_template(name, {p...})`, `fastcae.shell_template(name, {p...})` | mẫu khung/dàn 1D, tấm/vỏ |
| `fastcae.section(kind, ...)` | `A, Iy, Iz, J, width, height` của tiết diện `rect circle pipe ibeam box general` |
| `fastcae.materials` | `steel`, `concrete`, `aluminium` với `E, nu, rho, fy` |
| `fastcae.write_csv(path, rows [, header])`, `fastcae.read_file(path)` | ghi bảng số, đọc file văn bản |
| `fastcae.set_linear_solver(name)`, `fastcae.petsc_available()` | `auto`, `direct`, `cg`, `petsc` |
| `fastcae.show(res)`, `fastcae.open_case(text)` | **chỉ trong giao diện**: hiển thị kết quả lên khung 3D, nạp case vào bảng Bài toán |
| `fastcae.version`, `fastcae.author` | |

**Bảng kết quả** `res`: `ok, message, type, summary, log, nodes, cells, iterations, max_value, secondary`;
`res.values.<tên>` (đại lượng kỹ thuật có tên, ví dụ `max_displacement`, `max_stress`, `max_moment`, `max_axial`,
`max_utilization`, `reaction_fz`, `max_von_mises`, `t_max`, `frequency_1_hz`, `load_factor_1`...);
`res.series.<tên>` (dãy số: `node_uz`, `reaction_fz`, `member_n_max`, `member_m_max`, `member_stress_max`, `frequencies_hz`...);
phương thức `res:fields()`, `res:field(name)`, `res:field_stats(name)` (min, max, mean), `res:node(i)`, `res:save_vtk(path)`.
Gọi phương thức bằng dấu hai chấm (`res:field(...)`).

### Script mẫu (`scripts/`, cũng có trong menu Ví dụ)

| File | Nội dung |
|---|---|
| `00_xin_chao.lua` | bảng `fastcae`, đặc trưng tiết diện, dầm công xôn so với lý thuyết, `fastcae.show` |
| `01_tiet_dien.lua` | A, I, W, khối lượng/m và độ võng của các tiết diện |
| `02_quet_tham_so_dam.lua` | quét chiều cao dầm, so với 5qL⁴/384EI, ghi CSV |
| `03_toi_uu_tiet_dien.lua` | chia đôi tìm chiều cao nhỏ nhất thoả độ võng và ứng suất |
| `04_dan_so_khoang.lua` | dàn Warren theo số khoang: lực dọc, độ võng, khối lượng |
| `05_tan_so_theo_chieu_dai.lua` | tần số riêng theo chiều dài so với công thức |
| `06_to_hop_tai_trong.lua` | duyệt tổ hợp tải, lấy tổ hợp bất lợi nhất theo σ/fy |
| `07_hoi_tu_luoi_tam.lua` | hội tụ lưới của tấm vỏ (cần CalculiX) |

### An toàn

Script là mã chạy trên máy bạn như macro. Các hàm chạy lệnh hoặc xoá file bị tắt: `os.execute`, `os.exit`, `os.remove`,
`os.rename`, `os.tmpname`, `io.popen`, `dofile`, `loadfile`, `package.loadlib`. `io.open` vẫn dùng được để ghi CSV, kết quả.
Hãy chỉ chạy script bạn tin cậy: chặn này là lớp phòng ngừa, không phải môi trường cô lập hoàn toàn.

### Giới hạn

- Nút Dừng dừng script ở lần kiểm tra kế tiếp (mỗi 10 nghìn lệnh Lua); một lần giải solver đang chạy thì chờ xong.
- Mỗi lần chạy tạo một trạng thái Lua mới (biến không giữ lại giữa các lần chạy).
- `fastcae.show` hiển thị kết quả nhưng không đổi nội dung bảng Bài toán; dùng `fastcae.open_case` nếu muốn nạp case.
- API còn ở phiên bản 0.x, có thể đổi.

## 2. Python

```python
import fastcae                       # python/fastcae.py; đặt FASTCAE_CLI nếu fastcae-cli không có trong PATH
res = fastcae.run(fastcae.template("cantilever_beam", 3.0, 5000, 6)).raise_for_status()
print(res.values["max_displacement"], res.series["node_uz"])
vtk = fastcae.read_vtk("ket_qua.vtk")        # điểm, ô, trường vô hướng/vectơ (dùng numpy.array nếu muốn)
```

Chỉ cần thư viện chuẩn của Python. Module gọi `fastcae-cli run --json`, nên mọi bài toán (khối đặc, khung, tấm/vỏ, nhiệt, CFD)
đều dùng được. Ví dụ: `examples/python/quet_tham_so_dam.py` (vẽ biểu đồ nếu có matplotlib), test: `python/test_fastcae.py`.

## 3. Thư viện C++

Mọi solver nằm trong thư viện tĩnh `fastcae_core`. Ví dụ `examples/cpp/beam_sweep.cpp` (bật mặc định, mục tiêu
`fastcae_example_beam_sweep`):

```cpp
#include "app/case_runner.hpp"
#include "solver/frame.hpp"
...
CaseFile cf; cf.parse(caseText);
RunReport rep = fastcae::runCase(cf, std::cout, "", /*writeOutput=*/false);
double u = rep.values.at("max_displacement");
```

Dùng ngoài dự án: `add_subdirectory(FastCAE)` rồi `target_link_libraries(app PRIVATE fastcae_core)`, hoặc cài SDK:
`cmake --install build --component sdk --prefix <thư mục>` (thư viện + header). API C++ ở phiên bản 0.x, chưa ổn định.
Giấy phép: MIT cho FastCAE và Lua; xem `docs/LICENSES.md` nếu bạn liên kết thêm Gmsh (GPL) hoặc PETSc.


## Python, C++ và solver mã nguồn mở bên ngoài (từ 0.3.3)

Bảng **Lập trình** có ba ngôn ngữ (chọn ở góc trái thanh công cụ): **Lua** (nhúng sẵn), **Python** và **C++**.
Cả ba đều gọi được Code_Aster, OpenRadioss/OpenCourant, SU2, OpenFOAM, CalculiX, Elmer và chương trình bất kỳ.
FastCAE chạy các solver này như **tiến trình riêng** (ghi input → chạy → đọc kết quả), nên giấy phép của từng solver được giữ nguyên
và FastCAE vẫn là MIT. Đặt đường dẫn solver ở **Công cụ ▸ Quản lý solver** hoặc biến môi trường
(`FASTCAE_ASTER_BIN`, `FASTCAE_RADIOSS_BIN`, `FASTCAE_SU2_BIN`, `FASTCAE_OPENFOAM_BIN`, `FASTCAE_CCX`, `FASTCAE_ELMER_BIN`).

### Python
Cần Python 3 trên máy (đặt `FASTCAE_PYTHON` để chọn bản khác, ví dụ môi trường conda có Code_Aster). Module `python/fastcae.py` chỉ dùng thư viện chuẩn.

```python
import fastcae
for s in fastcae.solvers(): print(s["id"], s["installed"], s["path"])

r = fastcae.code_aster(r"D:\ca\model.export")                         # Code_Aster
r = fastcae.openradioss(r"D:\rad\model_0000.rad")                      # starter rồi engine
r = fastcae.su2(r"D:\su2\case.cfg", args="-n 4")                       # SU2_CFD
r = fastcae.openfoam(r"D:\foam\cav", ["blockMesh", "icoFoam", "foamToVTK"])
print(r.ok, r.find(".vtk", ".frd", ".rmed", ".csv"))                    # file kết quả mới tạo
```

Vì script chạy trong Python thật, mọi thư viện Python (numpy, scipy, meshio, pyvista, `code_aster`, `fenics`...) dùng như thường lệ.

### C++
Cần trình biên dịch C++20 (g++/clang++ — MinGW-w64 WinLibs trên Windows — hoặc `cl`) và FastCAE SDK
(`cmake --install build --component sdk`, hoặc đặt `FASTCAE_SDK`). Script **không cần `main()`**: phần mã được bọc tự động,
đã có `#include "sdk/fastcae_all.hpp"` và `using namespace fastcae`.

```cpp
fc::solver("code_aster", "D:/ca/model.export");
fc::solver("su2", "D:/su2/case.cfg", "-n 4");
fc::solver("openfoam", "D:/foam/cav", "", {"blockMesh", "icoFoam"});
auto r = fc::run(caseText);                 // bài toán nội tại, r.values / r.series / r.mesh
fc::shell("gmsh -3 a.geo");                 // lệnh bất kỳ
```

Có `main()` thì viết chương trình C++ bình thường và dùng mọi header của FastCAE. Thư viện C++ khác (MFEM, deal.II, Eigen...): thêm
`#include` và đặt cờ qua biến `FASTCAE_CXX_FLAGS` (ví dụ `-I... -L... -lmfem`).

### Dòng lệnh
`fastcae-cli solvers --json` · `fastcae-cli solver-run <id> <input> [--dir D] [--args "..."] [--cmd lenh]...` · `fastcae-cli cxx file.cpp`

### Giới hạn đã biết
- Code_Aster, OpenRadioss mới kiểm chứng đường gọi bằng chương trình giả; SU2 đã chạy với bản thật v8.0.1 (xem SOLVERS.md) (CalculiX và OpenFOAM đã chạy thật, xem SOLVERS.md).
- OpenFOAM cần `bash` (Linux, WSL hoặc MSYS); Code_Aster trên Windows thường dùng qua Salome-Meca/WSL — đặt `FASTCAE_ASTER_BIN` trỏ tới lệnh bao (wrapper) chạy được.
- Nút **Dừng** dừng tiến trình Python/`fastcae-cli`; solver con do nó khởi động có thể cần tắt thủ công.
- Chưa có hàm `fastcae.show()` để đẩy kết quả Python lên khung 3D; hiện mở file `.vtk`/`.frd` kết quả bằng **Tệp ▸ Mở** hoặc Lua `fastcae.show`.

### Hiển thị kết quả lên khung 3D: `fastcae.show()` (Python) và **Tệp ▸ Mở kết quả (.vtk)**
```python
r = fastcae.openfoam(r"D:\foam\pitzDaily", ["blockMesh", "simpleFoam", "foamToVTK -ascii"])
fastcae.show(r)                       # lấy file .vtk mới nhất do solver sinh ra
fastcae.show(r"D:\ket_qua\case.vtk")   # hoặc truyền thẳng đường dẫn
```
- Đọc **VTK legacy ASCII, UNSTRUCTURED_GRID**: tứ diện (cả bậc hai), lục diện, lăng trụ, chóp được tách thành tứ diện; ô tam giác/tứ giác/đường bị bỏ qua và báo số lượng.
- Trường theo nút hiện trong hộp **Trường**; trường vectơ thêm `<tên>_magnitude`; trường theo ô được lấy trung bình về nút.
- Không đọc được VTK nhị phân: với OpenFOAM dùng `foamToVTK -ascii`.
- `.frd` (CalculiX), `.rmed` (Code_Aster), `.rad`/animation (OpenRadioss), SU2 `.vtu`: **chưa đọc trực tiếp**; chuyển sang `.vtk` bằng `ccx2paraview`, `meshio` hoặc ParaView rồi gọi `fastcae.show`.
- Ngoài giao diện (chạy `python` thường), `show` chỉ in một dòng đánh dấu `@@FASTCAE_SHOW@@<đường dẫn>`.


## Thuật toán tự lập trình và solver ngoài theo hợp đồng mô hình

Python: `fastcae.model(case)` → `fastcae.model_arrays(m)` → thuật toán của bạn → `fastcae.write_vtk(...)` → `fastcae.show(...)`.
Đóng gói thành solver dùng bằng `solver = <id>`: file `*.fsolver`. Chi tiết và ví dụ: [ARCHITECTURE.md](ARCHITECTURE.md), `examples/solver_plugin/`.
