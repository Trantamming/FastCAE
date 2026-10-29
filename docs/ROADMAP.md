# Lộ trình

## Định hướng (cập nhật 10/2026)

FastCAE tập trung vào **dựng mô hình + nơi lập trình thuật toán CAE + gọi thư viện bên ngoài + hiển thị kết quả**; xem [ARCHITECTURE.md](ARCHITECTURE.md).
Không nhúng thư viện CAE vào bên trong FastCAE và không mở rộng thêm kho cấu kiện dựng sẵn (dầm/tấm/tường/móng/khung mẫu hiện có được giữ như ví dụ,
không phát triển tiếp). Thứ tự ưu tiên:

| # | Việc | Ghi chú |
|---|---|---|
| ✅ | Hợp đồng mô hình vào – kết quả ra cho elastic/thermal, `.fsolver`, `solver = <id>`, `fastcae-cli model`, `fastcae.model()` | đã có, có test |
| 1 | **Mặt biên theo tên** trong mô hình: danh sách mặt tam giác theo vùng (inlet, wall, load_face...) để áp suất, đối lưu, tiếp xúc, biên CFD | cần cho CFD và tải mặt |
| 2 | **Hợp đồng CFD**: vùng chất lỏng, biên theo tên, tính chất, mô hình rối; kết quả `u, p, k, ...` (ví dụ thuật toán CFD tự viết + cầu nối OpenFOAM/SU2 dùng chung) | CFD 3D hiện chỉ qua `type = cfd3d` riêng |
| 3 | **Kết quả nhiều bước** từ plugin (`<job>_result_0001.vtk...`) và giá trị có tên (`<job>_values.json`) | đọc chuỗi VTK đã có |
| 4 | **GUI**: chọn solver (kể cả plugin) trong bảng Bài toán, hiển thị tập nút/điều kiện biên trên khung 3D, nút "Giải bằng..." | cần dựng lại và thử GUI Qt |
| 5 | Phần tử ngoài tứ diện (hex, lăng trụ) giữ nguyên trong mô hình | hiện được đổi thành tứ diện |
| 6 | Ví dụ adapter cho scikit-fem / FEniCSx / MFEM | chương trình mỏng đọc `.fcmodel.json` |

## Lịch sử các giai đoạn trước

Phạm vi "CAE tổng hợp" (kết cấu + CFD + khí/lỏng/rắn + nhiều định dạng CAD) là chương trình nhiều năm. Hướng đi: **tích hợp các lõi mã nguồn mở đã trưởng thành** và giữ FastCAE làm lớp giao diện, mô hình và điều phối.

| Giai đoạn | Nội dung |
|---|---|
| 0.1 (hiện tại) | lõi lưới, đàn hồi/nhiệt tuyến tính T4+T10, tải nhất quán, CFD 2D, GUI Qt/OpenGL (không VTK), nhập STEP/IGES/BREP/MSH, đóng gói |
| 0.2 | hex bậc 1/2, áp suất mặt, trọng lực; chạy nền (thread) trong GUI; chọn mặt phẳng bằng chuột (✅ có từ 0.1.x; còn mặt cong, cạnh, điểm); tiền điều kiện mạnh hơn (AMG/IC) |
| 0.3 | cầu nối CalculiX (kết cấu phi tuyến, tiếp xúc, động lực, nhiệt–cơ) qua file `.inp`; modal/buckling |
| 0.4 | cầu nối OpenFOAM (CFD 3D, rối, truyền nhiệt đối lưu) và SU2; đọc trường kết quả về lại |
| 0.5 | lắp ráp (assembly) STEP nhiều chi tiết, tiếp xúc, vật liệu thư viện, báo cáo PDF |
| 1.0 | trình thiết kế CAD tham số riêng (sketch + ràng buộc + feature) trên nhân OpenCASCADE |

Về nhập CAD gốc (SLDPRT, IPT, CATPart, PRT, F3D, DWG…): cần SDK thương mại (Open Design Alliance, CAD Exchanger, Datakit…) — có thể thêm như plugin tuỳ chọn nếu có giấy phép; mặc định dùng STEP.
