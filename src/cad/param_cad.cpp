// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "cad/param_cad.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

#include "cad/cad_import.hpp"
#include "cad/sketch_solver.hpp"
#include "solver/external_run.hpp"

namespace fastcae {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- biểu thức
struct ExprParser {
    const std::string& s;
    const std::map<std::string, double>& vars;
    std::size_t i = 0;

    [[noreturn]] void fail(const std::string& m) const { throw std::runtime_error(m + " (trong biểu thức \"" + s + "\")"); }
    void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }

    double expr() {
        double v = term();
        for (;;) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
    double term() {
        double v = unary();
        for (;;) {
            if (eat('*')) v *= unary();
            else if (eat('/')) {
                const double d = unary();
                if (d == 0) fail("chia cho 0");
                v /= d;
            } else return v;
        }
    }
    double unary() {
        if (eat('-')) return -unary();
        if (eat('+')) return unary();
        return power();
    }
    double power() {
        const double b = primary();
        if (eat('^')) return std::pow(b, unary());
        return b;
    }
    double primary() {
        ws();
        if (i >= s.size()) fail("thiếu toán hạng");
        if (eat('(')) {
            const double v = expr();
            if (!eat(')')) fail("thiếu ')'");
            return v;
        }
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isdigit(c) || c == '.') {
            std::size_t used = 0;
            double v = 0;
            try { v = std::stod(s.substr(i), &used); } catch (...) { fail("số không hợp lệ"); }
            i += used;
            return v;
        }
        if (std::isalpha(c) || c == '_') {
            std::size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            const std::string name = s.substr(i, j - i);
            i = j;
            ws();
            if (i < s.size() && s[i] == '(') {
                ++i;
                std::vector<double> a;
                if (!eat(')')) {
                    do a.push_back(expr()); while (eat(','));
                    if (!eat(')')) fail("thiếu ')' sau hàm " + name);
                }
                return call(name, a);
            }
            if (name == "pi") return kPi;
            const auto it = vars.find(name);
            if (it == vars.end()) fail("tham số chưa khai báo: " + name);
            return it->second;
        }
        fail(std::string("ký tự lạ '") + s[i] + "'");
    }
    double call(const std::string& f, const std::vector<double>& a) {
        auto need = [&](std::size_t n) { if (a.size() != n) fail("hàm " + f + " cần " + std::to_string(n) + " đối số"); };
        if (f == "sin") { need(1); return std::sin(a[0]); }
        if (f == "cos") { need(1); return std::cos(a[0]); }
        if (f == "tan") { need(1); return std::tan(a[0]); }
        if (f == "asin") { need(1); return std::asin(a[0]); }
        if (f == "acos") { need(1); return std::acos(a[0]); }
        if (f == "atan") { need(1); return std::atan(a[0]); }
        if (f == "sqrt") { need(1); if (a[0] < 0) fail("sqrt của số âm"); return std::sqrt(a[0]); }
        if (f == "abs") { need(1); return std::fabs(a[0]); }
        if (f == "exp") { need(1); return std::exp(a[0]); }
        if (f == "log") { need(1); if (a[0] <= 0) fail("log của số không dương"); return std::log(a[0]); }
        if (f == "rad") { need(1); return a[0] * kPi / 180.0; }
        if (f == "deg") { need(1); return a[0] * 180.0 / kPi; }
        if (f == "min") { need(2); return std::min(a[0], a[1]); }
        if (f == "max") { need(2); return std::max(a[0], a[1]); }
        if (f == "pow") { need(2); return std::pow(a[0], a[1]); }
        fail("hàm không có: " + f);
    }
};

double evalOrThrow(const std::string& e, const std::map<std::string, double>& vars) {
    ExprParser p{e, vars};
    const double v = p.expr();
    p.ws();
    if (p.i != e.size()) p.fail("thừa ký tự sau biểu thức");
    if (!std::isfinite(v)) p.fail("kết quả không hữu hạn");
    return v;
}

// ---------------------------------------------------------------- biên dịch
struct Sketch {
    std::string plane;  // xy | yz | xz
    double offset = 0;
    struct Seg {                        // một đoạn của vòng dây (wire): thẳng hoặc cung, đã định hướng theo chiều đi
        bool arc = false;
        int s = 0, e = 0, c = -1;        // chỉ số điểm đầu / cuối / tâm trong phác thảo
        double sx = 0, sy = 0, ex = 0, ey = 0, cx = 0, cy = 0;
        bool ccw = true;                 // chiều đi trên cung ngược chiều kim đồng hồ (trong hệ u,v của phác thảo)
    };
    struct Loop { bool circle = false; std::vector<double> v; std::vector<Seg> segs; };   // circle: cx cy r; đa giác: x1 y1 x2 y2 ...; segs: vòng dây thẳng+cung
    std::vector<Loop> loops;                                         // vòng đầu = biên ngoài, các vòng sau = lỗ
};

// Phác thảo có ràng buộc đang được khai báo (khối csketch ... end)
struct CSketch {
    std::string name, plane;
    double offset = 0;
    SketchSystem sys;
    std::map<std::string, int> pts, lines, circs;
    struct ArcDef { int circ, c, p, q; };
    std::map<std::string, ArcDef> arcs;
    std::vector<std::pair<std::string, bool>> loopOrder;   // (tên đầu tiên của vòng, là tròn?) – chỉ để báo lỗi
    struct WireItem { bool arc; std::string name; };
    struct WireDef { std::vector<WireItem> items; };
    std::vector<std::pair<int, WireDef>> wires;   // (vị trí trong loops, định nghĩa)
    struct LoopDef { bool circle; std::vector<int> ids; };
    std::vector<LoopDef> loops;
};

struct Compiler {
    std::map<std::string, double> vars;
    const std::map<std::string, double>& overrides;
    double userMeshSize;
    int order;

    std::ostringstream geo;
    std::set<std::string> alive;           // thân đang tồn tại
    std::vector<std::string> creationOrder;
    std::map<std::string, Sketch> sketches;
    std::string explicitResult;
    std::vector<PostOp> post;
    std::vector<std::string> warnings;
    std::unique_ptr<CSketch> csk;
    int nextTag = 1;
    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    int line = 0;

    Compiler(const std::map<std::string, double>& ov, double h, int o) : overrides(ov), userMeshSize(h), order(o) {}

    [[noreturn]] void fail(const std::string& m) const { throw std::runtime_error("dòng " + std::to_string(line) + ": " + m); }

