# Tích hợp solver mã nguồn mở

Nguyên tắc: FastCAE là lớp **mô hình – lưới – giao diện – điều phối**; các solver trưởng thành chạy như **tiến trình ngoài** (ghi file đầu vào → chạy → đọc kết quả). Cách này giữ FastCAE ở giấy phép MIT dù solver là GPL/AGPL, và cho phép người dùng tự cài/thay phiên bản solver.
(Đây là nhận định kỹ thuật, không phải tư vấn pháp lý. **Gói kèm sẵn solver GPL/AGPL trong bộ cài** thì phải tuân điều khoản phân phối của chúng — kiểm tra trước khi phát hành.)

## Trạng thái

| Solver | Việc | Trạng thái |
|---|---|---|
| **CalculiX** (`ccx`) | kết cấu tuyến tính/phi tuyến, nhiệt, modal | ✅ `solver = calculix`: ghi `.inp` (C3D4), chạy `ccx`, đọc `.frd` (chuyển vị, ứng suất). Đã test ghi deck + đọc `.frd` mẫu; **đã chạy với ccx 2.21 thật**: dầm console T10 cho |u|max = 1,8917e-4 m, khớp solver nội bộ và lệch 0,7% so với lý thuyết dầm; T4 cứng quá mức như dự kiến |
| **OpenFOAM** | CFD 3D, rối, đa pha, truyền nhiệt | ⚙️ có sẵn: ghi case khoang kín icoFoam (nút OpenFOAM trong thẻ CFD), chạy blockMesh/icoFoam/foamToVTK; **chưa thử với OpenFOAM thật**. Thiết kế mở rộng: ghi `polyMesh` từ lưới Gmsh (`gmshToFoam`/ghi trực tiếp), `system/`, `constant/`, `0/`; chạy `simpleFoam`/`pimpleFoam`…; đọc kết quả bằng `foamToVTK` |
| **OpenRadioss / OpenCourant** | va chạm, động lực học hiện tại tường minh | 📝 thiết kế: ghi `_0000.rad` + `_0001.rad`, chạy `starter` rồi `engine`, đọc `A001` → VTK (`anim_to_vtk`) |

### CalculiX mở rộng: `analysis = ...` (đã kiểm chứng với ccx 2.21 thật)

Thêm `analysis` vào case `type = elastic` / `thermal` cùng `solver = calculix`. `fastcae-cli script scripts/08_ccx_kiem_chung.lua` chạy lại toàn bộ kiểm chứng.

| `analysis` | Nội dung | Khóa thêm | Kiểm chứng (độ lệch) |
|---|---|---|---|
| `static` (mặc định) | tuyến tính tĩnh | — | (bản cơ bản) |
| `frequency` | dao động riêng | `modes`, `mode` (dạng hiển thị) | f1 dầm công xôn 0,07% so với Euler-Bernoulli |
| `buckle` | mất ổn định tuyến tính | `modes`; tải khai báo là tải tham chiếu, tải tới hạn = hệ số × tải | Pcr cột Euler 0,43% |
| `nlstatic` | biến dạng lớn (NLGEOM) | `increments` | elastica PL²/EI = 1: 0,64% |
| `plastic` | dẻo, hóa bền tuyến tính | `yield`, `hardening` (H, Pa), `nlgeom` | kéo thanh: giãn dài 2,5% |
| `dynamic` | động lực học ngầm (HHT mặc định của ccx) | `time`, `increments` (bước cố định), `gravity` | hệ số khuếch đại tải bậc thang 1,96 (lý thuyết 2) |
| `thermal_stress` | ứng suất nhiệt | `alpha`, `t0`, `temp = <tập> T` | giãn nở tự do 1,0% |
| `heat` | dẫn nhiệt dừng (thermal) | `temp`, `heat` | — |
| `heat_transient` | dẫn nhiệt quá độ (thermal) | `time`, `increments`, `t0`, `cp`, `rho` | giữa thanh 2,85% so với chuỗi giải tích |

