add_executable(fastcae_tests test_main.cpp)
target_link_libraries(fastcae_tests PRIVATE fastcae_core)
target_compile_definitions(fastcae_tests PRIVATE FASTCAE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME fastcae_tests COMMAND fastcae_tests)

# Tài liệu hướng dẫn nhúng trong GUI (F1): đủ chương, liên kết nội bộ hợp lệ.
add_test(NAME help_guide COMMAND ${CMAKE_COMMAND} -DHELP=${CMAKE_SOURCE_DIR}/docs/help/huong_dan.html
         -P ${CMAKE_CURRENT_SOURCE_DIR}/check_help.cmake)

add_test(NAME cli_licenses COMMAND fastcae-cli licenses)
set_tests_properties(cli_licenses PROPERTIES PASS_REGULAR_EXPRESSION "OpenCourant.*AGPL-3.0")

add_test(NAME cli_demo_cantilever
         COMMAND fastcae-cli demo cantilever ${CMAKE_CURRENT_BINARY_DIR}/out)
add_test(NAME cli_demo_conduction
         COMMAND fastcae-cli demo conduction ${CMAKE_CURRENT_BINARY_DIR}/out)

# Chạy thật với OpenFOAM nếu máy có (blockMesh + icoFoam trong PATH hoặc etc/bashrc ở vị trí phổ biến).
find_program(FOAM_ICO icoFoam)
find_file(FOAM_BASHRC bashrc PATHS /usr/share/openfoam/etc /usr/lib/openfoam/etc NO_DEFAULT_PATH)
if(FOAM_ICO OR FOAM_BASHRC)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/openfoam_cavity.fcae
        "type = cavity\nsolver = openfoam\nn = 20\nRe = 100\nend_time = 2\nwork_dir = ${CMAKE_CURRENT_BINARY_DIR}/foam_run\n")
    add_test(NAME cli_openfoam_cavity COMMAND fastcae-cli run ${CMAKE_CURRENT_BINARY_DIR}/openfoam_cavity.fcae)
    set_tests_properties(cli_openfoam_cavity PROPERTIES PASS_REGULAR_EXPRESSION "Hoi tu \\(OpenFOAM icoFoam" TIMEOUT 300)
endif()

