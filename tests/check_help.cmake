# Kiểm tra tài liệu hướng dẫn nhúng trong phần mềm (docs/help/huong_dan.html):
#  - tồn tại, có đủ các chương;
#  - mọi liên kết nội bộ href="#x" đều có thẻ <a name="x"> tương ứng (HelpDialog cuộn theo neo này);
#  - không có thẻ tiêu đề h2 thiếu neo (mục lục tự sinh từ <h2><a name="...">).
# Chạy: cmake -DHELP=<đường dẫn huong_dan.html> -P check_help.cmake
if(NOT EXISTS "${HELP}")
    message(FATAL_ERROR "Không thấy ${HELP}")
endif()
file(READ "${HELP}" html)

string(REGEX MATCHALL "<h2>" h2_all "${html}")
string(REGEX MATCHALL "<h2><a name=\"[^\"]+\">" h2_anchored "${html}")
list(LENGTH h2_all n_all)
list(LENGTH h2_anchored n_anchored)
if(NOT n_all EQUAL n_anchored)
    message(FATAL_ERROR "Có ${n_all} tiêu đề h2 nhưng chỉ ${n_anchored} có neo <a name>")
endif()
if(n_all LESS 17)
    message(FATAL_ERROR "Hướng dẫn phải có ít nhất 17 chương, hiện có ${n_all}")
endif()

string(REGEX MATCHALL "name=\"[^\"]+\"" names "${html}")
set(known "")
foreach(n IN LISTS names)
    string(REGEX REPLACE "name=\"([^\"]+)\"" "\\1" id "${n}")
    list(APPEND known "${id}")
endforeach()
string(REGEX MATCHALL "href=\"#[^\"]+\"" links "${html}")
foreach(l IN LISTS links)
    string(REGEX REPLACE "href=\"#([^\"]+)\"" "\\1" id "${l}")
    list(FIND known "${id}" idx)
    if(idx EQUAL -1)
        message(FATAL_ERROR "Liên kết nội bộ #${id} không có neo tương ứng")
    endif()
endforeach()
foreach(must nang-luc bat-dau file-case gioi-han)   # các neo mà giao diện gọi trực tiếp
    list(FIND known "${must}" idx)
    if(idx EQUAL -1)
        message(FATAL_ERROR "Thiếu neo bắt buộc: ${must}")
    endif()
endforeach()
message(STATUS "Hướng dẫn: ${n_all} chương, ${n_anchored} neo, liên kết nội bộ hợp lệ")