Khóa chung: `alpha` (giãn nở nhiệt), `cp`, `gravity = gx gy gz`, `nlgeom = on`, `job_name`, `work_dir`, `ccx_path`. Kết quả: `values.frequency_k_hz`, `series.frequencies_hz`, `series.buckling_factors`, `series.max_displacement_history`, trường `displacement`, `von_mises_nodal`, `temperature`, `plastic_strain`.
Lưu ý: với buckling, CalculiX lấy tải tham chiếu từ chính bước `*BUCKLE` nên FastCAE lặp lại tải ở đó. Chưa hỗ trợ: ma sát, vật liệu dị hướng/siêu đàn hồi, giảm chấn Rayleigh qua case (xem ROADMAP).

**Tải áp suất theo mặt:** `pressure = <tập nút> p` (Pa, **dương = nén vào mặt**), lặp được; mặt = mặt biên có cả ba đỉnh trong tập. Kiểm chứng: thanh nén u = pL/E, lệch 0,09%. Chỉ với `solver = calculix`.

**Tiếp xúc không ma sát:** `contact = <tập slave> <tập master> [stiffness K] [adjust d]` (analysis tự là `contact`). Nhiều vật thể: `body = box lx ly lz at ox oy oz [div nx ny nz]` (không chung nút), chọn mặt riêng từng vật thể bằng `nodeset = ten plane nx ny nz d tol body k` (k = 1 cho `geometry`, 2, 3… cho các `body`). Kiểm chứng: hai khối chồng nhau, chuyển vị đỉnh lệch 0,51%; khe hở 1 mm đóng bằng `adjust = 0.002`: 0,41%.
**Áp suất tiếp xúc (mới):** với `analysis = contact`, FastCAE thêm `*CONTACT FILE` (CDIS, CSTR) và đọc về trường theo nút `contact_pressure` (CPRESS, Pa), `contact_shear` (|CSHEAR1, CSHEAR2|, Pa) và giá trị `values.max_contact_pressure`; khác 0 chỉ trên nút slave đang chạm. Kiểm chứng: hai khối chồng nhau chịu F, p_max = 9,99988e6 so với F/A = 1e7 Pa (scripts/08). Mới kiểm tra giá trị cực đại, chưa kiểm phân bố (hiệu ứng mép, tiếp xúc Hertz).
Giới hạn đã biết (đã gặp khi thử): (1) tiếp xúc không ma sát không giữ được khối theo phương ngang ⇒ **bắt buộc có điều kiện đối xứng/giữ ngang** (vd. `fix = xmin x`, `fix = ymin y`), nếu không ma trận suy biến; (2) **khe hở lớn tự đóng dưới tải không chạy được** (khối thả nổi, ccx không hội tụ): đặt hai vật thể chạm nhau hoặc dùng `adjust` ≥ khe hở, lưu ý `adjust` dời nút slave lên mặt master (đổi hình học); (3) ma sát bị từ chối với thông báo; (4) độ cứng phạt mặc định 1e3·E/kích thước lớn nhất, chỉnh bằng `stiffness`; (5) áp suất tiếp xúc: xem dưới.

### CFD 3D bằng OpenFOAM: `type = cfd3d` (đã chạy thật với OpenFOAM v1912)