    static bool validName(const std::string& n) {
        if (n.empty() || !(std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_')) return false;
        return std::all_of(n.begin(), n.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    }
    double num(const std::string& e) {
        try { return evalOrThrow(e, vars); } catch (const std::exception& ex) { fail(ex.what()); }
    }
    std::vector<double> nums(const std::vector<std::string>& t, std::size_t from, std::size_t count, const char* usage) {
        if (t.size() < from + count) fail(std::string("thiếu đối số. Cú pháp: ") + usage);
        std::vector<double> r;
        for (std::size_t k = 0; k < count; ++k) r.push_back(num(t[from + k]));
        return r;
    }
    void grow(double x, double y, double z, double r = 0) {
        const double p[3] = {x, y, z};
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], p[k] - r); hi[k] = std::max(hi[k], p[k] + r); }
    }
    void declare(const std::string& n) {
        if (!validName(n)) fail("tên không hợp lệ: \"" + n + "\" (chỉ chữ, số, gạch dưới; bắt đầu bằng chữ)");
        if (alive.count(n) || sketches.count(n)) fail("tên đã dùng: " + n);
        for (const auto& c : creationOrder) if (c == n) fail("tên đã dùng: " + n);
    }
    void make(const std::string& n) { declare(n); alive.insert(n); creationOrder.push_back(n); }
    void need(const std::string& n) {
        if (!alive.count(n)) fail("tính năng \"" + n + "\" không tồn tại hoặc đã bị dùng cho một phép toán khác");
    }
    static std::string V(const std::string& n) { return "b_" + n; }
    static std::string fmt(double v) { std::ostringstream o; o.precision(15); o << v; return o.str(); }

    void consume(const std::string& n) { alive.erase(n); }

    // Điểm 2D trên mặt phác thảo -> 3D
    void pt3(const Sketch& s, double u, double v, double out[3]) const {
        if (s.plane == "xy") { out[0] = u; out[1] = v; out[2] = s.offset; }
        else if (s.plane == "yz") { out[0] = s.offset; out[1] = u; out[2] = v; }
        else { out[0] = u; out[1] = s.offset; out[2] = v; }   // xz
    }
    static void normal(const Sketch& s, double n[3]) {
        n[0] = n[1] = n[2] = 0;
        if (s.plane == "xy") n[2] = 1; else if (s.plane == "yz") n[0] = 1; else n[1] = 1;
    }

    // Phác thảo -> bề mặt phẳng; trả về tên biến mảng chứa mặt
    std::string emitSurface(const std::string& name, const Sketch& s) {
        if (s.loops.empty()) fail("phác thảo \"" + name + "\" chưa có đường bao (polygon/circle)");
        std::vector<int> loopTags;
        std::map<int, int> ptag;   // điểm của phác thảo -> tag điểm Gmsh (dùng chung giữa các đoạn)
        auto gpt = [&](int id, double u, double v) {
            const auto it = ptag.find(id);
            if (it != ptag.end()) return it->second;
            double p[3]; pt3(s, u, v, p);
            const int t = nextTag++;
            geo << "Point(" << t << ") = {" << fmt(p[0]) << "," << fmt(p[1]) << "," << fmt(p[2]) << "};\n";
            grow(p[0], p[1], p[2]);
            ptag[id] = t;
            return t;
        };
        for (const auto& lp : s.loops) {
            if (!lp.segs.empty()) {
                std::vector<int> curves;
                for (const auto& sg : lp.segs) {
                    const int ts = gpt(sg.s, sg.sx, sg.sy), te = gpt(sg.e, sg.ex, sg.ey);
                    if (!sg.arc) { curves.push_back(nextTag); geo << "Line(" << nextTag++ << ") = {" << ts << "," << te << "};\n"; continue; }
                    // cung: tách thành các mảnh <= 120 độ (Gmsh dựng cung từ 3 điểm, mỗi mảnh phải nhỏ hơn 180 độ)
                    const int tc = gpt(sg.c, sg.cx, sg.cy);
                    const double r = std::hypot(sg.sx - sg.cx, sg.sy - sg.cy);
                    double a0 = std::atan2(sg.sy - sg.cy, sg.sx - sg.cx), a1 = std::atan2(sg.ey - sg.cy, sg.ex - sg.cx);
                    double sweep = sg.ccw ? a1 - a0 : a0 - a1;
                    while (sweep <= 1e-12) sweep += 2 * kPi;
                    while (sweep > 2 * kPi + 1e-12) sweep -= 2 * kPi;
                    const int n = std::max(1, static_cast<int>(std::ceil(sweep / (2 * kPi / 3 - 1e-9))));
                    int prev = ts;
                    for (int k = 1; k <= n; ++k) {
                        int cur;
                        if (k == n) cur = te;
                        else {
                            const double a = a0 + (sg.ccw ? 1 : -1) * sweep * k / n;
                            double p[3]; pt3(s, sg.cx + r * std::cos(a), sg.cy + r * std::sin(a), p);
                            cur = nextTag++;
                            geo << "Point(" << cur << ") = {" << fmt(p[0]) << "," << fmt(p[1]) << "," << fmt(p[2]) << "};\n";
                        }
                        curves.push_back(nextTag);
                        geo << "Circle(" << nextTag++ << ") = {" << prev << "," << tc << "," << cur << "};\n";
                        prev = cur;
                    }
                }
                const int lt = nextTag++;
                geo << "Curve Loop(" << lt << ") = {";
                for (std::size_t k = 0; k < curves.size(); ++k) geo << (k ? "," : "") << curves[k];
                geo << "};\n";
                loopTags.push_back(lt);
                continue;
            }
            if (lp.circle) {
                double c[3];
                pt3(s, lp.v[0], lp.v[1], c);
                if (lp.v[2] <= 0) fail("bán kính hình tròn phải dương");
                const int ct = nextTag++;
                const int lt = nextTag++;
                // Circle gốc OCC nằm trong mặt xy; xoay về đúng mặt phẳng phác thảo
                geo << "Circle(" << ct << ") = {" << fmt(c[0]) << "," << fmt(c[1]) << "," << fmt(c[2]) << "," << fmt(lp.v[2]) << "};\n";
                if (s.plane == "yz") geo << "Rotate {{0,1,0}, {" << fmt(c[0]) << "," << fmt(c[1]) << "," << fmt(c[2]) << "}, " << fmt(kPi / 2) << "} { Curve{" << ct << "}; }\n";
                if (s.plane == "xz") geo << "Rotate {{1,0,0}, {" << fmt(c[0]) << "," << fmt(c[1]) << "," << fmt(c[2]) << "}, " << fmt(kPi / 2) << "} { Curve{" << ct << "}; }\n";
                geo << "Curve Loop(" << lt << ") = {" << ct << "};\n";
                loopTags.push_back(lt);
                double n[3]; normal(s, n);
                grow(c[0], c[1], c[2], lp.v[2]);
            } else {
                const std::size_t np = lp.v.size() / 2;
                if (np < 3) fail("đa giác cần ít nhất 3 đỉnh");
                std::vector<int> pts;
                for (std::size_t k = 0; k < np; ++k) {
                    double p[3];
                    pt3(s, lp.v[2 * k], lp.v[2 * k + 1], p);
                    pts.push_back(nextTag);
                    geo << "Point(" << nextTag++ << ") = {" << fmt(p[0]) << "," << fmt(p[1]) << "," << fmt(p[2]) << "};\n";
                    grow(p[0], p[1], p[2]);
                }
                std::vector<int> lines;
                for (std::size_t k = 0; k < np; ++k) {
                    lines.push_back(nextTag);
                    geo << "Line(" << nextTag++ << ") = {" << pts[k] << "," << pts[(k + 1) % np] << "};\n";
                }
                const int lt = nextTag++;
                geo << "Curve Loop(" << lt << ") = {";
                for (std::size_t k = 0; k < lines.size(); ++k) geo << (k ? "," : "") << lines[k];
                geo << "};\n";
                loopTags.push_back(lt);
            }
        }
        const int st = nextTag++;
        geo << "Plane Surface(" << st << ") = {";
        for (std::size_t k = 0; k < loopTags.size(); ++k) geo << (k ? "," : "") << loopTags[k];
        geo << "};\n";
        return std::to_string(st);
    }

