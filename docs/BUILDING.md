# Biên dịch & đóng gói FastCAE

## Phụ thuộc

| Thư viện | Bắt buộc? | Dùng để |
|---|---|---|
| CMake ≥ 3.21, trình biên dịch C++20 (GCC 11+, Clang 14+, MSVC 2022) | có | build |
| Qt 6 (Widgets, OpenGLWidgets) — 6.4 trở lên chạy được; 6.5+ để tự gom DLL trên Windows | chỉ cho GUI | giao diện + hiển thị OpenGL (không cần VTK/ParaView) |
| Gmsh SDK 4.11+ (kèm OpenCASCADE) | tuỳ chọn | nhập STEP/IGES/BREP, trụ/cầu |
| `gmsh` (chương trình) trong PATH hoặc biến `FASTCAE_GMSH` | tuỳ chọn | thay cho SDK khi không có thư viện |
| OpenMP | tuỳ chọn | giải tuyến tính song song (tự dò) |
| CalculiX `ccx` | tuỳ chọn | `solver = calculix` |

Bộ giải tuyến tính nằm trong `src/solver/sparse.cpp` — không cần Eigen/PETSc.

## Linux (Ubuntu/Debian)

```bash
sudo apt install build-essential cmake qt6-base-dev libgl-dev libglu1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
cd build && cpack                      # .deb, .rpm, .tar.gz
../packaging/linux/build-appimage.sh   # AppImage di động
```
Chỉ cài `libgmsh-dev` và thêm `-DFASTCAE_WITH_GMSH=ON` khi chấp nhận bản build theo GPL; mặc định nhập CAD qua chương trình `gmsh` ngoài hoặc `.msh` (giữ MIT).

## Windows

**Cách khuyên dùng (Visual Studio 2022 + Qt MSVC):**
1. Cài Qt 6 bằng Qt Online Installer, chọn kit *MSVC 2019/2022 64-bit*. Đặt `CMAKE_PREFIX_PATH` về thư mục kit (vd. `C:\Qt\6.7.2\msvc2019_64`).
2. Nhập CAD: tải **gmsh.exe** (gmsh.info) và đặt vào PATH hoặc đặt biến `FASTCAE_GMSH=C:\...\gmsh.exe`. (Muốn liên kết thư viện thì dùng Gmsh SDK và `-DFASTCAE_WITH_GMSH=ON`; mặc định bản Windows dùng `gmsh.exe` ngoài.)
3. Build:
   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.7.2\msvc2019_64 -DFASTCAE_WITH_GMSH=OFF
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure
   ```
4. **Inno Setup (tạo file .exe cài đặt):** cài [Inno Setup 6](https://jrsoftware.org/isinfo.php) rồi
   `powershell -ExecutionPolicy Bypass -File packaging\windows\build-installer.ps1 -Version 1.0.0`
   (build → `cmake --install ... --prefix dist` gom DLL Qt → `iscc packaging\windows\fastcae.iss`) → `dist-installer\FastCAE-1.0.0-Setup.exe`.
   Có: chọn thư mục, liên kết `.fcae`, PATH (tuỳ chọn), shortcut, gỡ cài sạch, cài im lặng `/VERYSILENT`.
5. Hoặc CPack: `cd build; cpack -C Release -G NSIS` (cần NSIS) / `-G ZIP`.

**Chỉ CLI bằng MinGW (WinLibs):** `cmake -S . -B build -G Ninja -DFASTCAE_BUILD_GUI=OFF -DFASTCAE_WITH_GMSH=OFF`. Đừng trộn WinLibs với Qt MSVC; nếu muốn GUI bằng MinGW hãy dùng kit *MinGW* đi kèm Qt Installer cùng bộ biên dịch của nó.

Biểu tượng: `resources/fastcae.ico` đã được gắn vào `.exe` (qua `fastcae.rc`) và vào trình cài đặt.

## Tuỳ chọn CMake

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `FASTCAE_BUILD_GUI` | ON | build GUI nếu thấy Qt6 (không thì bỏ qua, vẫn build CLI) |
| `FASTCAE_WITH_GMSH` | OFF | liên kết thư viện Gmsh (GPL-2+ ⇒ bản build thành GPL); mặc định dùng `gmsh` ngoài để giữ MIT |
| `FASTCAE_BUILD_TESTS` | ON | build + `ctest` |
| `FASTCAE_USE_OPENMP` | ON | song song hoá CG nếu có OpenMP |

Nếu Gmsh SDK nằm ngoài đường dẫn mặc định: `-DCMAKE_PREFIX_PATH=<thư mục chứa lib và include>`.

## Chạy thử GUI không màn hình (kiểm tra / CI)

```bash
xvfb-run -a -s "-screen 0 1280x800x24" env LIBGL_ALWAYS_SOFTWARE=1 \
  fastcae --demo cantilever --scalar vonmises --warp 200 --view 0 --size 1000x640 --screenshot out.png
```
(Cần `xvfb`; Mesa phần mềm đủ dùng. Chế độ `QT_QPA_PLATFORM=offscreen` không tạo được ngữ cảnh OpenGL.)

## Giấy phép thư viện

Mã FastCAE: MIT. Gmsh: GPL-2+ (liên kết thư viện làm bản nhị phân có thể chịu GPL — nếu muốn giữ MIT thuần, dùng `gmsh` như chương trình ngoài); Qt: LGPL-3 (liên kết động); CalculiX: GPL (chỉ chạy như tiến trình ngoài). Kiểm tra lại trước khi phát hành.

## CI

`.github/workflows/build.yml` build + test trên Ubuntu và Windows, đóng gói, tải artifact. Chưa chạy thử trên GitHub nên lần đầu có thể phải chỉnh phiên bản gói/đường dẫn.


## PETSc (tuỳ chọn, bộ giải tuyến tính)

```bash
sudo apt install petsc-dev libopenmpi-dev pkg-config     # Debian/Ubuntu
cmake -S . -B build -G Ninja -DFASTCAE_WITH_PETSC=ON
```

CMake tìm PETSc qua `pkg-config` (file `PETSc.pc`) và cần header MPI (`mpi.h`). Trong case ghi `linear_solver = petsc`
(tuỳ chọn thêm `petsc_options = -ksp_type cg -pc_type gamg`). Mặc định tắt vì cài PETSc + MPI trên Windows phức tạp;
bản cài Windows hiện không kèm PETSc, khi đó `linear_solver = petsc` báo lỗi rõ ràng và dùng `cg` như thường.

## OpenFOAM (chạy ngoài)

Cài OpenFOAM (Linux hoặc WSL). FastCAE tự dò `etc/bashrc` (`FASTCAE_OPENFOAM_BASHRC`, `WM_PROJECT_DIR`, `/usr/share/openfoam`,
`/opt/openfoam*`). Trên Windows thuần chưa có OpenFOAM tương thích để thử: cần `blockMesh`/`icoFoam` trong PATH.
