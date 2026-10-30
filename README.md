# FastCAE

**Phần mềm CAE mã nguồn mở** — phân tích kết cấu (FEM), truyền nhiệt, CFD, nhập CAD (STEP/IGES/BREP/XAO/STL + lưới MSH/INP/UNV/BDF/MED/MESH), giao diện đồ họa **Qt6 + OpenGL tự viết (không cần ParaView/VTK)**.

Tác giả: **Trần Minh Tâm** · Giấy phép: MIT (xem `LICENSE`, `THIRD_PARTY_NOTICES.txt`) · Ngôn ngữ: C++20

## Định hướng

FastCAE là nơi **dựng mô hình** (CAD → lưới → liên kết → tải), nơi **lập trình thuật toán CAE** (FEM, CFD...) và **gọi thư viện/solver mã nguồn mở bên ngoài**, rồi **hiển thị kết quả** trên GUI. Thư viện CAE không được nhúng vào FastCAE. Xem [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) và ví dụ FEM tự viết bằng Python trong `examples/solver_plugin/`:

```bash
export FASTCAE_SOLVER_PLUGINS=$PWD/examples/solver_plugin
fastcae-cli run examples/solver_plugin/cantilever_myfem.fcae      # solver = my_fem (Python), kết quả hiện được trên GUI
fastcae-cli model examples/cantilever.fcae dam.fcmodel.json       # chỉ dựng mô hình, cho thuật toán/solver của bạn
```

## Hiện đã có (v0.1)

