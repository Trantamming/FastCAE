// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "cad/frame_templates.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace fastcae {

namespace {

using P = std::vector<ShapeParam>;

std::vector<ShapeInfo> makeCatalog() {
    std::vector<ShapeInfo> c;
    c.push_back({"simple_beam", "Dầm đơn giản chịu tải phân bố", "Dầm / dàn / khung 1D",
                 "Dầm thép chữ I trên hai gối (gối cố định và gối con lăn), tải phân bố đều.",
                 P{{"L", "Nhịp L (m)", 6.0}, {"q", "Tải phân bố q (N/m)", 10000}, {"n", "Số phần tử", 8}}});
    c.push_back({"cantilever_beam", "Dầm công xôn", "Dầm / dàn / khung 1D",
                 "Dầm thép chữ I ngàm một đầu, lực tập trung ở đầu tự do.",
                 P{{"L", "Chiều dài L (m)", 3.0}, {"P", "Lực P (N)", 5000}, {"n", "Số phần tử", 6}}});
    c.push_back({"portal_frame", "Khung cổng (một tầng, một nhịp)", "Dầm / dàn / khung 1D",
                 "Hai cột ngàm chân, dầm ngang; tải đứng phân bố trên dầm và lực ngang ở đỉnh cột trái.",
                 P{{"B", "Nhịp B (m)", 6.0}, {"H", "Chiều cao cột H (m)", 4.0}, {"q", "Tải trên dầm q (N/m)", 10000},
                   {"F", "Lực ngang F (N)", 10000}, {"n", "Số phần tử mỗi thanh", 4}}});
    c.push_back({"multistorey_frame", "Khung nhiều tầng, nhiều nhịp (bê tông cốt thép)", "Dầm / dàn / khung 1D",
                 "Cột 0,4 x 0,4 m, dầm 0,3 x 0,5 m (E = 30 GPa); tải sàn phân bố trên dầm, lực gió ngang mỗi tầng.",
                 P{{"nb", "Số nhịp", 2}, {"ns", "Số tầng", 3}, {"bay", "Bề rộng nhịp (m)", 5.0}, {"hs", "Cao tầng (m)", 3.5},
                   {"q", "Tải sàn lên dầm q (N/m)", 15000}, {"F", "Lực gió mỗi tầng (N)", 10000}}});
    c.push_back({"warren_truss", "Dàn Warren (cầu / sàn)", "Dầm / dàn / khung 1D",
                 "Dàn thép ống, hai biên song song, thanh xiên xen kẽ; tải tập trung ở các nút biên dưới.",
                 P{{"L", "Nhịp L (m)", 12.0}, {"h", "Chiều cao h (m)", 2.0}, {"n", "Số khoang (chẵn)", 6}, {"P", "Tải mỗi nút P (N)", 10000}}});
    c.push_back({"pratt_truss", "Dàn Pratt (cầu / mái)", "Dầm / dàn / khung 1D",
                 "Dàn thép ống có thanh đứng, thanh xiên dốc xuống về giữa nhịp; tải ở các nút biên trên.",
                 P{{"L", "Nhịp L (m)", 12.0}, {"h", "Chiều cao h (m)", 2.0}, {"n", "Số khoang (chẵn)", 6}, {"P", "Tải mỗi nút P (N)", 10000}}});
    c.push_back({"roof_truss", "Dàn mái tam giác", "Dầm / dàn / khung 1D",
                 "Dàn mái thép ống: biên trên dốc, biên dưới ngang, có thanh đứng và thanh xiên; tải ở các nút biên trên.",
                 P{{"L", "Nhịp L (m)", 8.0}, {"rise", "Độ cao mái (m)", 2.0}, {"n", "Số khoang (chẵn)", 4}, {"P", "Tải mỗi nút P (N)", 5000}}});
    return c;
}

struct Builder {
    std::ostringstream o;
    int nodeId = 0, memberId = 0;
    Builder() { o.precision(10); }
    int node(double x, double y, double z) {
        o << "node = " << ++nodeId << ' ' << x << ' ' << y << ' ' << z << "\n";
        return nodeId;
    }
    int member(int a, int b, const char* sec, const char* kind = "beam") {
        o << "member = " << a << ' ' << b << ' ' << sec << ' ' << kind << "\n";
        return ++memberId;
    }
};

const char* kIBeam = "section = IPE ibeam 0.15 0.30 0.0107 0.0071   # chu I: b h tf tw (m)\n";

}  // namespace

