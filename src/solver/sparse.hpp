// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#pragma once
#include <string>
#include <vector>

namespace fastcae {

struct Triplet {
    int r, c;
    double v;
};

// Compressed sparse row matrix (square).
struct CsrMatrix {
    int n = 0;
    std::vector<int> rowPtr, colIdx;
    std::vector<double> val;

    void multiply(const std::vector<double>& x, std::vector<double>& y) const;
    std::vector<double> diagonal() const;
};

// Duplicates are summed.
CsrMatrix buildCsr(int n, std::vector<Triplet>& triplets);

// Symmetric elimination of Dirichlet DOFs: rows/cols are replaced by identity
// and the right-hand side is corrected.
void applyDirichlet(CsrMatrix& K, std::vector<double>& rhs, const std::vector<int>& dofs,
                    const std::vector<double>& values);

struct CgResult {
    int iterations = 0;
    double relResidual = 0;
    bool converged = false;
};

// Phân tích Cholesky dải sau khi sắp xếp lại RCM, dùng lại được cho nhiều vế phải (giải trực tiếp ma trận đối xứng xác định dương).
class BandedCholesky {
public:
    // false nếu không xác định dương, quá nhiều bộ nhớ, hoặc chi phí ước tính n*băng thông^2 > maxCost.
    bool factor(const CsrMatrix& A, double maxCost = 1e300);
    void solve(const std::vector<double>& b, std::vector<double>& x) const;
    int bandwidth() const { return bw_; }

private:
    int n_ = 0, bw_ = 0;
    std::vector<int> order_;
    std::vector<double> L_;
};

// Chọn bộ giải tuyến tính cho các solver nội bộ (theo từng luồng):
//   "auto"   (mặc định) giải trực tiếp (Cholesky dải + RCM) nếu chi phí ước tính vừa phải, ngược lại CG
//   "direct" luôn giải trực tiếp (nhanh và chắc với hệ nhỏ/vừa, tốn bộ nhớ khi hệ lớn)
//   "cg"     Jacobi-CG tự viết (luôn có; chậm với dầm/vỏ mỏng)
//   "petsc" PETSc (BSD-2): CG + tiền điều kiện ICC theo mặc định, chỉnh qua `petscOptions`
//           (chuỗi tuỳ chọn PETSc, ví dụ "-ksp_type cg -pc_type gamg"). Chỉ có khi build với FASTCAE_WITH_PETSC.
// Trả về false nếu tên không hợp lệ hoặc bản build không có PETSc.
bool petscAvailable();
bool setLinearSolver(const std::string& name, const std::string& petscOptions = "");
std::string linearSolverName();

// Giải A x = b (A đối xứng xác định dương) bằng bộ giải đã chọn (mặc định Jacobi-CG).
CgResult solveCg(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                 double tol = 1e-10, int maxIter = 20000);

}  // namespace fastcae
