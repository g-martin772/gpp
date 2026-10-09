module;
#include <lua.h>
#include <lualib.h>
#include <luacode.h>
#include <cstdlib>
module GPP.Scripting;

import :Luau;
import std;
import glm;

namespace GPP
{
    namespace
    {
        lua_State* Lua(void* state) { return static_cast<lua_State*>(state); }

        int Line(const std::string& message)
        {
            const auto first = message.find(':');
            if (first == std::string::npos) return 0;
            int line = 0;
            const char* begin = message.data() + first + 1;
            const auto [end, ec] = std::from_chars(begin, message.data() + message.size(), line);
            return ec == std::errc{} && end != begin && end < message.data() + message.size() && *end == ':' ? line : 0;
        }
    }

    struct LuauVm::Impl
    {
        lua_State* L{nullptr};
        LuauLimits Limits;
        std::size_t Used{0};
        std::vector<LuauNative> Natives;
        std::int64_t Safepoints{0};
        double Deadline{0.0};
        int Vec2Ref{LUA_NOREF};
        int Vec4Ref{LUA_NOREF};
        bool Sealed{false};

        static void* Alloc(void* ud, void* ptr, std::size_t oldSize, std::size_t newSize)
        {
            auto* self = static_cast<Impl*>(ud);
            if (!ptr) oldSize = 0;
            if (newSize == 0)
            {
                std::free(ptr);
                self->Used -= oldSize;
                return nullptr;
            }
            if (self->Used - oldSize + newSize > self->Limits.MaxBytes) return nullptr;
            void* result = std::realloc(ptr, newSize);
            if (result) self->Used += newSize - oldSize;
            return result;
        }

        [[noreturn]] static void Fail(lua_State* state, const char* message)
        {
            luaL_where(state, 0);
            lua_pushstring(state, message);
            lua_concat(state, 2);
            lua_error(state);
        }

        static void Interrupt(lua_State* state, int gc)
        {
            if (gc >= 0) return;
            auto* self = static_cast<Impl*>(lua_callbacks(state)->userdata);
            if (++self->Safepoints > self->Limits.MaxSafepoints)
            {
                Fail(state, "script exceeded the instruction limit");
            }
            if ((self->Safepoints & 0xFF) == 0 && lua_clock() > self->Deadline)
            {
                Fail(state, "script exceeded the time limit");
            }
        }

        static int Trampoline(lua_State* state)
        {
            auto* self = static_cast<Impl*>(lua_tolightuserdata(state, lua_upvalueindex(1)));
            const auto index = static_cast<std::size_t>(lua_tointeger(state, lua_upvalueindex(2)));
            LuauNativeCall call(state);
            std::string failure;
            try
            {
                return self->Natives[index](call);
            }
            catch (const std::exception& e)
            {
                failure = e.what();
            }
            luaL_error(state, "%s", failure.c_str());
        }

        void ResetBudget()
        {
            Safepoints = 0;
            Deadline = lua_clock() + Limits.MaxSeconds;
        }

        LuauError Pop(lua_State* state, const std::string& chunk = {})
        {
            LuauError error;
            const char* text = lua_tostring(state, -1);
            error.Message = text ? text : "unknown error";
            error.Chunk = chunk;
            error.Line = Line(error.Message);
            lua_pop(state, 1);
            return error;
        }

        std::expected<void, LuauError> Execute(lua_State* state, std::string_view source, std::string_view chunkName,
                                               int results)
        {
            lua_CompileOptions options{};
            options.optimizationLevel = 1;
            options.debugLevel = 1;
            std::size_t size = 0;
            char* bytecode = luau_compile(source.data(), source.size(), &options, &size);
            const std::string name(chunkName);
            const int status = luau_load(state, name.c_str(), bytecode, size, 0);
            std::free(bytecode);
            if (status != 0) return std::unexpected(Pop(state, name));
            ResetBudget();
            if (lua_pcall(state, 0, results, 0) != 0) return std::unexpected(Pop(state, name));
            return {};
        }
    };

    LuauVm::LuauVm() : m_Impl(std::make_unique<Impl>())
    {
        m_Impl->L = lua_newstate(&Impl::Alloc, m_Impl.get());
        lua_callbacks(m_Impl->L)->userdata = m_Impl.get();
        lua_callbacks(m_Impl->L)->interrupt = &Impl::Interrupt;
        luaL_openlibs(m_Impl->L);
    }

    LuauVm::~LuauVm()
    {
        if (m_Impl && m_Impl->L) lua_close(m_Impl->L);
    }

    void LuauVm::SetLimits(const LuauLimits& limits) { m_Impl->Limits = limits; }
    const LuauLimits& LuauVm::Limits() const { return m_Impl->Limits; }
    std::size_t LuauVm::BytesUsed() const { return m_Impl->Used; }