```
type = cfd3d
solver = openfoam
geometry = box 10 1 0.5          # m
divisions = 100 20 2
nu_kin = 0.1                     # độ nhớt động học m2/s
inlet_velocity = 1 0 0
bc_xmin = inlet                  # inlet | outlet | wall | symmetry | slip  (mặc định xmin inlet, xmax outlet, còn lại wall)
bc_xmax = outlet
bc_zmin = symmetry
bc_zmax = symmetry
model = laminar                  # laminar | kepsilon | komegasst
transient = off                  # on: pimpleFoam (end_time, dt); off: simpleFoam (max_iter)
```
**CFD từ CAD** (`geometry = file mo_hinh.step`, `mesh_size`, `unit_scale`): FastCAE nhập CAD qua Gmsh rồi ghi **polyMesh trực tiếp từ lưới tứ diện** (không dùng `gmshToFoam`, vì không cần nhóm vật lý trong file .msh). Sáu mặt phẳng của hộp bao (`xmin … zmax`) được nhận diện theo hình học và gán bằng `bc_xmin … bc_zmax`; mọi mặt biên còn lại thuộc patch `walls` (`bc_other = wall | symmetry | slip`). Ví dụ: `examples/cad/pipe_d1_l10.step`. `checkMesh` được chạy để chẩn đoán (log.checkMesh).
**Sai số áp suất ~15% trên lưới tứ diện: đã tìm ra nguyên nhân và sửa (đo ngày 26/10/2026, OpenFOAM v1912).** Trước đây độ sụt áp tầng lưu trên lưới tứ diện cao hơn lý thuyết 14–16% (ống D = 1: 1,86 so với Hagen–Poiseuille 1,6; kênh phẳng: 1,365 so với 1,2) và không giảm khi làm mịn lưới. Các bước loại trừ:

| Giả thuyết | Cách thử | Kết quả |
|---|---|---|
| Bộ ghi polyMesh của FastCAE sai | cùng hình học: Gmsh → `gmshToFoam` chuẩn của OpenFOAM | 1,862 (vẫn lệch) ⇒ không phải lỗi bộ ghi |
| Đo sai (nội suy ra nút) | tính lại từ giá trị tâm ô, có trọng số thể tích | 1,859 ⇒ không phải lỗi đo |
| Số hạng `dev2(T(grad U))` hoặc thuật toán SIMPLE | `icoFoam` quá độ, cùng lưới | 1,843 ⇒ không phải |
| Thuật toán/chất lượng lưới Gmsh | Delaunay, HXT, tối ưu Netgen | 1,864 / 1,862 ⇒ không đổi |
| Hiệu chỉnh phi trực giao, lược đồ đối lưu | `nNonOrthogonalCorrectors` 2→6, `uncorrected`, `linear` thay `linearUpwind` | 1,859 / 2,038 / 1,846 ⇒ không sửa được |
| Độ mịn lưới | 2,7 nghìn → 72 nghìn ô (h = 0,25 → 0,08) | 1,82 / 1,85 / 1,86 / 1,85 ⇒ **không hội tụ** (lỗi bậc 0) |
| **Gradient `Gauss linear` không nhất quán trên lưới lệch** | `gradSchemes default pointCellsLeastSquares` | **1,595 (lệch −1,5%)** |

Cơ chế: gradient Gauss nội suy tuyến tính giá trị ra mặt tại giao điểm đường nối hai tâm ô với mặt, không phải tại tâm mặt; trên tứ diện (skewness tối đa ~0,75–0,8) sai lệch này không nhỏ đi khi làm mịn, nên gradient áp suất và số hạng hiệu chỉnh phi trực giao của Laplace đều sai ở bậc O(1). Gradient bình phương tối thiểu theo nút (`pointCellsLeastSquares`) chính xác cho trường tuyến tính trên mọi lưới. (`leastSquares` chỉ dùng ô lân cận theo mặt thì **không** đủ: 1,99 cho grad(p); `skewCorrected` ở nội suy làm phân kỳ.)

Kết quả sau khi sửa (mặc định mới cho lưới CAD, `gradSchemes default pointCellsLeastSquares`):

| h (m) | số ô | dp/dx (lý thuyết 1,6) |
|---|---|---|
| 0,25 | 2 752 | 1,676 (+4,7%) |
| 0,18 | 6 842 | 1,632 (+2,0%) |
| 0,12 | 22 030 | 1,595 (−0,3%) |
| 0,08 | 72 097 | 1,581 (−1,2%) |

