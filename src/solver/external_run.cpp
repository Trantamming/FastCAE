// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "solver/external_run.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "solver/openfoam_results.hpp"
#include "solver/solver_registry.hpp"

#ifdef _WIN32
#define FC_POPEN _popen
#define FC_PCLOSE _pclose
#else
#define FC_POPEN popen
#define FC_PCLOSE pclose
#endif

namespace fs = std::filesystem;

namespace fastcae {

static std::string q(const std::string& s) { return "\"" + s + "\""; }

int runCommandLine(const std::string& commandLine, const std::string& workDir, const OutputFn& onOutput,
                   const std::atomic<bool>* cancel, std::string* captured) {
    std::string full;
#ifdef _WIN32
    full = "cmd /c \"";
    if (!workDir.empty()) full += "cd /d " + q(workDir) + " && ";
    full += commandLine + " 2>&1\"";
#else
    if (!workDir.empty()) full += "cd " + q(workDir) + " && ";
    full += "(" + commandLine + ") 2>&1";
#endif
    FILE* p = FC_POPEN(full.c_str(), "r");
    if (!p) return -1;
    std::string line;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, p)) {
        line.assign(buf);
        if (captured) captured->append(line);
        if (onOutput) onOutput(line);
        if (cancel && cancel->load()) break;
    }
    const int rc = FC_PCLOSE(p);
#ifdef _WIN32
    return rc;
#else
    return rc == -1 ? -1 : (rc & 0x7f ? 128 + (rc & 0x7f) : (rc >> 8) & 0xff);
#endif
}

static std::string readAll(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void snapshot(const fs::path& dir, std::vector<std::pair<std::string, fs::file_time_type>>& out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec)) out.emplace_back(it->path().string(), it->last_write_time(ec));
    }
}

static std::string withExtra(const std::string& cmd, const std::string& extra) { return extra.empty() ? cmd : cmd + " " + extra; }

