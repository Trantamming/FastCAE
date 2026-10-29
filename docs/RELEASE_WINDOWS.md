# Phát hành bản cài đặt Windows (Inno Setup) và SmartScreen

Tác giả: Trần Minh Tâm (tam.ming.zhan@gmail.com) · MIT.

## 1. Dựng bộ cài `FastCAE-<phiên bản>-Setup.exe`

Cách tự động (khuyên dùng): đẩy mã lên GitHub; job `windows` trong `.github/workflows/build.yml` dựng FastCAE bằng Visual Studio + Qt 6, chạy kiểm thử,
rồi dựng bộ cài bằng Inno Setup và tải lên mục *Artifacts* (`dist-installer/`, kèm file `.sha256`). Không cần cài gì trên máy bạn.

Cách thủ công trên Windows 10/11 x64: cài Visual Studio 2022, Qt 6 (MSVC), CMake, Inno Setup 6, rồi

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\build-installer.ps1 -Version 1.0.0
```

Kết quả: `dist-installer\FastCAE-1.0.0-Setup.exe` và `.sha256`. Tên tác giả, email, bản quyền được ghi vào bộ cài (`AppPublisher`, `AppContact`, thông tin phiên bản của file)
và vào `fastcae.exe` (`packaging/windows/fastcae.rc`).

> Môi trường dựng mã trong phiên làm việc này là Linux, không có Windows, Inno Setup hay Qt, nên **file `.exe` chưa được dựng và cài thử**. Script và workflow đã viết
> nhưng chưa chạy; lần chạy đầu tiên có thể cần chỉnh nhỏ (ví dụ phiên bản Qt trong workflow).

## 2. SmartScreen: điều mã nguồn không tự giải quyết được

Windows SmartScreen cảnh báo ("Windows protected your PC") với file tải về chưa có **uy tín**. Uy tín đến từ **chữ ký số mã (code signing)** do một
tổ chức cấp chứng chỉ (CA) được Microsoft tin cậy cấp cho đúng danh tính của bạn, không đến từ nội dung script cài đặt. Vì vậy:

* Không có cách hợp pháp nào để loại bỏ hoàn toàn cảnh báo chỉ bằng mã nguồn hay cấu hình Inno Setup.
* Điền tên/email tác giả vào bộ cài là cần nhưng **chưa đủ**: SmartScreen chỉ hiện "Nhà phát hành đã xác minh: Trần Minh Tâm" khi file được **ký** bằng chứng chỉ có tên đó.

Các lựa chọn (chi phí và thời hạn thay đổi theo nhà cung cấp, hãy kiểm tra trang hiện hành):

| Lựa chọn | Hiệu quả với SmartScreen | Ghi chú |
|---|---|---|
| Chứng chỉ **EV** (Extended Validation) | Uy tín gần như ngay | đắt nhất, thường cần xác minh pháp nhân |
| Chứng chỉ **OV** (Organization/Individual Validation) | Cảnh báo giảm dần khi có đủ lượt tải "sạch" | rẻ hơn EV; cần thời gian tích luỹ uy tín |
| **Azure Trusted Signing** (dịch vụ ký của Microsoft) | Tốt, nếu đủ điều kiện đăng ký | trả theo tháng; không cần giữ file khoá |
| Không ký | Luôn cảnh báo | người dùng chọn *More info ▸ Run anyway*; công bố SHA-256 để họ đối chiếu |

Bạn cần tự mua/đăng ký chứng chỉ vì nó gắn với danh tính và thanh toán của bạn; tôi không thể làm thay.

## 3. Ký bộ cài khi đã có chứng chỉ

* **Thủ công:** `build-installer.ps1 -SignPfx đường\dẫn\chungchi.pfx` (đặt mật khẩu ở biến môi trường `FASTCAE_SIGN_PASSWORD`),
  hoặc `-SignCommand '<lệnh có $f>'` cho dịch vụ ký khác (ví dụ Azure Trusted Signing với `signtool` + dlib). Script ký từng `.exe` trong `dist\bin`, rồi ký bộ cài và trình gỡ cài đặt
  (`SignTool` / `SignedUninstaller` trong `fastcae.iss`), đều có dấu thời gian (`/tr`, `/td`).
* **GitHub Actions:** thêm hai secret `WINDOWS_PFX_BASE64` (file `.pfx` mã hoá base64) và `WINDOWS_PFX_PASSWORD`; workflow tự ký khi có secret.
  Không bao giờ đưa file `.pfx` hoặc mật khẩu vào kho mã.

## 4. Mẹo giảm cảnh báo ngoài việc ký

* Chỉ phân phối một địa chỉ tải chính thức (GitHub Releases) và công bố mã SHA-256 (script sinh `.sha256`).
* Ký **cả** `fastcae.exe`, `fastcae-cli.exe`, bộ cài và trình gỡ cài đặt; luôn dùng dấu thời gian.
* Giữ nguyên tên file và nhà phát hành giữa các bản để uy tín tích luỹ.
* Gửi bản mới cho Microsoft kiểm tra (Microsoft Security Intelligence, mục báo nhầm) nếu bị đánh dấu sai.
* Cần Python 3 + numpy + scipy trên máy người dùng nếu muốn chạy plugin `my_fem` và Bài 1–3 trong hướng dẫn; bộ cài **không** kèm Python.

## 5. Danh sách kiểm trước khi phát hành

1. Dựng sạch, `ctest` đạt trên Windows (job `windows` trong CI).
2. Cài thử trên Windows 10/11 sạch (máy ảo): mở GUI, Tệp ▸ Ví dụ, F5; mở `examples\tutorial\bai1_dam_fem_tu_viet.fcae`; F1 mở hướng dẫn, đọc chương 18–19.
3. Gỡ cài đặt, kiểm tra không còn file trong `Program Files\FastCAE` và PATH đã được dọn.
4. Ký, tính SHA-256, tải lên Releases kèm `CHANGELOG`.
