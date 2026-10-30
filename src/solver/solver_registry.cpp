// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/solver_registry.hpp"

#include "solver/openfoam_results.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#include "core/case_file.hpp"
#include "core/paths.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace fastcae {

namespace {

#ifdef _WIN32
constexpr char kPathSep = ';';
#else
constexpr char kPathSep = ':';
#endif

bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

std::vector<SolverInfo> buildRegistry() {
    using C = SolverCategory;
    using L = Copyleft;
    using I = Integration;
    std::vector<SolverInfo> v;

    v.push_back({"builtin", "FastCAE tích hợp sẵn (đàn hồi, nhiệt, CFD 2D)", C::Fem, "MIT", L::Permissive,
                 "Solver tự viết của FastCAE; dùng tự do cùng giấy phép MIT.", true,
                 "https://github.com/tranminhtam/FastCAE", I::Builtin,
                 "FEM đàn hồi tuyến tính T4/T10, truyền nhiệt dừng, CFD 2D khoang kín (đã kiểm chứng).",
                 "", {}, "", ""});

    v.push_back({"calculix", "CalculiX (ccx)", C::Fem, "GPL-2.0+ (gói Debian)", L::Strong,
                 "Chạy như tiến trình ngoài nên FastCAE vẫn là MIT. Gói kèm ccx.exe trong bộ cài thì phải "
                 "tuân điều khoản GPL khi phân phối.", true,
                 "https://www.calculix.de", I::Bridge,
                 "Kết cấu tuyến tính/phi tuyến, nhiệt, modal. FastCAE ghi .inp, chạy ccx, đọc .frd (đã kiểm chứng với ccx 2.21).",
                 "FASTCAE_CCX", {{{"ccx", "ccx_2.22", "ccx_2.21", "ccx_2.20", "ccx_2.19", "ccx_static"}, "{base}"}},
                 "CalculiX (*.inp)", "Deck CalculiX .inp (tên job = tên file, không đuôi)"});

    v.push_back({"openfoam", "OpenFOAM", C::Cfd, "GPL-3.0+", L::Strong,
                 "Hai nhánh (Foundation và ESI/OpenCFD). File copyright của gói Debian (ESI v1912) ghi GPL-3+, một phần LGPL-2.1+. Chạy như tiến trình ngoài.", true,
                 "https://openfoam.org", I::Bridge,
                 "CFD 3D, rối, đa pha, truyền nhiệt. FastCAE ghi case khoang kín (icoFoam), nạp môi trường etc/bashrc, chạy blockMesh/icoFoam/foamToVTK và đọc U, p về khung 3D (đã kiểm chứng với OpenFOAM v1912: u tâm khoang lệch 0,015% so với Ghia ở lưới 41x41).",
                 "FASTCAE_OPENFOAM_BIN", {{{"icoFoam"}, "-case {dir}"}},
                 "Điều khiển OpenFOAM (controlDict)", "Chọn file system/controlDict của case"});

    v.push_back({"openradioss", "OpenRadioss (Altair/Siemens)", C::Dynamics, "AGPL-3.0", L::Network,
                 "AGPL: nghĩa vụ mở nguồn kể cả khi cung cấp qua mạng. Siemens đã gỡ kho OpenRadioss/OpenRadioss khỏi GitHub vào đầu 10/2026 và thông báo "
                 "sẽ tiếp tục mã nguồn theo AGPL; vì kho gốc không còn nên chưa đối chiếu trực tiếp (xem OpenCourant bên dưới). "
                 "Bản phát hành cũ vẫn dùng theo AGPL-3.0. Thành phần nhị phân đóng hm_reader chưa từng nằm trong kho Git. "
                 "Có thông tin (chưa kiểm chứng) rằng CLA cho phép chủ bản quyền cấp phép khác cho bản sau: hãy đọc LICENSE của đúng bản bạn dùng.", false,
                 "https://www.openradioss.org", I::Launcher,
                 "Va chạm, động lực học tường minh. Chạy starter rồi engine trên file *_0000.rad / *_0001.rad có sẵn.",
                 "FASTCAE_RADIOSS_BIN",
                 {{{"starter_linux64_gf", "starter_win64", "starter_linuxa64", "starter"}, "-i {input} -np 1"},
                  {{"engine_linux64_gf", "engine_win64", "engine_linuxa64", "engine"}, "-i {base0001}"}},
                 "Radioss (*_0000.rad)", "File starter *_0000.rad (file engine *_0001.rad cùng thư mục)"});

    v.push_back({"opencourant", "OpenCourant (nhánh cộng đồng của OpenRadioss)", C::Dynamics, "AGPL-3.0", L::Network,
                 "Đã đối chiếu kho OpenCourant/OpenCourant (10/2026): GitHub ghi giấy phép AGPL-3.0 (file LICENSE.md) và README nói đây là bản tiếp nối "
                 "OpenRadioss theo GNU AGPL v3. Dự án mới và độc lập với Siemens; bản gốc có thành phần nhị phân đóng (hm_reader) mà nhánh này có thể thiếu. "
                 "Hãy kiểm tra cấu trúc input trước khi tích hợp sâu.", true,
                 "https://github.com/OpenCourant/OpenCourant", I::Launcher,
                 "Cùng cách chạy với OpenRadioss khi tên file thực thi tương thích.",
                 "FASTCAE_COURANT_BIN",
                 {{{"starter_linux64_gf", "starter_win64", "courant_starter", "starter"}, "-i {input} -np 1"},
                  {{"engine_linux64_gf", "engine_win64", "courant_engine", "engine"}, "-i {base0001}"}},
                 "Radioss/Courant (*_0000.rad)", "File starter *_0000.rad"});

    v.push_back({"code_aster", "Code_Aster (EDF)", C::Fem, "GPL-3.0+", L::Strong,
                 "Đã đối chiếu tiêu đề file nguồn trong kho chính thức gitlab.com/codeaster/src (bản 2025): GNU GPL phiên bản 3 hoặc bất kỳ bản sau (EDF R&D). "
                 "Thông tin 2001 (GPL-2 kèm ba ghi chú) đã cũ. Bản đóng gói conda-forge còn liệt kê thành phần CECILL-C, Apache-2.0, LGPL-3.0-only: "
                 "đọc giấy phép của đúng gói bạn cài. Dùng cùng Salome-Meca.", true,
                 "https://code-aster.org", I::Launcher,
                 "Kết cấu công nghiệp: phi tuyến, mỏi, tiếp xúc, nhiệt-cơ.",
                 "FASTCAE_ASTER_BIN", {{{"run_aster", "as_run"}, "{input}"}},
                 "Code_Aster export (*.export)", "File .export mô tả lần chạy (comm, mesh, đường dẫn)"});

    v.push_back({"elmer", "Elmer FEM (CSC)", C::Fem, "GPL-2.0+ (thư viện ElmerSolver: LGPL-2.1+)", L::Strong,
                 "Đã đối chiếu README chính thức của ElmerCSC/elmerfem: Elmer theo GPL, riêng thư viện ElmerSolver theo LGPL (tiêu đề ElmerSolver.F90: LGPL-2.1 hoặc bản sau); "
                 "văn bản LICENSE của bộ cài ghi GPL phiên bản 2 hoặc bản sau. ElmerGUI có ngoại lệ OpenCASCADE riêng; các thành phần con (METIS, umfpack...) có giấy phép riêng. "
                 "Ghi chú trước đây (\"chỉ GPL-2.0\") thiếu phần LGPL. Chạy như tiến trình ngoài nên FastCAE vẫn là MIT.", true,
                 "https://www.csc.fi/web/elmer", I::Launcher,
                 "Đa vật lý: nhiệt, điện từ, dòng chảy, kết cấu, ghép trường.",
                 "FASTCAE_ELMER_BIN", {{{"ElmerSolver", "ElmerSolver_mpi"}, "{input}"}},
                 "Elmer SIF (*.sif)", "File .sif (cần lưới ElmerGrid, ví dụ từ .msh của Gmsh)"});

    v.push_back({"su2", "SU2", C::Cfd, "LGPL-2.1", L::Weak,
                 "LGPL-2.1 từ bản 2.0.7 trở đi (v1.0-2.0 là GPL-3.0).", true,
                 "https://su2code.github.io", I::Launcher,
                 "CFD nén được và không nén, tối ưu hình dạng bằng adjoint, truyền nhiệt liên hợp.",
                 "FASTCAE_SU2_BIN", {{{"SU2_CFD"}, "{input}"}},
                 "SU2 cấu hình (*.cfg)", "File cấu hình .cfg (kèm file lưới .su2)"});

    v.push_back({"code_saturne", "Code_Saturne (EDF)", C::Cfd, "GPL-2.0+", L::Strong,
                 "Đã đối chiếu tiêu đề file nguồn (tài liệu doxygen 7.3 đến 8.3 trên code-saturne.org): GNU GPL phiên bản 2 hoặc bất kỳ bản sau (EDF S.A.); "
                 "file COPYING là văn bản GPL-2.0. Lệnh chạy thay đổi theo phiên bản - hãy sửa lệnh mẫu.", true,
                 "https://www.code-saturne.org", I::Launcher,
                 "CFD thể tích hữu hạn: không nén/nén thấp, rối, truyền nhiệt.",
                 "FASTCAE_SATURNE_BIN", {{{"code_saturne"}, "run --case {dir}"}},
                 "Code_Saturne (*.xml)", "File thiết lập .xml trong thư mục case"});

    v.push_back({"precice", "preCICE", C::Coupling, "LGPL-3.0", L::Weak,
                 "Thư viện ghép nối; kiểm tra file cấu hình bằng precice-tools.", true,
                 "https://precice.org", I::Launcher,
                 "Ghép nối solver (FSI, nhiệt-cơ giữa OpenFOAM và CalculiX...). Mỗi solver cần adapter riêng.",
                 "FASTCAE_PRECICE_BIN", {{{"precice-tools"}, "check {input}"}},
                 "preCICE cấu hình (*.xml)", "File precice-config.xml"});

    v.push_back({"chrono", "Project Chrono (PyChrono)", C::Dynamics, "BSD-3-Clause", L::Permissive,
                 "Cho phép dùng và đóng gói tự do; giữ thông báo bản quyền.", true,
                 "https://projectchrono.org", I::Launcher,
                 "Động lực học đa vật thể, xe, hạt, FSI. Chạy script Python dùng PyChrono.",
                 "FASTCAE_PYTHON", {{{"python3", "python"}, "{input}"}},
                 "Python (*.py)", "Script PyChrono .py"});

    v.push_back({"fenicsx", "FEniCSx / DOLFINx", C::Fem, "LGPL-3.0+", L::Weak,
                 "Đã đối chiếu DOLFINx, FFCx và Basix: LGPL-3.0 hoặc bản sau (tiêu đề file/README). Một số thành phần phụ dùng giấy phép khác (GPL-2+/BSD) theo rà soát của Debian.", true,
                 "https://fenicsproject.org", I::Launcher,
                 "Sinh solver từ dạng yếu (UFL), PDE tuỳ biến. Chạy script Python.",
                 "FASTCAE_PYTHON", {{{"python3", "python"}, "{input}"}},
                 "Python (*.py)", "Script DOLFINx .py"});

    v.push_back({"kratos", "Kratos Multiphysics", C::Fem, "BSD-4-Clause (có điều khoản quảng cáo)", L::Permissive,
                 "Đã đối chiếu kratos/license.txt: BSD có điều khoản quảng cáo, tài liệu quảng bá nêu tính năng hay việc dùng Kratos phải ghi "
                 "\"This product includes Kratos Multi-Physics technology.\" Mỗi ứng dụng con có thể có giấy phép riêng.", true,
                 "https://github.com/KratosMultiphysics/Kratos", I::Launcher,
                 "Kết cấu, CFD, FSI, tối ưu. Chạy script Python (MainKratos.py).",
                 "FASTCAE_PYTHON", {{{"python3", "python"}, "{input}"}},
                 "Python (*.py)", "Script Kratos .py"});

    v.push_back({"mfem", "MFEM (LLNL)", C::Library, "BSD-3-Clause", L::Permissive,
                 "Thư viện C++: nhúng thẳng vào FastCAE được, giữ thông báo bản quyền.", true,
                 "https://mfem.org", I::LibraryOnly,
                 "FEM bậc cao, GPU. Dùng bằng cách liên kết mã nguồn (chưa có mô-đun trong v0.1).",
                 "", {}, "", ""});

    v.push_back({"dealii", "deal.II", C::Library, "LGPL-2.1+ (một phần song song Apache-2.0 + ngoại lệ LLVM)", L::Weak,
                 "Đã đối chiếu LICENSE.md của kho gốc: thư viện hiện theo LGPL-2.1 hoặc bản sau; dự án ĐANG chuyển sang cấp song song Apache-2.0 + ngoại lệ LLVM "
                 "hoặc LGPL-2.1+ nên chỉ một phần mã là song song - không thể coi cả thư viện là Apache (bản FastCAE trước đây ghi sai là tự do hoàn toàn). "
                 "Nhúng thì tuân điều kiện LGPL, trừ khi đã xác minh từng file; một số file trong bundled/ có giấy phép riêng.", true,
                 "https://www.dealii.org", I::LibraryOnly,
                 "FEM thích ứng lưới, song song quy mô lớn (chưa có mô-đun trong v0.1).",
                 "", {}, "", ""});

    v.push_back({"gmsh", "Gmsh (chia lưới, nhập CAD)", C::Library, "GPL-2.0+ kèm ngoại lệ Gmsh", L::Strong,
                 "Đã đối chiếu file copyright (gói Debian): ngoại lệ CHỈ cho phép kết hợp với TetGen, Netgen, Chaco, METIS và OpenCASCADE, "
                 "KHÔNG phải ngoại lệ liên kết chung. Bản FastCAE build với FASTCAE_WITH_GMSH=ON liên kết thư viện Gmsh nên là tác phẩm gộp GPL-2+ "
                 "(phân phối nhị phân phải kèm mã nguồn và giấy phép GPL). Dùng chương trình gmsh ngoài (bản build không nhúng) thì FastCAE vẫn là MIT.", true,
                 "https://gmsh.info", I::Embedded,
                 "Nhập STEP/IGES/BREP, chia lưới tứ diện; trụ/cầu tham số. Tuỳ chọn khi build, hoặc gọi gmsh ngoài.",
                 "FASTCAE_GMSH", {}, "", ""});

    v.push_back({"lua", "Lua (ngôn ngữ lập trình tùy biến nhúng)", C::Library, "MIT", L::Permissive,
                 "Đã đối chiếu thông báo bản quyền trong lua.h (Lua 5.4): MIT. Nhúng và phân phối tự do, giữ thông báo bản quyền.", true,
                 "https://www.lua.org", I::Embedded,
                 "Bảng Lập trình: viết script tham số hoá mô hình, quét tham số, tối ưu, tính toán kỹ thuật, gọi mọi solver của FastCAE.",
                 "", {}, "", ""});

    v.push_back({"qt6", "Qt 6 (Widgets, OpenGLWidgets)", C::Library, "LGPL-3.0 (hoặc GPL-2.0/3.0, hoặc thương mại)", L::Weak,
                 "Đã đối chiếu doc.qt.io/qt-6/licensing.html: mã nguồn mở theo LGPL-3.0 (hoặc GPL), hoặc giấy phép thương mại; một số mô-đun CHỈ có GPL và công cụ Qt theo GPL-3.0 "
                 "kèm ngoại lệ Qt. FastCAE chỉ dùng Widgets và OpenGLWidgets (mô-đun LGPL - hãy đối chiếu lại danh sách mô-đun chỉ-GPL khi nâng cấp Qt). "
                 "Điều kiện LGPL: liên kết động, kèm văn bản LGPL-3.0 và thông báo bản quyền, người dùng phải thay được DLL Qt và biết cách lấy mã nguồn Qt "
                 "(download.qt.io). Qt chứa mã bên thứ ba có giấy phép riêng (SBOM từ Qt 6.8). Liên kết tĩnh hoặc cửa hàng ứng dụng có thể không hợp LGPL.", true,
                 "https://www.qt.io/licensing", I::Embedded,
                 "Giao diện đồ hoạ (cửa sổ, OpenGL). Liên kết động; DLL Qt nằm cạnh fastcae.exe trong bộ cài Windows.",
                 "", {}, "", ""});

    v.push_back({"occt", "Open CASCADE Technology (OCCT)", C::Library, "LGPL-2.1 + ngoại lệ Open CASCADE 1.0", L::Weak,
                 "Đã đối chiếu README kho Open-Cascade-SAS/OCCT và file copyright Debian: LGPL-2.1 kèm ngoại lệ trong OCCT_LGPL_EXCEPTION.txt (cho nhúng header vào tác phẩm phái sinh); "
                 "có thể dùng theo giấy phép thương mại riêng của Open CASCADE. Chỉ có trong bản build FASTCAE_WITH_GMSH=ON; một số phần phụ có giấy phép khác (Expat, Unicode...).", true,
                 "https://dev.opencascade.org", I::Embedded,
                 "Nhân hình học đọc STEP/IGES/BREP, đi kèm Gmsh. Chỉ có khi FASTCAE_WITH_GMSH=ON.",
                 "", {}, "", ""});

    v.push_back({"gomp", "Thư viện chạy OpenMP (libgomp-1.dll, GCC/MinGW)", C::Library, "GPL-3.0 + Ngoại lệ Thư viện Chạy của GCC", L::Weak,
                 "Đã đối chiếu tiêu đề libgomp.h (GPL-3.0 kèm GCC Runtime Library Exception 3.1): chương trình biên dịch bằng GCC và liên kết với thư viện này không bị buộc theo GPL. "
                 "Nhưng chính DLL kèm trong bộ cài vẫn là GPL-3.0: phải kèm văn bản giấy phép + ngoại lệ và chỉ rõ cách lấy mã nguồn GCC tương ứng (gcc.gnu.org).", true,
                 "https://gcc.gnu.org/onlinedocs/libgomp", I::Embedded,
                 "Chạy song song bộ giải tuyến tính (OpenMP). Windows/MinGW: libgomp-1.dll nằm cạnh file chạy.",
                 "", {}, "", ""});

    v.push_back({"mingw_rt", "Thư viện chạy MinGW (libstdc++-6, libgcc_s_seh-1, libwinpthread-1)", C::Library, "GPL-3.0 + Ngoại lệ Thư viện Chạy của GCC; winpthreads: MIT AND BSD-3-Clause-Clear", L::Weak,
                 "libstdc++ và libgcc theo GPL-3.0 kèm GCC Runtime Library Exception (như libgomp); winpthreads theo MIT kèm phần BSD (theo siêu dữ liệu gói MSYS2 và COPYING của mingw-w64). "
                 "Các DLL này chỉ có trong bộ cài Windows nếu windeployqt chép kèm: kiểm tra thư mục bin của bộ cài và, nếu có, kèm văn bản giấy phép (third_party/licenses) và chỉ rõ cách lấy mã nguồn GCC.", true,
                 "https://www.mingw-w64.org", I::Embedded,
                 "Thư viện chạy C++/luồng của trình biên dịch MinGW; xuất hiện cạnh fastcae.exe khi triển khai bằng Qt MinGW.",
                 "", {}, "", ""});

    v.push_back({"petsc", "PETSc", C::Library, "BSD-2-Clause", L::Permissive,
                 "Đã đối chiếu petsc.org: BSD-2-Clause, nhúng được tự do (giữ thông báo bản quyền); không áp dụng cho gói tải kèm bằng --download-package. "
                 "Cần MPI khi biên dịch (header mpi.h): Open MPI theo BSD 3 điều khoản biến thể Open MPI (thêm đoạn không bảo đảm về sáng chế, "
                 "một phần mã có giấy phép Chính phủ Mỹ) và gộp mã bên thứ ba như libevent với giấy phép riêng (docs.open-mpi.org/license).", true,
                 "https://petsc.org", I::Embedded,
                 "Bộ giải tuyến tính cho FEM nội bộ: case ghi `linear_solver = petsc` (CG + ICC; chỉnh bằng `petsc_options`). "
                 "Build với -DFASTCAE_WITH_PETSC=ON. Đã kiểm chứng với PETSc 3.19.6: cùng nghiệm, số vòng lặp giảm 3,6 lần trên tấm có lỗ T10.",
                 "", {}, "", ""});

    return v;
}

// ---- Solver do người dùng khai báo (*.fsolver) --------------------------------------------------------------------
// Cú pháp giống file case (khoá = giá trị, # là chú thích):
//   id = myfem                      (bắt buộc; chữ thường, số, '_' '-')
//   name = FEM tự viết (Python)
//   category = fem | cfd | dynamics | coupling | library
//   license = MIT                   copyleft = permissive | weak | strong | network
//   capability = mô tả ngắn         env = FASTCAE_MYFEM   (biến môi trường trỏ tới file chạy chính)
//   input = model | file            model: hợp đồng mô hình vào - kết quả ra (xem docs/ARCHITECTURE.md)
//   step = python3,python | "{plugin_dir}/myfem.py" {input} {result}     (lặp được; tên file chạy | mẫu tham số)
// {plugin_dir} được thay ngay khi nạp (tự đặt dấu nháy nếu đường dẫn có khoảng trắng); {input} {dir} {result} khi chạy
// đã có sẵn dấu nháy, {base} là tên không đuôi.
std::string trimCopy(std::string s) {
    const auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](char c) { return !ws(static_cast<unsigned char>(c)); }));
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool loadPluginManifest(const fs::path& file, SolverInfo& out) {
    CaseFile cf;
    std::string err;
    if (!cf.load(file.string(), &err)) return false;
    const std::string id = cf.get("id");
    if (id.empty()) return false;
    for (char c : id)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
    SolverInfo s;
    s.id = id;
    s.name = cf.get("name", id);
    const std::string cat = cf.get("category", "fem");
    s.category = cat == "cfd" ? SolverCategory::Cfd : cat == "dynamics" ? SolverCategory::Dynamics : cat == "coupling" ? SolverCategory::Coupling
               : cat == "library" ? SolverCategory::Library : SolverCategory::Fem;
    s.license = cf.get("license", "không rõ");
    const std::string cl = cf.get("copyleft", "permissive");
    s.copyleft = cl == "network" ? Copyleft::Network : cl == "strong" ? Copyleft::Strong : cl == "weak" ? Copyleft::Weak : Copyleft::Permissive;
    s.licenseNote = cf.get("license_note", "Solver do người dùng khai báo (" + file.filename().string() + "); chạy như tiến trình riêng.");
    s.licenseChecked = false;   // người dùng tự chịu trách nhiệm đối chiếu giấy phép
    s.homepage = cf.get("homepage");
    s.integration = Integration::Plugin;
    s.capability = cf.get("capability", "Solver do người dùng khai báo.");
    s.envVar = cf.get("env");
    s.takesModel = cf.get("input", "model") == "model";
    s.inputFilter = s.takesModel ? "Mô hình FastCAE (*.fcmodel.json)" : "File input của solver";
    s.inputHint = s.takesModel ? "Ghi tự động bởi FastCAE từ case (solver = " + id + ")" : "File input theo định dạng của solver";
    s.pluginFile = file.string();
    const std::string pdir = file.parent_path().generic_string();
    for (const auto& line : cf.getAll("step")) {
        const auto bar = line.find('|');
        if (bar == std::string::npos) return false;
        CommandStep st;
        std::stringstream ns(line.substr(0, bar));
        std::string n;
        while (std::getline(ns, n, ',')) { n = trimCopy(n); if (!n.empty()) st.exeNames.push_back(n); }
        st.args = trimCopy(line.substr(bar + 1));
        for (std::size_t at = st.args.find("{plugin_dir}"); at != std::string::npos; at = st.args.find("{plugin_dir}", at + pdir.size()))
            st.args.replace(at, 12, pdir);
        if (st.exeNames.empty()) return false;
        s.steps.push_back(std::move(st));
    }
    if (s.steps.empty()) return false;
    out = std::move(s);
    return true;
}