    void run(const std::string& text) {
        std::istringstream in(text);
        std::string raw;
        Sketch* cur = nullptr;
        std::string curName;
        while (std::getline(in, raw)) {
            ++line;
            const auto h = raw.find('#');
            if (h != std::string::npos) raw.erase(h);
            std::istringstream ls(raw);
            std::vector<std::string> t;
            for (std::string w; ls >> w;) t.push_back(w);
            if (t.empty()) continue;
            const std::string cmd = t[0];

            if (cmd == "param") {
                const auto eq = raw.find('=');
                if (t.size() < 4 || eq == std::string::npos) fail("Cú pháp: param <tên> = <biểu thức>");
                const std::string n = t[1];
                if (!validName(n) || n == "pi") fail("tên tham số không hợp lệ: " + n);
                double v;
                const auto ov = overrides.find(n);
                if (ov != overrides.end()) v = ov->second;
                else v = num(raw.substr(eq + 1));
                vars[n] = v;
                continue;
            }
            if (cmd == "csketch") {
                if (cur || csk) fail("thiếu 'end' cho phác thảo đang mở");
                if (t.size() < 3) fail("Cú pháp: csketch <tên> <xy|yz|xz> [độ_dời]");
                declare(t[1]);
                csk = std::make_unique<CSketch>();
                csk->name = t[1];
                csk->plane = t[2];
                if (csk->plane != "xy" && csk->plane != "yz" && csk->plane != "xz") fail("mặt phác thảo phải là xy, yz hoặc xz");
                csk->offset = t.size() > 3 ? num(t[3]) : 0.0;
                continue;
            }
            if (csk) {
                if (cmd == "end") { finishCSketch(); continue; }
                cskLine(t);
                continue;
            }
            if (cmd == "sketch") {
                if (cur) fail("thiếu 'end' cho phác thảo \"" + curName + "\"");
                if (t.size() < 3) fail("Cú pháp: sketch <tên> <xy|yz|xz> [độ_dời]");
                declare(t[1]);
                Sketch s;
                s.plane = t[2];
                if (s.plane != "xy" && s.plane != "yz" && s.plane != "xz") fail("mặt phác thảo phải là xy, yz hoặc xz");
                s.offset = t.size() > 3 ? num(t[3]) : 0.0;
                curName = t[1];
                cur = &(sketches[curName] = s);
                continue;
            }
            if (cur) {
                if (cmd == "end") { cur = nullptr; continue; }
                Sketch::Loop lp;
                if (cmd == "polygon") {
                    if (t.size() < 7 || (t.size() - 1) % 2) fail("Cú pháp: polygon x1 y1 x2 y2 x3 y3 ... (ít nhất 3 đỉnh)");
                    for (std::size_t k = 1; k < t.size(); ++k) lp.v.push_back(num(t[k]));
                } else if (cmd == "rect") {
                    const auto a = nums(t, 1, 4, "rect x y rộng cao");
                    lp.v = {a[0], a[1], a[0] + a[2], a[1], a[0] + a[2], a[1] + a[3], a[0], a[1] + a[3]};
                } else if (cmd == "circle") {
                    lp.circle = true;
                    lp.v = nums(t, 1, 3, "circle cx cy r");
                } else {
                    fail("lệnh phác thảo không có: " + cmd + " (polygon | rect | circle | end)");
                }
                cur->loops.push_back(lp);
                continue;
            }
            if (cmd == "end") fail("'end' không có phác thảo đang mở");

            if (cmd == "box") {
                make(t.size() > 1 ? t[1] : ""); 
                const auto a = nums(t, 2, 6, "box tên x y z dx dy dz");
                geo << "Box(" << nextTag << ") = {" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "," << fmt(a[3]) << "," << fmt(a[4]) << "," << fmt(a[5]) << "};\n";
                geo << V(t[1]) << "[] = {" << nextTag++ << "};\n";
                grow(a[0], a[1], a[2]); grow(a[0] + a[3], a[1] + a[4], a[2] + a[5]);
            } else if (cmd == "cylinder") {
                make(t.size() > 1 ? t[1] : "");
                const auto a = nums(t, 2, 7, "cylinder tên x y z dx dy dz r   (dx dy dz = trục, độ dài = chiều cao)");
                if (a[6] <= 0) fail("bán kính phải dương");
                geo << "Cylinder(" << nextTag << ") = {" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "," << fmt(a[3]) << "," << fmt(a[4]) << "," << fmt(a[5]) << "," << fmt(a[6]) << "};\n";
                geo << V(t[1]) << "[] = {" << nextTag++ << "};\n";
                grow(a[0], a[1], a[2], a[6]); grow(a[0] + a[3], a[1] + a[4], a[2] + a[5], a[6]);
            } else if (cmd == "sphere") {
                make(t.size() > 1 ? t[1] : "");
                const auto a = nums(t, 2, 4, "sphere tên x y z r");
                if (a[3] <= 0) fail("bán kính phải dương");
                geo << "Sphere(" << nextTag << ") = {" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "," << fmt(a[3]) << "};\n";
                geo << V(t[1]) << "[] = {" << nextTag++ << "};\n";
                grow(a[0], a[1], a[2], a[3]);
            } else if (cmd == "cone") {
                make(t.size() > 1 ? t[1] : "");
                const auto a = nums(t, 2, 8, "cone tên x y z dx dy dz r1 r2");
                geo << "Cone(" << nextTag << ") = {" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "," << fmt(a[3]) << "," << fmt(a[4]) << "," << fmt(a[5]) << "," << fmt(a[6]) << "," << fmt(a[7]) << "};\n";
                geo << V(t[1]) << "[] = {" << nextTag++ << "};\n";
                const double r = std::max(a[6], a[7]);
                grow(a[0], a[1], a[2], r); grow(a[0] + a[3], a[1] + a[4], a[2] + a[5], r);
            } else if (cmd == "torus") {
                make(t.size() > 1 ? t[1] : "");
                const auto a = nums(t, 2, 5, "torus tên x y z R r");
                geo << "Torus(" << nextTag << ") = {" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "," << fmt(a[3]) << "," << fmt(a[4]) << "};\n";
                geo << V(t[1]) << "[] = {" << nextTag++ << "};\n";
                grow(a[0], a[1], a[2], a[3] + a[4]);
            } else if (cmd == "extrude") {
                if (t.size() < 4) fail("Cú pháp: extrude <tên> <phác_thảo> <khoảng_cách>");
                const auto it = sketches.find(t[2]);
                if (it == sketches.end()) fail("phác thảo không có: " + t[2]);
                make(t[1]);
                const double d = num(t[3]);
                if (d == 0) fail("khoảng đùn phải khác 0");
                double n[3]; normal(it->second, n);
                const std::string st = emitSurface(t[2], it->second);
                geo << "e_" << t[1] << "[] = Extrude {" << fmt(n[0] * d) << "," << fmt(n[1] * d) << "," << fmt(n[2] * d) << "} { Surface{" << st << "}; };\n";
                geo << V(t[1]) << "[] = {e_" << t[1] << "[1]};\n";
                double p[3]; pt3(it->second, 0, 0, p);
                grow(p[0] + n[0] * d, p[1] + n[1] * d, p[2] + n[2] * d);
            } else if (cmd == "revolve") {
                if (t.size() < 9) fail("Cú pháp: revolve <tên> <phác_thảo> <góc_độ> px py pz ax ay az   (trục đi qua p, hướng a)");
                const auto it = sketches.find(t[2]);
                if (it == sketches.end()) fail("phác thảo không có: " + t[2]);
                make(t[1]);
                const double ang = num(t[3]);
                const auto a = nums(t, 4, 6, "revolve ... px py pz ax ay az");
                if (ang <= 0 || ang > 360) fail("góc xoay phải trong (0, 360]");
                const std::string st = emitSurface(t[2], it->second);
                geo << "e_" << t[1] << "[] = Extrude {{" << fmt(a[3]) << "," << fmt(a[4]) << "," << fmt(a[5]) << "},{" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "}," << fmt(ang * kPi / 180.0) << "} { Surface{" << st << "}; };\n";
                geo << V(t[1]) << "[] = {e_" << t[1] << "[1]};\n";
                const double ext = std::max({std::fabs(hi[0] - lo[0]), std::fabs(hi[1] - lo[1]), std::fabs(hi[2] - lo[2])});
                grow(a[0], a[1], a[2], ext);
            } else if (cmd == "cut" || cmd == "fuse" || cmd == "intersect") {
                if (t.size() < 4) fail("Cú pháp: " + cmd + " <tên_mới> <thân_chính> <thân_2> [thân_3...]");
                for (std::size_t k = 2; k < t.size(); ++k) need(t[k]);
                const std::string base = t[2];
                std::string others;
                for (std::size_t k = 3; k < t.size(); ++k) others += (k > 3 ? "," : "") + V(t[k]) + "[]";
                const std::string op = cmd == "cut" ? "BooleanDifference" : cmd == "fuse" ? "BooleanUnion" : "BooleanIntersection";
                for (std::size_t k = 2; k < t.size(); ++k) consume(t[k]);
                make(t[1]);
                geo << V(t[1]) << "[] = " << op << "{ Volume{" << V(base) << "[]}; Delete; }{ Volume{" << others << "}; Delete; };\n";
            } else if (cmd == "translate") {
                if (t.size() < 6) fail("Cú pháp: translate <tên_mới> <thân> dx dy dz");
                need(t[2]);
                const auto a = nums(t, 3, 3, "translate tên thân dx dy dz");
                const std::string src = t[2];
                consume(src);
                make(t[1]);
                geo << "Translate {" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "} { Volume{" << V(src) << "[]}; }\n";
                geo << V(t[1]) << "[] = " << V(src) << "[];\n";
                grow(a[0], a[1], a[2]);
            } else if (cmd == "rotate") {
                if (t.size() < 10) fail("Cú pháp: rotate <tên_mới> <thân> px py pz ax ay az góc_độ");
                need(t[2]);
                const auto a = nums(t, 3, 7, "rotate tên thân px py pz ax ay az góc");
                const std::string src = t[2];
                consume(src);
                make(t[1]);
                geo << "Rotate {{" << fmt(a[3]) << "," << fmt(a[4]) << "," << fmt(a[5]) << "},{" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "}," << fmt(a[6] * kPi / 180.0) << "} { Volume{" << V(src) << "[]}; }\n";
                geo << V(t[1]) << "[] = " << V(src) << "[];\n";
            } else if (cmd == "linear_pattern") {
                if (t.size() < 7) fail("Cú pháp: linear_pattern <tên_mới> <thân> <số_bản> dx dy dz   (khoảng cách giữa hai bản)");
                need(t[2]);
                const int n = static_cast<int>(num(t[3]));
                if (n < 2 || n > 500) fail("số bản phải trong [2, 500]");
                const auto a = nums(t, 4, 3, "linear_pattern tên thân n dx dy dz");
                const std::string src = t[2];
                consume(src);
                make(t[1]);
                std::string all = V(src) + "[]";
                for (int k = 1; k < n; ++k) {
                    const std::string c = "c_" + t[1] + "_" + std::to_string(k);
                    geo << c << "[] = Translate {" << fmt(a[0] * k) << "," << fmt(a[1] * k) << "," << fmt(a[2] * k) << "} { Duplicata{ Volume{" << V(src) << "[]}; } };\n";
                    all += "," + c + "[]";
                }
                geo << V(t[1]) << "[] = BooleanUnion{ Volume{" << V(src) << "[]}; Delete; }{ Volume{" << all.substr(V(src).size() + 3) << "}; Delete; };\n";
                grow(a[0] * (n - 1), a[1] * (n - 1), a[2] * (n - 1));
            } else if (cmd == "circular_pattern") {
                if (t.size() < 10) fail("Cú pháp: circular_pattern <tên_mới> <thân> <số_bản> px py pz ax ay az góc_tổng_độ");
                need(t[2]);
                const int n = static_cast<int>(num(t[3]));
                if (n < 2 || n > 500) fail("số bản phải trong [2, 500]");
                const auto a = nums(t, 4, 7, "circular_pattern tên thân n px py pz ax ay az góc");
                const std::string src = t[2];
                consume(src);
                make(t[1]);
                const bool full = std::fabs(a[6] - 360.0) < 1e-9;
                const double step = a[6] / (full ? n : (n - 1));
                std::string all;
                for (int k = 1; k < n; ++k) {
                    const std::string c = "c_" + t[1] + "_" + std::to_string(k);
                    geo << c << "[] = Duplicata{ Volume{" << V(src) << "[]}; };\n";
                    geo << "Rotate {{" << fmt(a[3]) << "," << fmt(a[4]) << "," << fmt(a[5]) << "},{" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "}," << fmt(step * k * kPi / 180.0) << "} { Volume{" << c << "[]}; }\n";
                    all += (k > 1 ? "," : "") + c + "[]";
                }
                geo << V(t[1]) << "[] = BooleanUnion{ Volume{" << V(src) << "[]}; Delete; }{ Volume{" << all << "}; Delete; };\n";
                const double ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
                grow(a[0], a[1], a[2], ext);
            } else if (cmd == "mirror") {
                if (t.size() < 7) fail("Cú pháp: mirror <tên_mới> <thân> a b c d   (mặt đối xứng ax+by+cz+d=0; kết quả = thân + ảnh đối xứng)");
                need(t[2]);
                const auto a = nums(t, 3, 4, "mirror tên thân a b c d");
                if (a[0] == 0 && a[1] == 0 && a[2] == 0) fail("pháp tuyến mặt đối xứng không được bằng 0");
                const std::string src = t[2];
                consume(src);
                make(t[1]);
                const std::string c = "m_" + t[1];
                geo << c << "[] = Duplicata{ Volume{" << V(src) << "[]}; };\n";
                geo << "Symmetry {" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "," << fmt(a[3]) << "} { Volume{" << c << "[]}; }\n";
                geo << V(t[1]) << "[] = BooleanUnion{ Volume{" << V(src) << "[]}; Delete; }{ Volume{" << c << "[]}; Delete; };\n";
                const double ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
                grow(lo[0], lo[1], lo[2], ext);
            } else if (cmd == "fillet" || cmd == "chamfer") {
                if (t.size() < 8) fail("Cú pháp: " + cmd + " <" + std::string(cmd == "fillet" ? "bán_kính" : "khoảng_vát") + "> xmin ymin zmin xmax ymax zmax   (chọn mọi cạnh nằm trọn trong hộp; áp dụng sau cùng)");
                const auto a = nums(t, 1, 7, "fillet r xmin ymin zmin xmax ymax zmax");
                if (a[0] <= 0) fail("kích thước phải dương");
                PostOp op;
                op.chamfer = cmd == "chamfer";
                op.size = a[0];
                for (int k = 0; k < 6; ++k) op.box[k] = a[1 + k];
                if (op.box[0] > op.box[3] || op.box[1] > op.box[4] || op.box[2] > op.box[5]) fail("hộp chọn cạnh phải có min <= max");
                post.push_back(op);
            } else if (cmd == "result") {
                if (t.size() < 2) fail("Cú pháp: result <tên_thân>");
                need(t[1]);
                explicitResult = t[1];
            } else {
                fail("lệnh không có: " + cmd);
            }
        }
        if (cur) fail("thiếu 'end' cho phác thảo \"" + curName + "\"");
        if (csk) fail("thiếu 'end' cho phác thảo ràng buộc \"" + csk->name + "\"");
    }

