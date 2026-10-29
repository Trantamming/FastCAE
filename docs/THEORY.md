# Cơ sở phương pháp số

**Đàn hồi tuyến tính.** T4 (biến dạng hằng): `Ke = V · Bᵀ D B`. T10 (bậc 2): tích phân Gauss 4 điểm, thứ tự nút giữa cạnh trùng CalculiX C3D10: (0,1),(1,2),(0,2),(0,3),(1,3),(2,3). Ràng buộc chuyển vị khử đối xứng; hệ `K u = f` giải bằng CG tiền điều kiện Jacobi (OpenMP). Von Mises tính tại điểm Gauss/đỉnh, trung bình về nút.

**Tải nhất quán.** Lực trên một tập nút được phân theo diện tích mặt biên: T4 mỗi nút diện tích/3; T10 nút đỉnh 0 và nút giữa cạnh diện tích/3. Không tìm được mặt thì chia đều.

**Truyền nhiệt dừng.** `Ke = k·V·GᵀG` (T4) và bản T10 tương ứng; tái tạo chính xác nghiệm tuyến tính.

**CFD 2D.** Hàm dòng–xoáy `∇²ψ = −ω`, `ω_t + u·∇ω = ν∇²ω`; sai phân trung tâm, Euler tường minh, SOR, biên xoáy Thom. Khoang kín Re = 100, lưới 41×41: u(0.5,0.5) ≈ −0.204 so với −0.2058 (Ghia 1982).

**Chia lưới.** Hộp: Kuhn 6 tứ diện/ô. CAD: Gmsh (OpenCASCADE) trên STEP/IGES/BREP; bề mặt rời (IGES) được khâu (`healShapes`) trước khi chia.

## Kết quả kiểm chứng (`tests/test_main.cpp`, `ctest`)

| Bài | Kết quả |
|---|---|
| Patch test T4, T10 (chuyển vị cưỡng bức) | ứng suất đều, co ngang Poisson đúng ~1e-6 |
| Dầm console 10×2×2 | T4: võng ≈ 0,36 lý thuyết (locking); **T10: 0,99** |
| Dầm console T10 (demo) | \|u\|max 1,905e-4 m vs 1,905e-4 lý thuyết (0,03%) |
| Tấm có lỗ từ STEP (T10, 14509 tứ diện) | von Mises max 42,3 MPa; Peterson Kt=2,23 → ~46,5 MPa (lưới thô, trung bình nút làm giảm đỉnh) |
| Thể tích STEP → lưới | 7,6096e-5 vs 7,598e-5 m³ (+0,15%, đa giác nội tiếp) |
| Khoang kín | khớp Ghia |