Qua đường FastCAE đầy đủ: ống CAD dp/dx lệch 0,26%, u_max lệch 0,33% (trước 2,9%); kênh phẳng tứ diện 1,183 so với 1,2 (−1,4%). Lưới lục diện (blockMesh) vẫn dùng `Gauss linear` (đã đạt 0,5%). Có thể ép bằng `grad_scheme = <tên lược đồ>` trong case. **Chưa kiểm chứng:** OpenFOAM Foundation (có thể không có `pointCellsLeastSquares`; khi đó đặt `grad_scheme = leastSquares` hoặc `Gauss linear` và chấp nhận sai số lớn hơn), snappyHexMesh, lớp biên lăng trụ thật (`BoundaryLayer` field của Gmsh 4.12 không sinh lăng trụ trên hình học OpenCASCADE khi thử), và ca rối trên tứ diện chỉ mới thử sơ bộ: kênh phẳng k-epsilon Re = 1e5 lệch Dean +20,5% (h = 0,15) → +10,3% (h = 0,10), giảm khi làm mịn; với `Gauss linear` cả hai ca đều phân kỳ (FPE).

Kết quả: `speed`, `pressure`, `velocity` (+ `k`, `nut` khi rối), `values.max_speed`, `p_min`, `p_max`, `mass_imbalance`. Kiểm chứng: `scripts/09_openfoam_kiem_chung.lua`. **Chưa có**: lớp biên lăng trụ (không còn cần để có áp suất tầng lưu đúng trên lưới tứ diện, nhưng vẫn cần cho rối sát tường), nhiều vật thể, snappyHexMesh, nhiệt–dòng, đa pha, FSI; k-ω SST mới chỉ ghi case. Các bản OpenFOAM khác v1912 (Foundation, ESI ≥ v2012) chưa thử: FastCAE ghi cả `turbulenceProperties`/`momentumTransport` và `transportProperties`/`physicalProperties` nhưng chưa kiểm chứng.

### Dùng CalculiX

```
type = elastic
solver = calculix
# ccx_path = C:\calculix\ccx.exe     (hoặc đặt FASTCAE_CCX, hoặc để ccx trong PATH)
job_name = dam_console
work_dir = runs
geometry = box 1.0 0.1 0.1
...
```
Nếu không có `ccx`, FastCAE vẫn ghi file `.inp` và báo đường dẫn để bạn chạy tay.

## OpenRadioss và "OpenCourant"