ExternalRunResult runExternalSolver(const ExternalRunOptions& opt) {
    ExternalRunResult R;
    const SolverInfo* s = findSolverInfo(opt.solverId);
    if (!s) { R.message = "Khong biet solver '" + opt.solverId + "' (xem fastcae-cli solvers)"; return R; }
    if (s->steps.empty() && s->id != "openfoam") { R.message = "Solver '" + s->id + "' khong co lenh chay tu dong (chi la thu vien)"; return R; }

    std::error_code ec;
    fs::path input = opt.input.empty() ? fs::path() : fs::absolute(opt.input, ec);
    if (input.empty()) { R.message = "Thieu file input"; return R; }
    if (!fs::exists(input, ec)) { R.message = "Khong thay input: " + input.string(); return R; }

    const bool isFoam = s->id == "openfoam";
    fs::path caseDir;
    if (isFoam) {
        caseDir = fs::is_directory(input, ec) ? input : (input.filename() == "controlDict" ? input.parent_path().parent_path() : input.parent_path());
        R.workDir = opt.workDir.empty() ? caseDir.string() : fs::absolute(opt.workDir, ec).string();
    } else {
        R.workDir = opt.workDir.empty() ? input.parent_path().string() : fs::absolute(opt.workDir, ec).string();
    }
    fs::create_directories(R.workDir, ec);

    std::vector<std::pair<std::string, fs::file_time_type>> before;
    snapshot(R.workDir, before);

    auto emit = [&](const std::string& t) {
        R.log += t;
        if (opt.onOutput) opt.onOutput(t);
    };

    std::vector<std::string> cmds;   // dòng lệnh đầy đủ, đã gồm đường dẫn file chạy
    if (isFoam) {
        std::vector<std::string> names = opt.commands;
        if (names.empty()) {
            if (fs::exists(caseDir / "system" / "blockMeshDict", ec)) names.push_back("blockMesh");
            std::smatch m;
            const std::string cd = readAll(caseDir / "system" / "controlDict");
            if (std::regex_search(cd, m, std::regex("application\\s+(\\w+)\\s*;"))) names.push_back(m[1]);
            else { R.message = "Khong doc duoc 'application' trong system/controlDict; truyen commands=[...]"; return R; }
        }
        const std::string bashrc = findOpenFoamBashrc(opt.executableHint);
        std::string script;
        if (!bashrc.empty()) script += "source " + q(bashrc) + " >/dev/null 2>&1; ";
        script += "cd " + q(caseDir.generic_string()) + " && ";
        for (std::size_t i = 0; i < names.size(); ++i) {
            script += "(" + withExtra(names[i], i + 1 == names.size() ? opt.extraArgs : "") + ") > log." + std::regex_replace(names[i], std::regex("\\s.*"), "") + " 2>&1 && ";
        }
        script += "echo FASTCAE_FOAM_OK";
        cmds.push_back("bash -c " + q(script));
    } else {
        for (const auto& step : s->steps) {
            const std::string exe = findExecutable(step.exeNames, opt.executableHint, s->envVar);
            if (exe.empty()) {
                R.message = "Chua cai '" + s->name + "' (khong thay " + step.exeNames.front() + "). Dat bien " + s->envVar + " hoac duong dan trong Quan ly solver.";
                return R;
            }
            const std::string args = expandArgs(opt.argsOverride.empty() ? step.args : opt.argsOverride, input.string());
            cmds.push_back(withExtra(q(exe) + " " + args, opt.extraArgs));
        }
    }

    int rc = 0;
    std::string captured;
    for (const auto& c : cmds) {
        R.commands.push_back(c);
        emit("$ " + c + "\n");
        captured.clear();
        rc = runCommandLine(c, R.workDir, emit, opt.cancel, &captured);
        R.exitCode = rc;
        if (opt.cancel && opt.cancel->load()) { R.message = "Da dung theo yeu cau"; return R; }
        if (rc != 0) { R.message = "Lenh that bai (ma thoat " + std::to_string(rc) + ")"; break; }
    }
    if (isFoam && rc == 0 && captured.find("FASTCAE_FOAM_OK") == std::string::npos) {
        rc = 1; R.exitCode = 1; R.message = "OpenFOAM that bai: xem log.* trong " + caseDir.string();
    }
    R.ok = rc == 0;
    if (R.ok) R.message = "Hoan tat";

    std::vector<std::pair<std::string, fs::file_time_type>> after;
    snapshot(R.workDir, after);
    for (const auto& a : after) {
        bool changed = true;
        for (const auto& b : before)
            if (b.first == a.first && b.second == a.second) { changed = false; break; }
        if (changed) R.files.push_back(a.first);
    }
    return R;
}

static std::string jesc(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += static_cast<char>(c);
    }
    return o + "\"";
}

std::string solversJson() {
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& s : allSolvers()) {
        std::string path = s.integration == Integration::Builtin ? "" : solverRuntimePath(s);
        os << (first ? "" : ",") << "{\"id\":" << jesc(s.id) << ",\"name\":" << jesc(s.name) << ",\"license\":" << jesc(s.license)
           << ",\"env\":" << jesc(s.envVar) << ",\"installed\":" << ((path.empty() && s.integration != Integration::Builtin) ? "false" : "true")
           << ",\"path\":" << jesc(path) << ",\"runnable\":" << (s.steps.empty() && s.id != "openfoam" ? "false" : "true")
           << ",\"capability\":" << jesc(s.capability) << ",\"steps\":[";
        for (std::size_t i = 0; i < s.steps.size(); ++i)
            os << (i ? "," : "") << "{\"exe\":" << jesc(findExecutable(s.steps[i].exeNames, "", s.envVar)) << ",\"names\":" << jesc(s.steps[i].exeNames.front())
               << ",\"args\":" << jesc(s.steps[i].args) << "}";
        os << "]}";
        first = false;
    }
    os << "]";
    return os.str();
}

}  // namespace fastcae
