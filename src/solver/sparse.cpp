// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/sparse.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fastcae {

void CsrMatrix::multiply(const std::vector<double>& x, std::vector<double>& y) const {
    y.resize(n);
#pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        double s = 0;
        for (int p = rowPtr[i]; p < rowPtr[i + 1]; ++p) s += val[p] * x[colIdx[p]];
        y[i] = s;
    }
}

std::vector<double> CsrMatrix::diagonal() const {
    std::vector<double> d(n, 0.0);
    for (int i = 0; i < n; ++i)
        for (int p = rowPtr[i]; p < rowPtr[i + 1]; ++p)
            if (colIdx[p] == i) d[i] += val[p];
    return d;
}

CsrMatrix buildCsr(int n, std::vector<Triplet>& t) {
    std::sort(t.begin(), t.end(), [](const Triplet& a, const Triplet& b) {
        return a.r != b.r ? a.r < b.r : a.c < b.c;
    });
    CsrMatrix A;
    A.n = n;
    A.rowPtr.assign(n + 1, 0);
    for (std::size_t i = 0; i < t.size();) {
        std::size_t j = i;
        double s = 0;
        while (j < t.size() && t[j].r == t[i].r && t[j].c == t[i].c) s += t[j++].v;
        A.colIdx.push_back(t[i].c);
        A.val.push_back(s);
        A.rowPtr[t[i].r + 1]++;
        i = j;
    }
    for (int i = 0; i < n; ++i) A.rowPtr[i + 1] += A.rowPtr[i];
    return A;
}

void applyDirichlet(CsrMatrix& K, std::vector<double>& rhs, const std::vector<int>& dofs,
                    const std::vector<double>& values) {
    std::vector<char> fixed(K.n, 0);
    std::vector<double> uval(K.n, 0.0);
    for (std::size_t i = 0; i < dofs.size(); ++i) {
        fixed[dofs[i]] = 1;
        uval[dofs[i]] = values[i];
    }
    // Move known values to the right-hand side (free rows only).
    for (int i = 0; i < K.n; ++i) {
        if (fixed[i]) continue;
        for (int p = K.rowPtr[i]; p < K.rowPtr[i + 1]; ++p)
            if (fixed[K.colIdx[p]]) rhs[i] -= K.val[p] * uval[K.colIdx[p]];
    }
    for (int i = 0; i < K.n; ++i) {
        for (int p = K.rowPtr[i]; p < K.rowPtr[i + 1]; ++p) {
            const int j = K.colIdx[p];
            if (fixed[i] || fixed[j]) K.val[p] = (i == j) ? 1.0 : 0.0;
        }
        if (fixed[i]) rhs[i] = uval[i];
    }
}

static CgResult solveCgBuiltin(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                               double tol, int maxIter) {
    const int n = A.n;
    if (static_cast<int>(x.size()) != n) x.assign(n, 0.0);
    std::vector<double> r(n), z(n), p(n), Ap(n);
    const auto diag = A.diagonal();
    A.multiply(x, Ap);
    for (int i = 0; i < n; ++i) r[i] = b[i] - Ap[i];

    auto dot = [&](const std::vector<double>& a, const std::vector<double>& c) {
        double s = 0;
#pragma omp parallel for reduction(+ : s) schedule(static)
        for (int i = 0; i < n; ++i) s += a[i] * c[i];
        return s;
    };
    const double bnorm = std::max(std::sqrt(dot(b, b)), 1e-300);
    for (int i = 0; i < n; ++i) z[i] = r[i] / (diag[i] != 0 ? diag[i] : 1.0);
    p = z;
    double rz = dot(r, z);

    CgResult res;
    for (int it = 0; it < maxIter; ++it) {
        res.relResidual = std::sqrt(dot(r, r)) / bnorm;
        if (res.relResidual < tol) {
            res.converged = true;
            break;
        }
        A.multiply(p, Ap);
        const double alpha = rz / dot(p, Ap);
#pragma omp parallel for schedule(static)
        for (int i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * Ap[i];
            z[i] = r[i] / (diag[i] != 0 ? diag[i] : 1.0);
        }
        const double rzNew = dot(r, z);
        const double beta = rzNew / rz;
        rz = rzNew;
#pragma omp parallel for schedule(static)
        for (int i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
        res.iterations = it + 1;
    }
    res.relResidual = std::sqrt(dot(r, r)) / bnorm;
    res.converged = res.relResidual < tol;
    return res;
}

}  // namespace fastcae