void loadPlugins(std::vector<SolverInfo>& v) {
    for (const auto& dir : solverPluginDirs()) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == ".fsolver") files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            SolverInfo s;
            if (!loadPluginManifest(f, s)) continue;
            const bool dup = std::any_of(v.begin(), v.end(), [&](const SolverInfo& x) { return x.id == s.id; });
            if (!dup) v.push_back(std::move(s));   // không cho plugin đè solver có sẵn / plugin đã nạp trước
        }
    }
}

}  // namespace

namespace {
// Thư mục chứa file chạy hiện tại (rỗng nếu không xác định được).
fs::path executableDir() {
#ifdef _WIN32
    char buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameA(nullptr, buf, static_cast<DWORD>(sizeof buf));
    if (n == 0 || n >= sizeof buf) return {};
    return fs::path(std::string(buf, n)).parent_path();
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return {};
    return fs::path(std::string(buf, static_cast<std::size_t>(n))).parent_path();
#endif
}
}  // namespace

std::vector<std::string> solverPluginDirs() {
    std::vector<std::string> dirs;
    std::error_code ec;
    if (const char* e = std::getenv("FASTCAE_SOLVER_PLUGINS")) {
        std::stringstream ss(e);
        std::string d;
        while (std::getline(ss, d, kPathSep))
            if (!d.empty() && fs::is_directory(d, ec)) dirs.push_back(d);
    }
    const fs::path user = fs::path(userWorkRoot()).parent_path() / "solvers.d";
    if (fs::is_directory(user, ec)) dirs.push_back(user.string());
    // Plugin đi kèm bản cài: <thư mục cài>/share/fastcae/solvers.d (cạnh bin/), hoặc solvers.d cạnh file chạy.
    const fs::path exe = executableDir();
    if (!exe.empty())
        for (const fs::path& c : {exe.parent_path() / "share" / "fastcae" / "solvers.d", exe / "solvers.d"})
            if (fs::is_directory(c, ec)) dirs.push_back(c.string());
    return dirs;
}