const std::vector<ShapeInfo>& frameTemplateCatalog() {
    static const std::vector<ShapeInfo> c = makeCatalog();
    return c;
}

const ShapeInfo* findFrameTemplate(const std::string& name) {
    for (const auto& s : frameTemplateCatalog())
        if (s.name == name) return &s;
    return nullptr;
}

std::string frameTemplateText(const std::string& name, const std::vector<double>& p, std::string& err) {
    const ShapeInfo* info = findFrameTemplate(name);
    if (!info) {
        err = "Khong co mau: " + name;
        return {};
    }
    if (p.size() < info->params.size()) {
        err = "Thieu tham so";
        return {};
    }
    for (std::size_t i = 0; i < info->params.size(); ++i)
        if (!(p[i] > 0)) {
            err = "Tham so '" + info->params[i].label + "' phai duong";
            return {};
        }
    Builder b;
    b.o << "# " << info->label << " (mau khung/dan FastCAE, tac gia Tran Minh Tam)\n# " << info->note << "\n";
    b.o << "type = frame\nE = 210e9\nnu = 0.3\nrho = 7850\nyield = 235e6   # gioi han chay thep CCT34 (Pa): tinh he so su dung\n";

    if (name == "simple_beam" || name == "cantilever_beam") {
        const double L = p[0];
        const int n = std::clamp(static_cast<int>(std::lround(p[2])), 1, 200);
        b.o << kIBeam;
        for (int i = 0; i <= n; ++i) b.node(L * i / n, 0, 0);
        for (int i = 1; i <= n; ++i) b.member(i, i + 1, "IPE");
        if (name == "simple_beam") {
            b.o << "support = 1 x y z rx\nsupport = " << n + 1 << " y z\n";
            for (int i = 1; i <= n; ++i) b.o << "udl = " << i << " 0 0 " << -p[1] << "\n";
        } else {
            b.o << "support = 1 fixed\nnload = " << n + 1 << " 0 0 " << -p[1] << "\n";
        }
    } else if (name == "portal_frame") {
        const double B = p[0], H = p[1];
        const int n = std::clamp(static_cast<int>(std::lround(p[4])), 1, 50);
        b.o << kIBeam;
        std::vector<int> left, right, top;
        for (int i = 0; i <= n; ++i) left.push_back(b.node(0, 0, H * i / n));
        for (int i = 0; i <= n; ++i) right.push_back(b.node(B, 0, H * i / n));
        for (int i = 1; i < n * 2; ++i) top.push_back(b.node(B * i / (2 * n), 0, H));
        for (int i = 0; i < n; ++i) b.member(left[i], left[i + 1], "IPE");
        for (int i = 0; i < n; ++i) b.member(right[i], right[i + 1], "IPE");
        std::vector<int> beamNodes;
        beamNodes.push_back(left[n]);
        for (int id : top) beamNodes.push_back(id);
        beamNodes.push_back(right[n]);
        std::vector<int> beamMembers;
        for (std::size_t i = 0; i + 1 < beamNodes.size(); ++i) beamMembers.push_back(b.member(beamNodes[i], beamNodes[i + 1], "IPE"));
        b.o << "support = " << left[0] << " fixed\nsupport = " << right[0] << " fixed\n";
        for (int mid : beamMembers) b.o << "udl = " << mid << " 0 0 " << -p[2] << "\n";
        b.o << "nload = " << left[n] << " " << p[3] << " 0 0\n";
    } else if (name == "multistorey_frame") {
        const int nb = std::clamp(static_cast<int>(std::lround(p[0])), 1, 12), ns = std::clamp(static_cast<int>(std::lround(p[1])), 1, 30);
        const double bay = p[2], hs = p[3];
        b.o.str("");
        b.o << "# " << info->label << " (mau khung FastCAE, tac gia Tran Minh Tam)\n# " << info->note << "\n";
        b.o << "type = frame\nE = 30e9\nnu = 0.2\nrho = 2500\n";
        b.o << "section = COT rect 0.4 0.4\nsection = DAM rect 0.3 0.5\n";
        std::vector<std::vector<int>> id(static_cast<std::size_t>(ns + 1), std::vector<int>(static_cast<std::size_t>(nb + 1)));
        for (int s = 0; s <= ns; ++s)
            for (int i = 0; i <= nb; ++i) id[s][i] = b.node(bay * i, 0, hs * s);
        for (int s = 0; s < ns; ++s)
            for (int i = 0; i <= nb; ++i) b.member(id[s][i], id[s + 1][i], "COT");
        std::vector<int> beams;
        for (int s = 1; s <= ns; ++s)
            for (int i = 0; i < nb; ++i) beams.push_back(b.member(id[s][i], id[s][i + 1], "DAM"));
        for (int i = 0; i <= nb; ++i) b.o << "support = " << id[0][i] << " fixed\n";
        for (int m : beams) b.o << "udl = " << m << " 0 0 " << -p[4] << "\n";
        for (int s = 1; s <= ns; ++s) b.o << "nload = " << id[s][0] << " " << p[5] << " 0 0\n";
    } else if (name == "warren_truss" || name == "pratt_truss" || name == "roof_truss") {
        int n = std::max(2, static_cast<int>(std::lround(p[2])));
        if (n % 2) ++n;
        const double L = p[0], h = p[1];
        b.o << "section = ONG pipe 0.1 0.005   # ong thep d t (m)\n";
        std::vector<int> bot(static_cast<std::size_t>(n + 1));
        for (int i = 0; i <= n; ++i) bot[i] = b.node(L * i / n, 0, 0);
        for (int i = 0; i < n; ++i) b.member(bot[i], bot[i + 1], "ONG", "truss");
        std::vector<int> top;
        if (name == "warren_truss") {
            for (int i = 0; i < n; ++i) top.push_back(b.node(L * (i + 0.5) / n, 0, h));
            for (int i = 0; i + 1 < n; ++i) b.member(top[i], top[i + 1], "ONG", "truss");
            for (int i = 0; i < n; ++i) {
                b.member(bot[i], top[i], "ONG", "truss");
                b.member(top[i], bot[i + 1], "ONG", "truss");
            }
            for (int i = 1; i < n; ++i) b.o << "nload = " << bot[i] << " 0 0 " << -p[3] << "\n";
        } else if (name == "pratt_truss") {
            for (int i = 0; i <= n; ++i) top.push_back(b.node(L * i / n, 0, h));
            for (int i = 0; i < n; ++i) b.member(top[i], top[i + 1], "ONG", "truss");
            for (int i = 0; i <= n; ++i) b.member(bot[i], top[i], "ONG", "truss");
            for (int i = 0; i < n; ++i) {
                if (i < n / 2) b.member(top[i], bot[i + 1], "ONG", "truss");
                else b.member(bot[i], top[i + 1], "ONG", "truss");
            }
            for (int i = 1; i < n; ++i) b.o << "nload = " << top[i] << " 0 0 " << -p[3] << "\n";
        } else {   // roof_truss: biên trên dốc, đỉnh ở giữa
            const double rise = p[1];
            top.assign(static_cast<std::size_t>(n + 1), 0);
            top[0] = bot[0];
            top[n] = bot[n];
            for (int i = 1; i < n; ++i) {
                const double x = L * i / n;
                top[i] = b.node(x, 0, rise * (1.0 - std::abs(2 * x / L - 1.0)));
            }
            for (int i = 0; i < n; ++i) b.member(top[i], top[i + 1], "ONG", "truss");
            for (int i = 1; i < n; ++i) b.member(bot[i], top[i], "ONG", "truss");
            for (int i = 1; i < n; ++i) {
                if (i < n / 2) b.member(bot[i + 1], top[i], "ONG", "truss");   // xiên dốc lên về đỉnh
                else if (i > n / 2) b.member(bot[i - 1], top[i], "ONG", "truss");
            }
            for (int i = 1; i < n; ++i) b.o << "nload = " << top[i] << " 0 0 " << -p[3] << "\n";
        }
        b.o << "support = " << bot[0] << " pinned\nsupport = " << bot[n] << " y z\n";
    }
    b.o << "output = " << name << ".vtk\n";
    return b.o.str();
}


