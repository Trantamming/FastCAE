# Nhật ký thay đổi

## 1.0.0 (chưa phát hành)

- **Hướng dẫn trong phần mềm (F1)** thêm chương 18 (quy trình dựng mô hình → solver ngoài → hiển thị, định dạng `.fcmodel.json`/`_result.vtk`/`.fsolver`) và chương 19 (4 bài thực hành); thêm mục xử lý sự cố SmartScreen và plugin Python. Bài mẫu `examples/tutorial/` (bài 1 dầm + FEM Python tự viết, bài 2 FEM trong bảng Lập trình, bài 3 CFD khoang kín tự lập trình lệch Ghia 0,9%, bài 4 nối CalculiX/OpenFOAM/SU2). Test `tutorial_examples`.
- Plugin đi kèm bản cài được tự dò (`share/fastcae/solvers.d`); `my_fem.fsolver` nhận `FASTCAE_PYTHON`.
- **Bộ cài Windows (Inno Setup)**: ghi tác giả Trần Minh Tâm + email trong bộ cài và `fastcae.exe`; hỗ trợ ký số (`-SignPfx` / `-SignCommand`, secret CI), sinh SHA-256, job CI dựng bộ cài; `docs/RELEASE_WINDOWS.md`. Chưa dựng/cài thử trên Windows; ký số cần chứng chỉ riêng của tác giả (SmartScreen không thể loại bỏ chỉ bằng mã nguồn).

- **Định hướng mới: FastCAE là nơi dựng mô hình, phần giải do thuật toán tự lập trình hoặc thư viện bên ngoài đảm nhiệm** ([docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)). Thêm: `fastcae-cli model case.fcae ra.fcmodel.json` (xuất lưới, tập nút, vật liệu, liên kết, tải dạng JSON trung lập; elastic/thermal, T4/T10); **solver khai báo bằng `*.fsolver`** (`FASTCAE_SOLVER_PLUGINS`, `solvers.d`) dùng bằng `solver = <id>` trong case — FastCAE ghi mô hình, chạy chương trình ngoài, đọc `<job>_result.vtk` và hiển thị; Python `fastcae.model()`, `model_arrays()`, `write_vtk()`; ví dụ `examples/solver_plugin/` (FEM 3D tự viết bằng numpy/scipy); test `solver_plugin_contract`. Chưa thử trên GUI Qt (không có Qt trong môi trường dựng lần này).

