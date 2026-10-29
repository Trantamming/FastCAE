cmake_minimum_required(VERSION 3.21)

project(FastCAE
    VERSION 1.0.0
    DESCRIPTION "FastCAE - Phan mem CAE ma nguon mo: FEM, truyen nhiet, CFD, nhap CAD"
    HOMEPAGE_URL "https://github.com/tranminhtam/FastCAE"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(FASTCAE_BUILD_GUI   "Build the Qt6 (OpenGL) graphical interface" ON)
option(FASTCAE_WITH_GMSH   "Link the Gmsh library (GPL-2+: the resulting binary becomes GPL). Default OFF keeps FastCAE MIT; CAD import then uses the external gmsh program" OFF)
option(FASTCAE_BUILD_TESTS "Build unit tests" ON)
option(FASTCAE_USE_OPENMP  "Parallelise the linear solver with OpenMP when available" ON)

# ---------------------------------------------------------------- Lua 5.4 (MIT) nhúng sẵn cho phần lập trình tùy biến
file(GLOB LUA_SOURCES ${CMAKE_SOURCE_DIR}/third_party/lua/*.c)
# Biên dịch Lua như C++ (cách Lua hỗ trợ chính thức): lỗi Lua thành ngoại lệ C++ nên mọi destructor
# (chuỗi, vector, kết quả...) chạy đúng khi script lỗi. Biên dịch C dùng longjmp, nhảy qua đối tượng C++ gây rò rỉ/UB.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(fastcae_lua STATIC ${LUA_SOURCES})
target_include_directories(fastcae_lua PUBLIC ${CMAKE_SOURCE_DIR}/third_party/lua)
set_target_properties(fastcae_lua PROPERTIES CXX_STANDARD 20 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(fastcae_lua PRIVATE -w)
endif()

# ---------------------------------------------------------------- core libs
add_library(fastcae_core STATIC
    src/core/mesh.cpp
    src/core/vtk_writer.cpp
    src/core/vtk_reader.cpp
    src/core/result_store.cpp
    src/core/history_csv.cpp
    src/core/vtk_sequence.cpp
    src/core/case_file.cpp
    src/core/paths.cpp
    src/solver/external_run.cpp
    src/script/cxx_runner.cpp
    src/mesh/box_mesher.cpp
    src/solver/sparse.cpp
    src/solver/elastic.cpp
    src/solver/thermal.cpp
    src/solver/cavity_cfd.cpp
    src/solver/calculix_bridge.cpp
    src/solver/ccx_analysis.cpp
    src/solver/openfoam_3d.cpp
    src/solver/frame.cpp
    src/script/script_engine.cpp
    src/solver/shell_ccx.cpp
    src/mesh/assembly_mesher.cpp
    src/cad/shape_library.cpp
    src/cad/frame_templates.cpp
    src/solver/solver_registry.cpp
    src/solver/openfoam_case.cpp
    src/solver/openfoam_results.cpp
    src/solver/quadratic.cpp
    src/cad/cad_import.cpp
    src/cad/param_cad.cpp
    src/cad/sketch_solver.cpp
    src/app/case_runner.cpp)
target_include_directories(fastcae_core PUBLIC src)
target_link_libraries(fastcae_core PUBLIC fastcae_lua)
target_compile_features(fastcae_core PUBLIC cxx_std_20)
target_compile_definitions(fastcae_core PUBLIC FASTCAE_VERSION="${PROJECT_VERSION}")
# Đường dẫn SDK khi chạy ngay từ cây build (bản cài dùng <app>/sdk hoặc FASTCAE_SDK)
target_compile_definitions(fastcae_core PRIVATE FASTCAE_DEV_INCLUDE="${CMAKE_SOURCE_DIR}/src" FASTCAE_DEV_LIBDIR="${CMAKE_BINARY_DIR}")
if(MSVC)
    target_compile_options(fastcae_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(fastcae_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optional: Gmsh (its bundled OpenCASCADE reads STEP/IGES/BREP and builds tets)
if(FASTCAE_WITH_GMSH)
    find_package(Gmsh QUIET)
    if(Gmsh_FOUND)
        message(STATUS "LUU Y GIAY PHEP: lien ket thu vien Gmsh (GPL-2+) lam ban nhi phan FastCAE thanh tac pham gop GPL; phan phoi kem ma nguon, hoac dung gmsh ngoai (FASTCAE_WITH_GMSH=OFF) de giu MIT")
        message(STATUS "Gmsh found: STEP/IGES/BREP import enabled")
        target_link_libraries(fastcae_core PUBLIC Gmsh::Gmsh)
        target_compile_definitions(fastcae_core PUBLIC FASTCAE_HAVE_GMSH=1)
    else()
        find_library(GMSH_LIB gmsh)
        find_path(GMSH_INC gmsh.h)
        if(GMSH_LIB AND GMSH_INC)
            message(STATUS "LUU Y GIAY PHEP: lien ket thu vien Gmsh (GPL-2+) lam ban nhi phan FastCAE thanh tac pham gop GPL; phan phoi kem ma nguon, hoac dung gmsh ngoai (FASTCAE_WITH_GMSH=OFF) de giu MIT")
            message(STATUS "Gmsh found (${GMSH_LIB}): STEP/IGES/BREP import enabled")
            target_link_libraries(fastcae_core PUBLIC ${GMSH_LIB})
            target_include_directories(fastcae_core PUBLIC ${GMSH_INC})
            target_compile_definitions(fastcae_core PUBLIC FASTCAE_HAVE_GMSH=1)
        else()
            message(STATUS "Gmsh not found: only built-in primitives can be meshed")
        endif()
    endif()
endif()

option(FASTCAE_WITH_PETSC "Dùng PETSc (BSD-2) làm bộ giải tuyến tính tuỳ chọn (linear_solver = petsc)" OFF)
if(FASTCAE_WITH_PETSC)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PETSC IMPORTED_TARGET PETSc)
    endif()
    if(PETSC_FOUND)
        message(STATUS "PETSc ${PETSC_VERSION} found: linear_solver = petsc enabled")
        target_link_libraries(fastcae_core PUBLIC PkgConfig::PETSC)
        enable_language(C)                         # FindMPI cần ngôn ngữ C
        find_package(MPI QUIET COMPONENTS C)       # PETSc include <mpi.h>
        if(MPI_FOUND)
            target_link_libraries(fastcae_core PUBLIC MPI::MPI_C)
        endif()
        target_compile_definitions(fastcae_core PUBLIC FASTCAE_HAVE_PETSC=1)
    else()
        message(WARNING "PETSc not found (need pkg-config file PETSc.pc, e.g. set PKG_CONFIG_PATH)")
    endif()
endif()

if(FASTCAE_USE_OPENMP)
    find_package(OpenMP QUIET COMPONENTS CXX)
    if(OpenMP_CXX_FOUND)
        message(STATUS "OpenMP found: parallel linear solver enabled")
        target_link_libraries(fastcae_core PUBLIC OpenMP::OpenMP_CXX)
    endif()
endif()

set(_fc_link_extra "")
if(FASTCAE_WITH_GMSH AND TARGET fastcae_core AND GMSH_LIB)
    set(_fc_link_extra "${GMSH_LIB}")
endif()
if(_fc_link_extra)
    target_compile_definitions(fastcae_core PRIVATE FASTCAE_LINK_EXTRA="${_fc_link_extra}")
endif()

# ---------------------------------------------------------------- CLI
add_executable(fastcae-cli src/app/main_cli.cpp)
target_link_libraries(fastcae-cli PRIVATE fastcae_core)
set_target_properties(fastcae-cli PROPERTIES OUTPUT_NAME fastcae-cli)

# ---------------------------------------------------------------- GUI
set(FASTCAE_GUI_ENABLED OFF)
if(FASTCAE_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets OpenGLWidgets)
    if(Qt6_FOUND)
        set(FASTCAE_GUI_ENABLED ON)
        message(STATUS "Qt6 ${Qt6_VERSION} found: building the graphical interface")
        add_subdirectory(src/gui)
    else()
        message(STATUS "Qt6 not found: GUI skipped (CLI is still built)")
    endif()
endif()

# ---------------------------------------------------------------- tests
if(FASTCAE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------- install + packaging
include(GNUInstallDirs)
install(TARGETS fastcae-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(MINGW AND OpenMP_CXX_FOUND)
    # MinGW: ship the OpenMP runtime next to the executables
    get_filename_component(_fastcae_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_file(FASTCAE_GOMP_DLL libgomp-1.dll HINTS "${_fastcae_cxx_dir}")
    if(FASTCAE_GOMP_DLL)
        install(FILES "${FASTCAE_GOMP_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.txt docs/LICENSES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/fastcae/examples)
# Plugin solver đi kèm (FastCAE tự dò <cài đặt>/share/fastcae/solvers.d)
install(DIRECTORY examples/solver_plugin/ DESTINATION ${CMAKE_INSTALL_DATADIR}/fastcae/solvers.d
        FILES_MATCHING PATTERN "*.fsolver" PATTERN "*.py")
install(DIRECTORY tools/ DESTINATION ${CMAKE_INSTALL_DATADIR}/fastcae/tools
        USE_SOURCE_PERMISSIONS)
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/fastcae.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/fastcae.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES resources/fastcae-256.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME fastcae.png)
endif()

option(FASTCAE_BUILD_EXAMPLES "Biên dịch ví dụ dùng FastCAE như thư viện C++ (examples/cpp)" ON)
if(FASTCAE_BUILD_EXAMPLES)
    add_subdirectory(examples/cpp)
endif()

# SDK tuỳ chọn cho người viết plugin/chương trình C++: cmake --install build --component sdk
install(TARGETS fastcae_core fastcae_lua ARCHIVE DESTINATION lib COMPONENT sdk EXCLUDE_FROM_ALL)
install(DIRECTORY src/ DESTINATION include/fastcae COMPONENT sdk EXCLUDE_FROM_ALL FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY third_party/lua/ DESTINATION include/fastcae/lua COMPONENT sdk EXCLUDE_FROM_ALL FILES_MATCHING PATTERN "*.h")
install(DIRECTORY scripts/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/scripts FILES_MATCHING PATTERN "*.lua")
install(FILES python/fastcae.py DESTINATION ${CMAKE_INSTALL_DOCDIR}/python)
install(FILES third_party/lua/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME LICENSE-Lua.txt)
# Văn bản giấy phép của thành phần đi kèm (LGPL-3.0 cho Qt, GPL-3.0 + ngoại lệ GCC cho thư viện chạy MinGW, winpthreads).
install(DIRECTORY third_party/licenses/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses FILES_MATCHING PATTERN "*.txt")

include(cmake/Packaging.cmake)