// ---------------------------------------------------------------- tấm / vỏ
const std::vector<ShapeInfo>& shellTemplateCatalog() {
    static const std::vector<ShapeInfo> c = {
        {"plate_simply_supported", "Tấm vuông tựa đơn bốn cạnh, áp suất đều", "Tấm / vỏ (CalculiX)",
         "Phần tử vỏ S4. Lý thuyết Kirchhoff: w_giữa = 0,00406 q a^4 / D.",
         P{{"a", "Cạnh a (m)", 1.0}, {"t", "Chiều dày t (m)", 0.01}, {"q", "Áp suất q (Pa)", 1000}, {"n", "Số ô mỗi cạnh", 24}}},
        {"plate_clamped", "Tấm vuông ngàm bốn cạnh, áp suất đều", "Tấm / vỏ (CalculiX)",
         "Phần tử vỏ S4. Lý thuyết Kirchhoff: w_giữa = 0,00126 q a^4 / D.",
         P{{"a", "Cạnh a (m)", 1.0}, {"t", "Chiều dày t (m)", 0.01}, {"q", "Áp suất q (Pa)", 1000}, {"n", "Số ô mỗi cạnh", 24}}},
        {"plate_cantilever", "Tấm công xôn, áp suất đều", "Tấm / vỏ (CalculiX)",
         "Ngàm một cạnh x = 0, ba cạnh còn lại tự do; áp suất hướng xuống.",
         P{{"L", "Dài L (m)", 1.0}, {"W", "Rộng W (m)", 0.5}, {"t", "Chiều dày t (m)", 0.01}, {"q", "Áp suất q (Pa)", 1000}, {"n", "Số ô theo L", 24}}},
        {"cylinder_tank", "Bồn trụ chịu áp suất trong (đáy ngàm, đỉnh hở)", "Tấm / vỏ (CalculiX)",
         "Ứng suất vòng xa đáy xấp xỉ p R / t (vỏ mỏng).",
         P{{"R", "Bán kính R (m)", 1.0}, {"L", "Chiều cao L (m)", 3.0}, {"t", "Chiều dày t (m)", 0.01}, {"p", "Áp suất trong p (Pa)", 100000},
           {"n", "Số ô quanh chu vi", 24}}},
    };
    return c;
}