| Mô-đun | Trạng thái | Chi tiết |
|---|---|---|
| Lõi lưới + I/O | ✅ chạy & kiểm chứng | lưới tứ diện, tập nút theo tên (`xmin`, `xmax`…), xuất `.vtk` (tuỳ chọn, để mở bằng phần mềm khác) |
| FEM đàn hồi tuyến tính | ✅ | tứ diện 4 nút (T4) và **bậc 2 10 nút (T10, `order = 2`)**; von Mises; CG-Jacobi song song OpenMP |
| Tải trọng | ✅ | quy đổi theo diện tích phụ trách (nhất quán, T4 và T10) |
| Truyền nhiệt dừng | ✅ | T4/T10, nhiệt độ cưỡng bức, nguồn nhiệt |
| CFD 2D | ✅ | khoang kín nắp trượt, so với Ghia 1982 |
| Nhập CAD | ✅ (STEP/IGES/BREP đã thử với Gmsh) | 3 đường: thư viện Gmsh (OpenCASCADE) · chương trình `gmsh` ngoài · đọc thẳng `.msh` (2.2/4.1) |
| Giao diện Qt6 + OpenGL | ✅ đã chạy thử (ảnh chụp headless) | xoay/dịch/zoom, tô màu (bảng màu turbo + thanh màu), biến dạng phóng đại, hiện lưới, các hướng nhìn chuẩn, bấm chuột để đọc giá trị, cây mô hình, trình soạn case, thiết kế hình học tham số |
| Cầu nối CalculiX (mở rộng) | ✅ đã kiểm chứng với `ccx` 2.21 thật: dao động riêng, mất ổn định Euler, biến dạng lớn, dẻo, động lực ngầm, nhiệt quá độ, ứng suất nhiệt (`analysis = ...`, [docs/SOLVERS.md](docs/SOLVERS.md)); ✅ tải áp suất theo mặt và tiếp xúc không ma sát nhiều vật thể (có giới hạn, xem SOLVERS.md); ✅ xuất áp suất tiếp xúc (`contact_pressure`); ⚙️ chưa có ma sát, khe hở lớn tự đóng, vỏ/dầm/tấm phi tuyến | `scripts/08_ccx_kiem_chung.lua` |
| Cầu nối CalculiX (cơ bản) | ✅ ghi deck/đọc `.frd` có test; ⚙️ chưa thử với ccx thật | `solver = calculix` ([docs/SOLVERS.md](docs/SOLVERS.md)) |
| Giao diện kiểu SolidWorks/FreeCAD | ✅ đã chạy thử (ảnh chụp headless) | Thanh lệnh ribbon theo thẻ (Tệp, Lưới, Kết cấu, Nhiệt, CFD, Ghép nối, Kết quả, Công cụ), cây mô hình, bảng bài toán/kết quả, nhật ký + đầu ra solver ngoài; **giao diện sáng và tối** (Công cụ ▸ Giao diện, hoặc `--theme light`) |
| CFD 3D (OpenFOAM) | ✅ đã chạy thật với OpenFOAM v1912: kênh tầng lưu (u_max 0,5%, dp/dx 0,5% so với Poiseuille) và rối k-epsilon (4,5% so với Dean); simpleFoam/pimpleFoam; ⚙️ k-ω SST đã ghi case nhưng chưa chạy kiểm chứng | `type = cfd3d`, `scripts/09_openfoam_kiem_chung.lua` |
| Solver mã nguồn mở | ✅ CalculiX (ccx 2.21), OpenFOAM (v1912), Gmsh (4.12) và PETSc (3.19) đã chạy thật, xem docs/SOLVERS.md; ✅ SU2 v8.0.1 đã chạy thật (CFD 2D nén được qua trụ, hội tụ −8; nhập kết quả `.vtk` 2D và `history.csv`; đối chiếu lý thuyết chỉ gần đúng ~1–7%); ⚙️ OpenRadioss/Code_Aster/Elmer/Code_Saturne/preCICE/Chrono/FEniCSx/Kratos chưa thử với bản thật | Bảng **Công cụ ▸ Quản lý solver**: giấy phép, trạng thái cài, đặt đường dẫn, chạy; `fastcae-cli solvers`; xem [docs/LICENSES.md](docs/LICENSES.md) |
| Thư viện cấu kiện kết cấu | ✅ đã kiểm chứng với lời giải giải tích và CalculiX | Thẻ **Cấu kiện**: dầm I/U/L/T, ống, tấm, tường, móng, tường chắn (khối đặc); **khung/dàn 1D** (dầm, thanh dàn, lò xo, cáp, bản lề, gối đàn hồi; phân tích tĩnh, **dao động riêng**, **mất ổn định Euler**) với 7 mẫu và nút dựng; **tấm/vỏ** bằng CalculiX; xem [docs/STRUCTURAL_LIBRARY.md](docs/STRUCTURAL_LIBRARY.md) |
| Lập trình tùy biến | ✅ đã kiểm chứng (12 test tự động); ⚙️ Python/C++ gọi Code_Aster/OpenRadioss mới thử bằng chương trình giả (SU2 đã chạy thật) | Bảng **Lập trình (Lua, Python, C++)** — Python/C++ gọi được solver ngoài (docs/SCRIPTING.md); Lua nhúng sẵn kèm 8 script mẫu (quét tham số, tối ưu, tổ hợp tải...), module **Python** `python/fastcae.py`, thư viện **C++** `fastcae_core`; xem [docs/SCRIPTING.md](docs/SCRIPTING.md) |
| Bộ giải tuyến tính | ✅ | Tự chọn Cholesky trực tiếp hoặc CG; PETSc tuỳ chọn |
| Đóng gói | ⚙️ Linux (DEB/TGZ) đã thử; Windows (Inno Setup/NSIS) chưa chạy thử | `packaging/windows/fastcae.iss`, CPack, AppImage, GitHub Actions |

Kiểm chứng: dầm console T10 sai 0,03% so với lý thuyết; tấm có lỗ từ STEP cho von Mises max 42,3 MPa (lý thuyết Kt≈2,23 → ~46,5 MPa, lưới thô); khoang kín khớp Ghia. Chi tiết: [docs/THEORY.md](docs/THEORY.md). Lộ trình: [docs/ROADMAP.md](docs/ROADMAP.md).

## Biên dịch nhanh (chỉ lõi + CLI, không cần thư viện ngoài)

```bash
cmake -S . -B build -DFASTCAE_BUILD_GUI=OFF -DFASTCAE_WITH_GMSH=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/fastcae-cli demo cantilever out
```

Bản đầy đủ (GUI + CAD) và đóng gói: [docs/BUILDING.md](docs/BUILDING.md).

## Dùng thử