    void LuauVm::Register(const std::string_view table, const std::string_view name, LuauNative function)
    {
        lua_State* L = m_Impl->L;
        const std::string tableName(table);
        const std::string functionName(name);
        lua_getglobal(L, tableName.c_str());
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setglobal(L, tableName.c_str());
        }
        m_Impl->Natives.push_back(std::move(function));
        lua_pushlightuserdata(L, m_Impl.get());
        lua_pushinteger(L, static_cast<int>(m_Impl->Natives.size() - 1));
        lua_pushcclosurek(L, &Impl::Trampoline, functionName.c_str(), 2, nullptr);
        lua_setfield(L, -2, functionName.c_str());
        lua_pop(L, 1);
    }

    std::expected<void, LuauError> LuauVm::RunTrusted(const std::string_view source, const std::string_view chunkName)
    {
        return m_Impl->Execute(m_Impl->L, source, chunkName, 0);
    }

    void LuauVm::Seal()
    {
        lua_State* L = m_Impl->L;
        for (const char* removed : {"os", "debug", "loadstring", "load", "dofile", "require", "io"})
        {
            lua_pushnil(L);
            lua_setglobal(L, removed);
        }
        luaL_sandbox(L);
        m_Impl->Sealed = true;
    }

    std::expected<int, LuauError> LuauVm::Load(const std::string_view source, const std::string_view chunkName)
    {
        lua_State* L = m_Impl->L;
        lua_State* thread = lua_newthread(L);
        luaL_sandboxthread(thread);
        const int threadRef = lua_ref(L, -1);
        lua_pop(L, 1);
        auto result = m_Impl->Execute(thread, source, chunkName, 1);
        if (!result)
        {
            lua_unref(L, threadRef);
            lua_settop(thread, 0);
            return std::unexpected(result.error());
        }
        const int handle = lua_ref(thread, -1);
        lua_pop(thread, 1);
        lua_unref(L, threadRef);
        return handle;
    }

    std::expected<void, LuauError> LuauVm::Call(const int handle, const std::string_view function,
                                                const std::span<const double> args)
    {
        lua_State* L = m_Impl->L;
        const int top = lua_gettop(L);
        lua_getref(L, handle);
        const std::string name(function);
        lua_getfield(L, -1, name.c_str());
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            return std::unexpected(LuauError{"script has no function '" + name + "'", {}, 0});
        }
        for (const double arg : args) lua_pushnumber(L, arg);
        m_Impl->ResetBudget();
        if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != 0)
        {
            auto error = m_Impl->Pop(L);
            lua_settop(L, top);
            return std::unexpected(std::move(error));
        }
        lua_settop(L, top);
        return {};
    }

    void LuauVm::Release(const int handle)
    {
        lua_unref(m_Impl->L, handle);
    }

    int LuauNativeCall::Count() const { return lua_gettop(Lua(m_State)); }
    bool LuauNativeCall::IsNil(const int index) const { return lua_isnoneornil(Lua(m_State), index); }
    bool LuauNativeCall::IsNumber(const int index) const { return lua_type(Lua(m_State), index) == LUA_TNUMBER; }
    bool LuauNativeCall::IsEntity(const int index) const { return lua_type(Lua(m_State), index) == LUA_TLIGHTUSERDATA; }
    double LuauNativeCall::Number(const int index, const double fallback) const
    {
        return IsNumber(index) ? lua_tonumber(Lua(m_State), index) : fallback;
    }

    std::string LuauNativeCall::String(const int index) const
    {
        std::size_t length = 0;
        const char* text = lua_type(Lua(m_State), index) == LUA_TSTRING ? lua_tolstring(Lua(m_State), index, &length) : nullptr;
        return text ? std::string(text, length) : std::string{};
    }

    std::uint64_t LuauNativeCall::Entity(const int index) const
    {
        return IsEntity(index) ? reinterpret_cast<std::uintptr_t>(lua_tolightuserdata(Lua(m_State), index)) : 0;
    }

    LuauValue LuauNativeCall::ToValue(const int index, const LuauType type) const
    {
        lua_State* L = Lua(m_State);
        if (lua_isnoneornil(L, index)) return std::monostate{};
        const int t = lua_type(L, index);
        const auto field = [&](const char* name, float& out)
        {
            lua_getfield(L, index, name);
            const bool ok = lua_type(L, -1) == LUA_TNUMBER;
            if (ok) out = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
            return ok;
        };
        switch (type)
        {
        case LuauType::Bool:
            if (t == LUA_TBOOLEAN) return lua_toboolean(L, index) != 0;
            break;
        case LuauType::Float:
            if (t == LUA_TNUMBER) return static_cast<float>(lua_tonumber(L, index));
            break;
        case LuauType::Int:
            if (t == LUA_TNUMBER) return static_cast<int>(lua_tonumber(L, index));
            break;
        case LuauType::String:
            if (t == LUA_TSTRING) return String(index);
            break;
        case LuauType::Entity:
            if (t == LUA_TLIGHTUSERDATA) return Entity(index);
            break;
        case LuauType::Vec3:
            if (t == LUA_TVECTOR)
            {
                const float* v = lua_tovector(L, index);
                return glm::vec3(v[0], v[1], v[2]);
            }
            break;
        case LuauType::Vec2:
            if (t == LUA_TTABLE)
            {
                glm::vec2 v;
                if (field("x", v.x) && field("y", v.y)) return v;
            }
            break;
        case LuauType::Vec4:
            if (t == LUA_TTABLE)
            {
                glm::vec4 v;
                if (field("x", v.x) && field("y", v.y) && field("z", v.z) && field("w", v.w)) return v;
            }
            break;
        }
        return std::monostate{};
    }

    LuauValue LuauNativeCall::ToValueAuto(const int index, const bool integers) const
    {
        lua_State* L = Lua(m_State);
        switch (lua_type(L, index))
        {
        case LUA_TBOOLEAN: return ToValue(index, LuauType::Bool);
        case LUA_TNUMBER: return ToValue(index, integers ? LuauType::Int : LuauType::Float);
        case LUA_TSTRING: return ToValue(index, LuauType::String);
        case LUA_TLIGHTUSERDATA: return ToValue(index, LuauType::Entity);
        case LUA_TVECTOR: return ToValue(index, LuauType::Vec3);
        case LUA_TTABLE:
        {
            lua_getfield(L, index, "w");
            const bool four = lua_type(L, -1) == LUA_TNUMBER;
            lua_pop(L, 1);
            return ToValue(index, four ? LuauType::Vec4 : LuauType::Vec2);
        }
        default: return std::monostate{};
        }
    }

    void LuauNativeCall::PushNil() { lua_pushnil(Lua(m_State)); }
    void LuauNativeCall::PushBoolean(const bool value) { lua_pushboolean(Lua(m_State), value); }
    void LuauNativeCall::PushNumber(const double value) { lua_pushnumber(Lua(m_State), value); }
    void LuauNativeCall::PushString(const std::string_view value) { lua_pushlstring(Lua(m_State), value.data(), value.size()); }
    void LuauNativeCall::PushEntity(const std::uint64_t guid)
    {
        lua_pushlightuserdata(Lua(m_State), reinterpret_cast<void*>(static_cast<std::uintptr_t>(guid)));
    }

    void LuauNativeCall::StoreVectorMetatables()
    {
        lua_State* L = Lua(m_State);
        auto* impl = static_cast<LuauVm::Impl*>(lua_callbacks(L)->userdata);
        lua_pushvalue(L, 2);
        impl->Vec4Ref = lua_ref(L, -1);
        lua_pop(L, 1);
        lua_pushvalue(L, 1);
        impl->Vec2Ref = lua_ref(L, -1);
        lua_pop(L, 1);
    }

    void LuauNativeCall::PushValue(const LuauValue& value)
    {
        lua_State* L = Lua(m_State);
        auto* impl = static_cast<LuauVm::Impl*>(lua_callbacks(L)->userdata);
        const auto pushVec = [&](const std::initializer_list<float> parts, const int ref)
        {
            lua_createtable(L, 0, 4);
            constexpr const char* names[] = {"x", "y", "z", "w"};
            int i = 0;
            for (const float part : parts)
            {
                lua_pushnumber(L, part);
                lua_setfield(L, -2, names[i++]);
            }
            if (ref != LUA_NOREF)
            {
                lua_getref(L, ref);
                lua_setmetatable(L, -2);
            }
        };
        std::visit([&](const auto& v)
        {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) lua_pushnil(L);
            else if constexpr (std::is_same_v<T, bool>) lua_pushboolean(L, v);
            else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>) lua_pushnumber(L, v);
            else if constexpr (std::is_same_v<T, std::string>) lua_pushlstring(L, v.data(), v.size());
            else if constexpr (std::is_same_v<T, std::uint64_t>) PushEntity(v);
            else if constexpr (std::is_same_v<T, glm::vec2>) pushVec({v.x, v.y}, impl->Vec2Ref);
            else if constexpr (std::is_same_v<T, glm::vec3>) lua_pushvector(L, v.x, v.y, v.z);
            else pushVec({v.x, v.y, v.z, v.w}, impl->Vec4Ref);
        }, value);
    }

    void LuauNativeCall::PushValues(const std::span<const LuauValue> values)
    {
        lua_State* L = Lua(m_State);
        lua_createtable(L, static_cast<int>(values.size()), 0);
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            PushValue(values[i]);
            lua_rawseti(L, -2, static_cast<int>(i) + 1);
        }
    }
}