# Tấm/vỏ giải bằng CalculiX thật nếu máy có ccx.
find_program(CCX_EXE NAMES ccx ccx_2.22 ccx_2.21 ccx_2.20 ccx_2.19)
if(CCX_EXE)
    add_test(NAME cli_shell_ccx COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:fastcae-cli> -DWORK=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_shell_case.cmake)
    # Kiểm chứng CalculiX mở rộng (dao động riêng, mất ổn định, phi tuyến hình học, dẻo, động lực, nhiệt) với lời giải giải tích.
    add_test(NAME ccx_verification COMMAND fastcae-cli script ${CMAKE_SOURCE_DIR}/scripts/08_ccx_kiem_chung.lua WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(ccx_verification PROPERTIES PASS_REGULAR_EXPRESSION "Tat ca chi tieu kiem chung CalculiX DAT|Tất cả chỉ tiêu kiểm chứng CalculiX ĐẠT" TIMEOUT 600)
endif()

# Kiểm chứng OpenFOAM 3D (kênh tầng lưu Poiseuille + kênh rối k-epsilon vs Dean) nếu máy có OpenFOAM.
find_program(FOAM_BLOCKMESH NAMES blockMesh)
if(FOAM_BLOCKMESH OR EXISTS /usr/share/openfoam/etc/bashrc OR DEFINED ENV{FASTCAE_OPENFOAM_BASHRC} OR DEFINED ENV{WM_PROJECT_DIR})
    add_test(NAME openfoam_verification COMMAND fastcae-cli script ${CMAKE_SOURCE_DIR}/scripts/09_openfoam_kiem_chung.lua ${CMAKE_SOURCE_DIR}/examples/cad/pipe_d1_l10.step WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(openfoam_verification PROPERTIES PASS_REGULAR_EXPRESSION "Tất cả chỉ tiêu kiểm chứng OpenFOAM ĐẠT" TIMEOUT 900)
endif()

# Script Lua mẫu (không cần solver ngoài): phải chạy hết, mã thoát 0.
foreach(S 01_tiet_dien 02_quet_tham_so_dam 03_toi_uu_tiet_dien 04_dan_so_khoang 05_tan_so_theo_chieu_dai 06_to_hop_tai_trong)
    add_test(NAME lua_${S} COMMAND fastcae-cli script ${CMAKE_SOURCE_DIR}/scripts/${S}.lua WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# Ví dụ C++ dùng thư viện lõi.
if(TARGET fastcae_example_beam_sweep)
    add_test(NAME cpp_example_beam_sweep COMMAND fastcae_example_beam_sweep)
endif()

# Module Python (cần python3).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME python_api COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/python/test_fastcae.py WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/python)
    set_tests_properties(python_api PROPERTIES ENVIRONMENT "FASTCAE_CLI=$<TARGET_FILE:fastcae-cli>")
endif()

# Hợp đồng solver theo mô hình (plugin .fsolver + FEM Python tự lập trình): cần python3 có numpy và scipy.
if(Python3_FOUND)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import numpy, scipy" RESULT_VARIABLE FC_NUMPY_RC OUTPUT_QUIET ERROR_QUIET)
    if(FC_NUMPY_RC EQUAL 0)
        add_test(NAME solver_plugin_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/plugin_check.py $<TARGET_FILE:fastcae-cli>)
        set_tests_properties(solver_plugin_contract PROPERTIES TIMEOUT 300)
        add_test(NAME tutorial_examples COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tutorial_check.py $<TARGET_FILE:fastcae-cli>)
        set_tests_properties(tutorial_examples PROPERTIES TIMEOUT 300)
    endif()
endif()

# Script C++ (không có main) biên dịch với SDK rồi chạy; cần g++/clang++ trên máy.
find_program(FC_CXX NAMES g++ clang++ c++)
# Lệnh "cxx" biên dịch mã người dùng bằng g++ và liên kết với thư viện của FastCAE: không thể khi thư viện build bằng MSVC (khác ABI)
# hoặc bằng sanitizer (thiếu -fsanitize ở dòng g++ của người dùng).
if(FC_CXX AND NOT MSVC AND NOT CMAKE_CXX_FLAGS MATCHES "sanitize")
    add_test(NAME cli_cxx_script COMMAND fastcae-cli cxx ${CMAKE_CURRENT_SOURCE_DIR}/cxx_smoke.cpp)
    set_tests_properties(cli_cxx_script PROPERTIES PASS_REGULAR_EXPRESSION "CXX_SMOKE_OK" TIMEOUT 300)
endif()

# Cầu nối solver ngoài: dùng solver giả (POSIX) kiểm tra chạy, tìm file mới, mã thoát.
if(UNIX)
    add_test(NAME cli_solver_run_fake COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:fastcae-cli> -DWORK=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_fake_solver.cmake)
endif()

# CAD tham số (cần python3 và gmsh trong PATH): thể tích lưới so với công thức giải tích
find_program(FC_PY NAMES python3 python)
find_program(FC_GMSH NAMES gmsh)
if(FC_PY AND FC_GMSH)
    add_test(NAME param_cad_validate COMMAND ${FC_PY} ${CMAKE_CURRENT_SOURCE_DIR}/param/validate.py $<TARGET_FILE:fastcae-cli>)
    set_tests_properties(param_cad_validate PROPERTIES TIMEOUT 600)
    add_test(NAME lua_param_sweep COMMAND fastcae-cli script ${CMAKE_SOURCE_DIR}/scripts/15_quet_tham_so_cad.lua ${CMAKE_SOURCE_DIR}/examples/param/dam_tham_so.fcad)
    set_tests_properties(lua_param_sweep PROPERTIES PASS_REGULAR_EXPRESSION "30\\.0 +0\\.3[0-9]+ +0\\.3175" TIMEOUT 300)
    add_test(NAME result_roundtrip COMMAND ${FC_PY} ${CMAKE_CURRENT_SOURCE_DIR}/param/result_roundtrip.py $<TARGET_FILE:fastcae-cli>)
endif()

# Kết quả 2D (tam giác/tứ giác phẳng như SU2) phải nhập được: đùn thành lớp mỏng, giữ trường nút, ghi được .fcres
add_test(NAME vtk_2d_import COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:fastcae-cli> -DIN=${CMAKE_CURRENT_SOURCE_DIR}/data/tri2d.vtk -DOUT=${CMAKE_CURRENT_BINARY_DIR}/tri2d.fcres
    -P ${CMAKE_CURRENT_SOURCE_DIR}/vtk_2d_import.cmake)

# Lịch sử hội tụ CSV kiểu SU2 (tiêu đề có ngoặc kép/khoảng trắng; cột chữ bị bỏ): vào .fcres thành các dãy số
add_test(NAME history_import COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:fastcae-cli> -DIN=${CMAKE_CURRENT_SOURCE_DIR}/data/tri2d.vtk -DHIST=${CMAKE_CURRENT_SOURCE_DIR}/data/history.csv
    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/hist.fcres -P ${CMAKE_CURRENT_SOURCE_DIR}/history_import.cmake)

# Chuỗi bước (step_0001, step_0002, step_0010 -> sắp theo số): nhập thành một .fcres có khung, mở lại vẫn đủ
add_test(NAME sequence_import COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:fastcae-cli> -DIN=${CMAKE_CURRENT_SOURCE_DIR}/data/seq/step_0002.vtk -DOUT=${CMAKE_CURRENT_BINARY_DIR}/seq.fcres
    -P ${CMAKE_CURRENT_SOURCE_DIR}/sequence_import.cmake)

# CalculiX thật (nếu có ccx): dao động riêng và động lực của dầm, mỗi dạng riêng / mốc thời gian là một khung
find_program(FC_CCX NAMES ccx ccx_2.21 ccx_2.20)
if(FC_PY AND FC_CCX)
    add_test(NAME ccx_real_validate COMMAND ${FC_PY} ${CMAKE_CURRENT_SOURCE_DIR}/ccx_validate.py $<TARGET_FILE:fastcae-cli>)
    set_tests_properties(ccx_real_validate PROPERTIES TIMEOUT 600)
endif()

# Ứng suất phần tử bậc 2: nút giữa cạnh phải có giá trị (không phải 0) và đúng lý thuyết dầm
find_program(FC_PY2 NAMES python3 python)
if(FC_PY2)
    add_test(NAME stress_midnodes COMMAND ${FC_PY2} ${CMAKE_CURRENT_SOURCE_DIR}/stress_midnodes.py $<TARGET_FILE:fastcae-cli>)
endif()