Trong giao diện, nhấn **F1** (hoặc **Trợ giúp ▸ Hướng dẫn sử dụng**) để mở hướng dẫn đầy đủ từng tính năng kèm phần giới thiệu năng lực tính toán CAE; nguồn tài liệu: `docs/help/huong_dan.html`.

```bash
fastcae                                  # mở giao diện (Tệp ▸ Ví dụ, F5 để giải)
fastcae examples/plate_hole.fcae         # mở thẳng một case
fastcae --demo cantilever --scalar vonmises --warp 200 --screenshot a.png   # chụp ảnh không cần bấm
fastcae-cli demo cantilever|conduction|cavity [thư_mục]
fastcae-cli run examples/cantilever.fcae
fastcae-cli import mau.step --size 0.003 --scale 0.001 --out mau.vtk    # --size theo mét
```

## Về nhập file CAD từ nhiều phần mềm

Hộp thoại **Mở** (Ctrl+O), **Tệp ▸ Nhập CAD** và kéo-thả nhận các định dạng sau; `fastcae-cli formats` cho biết máy bạn đang dùng được đường nào.

| Nhóm | Đuôi file | Cần gì |
|---|---|---|
| Hình học B-Rep | `.step .stp` `.iges .igs` `.brep .brp` `.xao` | Gmsh (thư viện hoặc `gmsh.exe` ngoài); FastCAE tự chia lưới khối |
| Lưới bề mặt | `.stl` | Gmsh; STL phải **kín**, FastCAE dựng khối rồi chia lưới |
| Lưới khối có sẵn | `.msh` (ASCII 2.2/4.1) · `.inp` (Abaqus/CalculiX) | **không cần** Gmsh |
| Lưới khối có sẵn | `.unv` (I-deas) · `.bdf .nas` (Nastran) · `.med` · `.mesh` (Medit) · `.msh` nhị phân | Gmsh |
| FreeCAD | `.FCStd` | FreeCAD (`freecadcmd` trong PATH hoặc `FASTCAE_FREECAD`) |
| Định dạng đóng | `.sldprt .ipt .CATPart .prt .f3d .dwg .x_t`… | xem bên dưới |

Lưới có sẵn có thể gồm tứ diện (4 hoặc 10 nút), lục diện, lăng trụ, tháp (tuyến tính hoặc bậc 2); FastCAE đổi lục diện/lăng trụ/tháp thành tứ diện (liên tục giữa các phần tử) rồi giải như bình thường. Lưới chỉ có phần tử bề mặt/vỏ/dầm sẽ bị từ chối kèm giải thích.

SolidWorks (`.sldprt`), Inventor (`.ipt`), CATIA (`.CATPart`), NX/Creo (`.prt`), Fusion (`.f3d`), DWG… là định dạng đóng, không có đặc tả mở nên **không đọc trực tiếp được**. Hai cách: (1) trong phần mềm gốc chọn *Save As → STEP*; (2) nếu có bộ chuyển đổi (CAD Exchanger, Datakit, FreeCAD…), đặt biến môi trường `FASTCAE_CAD_CONVERTER` thành lệnh có `{in}` và `{out}` (luôn là file `.step`), ví dụ `mytool --in "{in}" --out "{out}"`; khi đó mở thẳng `.sldprt` sẽ tự chuyển sang STEP rồi chia lưới.

## Giới hạn cần biết

- T4 cứng quá mức khi uốn → dùng `order = 2` (mặc định khi nhập CAD qua GUI).
- CFD 3D chỉ chạy qua OpenFOAM (`type = cfd3d`, hộp/kênh dựng bằng blockMesh); chưa có CFD 3D tích hợp sẵn. Lưới từ CAD (STEP) ghi trực tiếp polyMesh tứ diện: **sai số áp suất ~15% trước đây đã được tìm ra và sửa** (gradient `Gauss linear` không nhất quán trên lưới lệch; nay mặc định `pointCellsLeastSquares`, ống D = 1 lệch 0,26%, kênh phẳng −1,4% — xem SOLVERS.md). Chưa có lớp biên lăng trụ, nhiệt–dòng (buoyant), đa pha, ghép FSI. CFD tích hợp sẵn vẫn chỉ là 2D khoang kín.
- Chưa kiểm tra: bản dựng GUI trên Windows, chạy Inno Setup, `ccx` thật.