const std::vector<SolverInfo>& allSolvers() {
    static const std::vector<SolverInfo> reg = [] { auto v = buildRegistry(); loadPlugins(v); return v; }();
    return reg;
}

const SolverInfo* findSolverInfo(const std::string& id) {
    for (const auto& s : allSolvers())
        if (s.id == id) return &s;
    return nullptr;
}

std::string findExecutable(const std::vector<std::string>& names, const std::string& userHint,
                           const std::string& envVar) {
    if (!userHint.empty()) {
        if (isFile(userHint)) return userHint;
#ifdef _WIN32
        // hộp thoại Windows có thể trả về tên không có đuôi khi Explorer ẩn đuôi file ("ccx" thay vì "ccx.exe")
        for (const char* ext : {".exe", ".bat", ".cmd"})
            if (isFile(userHint + ext)) return userHint + ext;
#endif
        // người dùng có thể trỏ tới thư mục chứa file chạy
        for (const auto& n : names)
            for (const char* ext : {"", ".exe", ".bat", ".cmd"}) {
                const fs::path p = fs::path(userHint) / (n + ext);
                if (isFile(p)) return p.string();
            }
    }
    if (!envVar.empty())
        if (const char* e = std::getenv(envVar.c_str()))
            if (*e && isFile(e)) return e;
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return {};
    std::stringstream ss(pathEnv);
    std::string dir;
    while (std::getline(ss, dir, kPathSep)) {
        if (dir.empty()) continue;
        for (const auto& n : names) {
#ifdef _WIN32
            // Bỏ qua "python.exe"/"python3.exe" giả của Microsoft Store (thư mục WindowsApps): chưa phải Python thật
            if (n.rfind("python", 0) == 0 && dir.find("WindowsApps") != std::string::npos) continue;
            for (const char* ext : {".exe", ".bat", ".cmd"}) {
                const fs::path p = fs::path(dir) / (n + ext);
                if (isFile(p)) return p.string();
            }
#else
            const fs::path p = fs::path(dir) / n;
            if (isFile(p)) return p.string();
#endif
        }
    }
    return {};
}