- **CAD tham số theo cây tính năng** (`.fcad`, `geometry = param ...`, `fastcae-cli param`): tham số + biểu thức, hộp/trụ/cầu/nón/xuyến, phác thảo có lỗ, đùn, xoay, Boolean, mảng thẳng/tròn, đối xứng, tịnh tiến/xoay; sinh script Gmsh-OpenCASCADE. Kiểm chứng 18 mô hình theo công thức (`tests/param/validate.py`). Chưa có ràng buộc phác thảo / bo góc theo cạnh: xem `docs/PARAMETRIC_CAD.md`.
- **GUI**: nút **Lưu kết quả** (thẻ Tệp/Kết quả, menu Tệp) ghi `.fcres`; **Mở** nhận `.fcres` (hiện lại đủ trường, không giải lại) và `.fcad` (tạo case `geometry = param ...` rồi giải). Đã biên dịch và chạy thử GUI Qt 6.4 trên Linux (Xvfb, OpenGL phần mềm); chưa thử trên Windows.
- **Kết quả 2D từ solver ngoài** (SU2 chạy thật): lưới tam giác/tứ giác phẳng trong file VTK trước đây bị từ chối ("không có ô thể tích"); nay đùn thành lớp mỏng để hiển thị, giữ mọi trường nút; `fastcae-cli result import x.vtk ra.fcres`. Test `vtk_2d_import`.
- **Cảnh báo bước thời gian cho phân tích động lực CalculiX**: chạy nhanh phân tích tần số để lấy chu kỳ cơ bản, ghi `steps_per_period`, cảnh báo + gợi ý `increments` nếu < 20 bước/chu kỳ; `check_timestep = no` để tắt. Test trong `ccx_validate.py`.
- **Hội tụ theo thời gian của đáp ứng động** (CalculiX thật): hệ số động ở giữa nhịp hội tụ về 2,0 khi bước ≤ 0,5 ms (chu kỳ 12 ms), bước 1 ms chỉ đạt 1,84; `fastcae-cli result csv|vtk x.fcres ra [--frame N]` xuất từng khung. Script `tests/dynamic_time_convergence.py`.
- **Sửa lỗi ứng suất phần tử bậc 2 của solver nội**: nút giữa cạnh của T10 luôn có von Mises = 0 (xuất VTK/CSV/.fcres chứa số 0 giả; thang màu bắt đầu từ 0; trung bình mặt thấp hơn lý thuyết 39–81%); nay tính tại trung điểm cạnh, khớp CalculiX và lý thuyết dầm. Thêm khảo sát hội tụ lưới `tests/convergence_study.py` và test `stress_midnodes`; ứng suất tại ngàm không hội tụ (điểm kỳ dị), ghi rõ trong `docs/SOLVERS.md`.
- **Động lực CalculiX xuất ứng suất theo thời gian**: von Mises từng khung (thang màu khóa theo cả chuỗi), `values.max_von_mises` = đỉnh theo thời gian, dãy `max_von_mises_history`; `times` không còn bị nhân ba. Đồ thị có tùy chọn chuẩn hóa 0–1 (tự bật khi các dãy chênh >1000 lần).
- **CalculiX: mỗi dạng riêng / mốc thời gian là một khung** (frequency, buckle, dynamic, heat_transient, nhiều gia số): thanh trượt thời gian trong GUI, `ten_NNNN.vtk` và `result_file` lưu cả chuỗi; đã chạy thật với ccx 2.21 (`tests/ccx_validate.py`). **Sửa lỗi**: độ phóng đại biến dạng bị khổng lồ khi mở kết quả từ `.fcres`/`.vtk` (biên độ lấy từ `maxValue` = 0); nay lấy từ trường `displacement_magnitude` của khung đang xem.
- **Kết quả nhiều bước thời gian**: tự nhận chuỗi `flow_00000.vtk, flow_00002.vtk...` cùng thư mục; bảng "Bước thời gian" (thanh trượt, Phát/Dừng, khóa thang màu theo cả chuỗi); `.fcres` lưu được toàn chuỗi (`result import` tự gom). Chạy thật với SU2 không dừng (11 khung). Test `sequence_import`.
- **Đồ thị kết quả** (nút Đồ thị): vẽ lịch sử hội tụ/lực/chuỗi thời gian, chọn nhiều dãy, trục x tùy chọn, thang log, xuất PNG; tự nạp `history.csv` (SU2) nằm cạnh file `.vtk`; `fastcae-cli result import x.vtk ra.fcres [--history h.csv]`. Test `history_import`.
- **Phác thảo có ràng buộc 2D** (`csketch`): điểm/đường/tròn + 15 loại ràng buộc, kích thước là biểu thức tham số, bộ giải Levenberg–Marquardt, báo mâu thuẫn / thiếu / dư ràng buộc (`src/cad/sketch_solver.*`); có cung tròn (`arc`) và vòng dây đường+cung (`wire`).
- **Bo góc / vát cạnh** trong `.fcad` (`fillet`, `chamfer`, chọn cạnh bằng hộp bao, áp dụng sau cùng) qua Gmsh Python chạy như tiến trình riêng (không liên kết GPL). Ví dụ `examples/param/bracket_bo_goc.fcad`.
- **GUI: nút Sửa tham số** (thẻ Tệp ▸ Hình học CAD): bảng một ô số cho mỗi `param` của `.fcad`, tham số khác mặc định in đậm; Áp dụng ghi `param_set` vào case, dựng lại hình học, chia lưới và giải lại. Dòng lệnh kiểm tra: `fastcae file.fcad --set-param T=0.03,D=0.024`.
- **Lưu/nạp kết quả `.fcres`** (`result_file = ...`, `fastcae-cli result info|vtk|csv`), xuất lại VTK trùng từng byte với bản ghi trực tiếp.

- **Hướng dẫn sử dụng tích hợp trong phần mềm** (**F1**, hoặc **Trợ giúp ▸ Hướng dẫn sử dụng**, thẻ **Công cụ ▸ Trợ giúp**): cửa sổ không modal có mục lục, tìm kiếm, tự đổi theo giao diện sáng/tối; 17 chương từ năng lực tính toán CAE, bắt đầu nhanh, nhập CAD, kết cấu, nhiệt, CFD, cấu kiện, solver ngoài, lập trình, dòng lệnh, tham chiếu file case đến giới hạn và xử lý sự cố. Nguồn `docs/help/huong_dan.html` được nhúng vào tài nguyên (`:/help/huong_dan.html`). Thêm nút **Năng lực tính toán CAE** mở thẳng chương giới thiệu. Thêm bài kiểm tra `help_guide` (đủ chương, liên kết nội bộ hợp lệ).

