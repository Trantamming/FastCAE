# Giấy phép: FastCAE và các thành tựu CAE mã nguồn mở được kết nối

Tác giả FastCAE: **Trần Minh Tâm** · Giấy phép FastCAE: **MIT** (xem `LICENSE`).

> Đây là tổng hợp kỹ thuật dựa trên đối chiếu file giấy phép (10/2026), **không phải tư vấn pháp lý**. Trước khi phát hành
> bộ cài, hãy đọc lại giấy phép của từng dự án (đặc biệt các mục "chưa đối chiếu") và hỏi luật sư nếu cần.

## Trả lời ngắn: đã đủ giấy phép để kết nối chưa?

- Mọi dự án trong bảng đều dùng giấy phép phần mềm tự do/mã nguồn mở, **cho phép dùng** từ FastCAE. Chưa có dự án nào bị cấm.
- Nhưng "được dùng" khác "được gói kèm hoặc liên kết không điều kiện". Điều kiện phụ thuộc **cách FastCAE dùng** chúng:
  1. **Chạy như chương trình riêng** (CalculiX, OpenFOAM, Elmer, SU2, Code_Saturne...): FastCAE giữ nguyên MIT. Nếu bạn **gói kèm**
     chương trình đó vào bộ cài thì phải theo điều khoản phân phối của nó (với GPL/AGPL: kèm văn bản giấy phép và cung cấp hoặc chỉ rõ
     cách lấy mã nguồn). Cách đơn giản nhất: không gói kèm, để người dùng tự cài.
  2. **Liên kết thư viện vào chương trình**: điều kiện chặt hơn.
     - Qt (LGPL-3): liên kết động, kèm DLL, kèm văn bản LGPL, và người dùng phải thay được DLL.
     - **Gmsh (GPL-2+): bản FastCAE build với `FASTCAE_WITH_GMSH=ON` là tác phẩm gộp GPL-2+**, phân phối nhị phân phải theo GPL
       (kèm mã nguồn). Giữ MIT bằng cách build `FASTCAE_WITH_GMSH=OFF` (mặc định) và dùng `gmsh` ngoài.
     - PETSc (BSD-2), Lua (MIT): nhúng tự do, giữ thông báo bản quyền.
  3. **Chỉ có thông tin trong danh mục** (MFEM, deal.II): chưa có mã nào dùng chúng.
- Còn **một mục chưa đối chiếu trực tiếp**: OpenRadioss (kho gốc đã bị Siemens gỡ nên không đọc được file giấy phép; xem OpenCourant). Chạy `fastcae-cli licenses` để xem danh sách mục "CHUA doi chieu" hiện tại.
- Các mục dưới đây đối chiếu lại ngày 24/10/2026 từ nguồn gốc: Code_Aster, Elmer, Code_Saturne, deal.II, FEniCSx, OpenCourant, Qt, OpenCASCADE, PETSc, Open MPI, libgomp/MinGW.

## Bảng giấy phép và nguồn đối chiếu