std::string findSolverExecutable(const SolverInfo& s, const std::string& userHint) {
    if (s.steps.empty()) return {};
    std::string first;
    for (std::size_t i = 0; i < s.steps.size(); ++i) {
        // gợi ý của người dùng chỉ áp dụng cho bước đầu; các bước sau dò cùng thư mục với bước đầu
        std::string hint = i == 0 ? userHint : (first.empty() ? "" : fs::path(first).parent_path().string());
        std::string exe = findExecutable(s.steps[i].exeNames, hint, i == 0 ? s.envVar : "");
        if (exe.empty() && i > 0) exe = findExecutable(s.steps[i].exeNames, "", "");
        if (exe.empty()) return {};
        if (i == 0) first = exe;
    }
    return first;
}

std::string solverRuntimePath(const SolverInfo& s, const std::string& userHint) {
    std::string exe = findSolverExecutable(s, userHint);
    if (exe.empty() && s.id == "openfoam") exe = findOpenFoamBashrc(userHint);   // OpenFOAM thường chỉ chạy được sau khi nạp etc/bashrc
    return exe;
}

std::string expandArgs(const std::string& tmpl, const std::string& inputPath) {
    const fs::path p(inputPath);
    const std::string dir = p.parent_path().string();
    std::string base = p.stem().string();
    std::string resultBase = base;
    if (resultBase.size() > 8 && resultBase.compare(resultBase.size() - 8, 8, ".fcmodel") == 0) resultBase.erase(resultBase.size() - 8);
    // Làm việc trực tiếp trên chuỗi đầu vào để giữ nguyên dấu phân cách (fs::operator/ trên Windows chèn '\\').
    std::string base0001 = inputPath;
    {
        const auto slash = base0001.find_last_of("/\\");
        const auto pos = base0001.rfind("_0000");
        if (pos != std::string::npos && (slash == std::string::npos || pos > slash)) base0001.replace(pos, 5, "_0001");
    }

    const auto quote = [](const std::string& s) { return "\"" + s + "\""; };
    const std::string resultPath = dir.empty() ? resultBase + "_result.vtk" : dir + "/" + resultBase + "_result.vtk";
    const std::pair<std::string, std::string> rep[] = {
        {"{input}", quote(inputPath)}, {"{result}", quote(resultPath)}, {"{dir}", quote(dir)}, {"{base0001}", quote(base0001)}, {"{base}", base}};
    std::string out = tmpl;
    for (const auto& [k, v] : rep) {
        for (std::size_t at = out.find(k); at != std::string::npos; at = out.find(k, at + v.size()))
            out.replace(at, k.size(), v);
    }
    return out;
}