    // ---- phác thảo có ràng buộc ----
    int findIn(const std::map<std::string, int>& m, const std::string& n, const char* what) {
        const auto it = m.find(n);
        if (it == m.end()) fail(std::string(what) + " không có trong phác thảo: " + n);
        return it->second;
    }
    void addCon(SketchCon type, int a, int b, double v, double v2, const std::string& label) {
        SketchConstraint c;
        c.type = type; c.a = a; c.b = b; c.value = v; c.value2 = v2; c.label = label;
        csk->sys.add(c);
    }
    void cskLine(const std::vector<std::string>& t) {
        CSketch& k = *csk;
        const std::string& cmd = t[0];
        auto name = [&](std::size_t i) -> const std::string& { if (t.size() <= i) fail("thiếu đối số cho '" + cmd + "'"); return t[i]; };
        auto newName = [&](const std::string& n) {
            if (!validName(n)) fail("tên không hợp lệ: " + n);
            if (k.pts.count(n) || k.lines.count(n) || k.circs.count(n)) fail("tên đã dùng trong phác thảo: " + n);
        };
        auto pt = [&](std::size_t i) { return findIn(k.pts, name(i), "điểm"); };
        auto ln = [&](std::size_t i) { return findIn(k.lines, name(i), "đường"); };
        auto ci = [&](std::size_t i) { return findIn(k.circs, name(i), "đường tròn"); };
        // hai điểm: hoặc "A B" hoặc một tên đường
        auto twoPoints = [&](int& a, int& b) {
            if (t.size() >= 3 && k.pts.count(t[1]) && k.pts.count(t[2])) { a = pt(1); b = pt(2); return; }
            if (k.lines.count(name(1))) { const int l = ln(1); a = lineP[l]; b = lineQ[l]; return; }
            fail("'" + cmd + "' cần hai điểm hoặc một đường");
        };
        if (cmd == "pt") {
            newName(name(1));
            k.pts[t[1]] = k.sys.addPoint(num(name(2)), num(name(3)));
        } else if (cmd == "line") {
            newName(name(1));
            const int p = pt(2), q = pt(3);
            if (p == q) fail("đường thẳng cần hai điểm khác nhau");
            const int id = k.sys.addLine(p, q);
            k.lines[t[1]] = id;
            lineP.push_back(p); lineQ.push_back(q);
        } else if (cmd == "circle") {
            newName(name(1));
            const double r = num(name(3));
            if (r <= 0) fail("bán kính ban đầu phải dương");
            k.circs[t[1]] = k.sys.addCircle(pt(2), r);
            circC.push_back(pt(2));
        } else if (cmd == "arc") {
            // arc <tên> <tâm> <đầu> <cuối> : cung NGƯỢC chiều kim đồng hồ từ điểm đầu đến điểm cuối, tâm là điểm
            newName(name(1));
            const int c = pt(2), p = pt(3), q = pt(4);
            if (c == p || c == q || p == q) fail("cung cần ba điểm khác nhau (tâm, đầu, cuối)");
            const double r0 = std::hypot(k.sys.px(p) - k.sys.px(c), k.sys.py(p) - k.sys.py(c));
            if (r0 <= 0) fail("bán kính ban đầu của cung phải dương (đặt điểm đầu cách tâm)");
            const int cid = k.sys.addCircle(c, r0);
            k.circs[t[1]] = cid;
            circC.push_back(c);
            k.arcs[t[1]] = {cid, c, p, q};
            addCon(SketchCon::PointOnCircle, p, cid, 0, 0, "arc");   // đầu và cuối nằm trên đường tròn tâm c
            addCon(SketchCon::PointOnCircle, q, cid, 0, 0, "arc");
        } else if (cmd == "wire") {
            // wire e1 e2 ... : vòng kín gồm đường thẳng và cung nối tiếp nhau (chung đầu mút)
            if (t.size() < 3) fail("wire cần ít nhất 2 phần tử (đường hoặc cung)");
            CSketch::WireDef w;
            for (std::size_t i = 1; i < t.size(); ++i) {
                if (k.lines.count(t[i])) w.items.push_back({false, t[i]});
                else if (k.arcs.count(t[i])) w.items.push_back({true, t[i]});
                else fail("wire: \"" + t[i] + "\" không phải đường thẳng hoặc cung của phác thảo");
            }
            k.wires.push_back({static_cast<int>(k.loops.size()), w});
            k.loops.push_back({false, {}});   // chỗ giữ vị trí; dựng đoạn khi giải xong
        } else if (cmd == "fix") {
            const int a = pt(1);
            if (t.size() >= 4) addCon(SketchCon::Fix, a, -1, num(t[2]), num(t[3]), cmd);
            else { fixAtInitial.push_back(a); }
        } else if (cmd == "coincident") addCon(SketchCon::Coincident, pt(1), pt(2), 0, 0, cmd);
        else if (cmd == "horizontal" || cmd == "vertical") {
            int a, b; twoPoints(a, b);
            addCon(cmd == "horizontal" ? SketchCon::Horizontal : SketchCon::Vertical, a, b, 0, 0, cmd);
        } else if (cmd == "dist") addCon(SketchCon::Distance, pt(1), pt(2), num(name(3)), 0, cmd);
        else if (cmd == "length") {
            const int l = ln(1);
            addCon(SketchCon::Distance, lineP[l], lineQ[l], num(name(2)), 0, cmd);
        } else if (cmd == "parallel") addCon(SketchCon::Parallel, ln(1), ln(2), 0, 0, cmd);
        else if (cmd == "perpendicular") addCon(SketchCon::Perpendicular, ln(1), ln(2), 0, 0, cmd);
        else if (cmd == "equal") {
            if (k.lines.count(name(1))) addCon(SketchCon::EqualLength, ln(1), ln(2), 0, 0, cmd);
            else addCon(SketchCon::EqualRadius, ci(1), ci(2), 0, 0, cmd);
        } else if (cmd == "angle") addCon(SketchCon::Angle, ln(1), ln(2), num(name(3)) * kPi / 180.0, 0, cmd);
        else if (cmd == "radius") addCon(SketchCon::Radius, ci(1), -1, num(name(2)), 0, cmd);
        else if (cmd == "on") {
            if (k.lines.count(name(2))) addCon(SketchCon::PointOnLine, pt(1), ln(2), 0, 0, cmd);
            else addCon(SketchCon::PointOnCircle, pt(1), ci(2), 0, 0, cmd);
        } else if (cmd == "tangent") {
            const int l = ln(1), c = ci(2);
            // đường chung đầu mút với cung: tiếp xúc tại điểm đó = vuông góc bán kính (không suy biến như dạng khoảng cách)
            int shared = -1;
            const auto ait = k.arcs.find(name(2));
            if (ait != k.arcs.end()) {
                if (lineP[l] == ait->second.p || lineQ[l] == ait->second.p) shared = ait->second.p;
                else if (lineP[l] == ait->second.q || lineQ[l] == ait->second.q) shared = ait->second.q;
            }
            if (shared >= 0) addCon(SketchCon::TangentAtPoint, l, c, shared, 0, cmd);
            else addCon(SketchCon::Tangent, l, c, 0, 0, cmd);
        }
        else if (cmd == "midpoint") addCon(SketchCon::Midpoint, pt(1), ln(2), 0, 0, cmd);
        else if (cmd == "loop") {
            if (t.size() < 4) fail("loop cần ít nhất 3 điểm theo thứ tự quanh biên");
            CSketch::LoopDef d{false, {}};
            for (std::size_t i = 1; i < t.size(); ++i) d.ids.push_back(pt(i));
            k.loops.push_back(d);
        } else if (cmd == "loopcircle") {
            k.loops.push_back({true, {ci(1)}});
        } else fail("lệnh phác thảo ràng buộc không có: " + cmd);
    }
    Sketch::Loop buildWire(CSketch& k, const CSketch::WireDef& w) {
        struct Ends { int a, b; Sketch::Seg base; };
        std::vector<Ends> el;
        for (const auto& it : w.items) {
            Sketch::Seg sg;
            if (it.arc) {
                const auto& a = k.arcs[it.name];
                sg.arc = true; sg.c = a.c; sg.cx = k.sys.px(a.c); sg.cy = k.sys.py(a.c);
                el.push_back({a.p, a.q, sg});
            } else {
                const int l = k.lines[it.name];
                el.push_back({lineP[l], lineQ[l], sg});
            }
        }
        // đi theo chuỗi: phần tử đầu theo chiều khai báo, các phần tử sau tự định hướng
        Sketch::Loop lp;
        const int start = el[0].a;
        int cur = start;
        std::vector<bool> used(el.size(), false);
        for (std::size_t step = 0; step < el.size(); ++step) {
            int pick = -1;
            bool forward = true;
            for (std::size_t i = 0; i < el.size() && pick < 0; ++i) {
                if (used[i]) continue;
                if (el[i].a == cur) { pick = static_cast<int>(i); forward = true; }
                else if (el[i].b == cur) { pick = static_cast<int>(i); forward = false; }
            }
            if (pick < 0) fail("wire không liên tục: không có phần tử nào nối tiếp tại điểm " + std::to_string(cur));
            used[pick] = true;
            Sketch::Seg sg = el[pick].base;
            const int from = forward ? el[pick].a : el[pick].b, to = forward ? el[pick].b : el[pick].a;
            sg.s = from; sg.e = to;
            sg.sx = k.sys.px(from); sg.sy = k.sys.py(from); sg.ex = k.sys.px(to); sg.ey = k.sys.py(to);
            sg.ccw = forward;   // cung khai báo ngược chiều kim đồng hồ từ đầu đến cuối
            lp.segs.push_back(sg);
            cur = to;
        }
        if (cur != start) fail("wire không khép kín (kết thúc ở điểm khác điểm đầu)");
        return lp;
    }
    void finishCSketch() {
        CSketch& k = *csk;
        for (int a : fixAtInitial) addCon(SketchCon::Fix, a, -1, k.sys.px(a), k.sys.py(a), "fix");
        if (k.loops.empty()) fail("phác thảo \"" + k.name + "\" chưa có vòng biên (loop / loopcircle)");
        const SketchSolveResult sr = k.sys.solve();
        if (!sr.ok) fail("phác thảo \"" + k.name + "\": " + sr.message);
        if (sr.redundant > 0) warnings.push_back("phác thảo \"" + k.name + "\": dư " + std::to_string(sr.redundant) + " phương trình ràng buộc (nhất quán nhưng thừa)");
        if (sr.dof > 0) warnings.push_back("phác thảo \"" + k.name + "\": chưa ràng buộc đủ, còn " + std::to_string(sr.dof) + " bậc tự do (hình có thể thay đổi; thêm kích thước/ràng buộc)");
        Sketch s;
        s.plane = k.plane;
        s.offset = k.offset;
        std::map<int, Sketch::Loop> wireLoops;
        for (const auto& wd : k.wires) wireLoops[wd.first] = buildWire(k, wd.second);
        int loopIndex = -1;
        for (const auto& d : k.loops) {
            ++loopIndex;
            if (wireLoops.count(loopIndex)) { s.loops.push_back(wireLoops[loopIndex]); continue; }
            Sketch::Loop lp;
            if (d.circle) {
                lp.circle = true;
                const int c = d.ids[0];
                lp.v = {k.sys.px(circC[c]), k.sys.py(circC[c]), k.sys.radius(c)};
            } else {
                for (int p : d.ids) { lp.v.push_back(k.sys.px(p)); lp.v.push_back(k.sys.py(p)); }
            }
            s.loops.push_back(lp);
        }
        sketches[k.name] = s;
        csk.reset();
        lineP.clear(); lineQ.clear(); circC.clear(); fixAtInitial.clear();
    }
    std::vector<int> lineP, lineQ, circC, fixAtInitial;
};

}  // namespace

