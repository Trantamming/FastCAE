// Script C++ không có hàm main: được bọc tự động. Kiểm tra fc::run và danh mục solver.
// (Chú thích có chữ main() không được làm trình bọc nhầm.)
auto r = fc::run("type = frame\nE = 210e9\nnu = 0.3\nrho = 7850\nsection = S rect 0.1 0.2\n"
                 "node = 1 0 0 0\nnode = 2 3 0 0\nmember = 1 2 S beam\nsupport = 1 fixed\nnload = 2 0 0 -1000\n");
const double exact = 1000.0 * 27.0 / (3.0 * 210e9 * (0.1 * 0.008 / 12.0));
const double fe = r.values.at("max_displacement");
if (!r.ok || std::abs(fe - exact) / exact > 1e-3) { std::cerr << "sai: " << fe << " vs " << exact << "\n"; return 1; }
if (!findSolverInfo("code_aster") || !findSolverInfo("su2") || !findSolverInfo("openradioss")) return 2;
std::cout << "CXX_SMOKE_OK\n";
