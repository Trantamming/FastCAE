# Ví dụ dùng FastCAE như thư viện. Khi dùng ngoài dự án:
#   add_subdirectory(FastCAE)                      # hoặc find_package sau khi cài thành phần "sdk"
#   target_link_libraries(my_app PRIVATE fastcae_core)
add_executable(fastcae_example_beam_sweep beam_sweep.cpp)
target_link_libraries(fastcae_example_beam_sweep PRIVATE fastcae_core)