bool evalExpression(const std::string& expr, const std::map<std::string, double>& vars, double& out, std::string& err) {
    try { out = evalOrThrow(expr, vars); return true; }
    catch (const std::exception& e) { err = e.what(); return false; }
}

ParamModelResult compileParamModel(const std::string& text, const std::map<std::string, double>& overrides,
                                   double meshSize, int order) {
    ParamModelResult r;
    Compiler c(overrides, meshSize, order);
    try {
        c.geo << "// FastCAE - sinh tu dong tu file .fcad\nSetFactory(\"OpenCASCADE\");\n";
        c.run(text);
        std::string result = c.explicitResult;
        if (result.empty()) {
            if (c.alive.size() == 1) result = *c.alive.begin();
            else if (c.alive.empty()) throw std::runtime_error("mô hình rỗng: chưa tạo tính năng nào");
            else {
                std::string names;
                for (const auto& n : c.alive) names += (names.empty() ? "" : ", ") + n;
                throw std::runtime_error("còn nhiều thân độc lập (" + names + "); gộp bằng 'fuse' hoặc chỉ định 'result <tên>'");
            }
        }
        double h = meshSize;
        if (h <= 0 && c.hi[0] >= c.lo[0]) {
            const double dx = c.hi[0] - c.lo[0], dy = c.hi[1] - c.lo[1], dz = c.hi[2] - c.lo[2];
            h = std::sqrt(dx * dx + dy * dy + dz * dz) / 15.0;
        }
        c.geo << "Coherence;\n";
        r.postOps = c.post;
        r.warnings = c.warnings;
        r.meshSizeUsed = h;
        r.orderUsed = order == 2 ? 2 : 1;
        if (c.post.empty()) {   // có bo góc/vát: nhóm vật lý và cỡ lưới do script Python đặt sau khi bo
            c.geo << "Physical Volume(\"body\") = {" << Compiler::V(result) << "[]};\n";
            if (h > 0) c.geo << "Mesh.MeshSizeMax = " << Compiler::fmt(h) << ";\n";
            c.geo << "Mesh.ElementOrder = " << r.orderUsed << ";\n";
        }
        r.geo = c.geo.str();
        r.parameters = c.vars;
        r.resultBody = result;
        r.ok = true;
        r.message = "Đã biên dịch mô hình tham số: " + std::to_string(c.creationOrder.size()) + " tính năng, " +
                    std::to_string(c.vars.size()) + " tham số, thân kết quả = " + result;
    } catch (const std::exception& e) {
        r.message = e.what();
    }
    return r;
}

