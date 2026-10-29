// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Trần Minh Tâm
#include "script/script_engine.hpp"

// Lua được biên dịch như C++ (xem CMakeLists.txt) nên không bọc extern "C".
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include "app/case_runner.hpp"
#include "cad/frame_templates.hpp"
#include "cad/shape_library.hpp"
#include "core/about.hpp"
#include "core/case_file.hpp"
#include "core/vtk_writer.hpp"
#include "solver/frame.hpp"
#include "solver/sparse.hpp"

namespace fastcae {

struct ScriptEngine::Impl {
    lua_State* L = nullptr;
    std::function<void(const std::string&)> printer;
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(const RunReport&)> showHandler;
    std::function<void(const std::string&)> openCaseHandler;
};

namespace {

const char* kImplKey = "fastcae.impl";
const char* kResultMeta = "fastcae.result";
const char* kHandleMeta = "fastcae.handle";

ScriptEngine::Impl* implOf(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kImplKey);
    auto* p = static_cast<ScriptEngine::Impl*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return p;
}

void emit(lua_State* L, const std::string& s) {
    if (auto* im = implOf(L); im && im->printer) im->printer(s);
}

// Báo lỗi sang Lua: đặt thông báo rồi giải phóng chuỗi trước khi longjmp (không để C++ có destructor còn sống).
int raise(lua_State* L, std::string& msg) {
    lua_pushstring(L, msg.c_str());
    msg.clear();
    msg.shrink_to_fit();
    return lua_error(L);
}

// ---------------------------------------------------------------- in ra
int l_print(lua_State* L) {
    const int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        std::size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) line += '\t';
        line.append(s, len);
        lua_pop(L, 1);
    }
    line += '\n';
    emit(L, line);
    return 0;
}

int l_io_write(lua_State* L) {
    const int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        std::size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        out.append(s, len);
        lua_pop(L, 1);
    }
    emit(L, out);
    return 0;
}

// ---------------------------------------------------------------- kết quả
struct Handle {
    std::shared_ptr<RunReport> rep;
};

int handleGc(lua_State* L) {
    static_cast<Handle*>(lua_touserdata(L, 1))->~Handle();
    return 0;
}

RunReport* repOf(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    lua_getfield(L, idx, "_h");
    auto* h = static_cast<Handle*>(luaL_testudata(L, -1, kHandleMeta));
    lua_pop(L, 1);
    if (!h || !h->rep) luaL_error(L, "doi tuong ket qua khong hop le (dung result:method(), khong dung result.method())");
    return h->rep.get();
}

int r_fields(lua_State* L) {
    RunReport* r = repOf(L, 1);
    lua_newtable(L);
    int i = 1;
    for (const auto& kv : r->fields.pointScalars) { lua_pushstring(L, kv.first.c_str()); lua_rawseti(L, -2, i++); }
    for (const auto& kv : r->fields.pointVectors) { lua_pushstring(L, kv.first.c_str()); lua_rawseti(L, -2, i++); }
    return 1;
}

int r_field(lua_State* L) {
    RunReport* r = repOf(L, 1);
    const std::string name = luaL_checkstring(L, 2);
    auto s = r->fields.pointScalars.find(name);
    if (s != r->fields.pointScalars.end()) {
        lua_createtable(L, static_cast<int>(s->second.size()), 0);
        for (std::size_t i = 0; i < s->second.size(); ++i) { lua_pushnumber(L, s->second[i]); lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); }
        return 1;
    }
    auto v = r->fields.pointVectors.find(name);
    if (v != r->fields.pointVectors.end()) {
        const std::size_t n = v->second.size() / 3;
        lua_createtable(L, static_cast<int>(n), 0);
        for (std::size_t i = 0; i < n; ++i) {
            lua_createtable(L, 3, 0);
            for (int c = 0; c < 3; ++c) { lua_pushnumber(L, v->second[3 * i + c]); lua_rawseti(L, -2, c + 1); }
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        return 1;
    }
    std::string msg = "khong co truong '" + name + "' (xem result:fields())";
    return raise(L, msg);
}

