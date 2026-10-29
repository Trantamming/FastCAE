// FastCAE - C++ day du (co main()): dung thu vien fastcae_core truc tiep, quet chieu cao dam.
#include "sdk/fastcae_all.hpp"

int main() {
    const double L = 6.0, q = 12000.0, b = 0.15, E = 210e9;
    const int n = 8;
    std::printf("%8s %14s %14s %10s\n", "h (m)", "vong FE (mm)", "ly thuyet", "lech %");
    for (int k = 0; k < 6; ++k) {
        const double h = 0.20 + 0.05 * k;
        std::ostringstream c;
        c << "type = frame\nE = " << E << "\nnu = 0.3\nrho = 7850\nsection = S rect " << b << ' ' << h << "\n";
        for (int i = 0; i <= n; ++i) c << "node = " << i + 1 << ' ' << L * i / n << " 0 0\n";
        for (int i = 1; i <= n; ++i) c << "member = " << i << ' ' << i + 1 << " S beam\nudl = " << i << " 0 0 " << -q << "\n";
        c << "support = 1 x y z rx\nsupport = " << n + 1 << " y z\n";
        CaseFile cf;
        cf.parse(c.str());
        std::ostringstream log;
        const auto r = runCase(cf, log, "", false);
        const double exact = 5 * q * std::pow(L, 4) / (384 * E * b * std::pow(h, 3) / 12);
        std::printf("%8.2f %14.3f %14.3f %10.4f\n", h, r.values.at("max_displacement") * 1000, exact * 1000,
                    100 * std::abs(r.values.at("max_displacement") - exact) / exact);
    }
    return 0;
}