const char* categoryName(SolverCategory c) {
    switch (c) {
        case SolverCategory::Fem: return "FEM";
        case SolverCategory::Cfd: return "CFD";
        case SolverCategory::Dynamics: return "Động lực / va chạm";
        case SolverCategory::Coupling: return "Ghép nối";
        case SolverCategory::Library: return "Thư viện";
    }
    return "";
}

const char* copyleftName(Copyleft c) {
    switch (c) {
        case Copyleft::Permissive: return "Thoáng (BSD/MIT/Apache)";
        case Copyleft::Weak: return "Copyleft yếu (LGPL)";
        case Copyleft::Strong: return "Copyleft mạnh (GPL)";
        case Copyleft::Network: return "Copyleft mạng (AGPL)";
    }
    return "";
}

const char* integrationName(Integration i) {
    switch (i) {
        case Integration::Builtin: return "Tích hợp sẵn";
        case Integration::Bridge: return "Cầu nối";
        case Integration::Launcher: return "Chạy ngoài (lệnh mẫu)";
        case Integration::Embedded: return "Nhúng thư viện (tuỳ chọn khi build)";
        case Integration::LibraryOnly: return "Thư viện (chưa có mô-đun)";
        case Integration::Plugin: return "Plugin người dùng (.fsolver)";
    }
    return "";
}

std::size_t unverifiedLicenseCount() {
    std::size_t n = 0;
    for (const auto& s : allSolvers())
        if (!s.licenseChecked) ++n;
    return n;
}

std::string licenseReport() {
    std::ostringstream o;
    o << "Thanh phan ben thu ba (khong phai tu van phap ly; xem THIRD_PARTY_NOTICES.txt, docs/LICENSES.md)\n"
      << "id | ten | giay phep | rang buoc | tich hop | doi chieu\n";
    for (const auto& s : allSolvers()) {
        if (s.id == "builtin") continue;
        o << s.id << " | " << s.name << " | " << s.license << " | " << copyleftName(s.copyleft) << " | " << integrationName(s.integration)
          << " | " << (s.licenseChecked ? "da doi chieu" : "CHUA doi chieu") << "\n";
        if (!s.licenseNote.empty()) o << "    Luu y: " << s.licenseNote << "\n";
    }
    o << unverifiedLicenseCount() << " muc chua doi chieu nguon goc.\n";
    return o.str();
}

}  // namespace fastcae