- **Mở được nhiều định dạng CAD/lưới hơn** (Ctrl+O, Nhập CAD, kéo-thả, `geometry = file ...`, `fastcae-cli import`): thêm `.xao`, `.stl` (dựng khối từ lưới bề mặt kín), lưới khối `.inp` (Abaqus/CalculiX, đọc thẳng không cần Gmsh), `.unv`, `.bdf/.nas`, `.med`, `.mesh` (qua Gmsh), `.msh` nhị phân (Gmsh ngoài đổi sang ASCII), và `.FCStd` (qua `freecadcmd`).
- Bộ đọc lưới nhận thêm phần tử lục diện, lăng trụ, tháp (tuyến tính và bậc 2) và tứ diện bậc 2/3: cắt thành tứ diện liên tục, bỏ nút không dùng (nút giữa cạnh) để ma trận cứng không suy biến.
- Định dạng đóng (SLDPRT, IPT, CATPart, PRT, F3D…): biến môi trường `FASTCAE_CAD_CONVERTER` (lệnh có `{in}`/`{out}`) cho phép nối bộ chuyển đổi của bạn; không có thì hướng dẫn xuất STEP như trước.
- Chế độ gmsh ngoài không còn bắt buộc `mesh_size` (bỏ trống = mặc định của Gmsh). Hộp thoại Nhập CAD không hỏi cỡ lưới với file đã là lưới.
- Bộ lọc hộp thoại Mở sinh từ danh sách đuôi file ở lõi (`cadImportExtensions`); thêm bài kiểm thử (hexa/lăng trụ/tháp/T10, INP, bộ chuyển đổi) và ví dụ `examples/cad/plate_hole.stl`, `plate_hole.inp`.

- Tiêu đề cửa sổ chỉ còn "FastCAE 1.0.0 - Trần Minh Tâm" (bỏ phần lặp tên và tên file).
- Đổi số phiên bản 0.3.x → 1.0.0.
- Sửa lỗi liên kết C++ trên Windows (test `cli_cxx_script`): `-L"...\\"` làm dấu nháy bị nuốt, nay chuẩn hóa -I/-L sang `/`.
- Hộp thoại **Mở** (Ctrl+O) và kéo-thả nhận nhiều định dạng theo đuôi file: .fcae/.txt, .step/.stp/.iges/.igs/.brep/.msh, .vtk, .lua/.py/.cpp.
- Sửa lỗi "Không tìm thấy CalculiX" ở bài tấm/vỏ dù đã đặt ccx: đường dẫn lưu ở **Quản lý solver** nay được đẩy vào biến môi trường của solver (FASTCAE_CCX...) khi khởi động và khi chọn, nên mọi luồng chạy đều thấy; `findCalculix` dùng chung bộ dò với bảng Quản lý solver (nhận thư mục, tên thiếu .exe).
- Mới: `fastcae.show()` (Python) và **Tệp ▸ Mở kết quả (.vtk)** đưa kết quả của solver ngoài lên khung 3D; lõi có `core/vtk_reader` (9 kiểm thử mới); ví dụ 14.

### Gộp từ 0.3.3
- Bảng Lập trình có **Lua, Python và C++**; cả ba gọi được Code_Aster, OpenRadioss/OpenCourant, SU2, OpenFOAM, CalculiX, Elmer và lệnh bất kỳ: lõi `solver/external_run`, `fastcae-cli solver-run`, `fastcae-cli cxx`, `fastcae.code_aster/openradioss/su2/openfoam/calculix/elmer` (Python), `fc::solver` (C++). Xem docs/SCRIPTING.md.
- Sửa lỗi "Khong ghi duoc C:\Program Files\FastCAE\bin\ccx_shell\fastcae_shell.inp": thư mục làm việc mặc định không còn là đường dẫn tương đối (rơi vào thư mục cài đặt). Thêm `core/paths` (`resolveWorkDir`, `userWorkRoot`): ưu tiên cạnh file case, nếu không ghi được thì dùng `%LOCALAPPDATA%\FastCAE\work` (Linux: `~/.local/share/FastCAE/work`; ghi đè bằng biến `FASTCAE_WORK`).
- Hộp thoại Mở/Lưu/Nhập CAD/Xuất VTK/Lưu ảnh mở sẵn thư mục lần trước hoặc `Documents\FastCAE` thay vì `Program Files\FastCAE\bin`; báo lỗi rõ khi không lưu được.

