// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
//
// Ví dụ dùng FastCAE như thư viện C++: quét chiều cao dầm chữ nhật đơn giản bằng solver khung 1D,
// so với lý thuyết 5qL^4/(384EI). Biên dịch cùng dự án (mặc định bật FASTCAE_BUILD_EXAMPLES) hoặc xem docs/SCRIPTING.md.
#include <cmath>
#include <cstdio>
#include <sstream>

#include "app/case_runner.hpp"
#include "core/case_file.hpp"
#include "solver/frame.hpp"

int main() {
    using namespace fastcae;
    const double L = 6.0, q = 12000.0, b = 0.15, E = 210e9;
    const int n = 8;
    std::printf("%8s %14s %14s %10s\n", "h (m)", "vong FE (mm)", "ly thuyet", "lech %");
    for (int k = 0; k < 7; ++k) {
        const double h = 0.20 + 0.05 * k;

        // Cách 1: dựng văn bản case rồi chạy bằng trình chạy chung (giống file .fcae)
        std::ostringstream c;
        c << "type = frame\nE = " << E << "\nnu = 0.3\nrho = 7850\nsection = S rect " << b << ' ' << h << "\n";
        for (int i = 0; i <= n; ++i) c << "node = " << i + 1 << ' ' << L * i / n << " 0 0\n";
        for (int i = 1; i <= n; ++i) c << "member = " << i << ' ' << i + 1 << " S beam\nudl = " << i << " 0 0 " << -q << "\n";
        c << "support = 1 x y z rx\nsupport = " << n + 1 << " y z\n";
        CaseFile cf;
        cf.parse(c.str());
        std::ostringstream log;
        const RunReport rep = runCase(cf, log, "", /*writeOutput=*/false);
        if (!rep.ok) { std::fprintf(stderr, "loi: %s\n", rep.message.c_str()); return 1; }

        // Cách 2: gọi thẳng API solver khung (không qua văn bản) — ở đây chỉ đọc lại mô hình để minh hoạ
        FrameModel fm;
        std::string err;
        if (!readFrameModel(cf, fm, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        const FrameResult fr = solveFrame(fm);

        const double exact = 5 * q * std::pow(L, 4) / (384 * E * b * h * h * h / 12);
        std::printf("%8.2f %14.4f %14.4f %10.5f   (truc tiep: %.4f mm)\n", h, rep.values.at("max_displacement") * 1000, exact * 1000,
                    100 * std::abs(rep.values.at("max_displacement") - exact) / exact, fr.maxDisp * 1000);
    }
    return 0;
}
