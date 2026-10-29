// Thu muc lam viec ghi duoc cho nguoi dung (khong bao gio ghi vao Program Files).
// FastCAE - Tran Minh Tam (MIT)
#pragma once
#include <string>

namespace fastcae {

// Thu muc goc ghi duoc: Windows %LOCALAPPDATA%\FastCAE\work, Linux $XDG_DATA_HOME|~/.local/share/FastCAE/work,
// du phong: thu muc tam he thong. Co the ghi de bang bien moi truong FASTCAE_WORK.
std::string userWorkRoot();

// true neu tao duoc thu muc va ghi duoc file thu vao do.
bool isWritableDir(const std::string& dir);

// Chon thu muc lam viec ghi duoc.
//  - requested: gia tri work_dir trong case (rong = tu chon)
//  - baseDir:   thu muc chua file case (rong neu case chua luu)
//  - sub:       ten thu muc con mac dinh (vd "ccx_shell")
// Neu thu muc muon dung khong ghi duoc (vd C:\Program Files\...), roi ve userWorkRoot()/sub.
std::string resolveWorkDir(const std::string& requested, const std::string& baseDir, const std::string& sub);

// Thu muc mac dinh cho hop thoai Mo/Luu (Documents/FastCAE neu co, neu khong thi thu muc nguoi dung).
std::string userDocumentsDir();

}  // namespace fastcae