## 0.3.0

### Mới
- **CalculiX**: tải áp suất theo mặt (`pressure = ...`) và tiếp xúc không ma sát nhiều vật thể (`contact`, `body`, `nodeset ... body k`, `adjust`). Kiểm chứng ccx thật: áp suất 0,09%, tiếp xúc 0,41–0,51%.
- **OpenFOAM từ CAD**: `type = cfd3d` với `geometry = file x.step`: ghi polyMesh trực tiếp từ lưới tứ diện Gmsh (nhận diện sáu mặt phẳng hộp bao, patch `walls` cho phần còn lại), `checkMesh`, ví dụ `examples/cad/pipe_d1_l10.step`. `checkMesh` đạt, u_max sai 2,9%, lưu lượng 0,2%.
### Giới hạn đã biết (ghi rõ trong docs/SOLVERS.md)
- Sụt áp tầng lưu trên lưới tứ diện cao hơn lý thuyết ~15% và không giảm khi làm mịn; nguyên nhân chưa xác định.
- Tiếp xúc: cần giữ ngang bằng điều kiện đối xứng; khe hở lớn tự đóng dưới tải chưa chạy; chưa có ma sát.
### Sửa lỗi
- Chạy lại case OpenFOAM trong cùng thư mục có thể đọc nhầm thư mục thời gian cũ lớn hơn; nay dọn kết quả cũ trước khi ghi case (cả `cfd3d` và khoang kín).

## 0.2.0 (cầu nối solver trưởng thành)

### Mới
- **CalculiX mở rộng** (`analysis = frequency | buckle | nlstatic | plastic | dynamic | thermal_stress | heat | heat_transient`): module `ccx_analysis`, đọc `.frd` nhiều gia số và bảng eigenvalue/buckling từ `.dat`. Kiểm chứng với `ccx` 2.21 thật bằng `scripts/08_ccx_kiem_chung.lua` (8 chỉ tiêu, độ lệch 0,07–4,3% so với lời giải giải tích).
- **CFD 3D bằng OpenFOAM** (`type = cfd3d`): module `openfoam_3d`, hộp/kênh blockMesh, simpleFoam/pimpleFoam, tầng lưu / k-epsilon / k-omega SST, điều kiện biên theo mặt. Kiểm chứng với OpenFOAM v1912 thật bằng `scripts/09_openfoam_kiem_chung.lua`.
- Thêm test đơn vị (deck CalculiX, đọc frd, ghi case OpenFOAM 3D) và hai bài kiểm chứng ctest chạy tự động khi máy có `ccx`/OpenFOAM.

## 0.1.1 (rà soát và gia cố)

### Sửa lỗi
- **Script Lua rò rỉ bộ nhớ / hành vi không xác định khi lỗi**: Lua nay được biên dịch như C++ (lỗi thành ngoại lệ, destructor chạy đúng) thay vì `longjmp` qua đối tượng C++.
- **GUI sập khi không tạo được OpenGL**: hiện thông báo hướng dẫn và thoát (mã 3).
- **Đầu vào xấu** (tìm bằng fuzz + ASan/UBSan): giới hạn lưới tối đa 5 triệu phần tử, kiểm tra `n`/`Re`/`lid_speed` hữu hạn và hợp lý, đổi số thực sang int an toàn (NaN/Inf), kiểm tra chỉ số thanh `udl`.

### Giấy phép
- Gộp bản cập nhật giấy phép: lệnh `fastcae-cli licenses`, văn bản LGPL-3.0 / GPL-3.0 / GCC Runtime Library Exception / winpthreads trong `third_party/licenses`, cài vào bộ cài và tài nguyên GUI; thêm Qt 6, OCCT, libgomp, MinGW vào danh mục.
- **`FASTCAE_WITH_GMSH` mặc định OFF** (trước đây ON, mâu thuẫn với tuyên bố MIT vì liên kết Gmsh làm bản build thành GPL). CI Linux không còn cài `libgmsh-dev`.

### Kiểm thử / CI
- Thêm `testHardening` (6 kiểm tra hồi quy); 12/12 test qua, kể cả dưới ASan/UBSan và với `ccx` thật.
- CI: thêm job `sanitizers` (ASan + UBSan).