Theo [It's FOSS](https://itsfoss.com/news/siemens-openradioss-fork/) (5/10/2026): Siemens (đã mua Altair năm 2025) gỡ kho OpenRadioss khỏi GitHub vào đầu tháng 10/2026; **OpenCourant** là nhánh cộng đồng do Brian Clemens (đồng sáng lập Rocky Linux) tạo, giữ toàn bộ lịch sử commit, giấy phép **AGPLv3**. Xem thêm [Phoronix](https://phoronix.com/news/openradioss-opencourant). Dự án còn rất mới, nên: (1) tự lưu bản sao nguồn/binary bạn dùng, (2) kiểm tra lại tình trạng, cấu trúc input và giấy phép trước khi tích hợp sâu. AGPL còn ràng buộc cả khi cung cấp qua mạng — cân nhắc nếu sau này làm dịch vụ đám mây.

## Các dự án uy tín khác nên biết

(Giấy phép đã đối chiếu lại ngày 24/10/2026 cho các mục có trong bảng docs/LICENSES.md; các dự án khác ghi theo hiểu biết, hãy kiểm tra trước khi dùng.)

**FEM / đa vật lý**
- **Code_Aster** (EDF, GPL-3.0+) — kết cấu công nghiệp rất mạnh (phi tuyến, mỏi, hạt nhân); đi kèm **Salome-Meca**.
- **Elmer FEM** (CSC, GPL-2.0+; thư viện ElmerSolver LGPL-2.1+) — đa vật lý: nhiệt, điện từ, dòng chảy, kết cấu, ghép trường.
- **FEniCSx / DOLFINx** (LGPL) — sinh solver từ dạng yếu (UFL), dùng nghiên cứu/PDE tuỳ biến.
- **deal.II** (LGPL-2.1+; một phần mã song song Apache-2.0 + ngoại lệ LLVM) — thư viện FEM C++ thích ứng lưới, song song quy mô lớn.
- **MFEM** (BSD, LLNL) — FEM bậc cao, GPU, rất hợp để nhúng vào C++ như FastCAE.
- **MOOSE** (LGPL, INL) — framework đa vật lý.
- **Kratos Multiphysics** (BSD) — kết cấu, CFD, ghép FSI, tối ưu hình học.
- **NGSolve / Netgen** (LGPL) — FEM + chia lưới.

**CFD**
- **OpenFOAM** (GPL; hai nhánh: openfoam.org của Foundation và openfoam.com của ESI) — bản tham chiếu cộng đồng.
- **SU2** (LGPL) — CFD nén được, tối ưu hình dạng bằng adjoint.
- **Code_Saturne** (EDF, GPL-2.0+), **Palabos** (lattice-Boltzmann), **DualSPHysics** (SPH, mặt thoáng).

**Động lực học đa vật thể / va chạm**: **Project Chrono** (BSD), **MBDyn**.

**Hạ tầng**
- **Gmsh** (GPL, đã dùng cho lưới + CAD), **OpenCASCADE** (LGPL + ngoại lệ), **FreeCAD** (LGPL).
- **PETSc / Trilinos / HYPRE** (BSD) — bộ giải tuyến tính song song, tiền điều kiện đa lưới (thay CG-Jacobi hiện tại khi mô hình lớn).
- **ParaView / VTK** (BSD) — không còn là phụ thuộc; FastCAE tự hiển thị bằng Qt/OpenGL, vẫn xuất `.vtk` để mở bằng ParaView khi cần.
- **preCICE** (LGPL) — ghép nối solver (FSI, nhiệt–cơ giữa OpenFOAM và CalculiX).
- **Salome** (LGPL) — nền tảng tiền/hậu xử lý, mô-đun hoá.

## Thứ tự đề xuất

1. **CalculiX** (đã có, cần thử với ccx thật) → hoàn thiện nhiệt, modal, tiếp xúc, phần tử bậc 2 (C3D10).
2. **OpenFOAM** — giá trị lớn nhất để có CFD 3D thật.
3. **Code_Aster / Elmer** làm lựa chọn thứ hai cho kết cấu và đa vật lý.
4. **OpenCourant/OpenRadioss** sau khi dự án ổn định.
5. **MFEM** hoặc **PETSc** nếu muốn nâng cấp solver nội bộ thay vì gọi ngoài.


## Giấy phép

Bảng giấy phép và mức ràng buộc từng solver: [LICENSES.md](LICENSES.md). Giao diện: Công cụ ▸ Quản lý solver.


## Cập nhật tích hợp (kiểm chứng với phần mềm thật, Linux)

| Thành phần | Kết quả kiểm chứng |
|---|---|
| OpenFOAM v1912 (ESI) | Case do FastCAE ghi chạy được với `blockMesh` + `icoFoam` (sau khi nạp `etc/bashrc`). Re = 100, lưới 41x41: u tại tâm = -0,20584, Ghia 1982 = -0,20581 (lệch 0,015%). Kết quả được đọc về khung 3D. Lưới 20x20 cho -0,194 (lệch 5,6%, lưới thô). |
| CalculiX 2.21 | Dầm console T10: |u|max = 1,8917e-4 m, trùng solver nội bộ, lệch 0,7% so với lý thuyết dầm. |
| Gmsh 4.12.1 | Nhập STEP bằng thư viện nhúng và bằng `gmsh` ngoài cho kết quả y hệt. Tấm có lỗ: von Mises max 42,3 MPa (lưới 3 mm) -> 45,7 MPa (lưới 1,5 mm), lý thuyết Kt = 2,23 cho 46,5 MPa. |
| PETSc 3.19.6 | Cùng nghiệm với CG tự viết; số vòng lặp 446 so với 1623 trên tấm có lỗ T10 (23 nghìn nút). |

| CalculiX 2.21, phần tử vỏ S4 | Tấm tựa đơn/ngàm 64x64 lệch +0,5% so với Kirchhoff; bồn trụ giữa chiều cao lệch +0,2% so với pR/t. Xem docs/STRUCTURAL_LIBRARY.md. |

**SU2 v8.0.1 (bản nhị phân chính thức) đã chạy thật**: dòng Euler nén được Mach 0,3 qua trụ tròn 2D (`examples/su2/`; lưới tạo bằng `make_mesh.py`,
5028 vòng lặp, dư lượng mật độ giảm tới −8,0). Gọi qua `fastcae-cli solver-run su2 cylinder.cfg` (đặt `OUTPUT_FILES= (PARAVIEW_ASCII)` để có `flow.vtk`), nhập bằng
`fastcae-cli result import flow.vtk ket_qua.fcres` (tự nạp `history.csv` cạnh đó) hoặc mở thẳng `flow.vtk` trong GUI: hiện đủ Density, Pressure, Mach, Temperature,
Pressure_Coefficient, Velocity... Số liệu hội tụ: Mach lớn nhất 0,671 ở hai vai trụ; Cp tại điểm dừng 1,035 (đẳng entropy 1,023, lệch 1,2%);
Cp nhỏ nhất −3,43, thấp hơn công thức Kármán–Tsien (−3,68) khoảng 7%: **đây là đối chiếu gần đúng chứ không phải kiểm chứng chặt** (lưới thô, miền chỉ 10 đường kính, 10% cản dòng).
Lưu ý: cấu hình mặc định bộ giới hạn Venkatakrishnan làm dư lượng đứng yên ở −2,3 (không hội tụ); ví dụ dùng `SLOPE_LIMITER_FLOW= NONE` và `CFL_ADAPT`.
Kết quả 2D (tam giác/tứ giác phẳng) được đùn thành lớp mỏng dày 0,4% kích thước để hiển thị trên khung 3D; trường nút được giữ nguyên cho cả hai lớp.

**Kết quả không dừng**: SU2 `TIME_DOMAIN= YES` + `OUTPUT_WRT_FREQ` sinh `flow_NNNNN.vtk`; mở một file bất kỳ của chuỗi là GUI nạp cả chuỗi (cùng lưới) với thanh trượt thời gian. Đã chạy thật 11 khung (khởi động đột ngột dòng Mach 0,3 quanh trụ: sóng áp suất lan thành vòng).

**CalculiX 2.21: dao động riêng và động lực chạy thật với dầm console 1 x 0,1 x 0,1 m** (`tests/ccx_validate.py`): f1 = 83,456 Hz so với 83,55 Hz của lý thuyết dầm (lệch 0,11%); dạng uốn bậc 2 thấp hơn Euler–Bernoulli 4,4% (hiệu chỉnh cắt của dầm dày). Động lực, tải bước nhảy: đỉnh 3,72e-4 m so với 2 lần độ võng tĩnh 3,81e-4 m (lệch 2,4%), chu kỳ 12 ms ≈ 83 Hz. Bước động lực xuất cả ứng suất (`*EL FILE S`): von Mises theo từng khung và dãy `max_von_mises_history`; đỉnh von Mises 13,7 MPa tại ngàm (tĩnh theo lý thuyết dầm 6,0 MPa, CalculiX tĩnh 6,17 MPa; hệ số động 2,2 > 1,95 của chuyển vị vì tải điểm đột ngột kích thích cả các dạng riêng cao; ứng suất tại ngàm còn phụ thuộc lưới). **Mỗi dạng riêng và mỗi mốc thời gian là một khung** trong thanh trượt thời gian; case ghi thêm `ten_0001.vtk, ten_0002.vtk...` và lưu cả chuỗi vào `result_file`.

**Hội tụ lưới của dầm console tĩnh** (`tests/convergence_study.py`, 4 lưới từ 525 đến 27 225 nút, T10; solver nội và CalculiX thật cho cùng kết quả):

| Lưới | Chuyển vị đầu | Ứng suất giữa nhịp (lý thuyết 3,00 MPa) | max von Mises |
|---|---|---|---|
| 10×2×2 | 1,8917e-4 (−0,68%) | 3,05 | 5,83 MPa |
| 20×3×3 | 1,9052e-4 (+0,03%) | 3,01 | 6,18 |
| 40×5×5 | 1,9098e-4 (+0,26%) | 3,00 | 6,36 |
| 60×7×7 | 1,9108e-4 (+0,32%) | 3,00 | 7,01 |

Chuyển vị hội tụ về 1,911e-4 m (+0,3% so với Euler–Bernoulli 1,9048e-4; thấp hơn Timoshenko 1,92e-4 vì ngàm ngăn mặt cắt vênh/co ngang nên cứng hơn lý thuyết dầm). Ứng suất giữa nhịp hội tụ đúng lý thuyết.
**Ứng suất lớn nhất tại ngàm KHÔNG hội tụ** (5,8 → 7,0 MPa và còn tăng khi mịn hơn): đó là điểm kỳ dị ứng suất ở mép ngàm (ngàm cứng ngăn co ngang), không dùng giá trị này để kiểm bền; lấy ứng suất cách ngàm vài lần chiều cao mặt cắt hoặc mô hình hóa ngàm thực.
**Lỗi đã sửa**: bộ khôi phục ứng suất T10 của solver nội bỏ qua nút giữa cạnh (von Mises = 0 ở các nút này trong VTK/CSV/.fcres, thang màu luôn bắt đầu từ 0, trung bình mặt thấp hơn lý thuyết 39–81%); nay tính tại trung điểm cạnh. Test `stress_midnodes`.

**Hệ số động của dầm console (tải bước nhảy 1 kN ở đầu dầm, CalculiX thật; lý thuyết không giảm chấn = 2,0)**, đo ở giữa nhịp (ứng suất hội tụ):

| Bước thời gian | 2 ms | 1 ms | 0,5 ms | 0,25 ms |
|---|---|---|---|---|
| Hệ số động ứng suất | 1,77 | 1,84 | 2,01 | 2,00 |
| Hệ số động chuyển vị đầu | 1,92 | 1,95 | 1,99 | 1,99 |

(lưới 20×3×3; theo lưới 10×2×2 / 20×3×3 / 40×5×5 ở bước 1 ms: 1,87 / 1,84 / 1,84). **Bước thời gian quá thô làm giảm đỉnh đáp ứng** (tích phân ẩn có giảm chấn số): chu kỳ cơ bản ở đây 12 ms, cần ≥ ~25 bước mỗi chu kỳ để sai số đỉnh dưới 1%, và ≥ 50 bước nếu quan tâm các dạng riêng cao.
**FastCAE tự kiểm tra**: trước khi chạy `analysis = dynamic` bằng CalculiX, chạy nhanh một phân tích tần số (1 dạng), ghi `fundamental_frequency_hz` và `steps_per_period` vào kết quả, và in `CANH BAO` kèm số `increments` gợi ý nếu dưới 20 bước mỗi chu kỳ cơ bản (tắt bằng `check_timestep = no`). Chỉ kiểm tra dạng cơ bản; các dạng riêng cao hơn cần bước nhỏ hơn.
Hệ số 2,2–2,3 báo trước đó lấy từ ứng suất tại ngàm (điểm kỳ dị) nên không dùng. Script: `tests/dynamic_time_convergence.py`; xuất từng khung: `fastcae-cli result csv x.fcres ra.csv --frame N`.

Chưa thử với bản thật: OpenRadioss/OpenCourant, Code_Aster, Elmer, Code_Saturne, preCICE, Chrono, FEniCSx, Kratos.