int r_field_stats(lua_State* L) {
    RunReport* r = repOf(L, 1);
    const std::string name = luaL_checkstring(L, 2);
    auto s = r->fields.pointScalars.find(name);
    if (s == r->fields.pointScalars.end() || s->second.empty()) {
        std::string msg = "khong co truong vo huong '" + name + "'";
        return raise(L, msg);
    }
    double mn = s->second[0], mx = s->second[0], sum = 0;
    for (double v : s->second) { mn = std::min(mn, v); mx = std::max(mx, v); sum += v; }
    lua_pushnumber(L, mn);
    lua_pushnumber(L, mx);
    lua_pushnumber(L, sum / static_cast<double>(s->second.size()));
    return 3;
}

int r_node(lua_State* L) {
    RunReport* r = repOf(L, 1);
    const lua_Integer i = luaL_checkinteger(L, 2);
    if (i < 1 || i > static_cast<lua_Integer>(r->mesh.nodes.size())) return luaL_error(L, "chi so nut ngoai pham vi 1..%d", static_cast<int>(r->mesh.nodes.size()));
    const auto& p = r->mesh.nodes[static_cast<std::size_t>(i - 1)];
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    lua_pushnumber(L, p.z);
    return 3;
}

int r_save_vtk(lua_State* L) {
    RunReport* r = repOf(L, 1);
    const std::string path = luaL_checkstring(L, 2);
    std::string err;
    const bool ok = r->mesh.numCells() > 0 && writeVtk(path, r->mesh, r->fields, &err);
    if (!ok && err.empty()) err = "ket qua khong co luoi de ghi";
    lua_pushboolean(L, ok);
    if (!ok) {
        lua_pushstring(L, err.c_str());
        return 2;
    }
    return 1;
}

void pushResult(lua_State* L, const RunReport& rep, const std::string& log) {
    lua_newtable(L);
    lua_pushboolean(L, rep.ok); lua_setfield(L, -2, "ok");
    lua_pushstring(L, rep.message.c_str()); lua_setfield(L, -2, "message");
    lua_pushstring(L, rep.type.c_str()); lua_setfield(L, -2, "type");
    lua_pushstring(L, rep.summary.c_str()); lua_setfield(L, -2, "summary");
    lua_pushstring(L, log.c_str()); lua_setfield(L, -2, "log");
    lua_pushnumber(L, rep.maxValue); lua_setfield(L, -2, "max_value");
    lua_pushnumber(L, rep.secondary); lua_setfield(L, -2, "secondary");
    lua_pushinteger(L, rep.iterations); lua_setfield(L, -2, "iterations");
    lua_pushinteger(L, static_cast<lua_Integer>(rep.mesh.numNodes())); lua_setfield(L, -2, "nodes");
    lua_pushinteger(L, static_cast<lua_Integer>(rep.mesh.numCells())); lua_setfield(L, -2, "cells");
    lua_newtable(L);
    for (const auto& kv : rep.values) { lua_pushnumber(L, kv.second); lua_setfield(L, -2, kv.first.c_str()); }
    lua_setfield(L, -2, "values");
    lua_newtable(L);
    for (const auto& kv : rep.series) {
        lua_createtable(L, static_cast<int>(kv.second.size()), 0);
        for (std::size_t i = 0; i < kv.second.size(); ++i) { lua_pushnumber(L, kv.second[i]); lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); }
        lua_setfield(L, -2, kv.first.c_str());
    }
    lua_setfield(L, -2, "series");
    void* mem = lua_newuserdatauv(L, sizeof(Handle), 0);
    auto* h = new (mem) Handle{std::make_shared<RunReport>(rep)};
    (void)h;
    luaL_setmetatable(L, kHandleMeta);
    lua_setfield(L, -2, "_h");
    luaL_setmetatable(L, kResultMeta);
}

// ---------------------------------------------------------------- fastcae.*
std::vector<double> numberArray(lua_State* L, int idx) {
    std::vector<double> v;
    if (lua_istable(L, idx)) {
        const lua_Integer n = luaL_len(L, idx);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, idx, i);
            if (!lua_isnumber(L, -1)) { lua_pop(L, 1); luaL_error(L, "tham so thu %d phai la so", static_cast<int>(i)); }
            v.push_back(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
    }
    return v;
}