namespace fastcae {

// ---------------------------------------------------------------- giải trực tiếp: RCM + Cholesky dải
bool BandedCholesky::factor(const CsrMatrix& A, double maxCost) {
    const int n = A.n;
    n_ = n;
    if (n == 0) return false;
    // --- RCM
    std::vector<int> deg(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) deg[i] = A.rowPtr[i + 1] - A.rowPtr[i];
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(n));
    std::vector<char> seen(static_cast<std::size_t>(n), 0);
    std::vector<int> byDeg(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) byDeg[i] = i;
    std::sort(byDeg.begin(), byDeg.end(), [&](int a, int c) { return deg[a] < deg[c]; });
    for (int s : byDeg) {
        if (seen[s]) continue;
        std::size_t head = order.size();
        order.push_back(s);
        seen[s] = 1;
        while (head < order.size()) {
            const int v = order[head++];
            std::vector<int> nb;
            for (int p = A.rowPtr[v]; p < A.rowPtr[v + 1]; ++p) {
                const int w = A.colIdx[p];
                if (!seen[w]) {
                    seen[w] = 1;
                    nb.push_back(w);
                }
            }
            std::sort(nb.begin(), nb.end(), [&](int a, int c) { return deg[a] < deg[c]; });
            order.insert(order.end(), nb.begin(), nb.end());
        }
    }
    std::reverse(order.begin(), order.end());
    std::vector<int> inv(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) inv[order[i]] = i;
    int bw = 0;
    for (int i = 0; i < n; ++i)
        for (int p = A.rowPtr[i]; p < A.rowPtr[i + 1]; ++p) bw = std::max(bw, std::abs(inv[i] - inv[A.colIdx[p]]));
    const std::size_t width = static_cast<std::size_t>(bw) + 1;
    if (static_cast<double>(n) * static_cast<double>(width) * 8.0 > 1.5e9) return false;   // ~1.5 GB
    if (static_cast<double>(n) * static_cast<double>(width) * static_cast<double>(width) > maxCost) return false;   // quá lâu

    // --- Cholesky L L^T trong dải (L[i][d] = L(i, i-d))
    std::vector<double> L(static_cast<std::size_t>(n) * width, 0.0);
    for (int i = 0; i < n; ++i)
        for (int p = A.rowPtr[i]; p < A.rowPtr[i + 1]; ++p) {
            const int pi = inv[i], pj = inv[A.colIdx[p]];
            if (pj <= pi) L[static_cast<std::size_t>(pi) * width + (pi - pj)] = A.val[p];
        }
    for (int i = 0; i < n; ++i) {
        const int j0 = std::max(0, i - bw);
        for (int j = j0; j <= i; ++j) {
            double sum = L[static_cast<std::size_t>(i) * width + (i - j)];
            const int k0 = std::max(j0, std::max(0, j - bw));
            const double* Li = &L[static_cast<std::size_t>(i) * width];
            const double* Lj = &L[static_cast<std::size_t>(j) * width];
            for (int k = k0; k < j; ++k) sum -= Li[i - k] * Lj[j - k];
            if (j == i) {
                if (!(sum > 0)) return false;
                L[static_cast<std::size_t>(i) * width] = std::sqrt(sum);
            } else {
                L[static_cast<std::size_t>(i) * width + (i - j)] = sum / L[static_cast<std::size_t>(j) * width];
            }
        }
    }
    bw_ = bw;
    order_ = std::move(order);
    L_ = std::move(L);
    return true;
}

void BandedCholesky::solve(const std::vector<double>& b, std::vector<double>& x) const {
    const int n = n_, bw = bw_;
    const std::size_t width = static_cast<std::size_t>(bw) + 1;
    std::vector<double> y(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        double s = b[order_[i]];
        const int j0 = std::max(0, i - bw);
        const double* Li = &L_[static_cast<std::size_t>(i) * width];
        for (int j = j0; j < i; ++j) s -= Li[i - j] * y[j];
        y[i] = s / Li[0];
    }
    for (int i = n - 1; i >= 0; --i) {
        double s = y[i];
        const int jmax = std::min(n - 1, i + bw);
        for (int j = i + 1; j <= jmax; ++j) s -= L_[static_cast<std::size_t>(j) * width + (j - i)] * y[j];
        y[i] = s / L_[static_cast<std::size_t>(i) * width];
    }
    x.assign(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) x[order_[i]] = y[i];
}

static bool solveDirectBanded(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x, CgResult& res,
                              double maxCost) {
    BandedCholesky chol;
    if (!chol.factor(A, maxCost)) return false;
    chol.solve(b, x);
    std::vector<double> Ax(static_cast<std::size_t>(A.n));
    A.multiply(x, Ax);
    double rn = 0, bn = 0;
    for (int i = 0; i < A.n; ++i) {
        rn += (b[i] - Ax[i]) * (b[i] - Ax[i]);
        bn += b[i] * b[i];
    }
    res.iterations = 0;
    res.relResidual = std::sqrt(rn) / std::max(std::sqrt(bn), 1e-300);
    res.converged = res.relResidual < 1e-6;
    return true;
}

}  // namespace fastcae

