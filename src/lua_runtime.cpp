#include "lua_runtime.h"
#include "system.h"
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include <QRegularExpression>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr size_t MemoryLimit = 1024 * 1024;
constexpr int OutputLimit = 64 * 1024;
constexpr int SourceLimit = 128 * 1024;
constexpr int FileLimit = 256 * 1024;
constexpr int InstructionLimit = 500000;
constexpr int InputLimit = 4096;
bool validInputText(const QString &value) {
    return !value.contains(QChar::Null) && value.toUtf8().size() <= InputLimit &&
           QString::fromUtf8(value.toUtf8()) == value;
}
} // namespace
struct LuaRuntime::Impl {
    SimSystem *system;
    lua_State *state = nullptr;
    size_t used = 0;
    int remaining = InstructionLimit;
    QByteArray output, scratch;
    QVariant value;
    QString title, text, apiError;
    QStringList labels;
    QList<LuaInput> inputs, pendingInputs;
    std::vector<int> callbacks;
    bool truncated = false;
    explicit Impl(SimSystem *s) : system(s) {}
    ~Impl() {
        if (state)
            lua_close(state);
    }
    static void *allocate(void *opaque, void *ptr, size_t oldSize, size_t size) {
        auto *self = static_cast<Impl *>(opaque);
        if (!ptr)
            oldSize = 0;
        if (size == 0) {
            std::free(ptr);
            self->used -= qMin(self->used, oldSize);
            return nullptr;
        }
        if (size > MemoryLimit || size > MemoryLimit - (self->used - qMin(self->used, oldSize)))
            return nullptr;
        void *next = std::realloc(ptr, size);
        if (next)
            self->used = self->used - qMin(self->used, oldSize) + size;
        return next;
    }
    static Impl *self(lua_State *L) { return *static_cast<Impl **>(lua_getextraspace(L)); }
    static void charge(lua_State *L, int amount) {
        auto *p = self(L);
        p->remaining -= amount;
        if (p->remaining <= 0)
            luaL_error(L, "AURORA instruction limit exceeded");
    }
    static void hook(lua_State *L, lua_Debug *) { charge(L, 1000); }
    static int boundedTable(lua_State *L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        const size_t size = lua_rawlen(L, 1);
        if (size > 4096)
            return luaL_error(L, "AURORA table operation limit exceeded (4096 entries)");
        charge(L, int(qMax(size, size_t(1))) * int(lua_tointeger(L, lua_upvalueindex(2))));
        const int args = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, args, LUA_MULTRET);
        return lua_gettop(L);
    }
    static bool argument(lua_State *L, int index, int max, Impl *p) {
        size_t size = 0;
        const char *bytes = lua_tolstring(L, index, &size);
        if (!bytes || size > size_t(max)) {
            p->apiError = "Expected text within the API size limit.";
            return false;
        }
        p->scratch = QByteArray(bytes, qsizetype(size));
        return true;
    }
    static int failure(lua_State *L) {
        auto *p = self(L);
        p->scratch = p->apiError.toUtf8();
        lua_pushnil(L);
        lua_pushlstring(L, p->scratch.constData(), size_t(p->scratch.size()));
        return 2;
    }
    int failResult(lua_State *L, const QString &message) {
        apiError = message;
        return failure(L);
    }
    void append(const char *bytes, size_t size) {
        const int free = OutputLimit - int(output.size());
        if (free > 0)
            output.append(bytes, qsizetype(qMin(size, size_t(free))));
        if (size > size_t(qMax(free, 0)))
            truncated = true;
    }
    static int print(lua_State *L) {
        auto *p = self(L);
        const int count = qMin(lua_gettop(L), 32);
        for (int i = 1; i <= count; ++i) {
            if (i > 1)
                p->append("\t", 1);
            size_t size = 0;
            const char *bytes = luaL_tolstring(L, i, &size);
            p->append(bytes, size);
            lua_pop(L, 1);
        }
        p->append("\n", 1);
        return 0;
    }
    static int read(lua_State *L) {
        charge(L, 2000);
        auto *p = self(L);
        if (!argument(L, 1, 1024, p))
            return failure(L);
        p->scratch = p->system->read(QString::fromUtf8(p->scratch));
        p->apiError = p->system->lastError();
        if (!p->apiError.isEmpty())
            return failure(L);
        if (p->scratch.size() > FileLimit) {
            p->apiError = "File exceeds the Lua reader limit (256 KiB).";
            return failure(L);
        }
        lua_pushlstring(L, p->scratch.constData(), size_t(p->scratch.size()));
        return 1;
    }
    static int write(lua_State *L) {
        charge(L, 50000);
        auto *p = self(L);
        if (!argument(L, 1, 1024, p))
            return failure(L);
        // No nontrivial stack objects stay alive across Lua allocation calls.
        p->apiError = QString::fromUtf8(p->scratch);
        if (!argument(L, 2, FileLimit, p))
            return failure(L);
        const bool ok = p->system->write(p->apiError, p->scratch);
        p->apiError = ok ? QString() : p->system->lastError();
        if (!ok)
            return failure(L);
        lua_pushboolean(L, true);
        return 1;
    }
    static int mkdir(lua_State *L) {
        charge(L, 50000);
        auto *p = self(L);
        if (!argument(L, 1, 1024, p))
            return failure(L);
        const bool ok = p->system->mkdir(QString::fromUtf8(p->scratch));
        p->apiError = ok ? QString() : p->system->lastError();
        if (!ok)
            return failure(L);
        lua_pushboolean(L, true);
        return 1;
    }
    QList<VEntry> entries;
    static int list(lua_State *L) {
        charge(L, 2000);
        auto *p = self(L);
        if (!argument(L, 1, 1024, p))
            return failure(L);
        p->entries = p->system->list(QString::fromUtf8(p->scratch));
        p->apiError = p->system->lastError();
        if (!p->apiError.isEmpty())
            return failure(L);
        if (p->entries.size() > 1024) {
            p->apiError = "Directory has too many entries for Lua (limit 1024).";
            return failure(L);
        }
        lua_createtable(L, int(p->entries.size()), 0);
        for (int i = 0; i < p->entries.size(); ++i) {
            p->scratch = p->entries[i].path.toUtf8();
            lua_pushlstring(L, p->scratch.constData(), size_t(p->scratch.size()));
            lua_rawseti(L, -2, i + 1);
        }
        return 1;
    }
    static int setting(lua_State *L) {
        charge(L, lua_gettop(L) < 2 ? 500 : 50000);
        auto *p = self(L);
        if (!argument(L, 1, 40, p))
            return failure(L);
        bool ok = false;
        {
            const QString key = QString::fromUtf8(p->scratch);
            QVariant value;
            if (lua_gettop(L) < 2) {
                if (!QStringList{"theme", "fontSize", "scanlines", "wallpaperPath", "loginMessage",
                                 "displayName", "profileBio"}
                         .contains(key)) {
                    p->apiError = "This setting is not available to Lua mods.";
                } else {
                    p->value = p->system->setting(key);
                    p->scratch = p->value.toString().toUtf8();
                    ok = true;
                }
            } else {
                if (key == "theme") {
                    if (lua_type(L, 2) == LUA_TSTRING) {
                        size_t n = 0;
                        const char *v = lua_tolstring(L, 2, &n);
                        if (n <= 16)
                            value = QString::fromUtf8(v, qsizetype(n));
                    }
                    if (!QStringList{"green", "amber", "paper"}.contains(value.toString()))
                        value.clear();
                } else if (key == "fontSize") {
                    if (lua_isinteger(L, 2)) {
                        const auto n = lua_tointeger(L, 2);
                        if (n == 14 || n == 16 || n == 18 || n == 20)
                            value = int(n);
                    }
                } else if (key == "scanlines") {
                    if (lua_isboolean(L, 2))
                        value = bool(lua_toboolean(L, 2));
                } else if (QStringList{"wallpaperPath", "loginMessage", "displayName", "profileBio"}.contains(
                               key) &&
                           lua_type(L, 2) == LUA_TSTRING) {
                    size_t n = 0;
                    const char *v = lua_tolstring(L, 2, &n);
                    const size_t limit = key == "wallpaperPath" ? 1024 : key == "displayName" ? 64 : 256;
                    if (n <= limit) {
                        QString str = QString::fromUtf8(v, qsizetype(n));
                        if (!str.contains(QChar::Null))
                            value = str;
                    }
                    if (key == "wallpaperPath" && value.isValid() && !value.toString().isEmpty()) {
                        const QString path = p->system->normalize(value.toString());
                        if (!path.startsWith("/home/guest/") || !p->system->exists(path) ||
                            p->system->isDirectory(path))
                            value.clear();
                        else
                            value = path;
                    }
                }
                if (value.isValid()) {
                    ok = p->system->setSettings({{key, value}});
                    p->apiError = ok ? QString() : p->system->lastError();
                } else
                    p->apiError = "Setting not allowed, or value is outside its supported range.";
            }
        }
        if (!ok)
            return failure(L);
        if (lua_gettop(L) < 2) {
            if (!p->value.isValid())
                lua_pushnil(L);
            else if (p->value.metaType().id() == QMetaType::Bool)
                lua_pushboolean(L, p->value.toBool());
            else if (p->value.metaType().id() == QMetaType::Int ||
                     p->value.metaType().id() == QMetaType::LongLong ||
                     p->value.metaType().id() == QMetaType::Double)
                lua_pushnumber(L, p->value.toDouble());
            else
                lua_pushlstring(L, p->scratch.constData(), size_t(p->scratch.size()));
            return 1;
        }
        lua_pushboolean(L, true);
        return 1;
    }
    static int theme(lua_State *L) {
        charge(L, 50000);
        auto *p = self(L);
        if (!argument(L, 1, 16, p))
            return failure(L);
        p->apiError.clear();
        bool ok = false;
        {
            const QString value = QString::fromUtf8(p->scratch);
            if (QStringList{"green", "amber", "paper"}.contains(value))
                ok = p->system->setSettings({{"theme", value}});
            else
                p->apiError = "Choose green, amber or paper.";
        }
        if (!ok) {
            if (p->apiError.isEmpty())
                p->apiError = p->system->lastError();
            return failure(L);
        }
        lua_pushboolean(L, true);
        return 1;
    }
    static int notify(lua_State *L) {
        auto *p = self(L);
        if (!argument(L, 1, 4096, p))
            return failure(L);
        p->append(p->scratch.constData(), size_t(p->scratch.size()));
        p->append("\n", 1);
        return 0;
    }
    static int textApi(lua_State *L) {
        auto *p = self(L);
        if (!argument(L, 1, OutputLimit, p))
            return failure(L);
        p->text = QString::fromUtf8(p->scratch);
        return 0;
    }
    static bool inputText(lua_State *L, int index, int limit, Impl *p) {
        if (lua_type(L, index) != LUA_TSTRING || !argument(L, index, limit, p) ||
            p->scratch.contains('\0')) {
            p->apiError = "Input fields require UTF-8 text within their size limit.";
            return false;
        }
        if (QString::fromUtf8(p->scratch).toUtf8() != p->scratch) {
            p->apiError = "Input fields require valid UTF-8 text.";
            return false;
        }
        return true;
    }
    static int input(lua_State *L) {
        charge(L, 500);
        auto *p = self(L);
        if (!inputText(L, 1, 32, p))
            return failure(L);
        int index = -1;
        {
            const QString id = QString::fromUtf8(p->scratch);
            for (int i = 0; i < p->inputs.size(); ++i)
                if (p->inputs[i].id == id) {
                    index = i;
                    break;
                }
        }
        if (index < 0)
            return p->failResult(L, "Unknown input field.");
        if (lua_gettop(L) >= 2) {
            if (!inputText(L, 2, InputLimit, p))
                return failure(L);
            p->inputs[index].value = QString::fromUtf8(p->scratch);
            lua_pushboolean(L, true);
            return 1;
        }
        p->scratch = p->inputs[index].value.toUtf8();
        lua_pushlstring(L, p->scratch.constData(), size_t(p->scratch.size()));
        return 1;
    }
    static int inputError(lua_State *L, const char *message) {
        self(L)->pendingInputs.clear();
        return luaL_error(L, "%s", message);
    }
    static void parseInputs(lua_State *L) {
        auto *p = self(L);
        p->pendingInputs.clear();
        lua_getfield(L, 1, "inputs");
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return;
        }
        if (!lua_istable(L, -1) || lua_rawlen(L, -1) > 8)
            inputError(L, "aurora.window inputs must be a list of at most 8 fields.");
        const auto count = lua_rawlen(L, -1);
        for (size_t i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, lua_Integer(i));
            if (!lua_istable(L, -1))
                inputError(L, "Each input field must be a table.");
            p->pendingInputs.append(LuaInput{});
            lua_getfield(L, -1, "id");
            if (!inputText(L, -1, 32, p))
                inputError(L, "Each input field needs an ASCII id of 1 to 32 characters.");
            p->pendingInputs.last().id = QString::fromUtf8(p->scratch);
            lua_pop(L, 1);
            if (!QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,31}$")
                     .match(p->pendingInputs.last().id).hasMatch())
                inputError(L, "Each input id must start with a letter and use letters, digits or underscores.");
            for (int previous = 0; previous + 1 < p->pendingInputs.size(); ++previous)
                if (p->pendingInputs[previous].id == p->pendingInputs.last().id)
                    inputError(L, "Input ids must be unique.");
            p->pendingInputs.last().label = p->pendingInputs.last().id;
            lua_getfield(L, -1, "label");
            if (!lua_isnil(L, -1)) {
                if (!inputText(L, -1, 80, p))
                    inputError(L, "Input labels must be UTF-8 text of at most 80 bytes.");
                p->pendingInputs.last().label = QString::fromUtf8(p->scratch);
            }
            lua_pop(L, 1);
            lua_getfield(L, -1, "value");
            if (!lua_isnil(L, -1)) {
                if (!inputText(L, -1, InputLimit, p))
                    inputError(L, "Input values must be UTF-8 text of at most 4096 bytes.");
                p->pendingInputs.last().value = QString::fromUtf8(p->scratch);
            }
            lua_pop(L, 1);
            lua_getfield(L, -1, "multiline");
            if (!lua_isnil(L, -1) && !lua_isboolean(L, -1))
                inputError(L, "The input multiline option must be a boolean.");
            p->pendingInputs.last().multiline = lua_toboolean(L, -1);
            lua_pop(L, 2);
        }
        lua_pop(L, 1);
    }
    static int window(lua_State *L) {
        auto *p = self(L);
        if (!lua_istable(L, 1)) {
            p->apiError = "aurora.window expects a table.";
            return failure(L);
        }
        parseInputs(L);
        p->inputs = std::move(p->pendingInputs);
        for (int ref : p->callbacks)
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        p->callbacks.clear();
        p->labels.clear();
        lua_getfield(L, 1, "title");
        if (lua_type(L, -1) == LUA_TSTRING && argument(L, -1, 100, p))
            p->title = QString::fromUtf8(p->scratch);
        lua_pop(L, 1);
        lua_getfield(L, 1, "text");
        if (lua_type(L, -1) == LUA_TSTRING && argument(L, -1, OutputLimit, p))
            p->text = QString::fromUtf8(p->scratch);
        lua_pop(L, 1);
        lua_getfield(L, 1, "buttons");
        if (lua_istable(L, -1)) {
            const auto count = qMin(lua_rawlen(L, -1), size_t(16));
            for (size_t i = 1; i <= count; ++i) {
                lua_rawgeti(L, -1, lua_Integer(i));
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "label");
                    if (argument(L, -1, 80, p))
                        p->labels.append(QString::fromUtf8(p->scratch));
                    else
                        p->labels.append("Action");
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "action");
                    if (lua_isfunction(L, -1))
                        p->callbacks.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
                    else {
                        lua_pop(L, 1);
                        p->callbacks.push_back(LUA_NOREF);
                    }
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        return 0;
    }
    bool initialize() {
        if (state) {
            lua_close(state);
            state = nullptr;
        }
        used = 0;
        callbacks.clear();
        labels.clear();
        inputs.clear();
        pendingInputs.clear();
        title.clear();
        text.clear();
        output.clear();
        apiError.clear();
        truncated = false;
        state = lua_newstate(&allocate, this);
        if (!state)
            return false;
        *static_cast<Impl **>(lua_getextraspace(state)) = this;
        luaL_requiref(state, "_G", luaopen_base, 1);
        lua_pop(state, 1);
        luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
        lua_pop(state, 1);
        luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
        lua_pop(state, 1);
        luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
        lua_pop(state, 1);
        luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
        lua_pop(state, 1);
        lua_getglobal(state, "table");
        for (const char *name : {"insert", "remove", "sort"}) {
            lua_getfield(state, -1, name);
            lua_pushinteger(state, std::strcmp(name, "sort") == 0 ? 16 : 1);
            lua_pushcclosure(state, &boundedTable, 2);
            lua_setfield(state, -2, name);
        }
        lua_pushnil(state);
        lua_setfield(state, -2, "move");
        lua_pop(state, 1);
        for (const char *name : {"dofile", "loadfile", "load", "collectgarbage", "require", "pcall", "xpcall",
                                 "getmetatable", "setmetatable", "warn"}) {
            lua_pushnil(state);
            lua_setglobal(state, name);
        }
        lua_getglobal(state, "string");
        for (const char *name : {"dump", "match", "gmatch", "gsub", "find"}) {
            lua_pushnil(state);
            lua_setfield(state, -2, name);
        }
        lua_pop(state, 1);
        lua_pushcfunction(state, &print);
        lua_setglobal(state, "print");
        lua_createtable(state, 0, 11);
        const luaL_Reg functions[] = {{"read", &read},     {"write", &write},     {"list", &list},
                                      {"mkdir", &mkdir},   {"setting", &setting}, {"theme", &theme},
                                      {"notify", &notify}, {"window", &window},   {"text", &textApi},
                                      {"input", &input},
                                      {nullptr, nullptr}};
        luaL_setfuncs(state, functions, 0);
        lua_setglobal(state, "aurora");
        return true;
    }
    LuaResult result(bool ok, const QString &error = {}) const {
        return {ok,    QString::fromUtf8(output) + (truncated ? "\n[output truncated at 64 KiB]" : QString()),
                error, title,
                text,  labels, inputs};
    }
    LuaResult execute(int nargs) {
        remaining = InstructionLimit;
        lua_sethook(state, &hook, LUA_MASKCOUNT, 1000);
        const int status = lua_pcall(state, nargs, 0, 0);
        lua_sethook(state, nullptr, 0, 0);
        if (status != LUA_OK) {
            const char *message = lua_tostring(state, -1);
            QString error = message ? QString::fromUtf8(message).left(4096) : "Lua execution failed.";
            lua_settop(state, 0);
            return result(false, error);
        }
        return result(true);
    }
};
LuaRuntime::LuaRuntime(SimSystem *system) : impl_(std::make_unique<Impl>(system)) {}
LuaRuntime::~LuaRuntime() = default;
bool LuaRuntime::setInput(const QString &id, const QString &value) {
    if (!validInputText(value))
        return false;
    for (auto &input : impl_->inputs)
        if (input.id == id) {
            input.value = value;
            return true;
        }
    return false;
}
LuaResult LuaRuntime::run(const QByteArray &source, const QString &name) {
    if (source.isEmpty() || source.size() > SourceLimit || source.contains('\0') || source.startsWith('\x1b'))
        return {false, {}, "Expected a text Lua program between 1 byte and 128 KiB.", {}, {}, {}};
    if (!impl_->initialize())
        return {false, {}, "Could not allocate the Lua runtime.", {}, {}, {}};
    const QByteArray chunk = (name.isEmpty() ? QString("AURORA program") : name).left(120).toUtf8();
    if (luaL_loadbufferx(impl_->state, source.constData(), size_t(source.size()), chunk.constData(), "t") !=
        LUA_OK) {
        const char *error = lua_tostring(impl_->state, -1);
        QString message = error ? QString::fromUtf8(error).left(4096) : "Could not parse Lua program.";
        lua_settop(impl_->state, 0);
        return impl_->result(false, message);
    }
    return impl_->execute(0);
}
LuaResult LuaRuntime::click(int index) {
    if (!impl_->state || index < 0 || size_t(index) >= impl_->callbacks.size())
        return impl_->result(false, "Unknown Lua button.");
    int ref = impl_->callbacks[size_t(index)];
    if (ref == LUA_NOREF)
        return impl_->result(true);
    lua_rawgeti(impl_->state, LUA_REGISTRYINDEX, ref);
    return impl_->execute(0);
}