int l_run_impl(lua_State* L, bool mustSucceed) {
    const std::string text = luaL_checkstring(L, 1);
    std::string base;
    bool write = false;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "base_dir");
        if (lua_isstring(L, -1)) base = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "write_output");
        write = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    RunReport rep;
    std::string log, err;
    try {
        CaseFile cf;
        cf.parse(text);
        std::ostringstream os;
        rep = runCase(cf, os, base, write);
        log = os.str();
    } catch (const std::exception& e) {
        err = std::string("loi khi chay: ") + e.what();
    }
    if (!err.empty()) return raise(L, err);
    if (mustSucceed && !rep.ok) {
        std::string msg = "giai that bai: " + rep.message;
        return raise(L, msg);
    }
    pushResult(L, rep, log);
    return 1;
}
int l_run(lua_State* L) { return l_run_impl(L, false); }
int l_solve(lua_State* L) { return l_run_impl(L, true); }

// fastcae.case{type="frame", node={"1 0 0 0", ...}, E=2.1e11}  ->  văn bản file case
int l_case(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    std::vector<std::pair<std::string, std::vector<std::string>>> items;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_type(L, -2) != LUA_TSTRING) { lua_pop(L, 1); continue; }
        const std::string key = lua_tostring(L, -2);
        std::vector<std::string> vals;
        if (lua_istable(L, -1)) {
            const lua_Integer n = luaL_len(L, -1);
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, -1, i);
                std::size_t len = 0;
                const char* s = luaL_tolstring(L, -1, &len);
                vals.emplace_back(s, len);
                lua_pop(L, 2);
            }
        } else {
            std::size_t len = 0;
            const char* s = luaL_tolstring(L, -1, &len);
            vals.emplace_back(s, len);
            lua_pop(L, 1);
        }
        items.emplace_back(key, std::move(vals));
        lua_pop(L, 1);
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        if (a.first == "type") return b.first != "type";
        if (b.first == "type") return false;
        return a.first < b.first;
    });
    std::ostringstream o;
    o << "# Tao boi script FastCAE - " << about::kAuthor << "\n";
    for (const auto& it : items)
        for (const auto& v : it.second) o << it.first << " = " << v << "\n";
    const std::string s = o.str();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int l_templates(lua_State* L) {
    lua_newtable(L);
    int idx = 1;
    auto add = [&](const ShapeInfo& s, const char* kind) {
        lua_newtable(L);
        lua_pushstring(L, s.name.c_str()); lua_setfield(L, -2, "name");
        lua_pushstring(L, s.label.c_str()); lua_setfield(L, -2, "label");
        lua_pushstring(L, s.category.c_str()); lua_setfield(L, -2, "category");
        lua_pushstring(L, kind); lua_setfield(L, -2, "kind");
        lua_newtable(L);
        int pi = 1;
        for (const auto& p : s.params) {
            lua_newtable(L);
            lua_pushstring(L, p.key.c_str()); lua_setfield(L, -2, "key");
            lua_pushstring(L, p.label.c_str()); lua_setfield(L, -2, "label");
            lua_pushnumber(L, p.def); lua_setfield(L, -2, "default");
            lua_rawseti(L, -2, pi++);
        }
        lua_setfield(L, -2, "params");
        lua_rawseti(L, -2, idx++);
    };
    for (const auto& s : shapeCatalog()) add(s, "solid");
    for (const auto& s : frameTemplateCatalog()) add(s, "frame");
    for (const auto& s : shellTemplateCatalog()) add(s, "shell");
    return 1;
}

int templateText(lua_State* L, int kind) {
    const std::string name = luaL_checkstring(L, 1);
    std::vector<double> p = numberArray(L, 2);
    const std::string material = lua_isstring(L, 3) ? lua_tostring(L, 3) : "steel";
    std::string text, err;
    {
        const ShapeInfo* info = kind == 0 ? findShape(name) : kind == 1 ? findFrameTemplate(name) : findShellTemplate(name);
        if (!info) err = "khong co mau '" + name + "' (xem fastcae.templates())";
        else {
            if (p.empty()) for (const auto& q : info->params) p.push_back(q.def);
            if (p.size() < info->params.size()) err = "thieu tham so cho '" + name + "' (can " + std::to_string(info->params.size()) + ")";
            else if (kind == 0) text = shapeCaseText(name, p, material);
            else if (kind == 1) text = frameTemplateText(name, p, err);
            else text = shellTemplateText(name, p, err);
            if (text.empty() && err.empty()) err = "tham so khong hop le";
        }
    }
    if (!err.empty()) return raise(L, err);
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}
int l_shape(lua_State* L) { return templateText(L, 0); }
int l_frame_template(lua_State* L) { return templateText(L, 1); }
int l_shell_template(lua_State* L) { return templateText(L, 2); }