#ifdef FASTCAE_HAVE_PETSC
#include <petscksp.h>
#include <cstdlib>
#include <mutex>
#endif

namespace fastcae {

namespace {
thread_local std::string g_backend = "auto";
thread_local std::string g_petscOpts;
}  // namespace

bool petscAvailable() {
#ifdef FASTCAE_HAVE_PETSC
    return true;
#else
    return false;
#endif
}

bool setLinearSolver(const std::string& name, const std::string& petscOptions) {
    if (name == "auto" || name == "cg" || name == "direct" || name.empty()) {
        g_backend = name.empty() ? "auto" : name;
        return true;
    }
    if (name == "petsc" && petscAvailable()) {
        g_backend = "petsc";
        g_petscOpts = petscOptions;
        return true;
    }
    return false;
}

std::string linearSolverName() { return g_backend; }

#ifdef FASTCAE_HAVE_PETSC
// PETSc: CG (hoặc tuỳ chọn người dùng) trên ma trận CSR. Sao chép chỉ số sang PetscInt để không phụ thuộc kích thước PetscInt.
static CgResult solvePetscImpl(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x, double tol, int maxIter) {
    static std::once_flag once;
    std::call_once(once, [] {
        PetscInitializeNoArguments();
        std::atexit([] { PetscFinalize(); });
    });
    CgResult res;
    const PetscInt n = A.n;
    std::vector<PetscInt> ia(A.rowPtr.begin(), A.rowPtr.end()), ja(A.colIdx.begin(), A.colIdx.end());
    std::vector<PetscScalar> va(A.val.begin(), A.val.end());
    if (static_cast<PetscInt>(x.size()) != n) x.assign(static_cast<std::size_t>(n), 0.0);

    Mat M;
    Vec vb, vx;
    KSP ksp;
    PC pc;
    if (MatCreateSeqAIJWithArrays(PETSC_COMM_SELF, n, n, ia.data(), ja.data(), va.data(), &M)) return res;
    MatSetOption(M, MAT_SPD, PETSC_TRUE);
    VecCreateSeqWithArray(PETSC_COMM_SELF, 1, n, b.data(), &vb);
    VecCreateSeqWithArray(PETSC_COMM_SELF, 1, n, x.data(), &vx);   // x vừa là nghiệm đầu vừa nhận kết quả
    KSPCreate(PETSC_COMM_SELF, &ksp);
    KSPSetOperators(ksp, M, M);
    KSPSetType(ksp, KSPCG);
    KSPGetPC(ksp, &pc);
    PCSetType(pc, PCICC);
    KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED);
    KSPSetTolerances(ksp, tol, PETSC_DEFAULT, PETSC_DEFAULT, maxIter);
    KSPSetInitialGuessNonzero(ksp, PETSC_TRUE);
    if (!g_petscOpts.empty()) PetscOptionsInsertString(nullptr, g_petscOpts.c_str());
    KSPSetFromOptions(ksp);
    const PetscErrorCode err = KSPSolve(ksp, vb, vx);
    PetscInt its = 0;
    KSPConvergedReason reason;
    KSPGetIterationNumber(ksp, &its);
    KSPGetConvergedReason(ksp, &reason);
    res.iterations = static_cast<int>(its);
    res.converged = (err == 0) && reason > 0;
    // ||b - A x|| / ||b|| tính lại bằng chính ma trận để cùng thước đo với bộ CG tự viết
    std::vector<double> Ax(static_cast<std::size_t>(n));
    A.multiply(x, Ax);
    double rn = 0, bn = 0;
    for (PetscInt i = 0; i < n; ++i) {
        rn += (b[i] - Ax[i]) * (b[i] - Ax[i]);
        bn += b[i] * b[i];
    }
    res.relResidual = std::sqrt(rn) / std::max(std::sqrt(bn), 1e-300);
    KSPDestroy(&ksp);
    VecDestroy(&vb);
    VecDestroy(&vx);
    MatDestroy(&M);
    return res;
}
#endif

CgResult solveCg(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x, double tol, int maxIter) {
#ifdef FASTCAE_HAVE_PETSC
    if (g_backend == "petsc") return solvePetscImpl(A, b, x, tol, maxIter);
#endif
    if (g_backend == "direct" || g_backend == "auto") {
        // auto: chỉ giải trực tiếp khi chi phí ước tính (n * băng thông^2) vừa phải, ngược lại dùng CG
        CgResult r;
        const double maxCost = g_backend == "direct" ? 1e300 : 1.5e11;
        if (solveDirectBanded(A, b, x, r, maxCost) && r.converged) return r;
        // không xác định dương hoặc quá lớn: lùi về CG
    }
    return solveCgBuiltin(A, b, x, tol, maxIter);
}

}  // namespace fastcae