| Dự án | Giấy phép | Nguồn đối chiếu | FastCAE dùng thế nào | Điều kiện đáng nhớ |
|---|---|---|---|---|
| **Lua 5.4.7** | MIT | thông báo trong `lua.h` (kho `lua/lua`) | **nhúng sẵn** (`third_party/lua`) | giữ thông báo bản quyền (đã kèm `LICENSE-Lua.txt`) |
| **Qt 6** | LGPL-3.0 hoặc GPL, hoặc thương mại | `doc.qt.io/qt-6/licensing.html` | liên kết động (giao diện): chỉ Widgets và OpenGLWidgets | kèm DLL + văn bản LGPL-3.0 (`licenses\LGPL-3.0.txt`); cho thay DLL; chỉ rõ cách lấy mã nguồn Qt; một số mô-đun Qt chỉ có GPL (không dùng); Qt chứa mã bên thứ ba có giấy phép riêng |
| **Gmsh 4.12** | GPL-2.0+ kèm ngoại lệ **chỉ** cho TetGen, Netgen, Chaco, METIS, OpenCASCADE | file copyright gói Debian `gmsh` | tuỳ chọn khi build (liên kết) hoặc `gmsh` ngoài | **liên kết ⇒ tác phẩm gộp GPL**; ngoại lệ không phải ngoại lệ liên kết chung |
| **OpenCASCADE** | LGPL-2.1 + ngoại lệ Open CASCADE 1.0 | README kho `Open-Cascade-SAS/OCCT`, file copyright Debian | đi kèm Gmsh (chỉ khi `FASTCAE_WITH_GMSH=ON`) | như LGPL; có thể mua giấy phép thương mại |
| **PETSc 3.19** | BSD-2-Clause | `petsc.org/main/install/license` | tuỳ chọn khi build (liên kết) | không áp dụng cho gói tải kèm `--download-*` |
| **Open MPI** (PETSc cần) | BSD 3 điều khoản, biến thể Open MPI (SPDX `BSD-3-Clause-Open-MPI`) | `open-mpi.org/community/license.php`, SPDX | cần khi build PETSc | thêm đoạn không bảo đảm về sáng chế; một phần mã có giấy phép Chính phủ Mỹ; gộp libevent... với giấy phép riêng |
| **CalculiX 2.21** | GPL-2.0+ | file copyright gói Debian `calculix-ccx` | chương trình riêng (cầu nối + tấm/vỏ) | gói kèm thì theo GPL |
| **OpenFOAM** (ESI v1912) | GPL-3.0+ (một phần LGPL-2.1+) | file copyright gói Debian `openfoam` | chương trình riêng | gói kèm thì theo GPL; hai nhánh Foundation/ESI |
| **SU2** | LGPL-2.1 | `LICENSE.md` (kho gốc) | chương trình riêng (lệnh mẫu) | |
| **preCICE** | LGPL-3.0 | `LICENSE` (kho gốc) | chương trình riêng (`precice-tools`) | |
| **Project Chrono** | BSD (3 điều khoản) | `LICENSE` (kho gốc) | script Python (PyChrono) | |
| **MFEM** | BSD-3-Clause | `LICENSE` (kho gốc) | chỉ thông tin | |
| **deal.II** | **LGPL-2.1+**; một phần mã được cấp song song Apache-2.0 + ngoại lệ LLVM hoặc LGPL-2.1+ (đang chuyển đổi) | `LICENSE.md` (kho gốc) và tiêu đề file | chỉ thông tin | không thể coi cả thư viện là Apache; một số file trong `bundled/` có giấy phép riêng |
| **FEniCSx / DOLFINx** | LGPL-3.0+ | README/tiêu đề file DOLFINx, FFCx, Basix | script Python | một số thành phần phụ giấy phép khác |
| **Elmer FEM** | **GPL-2.0+**; thư viện **ElmerSolver: LGPL-2.1+**; ElmerGUI có ngoại lệ OpenCASCADE | README chính thức `ElmerCSC/elmerfem`, tiêu đề `ElmerSolver.F90`, LICENSE bộ cài | chương trình riêng (lệnh mẫu) | thành phần con (METIS, umfpack...) có giấy phép riêng; chạy tiến trình riêng nên FastCAE vẫn MIT |
| **Code_Saturne** | **GPL-2.0+** | tiêu đề file nguồn (doxygen 7.3-8.3 trên `code-saturne.org`), `COPYING` | chương trình riêng (lệnh mẫu) | "phiên bản 2 hoặc bản sau" theo tiêu đề file |
| **Kratos Multiphysics** | **BSD-4-Clause (có điều khoản quảng cáo)** | `kratos/license.txt` (kho gốc) | script Python | tài liệu quảng bá nêu tính năng/việc dùng Kratos phải ghi *"This product includes Kratos Multi-Physics technology."*; mỗi ứng dụng con có thể có giấy phép riêng |
| **Code_Aster** | **GPL-3.0+** | tiêu đề file nguồn trong `gitlab.com/codeaster/src` (bản 2025) | chương trình riêng (lệnh mẫu) | bản đóng gói conda-forge còn có thành phần CECILL-C, Apache-2.0, LGPL-3.0-only |
| **OpenRadioss** | AGPL-3.0 | tin tức (It's FOSS, Phoronix, Slashdot 10/2026); kho gốc đã gỡ nên **chưa đối chiếu** | chương trình riêng (lệnh mẫu) | Siemens gỡ kho và thông báo tiếp tục theo AGPL; `hm_reader` là nhị phân đóng; AGPL có nghĩa vụ cả khi cung cấp qua mạng |
| **OpenCourant** | AGPL-3.0 | kho `OpenCourant/OpenCourant` (GitHub ghi AGPL-3.0, `LICENSE.md`, README) | chương trình riêng (lệnh mẫu) | nhánh cộng đồng độc lập, tiếp nối OpenRadioss; có thể thiếu `hm_reader` |
| **libgomp** (OpenMP, MinGW) | GPL-3.0 + Ngoại lệ Thư viện Chạy của GCC | tiêu đề `libgomp.h` | `libgomp-1.dll` kèm cạnh file chạy trên Windows | chương trình liên kết không bị buộc GPL, nhưng DLL kèm bộ cài là GPL-3.0: kèm văn bản + ngoại lệ và chỉ cách lấy mã nguồn GCC |
| **libstdc++, libgcc, winpthreads** (MinGW) | GPL-3.0 + Ngoại lệ Thư viện Chạy của GCC; winpthreads: MIT AND BSD-3-Clause-Clear | siêu dữ liệu gói MSYS2, `COPYING` của mingw-w64 | DLL do `windeployqt` chép kèm (nếu có trong `bin\`) | như libgomp; văn bản winpthreads: `licenses\winpthreads-COPYING.txt` |

## Những điều đã sửa so với bản trước

- Gmsh: trước đây ghi "ngoại lệ liên kết cho API". **Sai**: ngoại lệ chỉ cho TetGen, Netgen, Chaco, METIS, OpenCASCADE. Bản nhúng Gmsh là tác phẩm gộp GPL.
- Elmer: trước đây ghi "ElmerSolver: LGPL-2.1". `LICENSE.md` gốc không nêu; ghi GPL-2.0.
- Kratos: xác nhận BSD-4 và bổ sung điều khoản ghi nhận khi quảng bá.
- deal.II: trước đây ghi "Apache-2.0 + ngoại lệ LLVM hoặc LGPL-2.1+ (chọn một)" và xếp vào nhóm tự do hoàn toàn. **Sai**: `LICENSE.md` ghi thư viện đang theo LGPL-2.1+ và dự án *đang chuyển* sang cấp song song, chỉ một phần mã là song song. Đã chuyển sang LGPL-2.1+ (copyleft yếu).
- Elmer: bản vừa rồi ghi "chỉ GPL-2.0, LICENSE.md không nêu LGPL". README chính thức của kho **có nêu**: GPL, riêng thư viện ElmerSolver là LGPL; tiêu đề `ElmerSolver.F90` ghi LGPL-2.1+. Đã ghi lại cả hai, và GPL là "2 hoặc bản sau".
- Code_Aster: trước đây "GPL" theo bản tin 2001 (khi đó là GPL-2 kèm ba ghi chú). Tiêu đề file nguồn hiện hành ghi **GPL-3.0+**.
- Code_Saturne: tiêu đề file nguồn ghi "phiên bản 2 hoặc bản sau" nên là GPL-2.0+ (trước đây còn để ngỏ).
- OpenCourant: đã đối chiếu trên kho (AGPL-3.0). OpenRadioss vẫn "chưa đối chiếu" vì kho gốc không còn.
- Thêm vào danh mục (trước đây chỉ có trong bảng gõ cứng của hộp thoại Giới thiệu hoặc không có): Qt 6, OpenCASCADE, libgomp, thư viện chạy MinGW. Hộp thoại Giới thiệu không còn lặp dòng Gmsh.

## Việc cần làm trước khi phát hành bộ cài

1. Bộ cài Windows hiện build `FASTCAE_WITH_GMSH=OFF` nên giữ MIT. Nếu bật Gmsh, phát hành theo GPL-2+ kèm mã nguồn.
2. Kèm văn bản giấy phép Qt (LGPL-3), thông báo bản quyền Qt, và để người dùng thay được DLL Qt.
3. Đã kèm `LICENSE-Lua.txt` (MIT). Giữ thông báo bản quyền khi sửa Lua.
4. Không gói kèm ccx.exe, OpenFOAM, Elmer...; nếu gói kèm, kèm giấy phép và cách lấy mã nguồn.
5. OpenRadioss: kho gốc đã gỡ; nếu định quảng bá hỗ trợ, dùng OpenCourant (đã đối chiếu) hoặc đọc lại LICENSE của đúng bản bạn phân phối.
6. Thay email ví dụ trong `cmake/Packaging.cmake` bằng email thật của tác giả.
7. Bộ cài Windows chép kèm DLL của MinGW (`libgomp-1.dll`, thường cả `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`): đã kèm văn bản GPL-3.0, ngoại lệ GCC và winpthreads trong `licenses\`. Hãy mở thư mục `bin\` của bộ cài để xác nhận DLL nào thực sự có, và ghi cách lấy mã nguồn GCC 13.1.0 tương ứng.
8. Khi nâng cấp Qt: đối chiếu lại danh sách mô-đun chỉ-GPL trên trang giấy phép Qt (FastCAE chỉ dùng Widgets và OpenGLWidgets).
9. `ctest` có test `notices: ...` so khớp `THIRD_PARTY_NOTICES.txt` với danh mục: thêm thành phần mới vào `solver_registry.cpp` mà quên cập nhật thông báo sẽ làm test thất bại.