// fastcae.section("ibeam", b, h, tf, tw) -> {A=, Iy=, Iz=, J=, width=, height=}
int l_section(lua_State* L) {
    const std::string kind = luaL_checkstring(L, 1);
    std::vector<double> p;
    for (int i = 2; i <= lua_gettop(L); ++i) p.push_back(luaL_checknumber(L, i));
    FrameSection s;
    std::string err;
    const bool ok = makeSection("S", kind, p, s, err);
    if (!ok) return raise(L, err);
    lua_newtable(L);
    lua_pushnumber(L, s.A); lua_setfield(L, -2, "A");
    lua_pushnumber(L, s.Iy); lua_setfield(L, -2, "Iy");
    lua_pushnumber(L, s.Iz); lua_setfield(L, -2, "Iz");
    lua_pushnumber(L, s.J); lua_setfield(L, -2, "J");
    lua_pushnumber(L, s.width); lua_setfield(L, -2, "width");
    lua_pushnumber(L, s.height); lua_setfield(L, -2, "height");
    return 1;
}

int l_write_csv(lua_State* L) {
    const std::string path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    std::ostringstream o;
    auto row = [&](int idx) {
        const lua_Integer n = luaL_len(L, idx);
        for (lua_Integer j = 1; j <= n; ++j) {
            lua_rawgeti(L, idx, j);
            std::size_t len = 0;
            const char* s = luaL_tolstring(L, -1, &len);
            if (j > 1) o << ',';
            std::string cell(s, len);
            if (cell.find_first_of(",\"\n") != std::string::npos) {
                std::string q = "\"";
                for (char ch : cell) q += (ch == '"') ? std::string("\"\"") : std::string(1, ch);
                cell = q + "\"";
            }
            o << cell;
            lua_pop(L, 2);
        }
        o << "\n";
    };
    if (lua_istable(L, 3)) row(3);   // dòng tiêu đề (tuỳ chọn)
    const lua_Integer n = luaL_len(L, 2);
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, 2, i);
        if (lua_istable(L, -1)) row(lua_gettop(L));
        lua_pop(L, 1);
    }
    std::ofstream f(path);
    const bool ok = static_cast<bool>(f);
    if (ok) f << o.str();
    lua_pushboolean(L, ok);
    return 1;
}

int l_set_linear_solver(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    const std::string opts = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    lua_pushboolean(L, setLinearSolver(name, opts));
    return 1;
}

int l_petsc_available(lua_State* L) {
    lua_pushboolean(L, petscAvailable());
    return 1;
}