ParamMeshResult meshParamModel(const std::string& text, const std::map<std::string, double>& overrides,
                               double meshSize, int order, const std::string& workDirIn) {
    ParamMeshResult out;
    const ParamModelResult m = compileParamModel(text, overrides, meshSize, order);
    out.parameters = m.parameters;
    out.geo = m.geo;
    out.warnings = m.warnings;
    if (!m.ok) { out.message = m.message; return out; }

    namespace fs = std::filesystem;
    static std::atomic<int> counter{0};
    fs::path dir = workDirIn;
    std::error_code ec;
    if (dir.empty()) {
        dir = fs::temp_directory_path(ec) / ("fastcae_param_" + std::to_string(counter++) + "_" +
                                              std::to_string(static_cast<long long>(std::hash<std::string>{}(m.geo) % 1000000007LL)));
    }
    fs::create_directories(dir, ec);
    const fs::path geoPath = dir / "model.geo";
    const fs::path mshPath = dir / "model.msh";
    const fs::path logPath = dir / "gmsh.log";
    { std::ofstream f(geoPath); f << m.geo; if (!f) { out.message = "Không ghi được " + geoPath.string(); return out; } }
    fs::remove(mshPath, ec);

    if (!m.postOps.empty()) {
        // Bo góc / vát cạnh: Gmsh Python (tiến trình riêng, không liên kết vào FastCAE)
        const fs::path pyPath = dir / "post.py";
        {
            std::ofstream py(pyPath);
            py << "import gmsh, sys\n"
                  "gmsh.initialize()\n"
                  "gmsh.option.setNumber('General.Terminal', 1)\n"
                  "gmsh.open('model.geo')\n"
                  "occ = gmsh.model.occ\n"
                  "occ.synchronize()\n"
                  "vols = [t for d, t in gmsh.model.getEntities(3)]\n"
                  "tol = 1e-6\n"
                  "OPS = [";
            py.precision(17);
            for (const auto& op : m.postOps) {
                py << "(" << (op.chamfer ? "True" : "False") << "," << op.size;
                for (double b : op.box) py << "," << b;
                py << "),";
            }
            py << "]\n"
                  "for ch, sz, x0, y0, z0, x1, y1, z1 in OPS:\n"
                  "    cs = [t for d, t in gmsh.model.getEntitiesInBoundingBox(x0 - tol, y0 - tol, z0 - tol, x1 + tol, y1 + tol, z1 + tol, 1)]\n"
                  "    if not cs:\n"
                  "        sys.stderr.write('FCERR khong co canh nao nam tron trong hop chon\\n'); sys.exit(3)\n"
                  "    if ch:\n"
                  "        sf = [gmsh.model.getAdjacencies(1, c)[0][0] for c in cs]\n"
                  "        out = occ.chamfer(vols, cs, sf, [sz] * len(cs), True)\n"
                  "    else:\n"
                  "        out = occ.fillet(vols, cs, [sz] * len(cs), True)\n"
                  "    occ.synchronize()\n"
                  "    vols = [t for d, t in out if d == 3]\n"
                  "    if not vols:\n"
                  "        sys.stderr.write('FCERR bo goc/vat canh that bai (kich thuoc qua lon?)\\n'); sys.exit(4)\n"
                  "gmsh.model.addPhysicalGroup(3, vols, name='body')\n";
            py << "gmsh.option.setNumber('Mesh.MeshSizeMax', " << (m.meshSizeUsed > 0 ? m.meshSizeUsed : 1e22) << ")\n"
               << "gmsh.option.setNumber('Mesh.ElementOrder', " << m.orderUsed << ")\n"
               << "gmsh.option.setNumber('Mesh.MshFileVersion', 2.2)\n"
                  "gmsh.model.mesh.generate(3)\n"
                  "gmsh.write('model.msh')\n"
                  "gmsh.finalize()\n";
        }
        std::string py;
        if (const char* e = std::getenv("FASTCAE_PYTHON")) py = e;
#ifdef _WIN32
        else py = "python";
#else
        else py = "python3";
#endif
        const std::string cmd = "\"" + py + "\" post.py > \"" + logPath.string() + "\" 2>&1";
        std::string cap;
        const int rc = runCommandLine(cmd, dir.string(), nullptr, nullptr, &cap);
        if (rc != 0 || !fs::exists(mshPath)) {
            std::ifstream lf(logPath);
            std::vector<std::string> lines;
            for (std::string l; std::getline(lf, l);) lines.push_back(l);
            std::string tail;
            for (std::size_t k = lines.size() > 6 ? lines.size() - 6 : 0; k < lines.size(); ++k) tail += lines[k] + "\n";
            out.message = "Bo góc/vát cạnh thất bại (cần Python có gói gmsh: pip install gmsh; đặt FASTCAE_PYTHON nếu cần):\n" + tail;
            return out;
        }
    } else {
    const std::string exe = findGmshExecutable();
    if (exe.empty()) {
        out.message = "Không tìm thấy chương trình gmsh để chia lưới (đặt biến FASTCAE_GMSH hoặc đưa gmsh vào PATH). Script đã sinh: " + geoPath.string();
        return out;
    }
    const std::string cmd = "\"" + exe + "\" -3 -format msh2 -o \"" + mshPath.string() + "\" \"" + geoPath.string() + "\" > \"" + logPath.string() + "\" 2>&1";
    std::string captured;
    const int rc = runCommandLine(cmd, dir.string(), nullptr, nullptr, &captured);
    if (rc != 0 || !fs::exists(mshPath)) {
        std::string tail;
        std::ifstream lf(logPath);
        std::string l;
        std::vector<std::string> lines;
        while (std::getline(lf, l)) lines.push_back(l);
        for (std::size_t k = lines.size() > 6 ? lines.size() - 6 : 0; k < lines.size(); ++k) tail += lines[k] + "\n";
        out.message = "gmsh không dựng được lưới (mã " + std::to_string(rc) + "):\n" + tail;
        return out;
    }
    }
    std::string err;
    if (!readGmshMsh(mshPath.string(), out.mesh, &err)) { out.message = "Đọc lưới thất bại: " + err; return out; }
    if (out.mesh.numCells() == 0) { out.message = "Lưới rỗng (hình học có thể không phải vật thể khối kín)"; return out; }
    out.mesh.fixOrientation();
    out.ok = true;
    out.message = m.message + "; lưới " + std::to_string(out.mesh.numNodes()) + " nút, " + std::to_string(out.mesh.numCells()) + " phần tử";
    return out;
}

}  // namespace fastcae