const ShapeInfo* findShellTemplate(const std::string& name) {
    for (const auto& s : shellTemplateCatalog())
        if (s.name == name) return &s;
    return nullptr;
}

std::string shellTemplateText(const std::string& name, const std::vector<double>& p, std::string& err) {
    const ShapeInfo* info = findShellTemplate(name);
    if (!info) {
        err = "Khong co mau: " + name;
        return {};
    }
    if (p.size() < info->params.size()) {
        err = "Thieu tham so";
        return {};
    }
    for (std::size_t i = 0; i < info->params.size(); ++i)
        if (!(p[i] > 0)) {
            err = "Tham so '" + info->params[i].label + "' phai duong";
            return {};
        }
    std::ostringstream o;
    o.precision(10);
    o << "# " << info->label << " (mau tam/vo FastCAE, tac gia Tran Minh Tam)\n# " << info->note << "\n";
    o << "# Can CalculiX (ccx) trong PATH, bien FASTCAE_CCX hoac ccx_path = ... (xem Cong cu > Quan ly solver)\n";
    o << "type = shell\nE = 210e9\nnu = 0.3\nrho = 7850\n";
    const int n = std::clamp(static_cast<int>(std::lround(p.back())), 2, 200);
    if (name == "plate_simply_supported" || name == "plate_clamped") {
        o << "geometry = shell_plate " << p[0] << ' ' << p[0] << ' ' << p[1] << ' ' << n << ' ' << n << "\n";
        if (name == "plate_simply_supported") o << "fix = xmin x y z\nfix = xmax z\nfix = ymin z\nfix = ymax z\n";
        else o << "fix = xmin\nfix = xmax\nfix = ymin\nfix = ymax\n";
        o << "pressure = " << p[2] << "\n";
    } else if (name == "plate_cantilever") {
        const int ny = std::max(2, static_cast<int>(std::lround(n * p[1] / p[0])));
        o << "geometry = shell_plate " << p[0] << ' ' << p[1] << ' ' << p[2] << ' ' << n << ' ' << ny << "\nfix = xmin\npressure = " << p[3] << "\n";
    } else if (name == "cylinder_tank") {
        const int nc = std::max(8, n);
        const double dz = 2 * 3.14159265358979 * p[0] / nc;
        const int nl = std::clamp(static_cast<int>(std::lround(p[1] / dz)), 2, 200);
        o << "geometry = shell_cylinder " << p[0] << ' ' << p[1] << ' ' << p[2] << ' ' << nc << ' ' << nl << "\nfix = zmin\npressure = " << p[3] << "\n";
    }
    o << "output = " << name << ".vtk\n";
    return o.str();
}

}  // namespace fastcae
