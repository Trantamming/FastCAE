// FastCAE - C++: khong can viet main(); phan than duoc boc vao main() va co san fc::run, fc::solver, fc::shell.
// Can trinh bien dich C++20 (g++/MinGW hoac cl) va FastCAE SDK; bam Chay (F6) de bien dich va chay.
// Goi solver ma nguon mo (bo dau // de dung):
//   fc::solver("code_aster", "D:/ca/model.export");
//   fc::solver("openradioss", "D:/rad/model_0000.rad");
//   fc::solver("su2", "D:/su2/case.cfg", "-n 4");
//   fc::solver("openfoam", "D:/foam/pitzDaily", "", {"blockMesh", "simpleFoam", "foamToVTK"});

std::cout << "Solver ma nguon mo trong FastCAE:\n";
for (const auto& s : allSolvers()) {
    if (s.steps.empty() && s.id != "openfoam") continue;
    const std::string path = solverRuntimePath(s);
    std::printf("  %-12s %-9s %s\n", s.id.c_str(), path.empty() ? "chua cai" : "DA CAI", path.c_str());
}

// Chay mot bai toan noi tai (khung 1D: dam cong xon dai 3 m, luc 1 kN)
auto r = fc::run(
    "type = frame\nE = 210e9\nnu = 0.3\nrho = 7850\nsection = S rect 0.1 0.2\n"
    "node = 1 0 0 0\nnode = 2 3 0 0\nmember = 1 2 S beam\nsupport = 1 fixed\nnload = 2 0 0 -1000\n");
std::cout << "ket qua: " << (r.ok ? "OK" : r.message) << "\n";