int l_read_file(lua_State* L) {
    const std::string path = luaL_checkstring(L, 1);
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        lua_pushnil(L);
        lua_pushstring(L, "khong doc duoc file");
        return 2;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int l_show(lua_State* L) {
    RunReport* r = repOf(L, 1);
    auto* im = implOf(L);
    if (im && im->showHandler) im->showHandler(*r);
    else emit(L, "(fastcae.show chi co trong giao dien do hoa; dung res:save_vtk(path) de xem bang ParaView)\n");
    return 0;
}

int l_open_case(lua_State* L) {
    const std::string text = luaL_checkstring(L, 1);
    auto* im = implOf(L);
    if (im && im->openCaseHandler) im->openCaseHandler(text);
    else emit(L, "(fastcae.open_case chi co trong giao dien do hoa)\n");
    return 0;
}

int l_denied(lua_State* L) { return luaL_error(L, "ham nay bi tat trong script FastCAE (an toan)"); }

void cancelHook(lua_State* L, lua_Debug*) {
    auto* im = implOf(L);
    if (im && im->cancel && im->cancel->load()) luaL_error(L, "script da bi dung");
}

void registerApi(lua_State* L) {
    luaL_newmetatable(L, kHandleMeta);
    lua_pushcfunction(L, handleGc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    luaL_newmetatable(L, kResultMeta);
    lua_newtable(L);
    static const luaL_Reg methods[] = {{"fields", r_fields}, {"field", r_field}, {"field_stats", r_field_stats},
                                       {"node", r_node}, {"save_vtk", r_save_vtk}, {nullptr, nullptr}};
    luaL_setfuncs(L, methods, 0);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    static const luaL_Reg funcs[] = {{"run", l_run}, {"solve", l_solve}, {"case", l_case}, {"templates", l_templates},
                                     {"shape", l_shape}, {"frame_template", l_frame_template}, {"shell_template", l_shell_template},
                                     {"section", l_section}, {"write_csv", l_write_csv}, {"set_linear_solver", l_set_linear_solver},
                                     {"petsc_available", l_petsc_available}, {"read_file", l_read_file}, {"show", l_show},
                                     {"open_case", l_open_case}, {nullptr, nullptr}};
    luaL_newlib(L, funcs);
    lua_pushstring(L, FASTCAE_VERSION); lua_setfield(L, -2, "version");
    lua_pushstring(L, about::kAuthor); lua_setfield(L, -2, "author");
    // vật liệu mẫu
    lua_newtable(L);
    struct Mat { const char* n; double E, nu, rho, fy; };
    for (const Mat& m : {Mat{"steel", 210e9, 0.30, 7850, 235e6}, Mat{"concrete", 30e9, 0.20, 2500, 0}, Mat{"aluminium", 70e9, 0.33, 2700, 240e6}}) {
        lua_newtable(L);
        lua_pushnumber(L, m.E); lua_setfield(L, -2, "E");
        lua_pushnumber(L, m.nu); lua_setfield(L, -2, "nu");
        lua_pushnumber(L, m.rho); lua_setfield(L, -2, "rho");
        lua_pushnumber(L, m.fy); lua_setfield(L, -2, "fy");
        lua_setfield(L, -2, m.n);
    }
    lua_setfield(L, -2, "materials");
    lua_setglobal(L, "fastcae");
}

// Tắt các hàm chạy lệnh hệ điều hành và nạp thư viện động; giữ io.open để ghi CSV, kết quả.
void sandbox(lua_State* L) {
    lua_getglobal(L, "os");
    for (const char* f : {"execute", "exit", "remove", "rename", "tmpname", "setlocale"}) {
        lua_pushcfunction(L, l_denied);
        lua_setfield(L, -2, f);
    }
    lua_pop(L, 1);
    lua_getglobal(L, "io");
    lua_pushcfunction(L, l_denied);
    lua_setfield(L, -2, "popen");
    lua_pushcfunction(L, l_io_write);
    lua_setfield(L, -2, "write");
    lua_pop(L, 1);
    lua_getglobal(L, "package");
    lua_pushnil(L);
    lua_setfield(L, -2, "loadlib");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");
}

}  // namespace

ScriptEngine::ScriptEngine() : impl_(new Impl) {
    impl_->L = luaL_newstate();
    lua_State* L = impl_->L;
    luaL_openlibs(L);
    lua_pushlightuserdata(L, impl_.get());
    lua_setfield(L, LUA_REGISTRYINDEX, kImplKey);
    registerApi(L);
    sandbox(L);
    lua_sethook(L, cancelHook, LUA_MASKCOUNT, 10000);
}

ScriptEngine::~ScriptEngine() {
    if (impl_ && impl_->L) lua_close(impl_->L);
}

void ScriptEngine::setPrinter(std::function<void(const std::string&)> printer) { impl_->printer = std::move(printer); }
void ScriptEngine::setShowHandler(std::function<void(const RunReport&)> handler) { impl_->showHandler = std::move(handler); }
void ScriptEngine::setOpenCaseHandler(std::function<void(const std::string&)> handler) { impl_->openCaseHandler = std::move(handler); }
void ScriptEngine::setCancelFlag(const std::atomic<bool>* flag) { impl_->cancel = flag; }

void ScriptEngine::setArguments(const std::string& scriptName, const std::vector<std::string>& args) {
    lua_State* L = impl_->L;
    lua_newtable(L);
    lua_pushstring(L, scriptName.c_str());
    lua_rawseti(L, -2, 0);
    for (std::size_t i = 0; i < args.size(); ++i) {
        lua_pushstring(L, args[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setglobal(L, "arg");
}

ScriptOutcome ScriptEngine::runString(const std::string& code, const std::string& chunkName) {
    ScriptOutcome out;
    lua_State* L = impl_->L;
    const std::string name = "=" + chunkName;
    int rc = luaL_loadbufferx(L, code.data(), code.size(), name.c_str(), "t");
    if (rc == LUA_OK) rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) {
        out.error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "loi khong ro";
        lua_pop(L, 1);
        return out;
    }
    out.ok = true;
    return out;
}

ScriptOutcome ScriptEngine::runFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ScriptOutcome o;
        o.error = "Khong doc duoc file " + path;
        return o;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string code = ss.str();
    if (code.size() >= 3 && static_cast<unsigned char>(code[0]) == 0xEF && static_cast<unsigned char>(code[1]) == 0xBB &&
        static_cast<unsigned char>(code[2]) == 0xBF)
        code.erase(0, 3);   // bỏ BOM UTF-8
    const auto slash = path.find_last_of("/\\");
    return runString(code, slash == std::string::npos ? path : path.substr(slash + 1));
}

const char* scriptApiHelp() {
    return
"BẢNG `fastcae` (Lua 5.4, kèm đủ thư viện chuẩn: math, string, table, io.open, os.time...)\n"
"\n"
"Chạy bài toán\n"
"  res = fastcae.run(caseText [, {base_dir=\"...\", write_output=true}])   -> bảng kết quả (không báo lỗi; kiểm tra res.ok)\n"
"  res = fastcae.solve(caseText)                                           -> như run nhưng báo lỗi nếu giải thất bại\n"
"  res.ok, res.message, res.type, res.summary, res.log, res.nodes, res.cells, res.iterations\n"
"  res.values.max_displacement, max_stress, max_moment, max_axial, max_utilization, reaction_fz,\n"
"             max_von_mises, t_max, t_min, frequency_1_hz, load_factor_1, ...   (tuỳ loại bài toán)\n"
"  res.series.node_uz, reaction_fz, member_n_max, member_m_max, member_stress_max, frequencies_hz, load_factors, ...\n"
"  res:fields()  res:field(\"von_mises_nodal\")  res:field_stats(name) -> min, max, mean\n"
"  res:node(i) -> x, y, z    res:save_vtk(path)\n"
"\n"
"Dựng mô hình\n"
"  fastcae.case{type=\"frame\", E=210e9, node={\"1 0 0 0\",\"2 3 0 0\"}, member=\"1 2 IPE beam\", ...}  -> văn bản case (khoá lặp = mảng)\n"
"  fastcae.templates()                                   -> danh sách cấu kiện {name,label,category,kind,params}\n"
"  fastcae.shape(name, {p1,p2,...} [, \"steel\"|\"concrete\"|\"aluminium\"])   dầm I/U/L/T, tấm, tường, móng... (khối đặc)\n"
"  fastcae.frame_template(name, {...})                   dầm, khung, dàn 1D\n"
"  fastcae.shell_template(name, {...})                   tấm/vỏ (cần CalculiX)\n"
"\n"
"Tính toán kỹ thuật\n"
"  s = fastcae.section(\"ibeam\", b, h, tf, tw)           -> A, Iy, Iz, J, width, height   (rect, circle, pipe, ibeam, box, general)\n"
"  fastcae.materials.steel / concrete / aluminium        -> E, nu, rho, fy\n"
"  fastcae.write_csv(path, rows [, header])              ghi bảng số (rows = mảng các mảng)\n"
"  fastcae.read_file(path)   fastcae.set_linear_solver(\"auto\"|\"direct\"|\"cg\"|\"petsc\")   fastcae.petsc_available()\n"
"  fastcae.show(res)                                     (chỉ trong giao diện) hiển thị kết quả lên khung 3D\n"
"  fastcae.open_case(text)                               (chỉ trong giao diện) nạp case vào bảng Bài toán và giải\n"
"  fastcae.version, fastcae.author\n"
"\n"
"An toàn: os.execute, os.exit, os.remove, os.rename, io.popen, dofile, loadfile, package.loadlib bị tắt. io.open vẫn dùng được.\n";
}

}  // namespace fastcae
