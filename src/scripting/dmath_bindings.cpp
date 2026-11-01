module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <lua.h>
#include <lualib.h>
module GPP.Scripting;

import std;
import glm;
import :Luau;
import :Bindings;

namespace GPP
{
    namespace
    {
        constexpr int kVecTag = 21;
        constexpr int kQuatTag = 22;
        constexpr double kParallel = 1.0 - 1e-12;

        struct Vec
        {
            double X;
            double Y;
            double Z;
        };

        struct Quat
        {
            double X;
            double Y;
            double Z;
            double W;
        };

        lua_State* Lua(void* state) { return static_cast<lua_State*>(state); }

        Vec* VecAt(lua_State* L, const int index) { return static_cast<Vec*>(lua_touserdatatagged(L, index, kVecTag)); }
        Quat* QuatAt(lua_State* L, const int index) { return static_cast<Quat*>(lua_touserdatatagged(L, index, kQuatTag)); }

        const char* Describe(lua_State* L, const int index)
        {
            if (lua_isnone(L, index)) return "no value";
            if (lua_type(L, index) == LUA_TUSERDATA)
            {
                if (const int tag = lua_userdatatag(L, index); tag >= 0) return lua_getuserdataname(L, tag);
            }
            return luaL_typename(L, index);
        }

        template <typename... Args>
        [[noreturn]] void Fail(lua_State* L, std::format_string<Args...> format, Args&&... args)
        {
            char buffer[256];
            const auto result = std::format_to_n(buffer, sizeof(buffer) - 1, format, std::forward<Args>(args)...);
            *result.out = '\0';
            luaL_error(L, "%s", buffer);
        }

        void PushVec(lua_State* L, const double x, const double y, const double z)
        {
            auto* v = static_cast<Vec*>(lua_newuserdatataggedwithmetatable(L, sizeof(Vec), kVecTag));
            v->X = x;
            v->Y = y;
            v->Z = z;
        }

        void PushVec(lua_State* L, const glm::dvec3& value) { PushVec(L, value.x, value.y, value.z); }

        void PushQuat(lua_State* L, const double x, const double y, const double z, const double w)
        {
            auto* q = static_cast<Quat*>(lua_newuserdatataggedwithmetatable(L, sizeof(Quat), kQuatTag));
            q->X = x;
            q->Y = y;
            q->Z = z;
            q->W = w;
        }

        double Number(lua_State* L, const int index, const char* fn, const char* what)
        {
            if (lua_type(L, index) != LUA_TNUMBER) Fail(L, "{}: {} must be a number, got {}", fn, what, Describe(L, index));
            return lua_tonumber(L, index);
        }

        glm::dvec3 Vector(lua_State* L, const int index, const char* fn, const char* what, const bool allowVector = false)
        {
            if (const auto* v = VecAt(L, index)) return {v->X, v->Y, v->Z};
            if (allowVector && lua_type(L, index) == LUA_TVECTOR)
            {
                const float* f = lua_tovector(L, index);
                return {f[0], f[1], f[2]};
            }
            Fail(L, "{}: {} must be a dvec3{}, got {}", fn, what, allowVector ? " or vector" : "", Describe(L, index));
        }

        glm::dquat Rotation(lua_State* L, const int index, const char* fn, const char* what)
        {
            if (const auto* q = QuatAt(L, index)) return {q->W, q->X, q->Y, q->Z};
            Fail(L, "{}: {} must be a dquat, got {}", fn, what, Describe(L, index));
        }

        [[noreturn]] void BadOperands(lua_State* L, const char* op)
        {
            Fail(L, "cannot apply '{}' to {} and {}; both must be dvec3/dquat (dvec3.from_vec converts a vector)", op,
                 Describe(L, 1), Describe(L, 2));
        }

        double Dot(const glm::dvec3& a, const glm::dvec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

        glm::dvec3 Cross(const glm::dvec3& a, const glm::dvec3& b)
        {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }

        double Length(const glm::dvec3& v) { return std::sqrt(Dot(v, v)); }

        glm::dvec3 Rotate(const glm::dquat& q, const glm::dvec3& v)
        {
            const glm::dvec3 u(q.x, q.y, q.z);
            const double norm = q.w * q.w + Dot(u, u);
            if (!(norm > 0.0)) return v;
            const glm::dvec3 rotated = (q.w * q.w - Dot(u, u)) * v + 2.0 * Dot(u, v) * u + 2.0 * q.w * Cross(u, v);
            return rotated / norm;
        }

        glm::dquat Multiply(const glm::dquat& a, const glm::dquat& b)
        {
            return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
        }

        double QuatDot(const glm::dquat& a, const glm::dquat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

        glm::dquat Normalized(const glm::dquat& q)
        {
            const double length = std::sqrt(QuatDot(q, q));
            if (!(length > 0.0)) return {1.0, 0.0, 0.0, 0.0};
            return {q.w / length, q.x / length, q.y / length, q.z / length};
        }

        void PushQuat(lua_State* L, const glm::dquat& q) { PushQuat(L, q.x, q.y, q.z, q.w); }

        int VecNew(lua_State* L)
        {
            if (lua_gettop(L) == 0)
            {
                PushVec(L, 0.0, 0.0, 0.0);
                return 1;
            }
            PushVec(L, Number(L, 1, "dvec3.new", "x"), Number(L, 2, "dvec3.new", "y"), Number(L, 3, "dvec3.new", "z"));
            return 1;
        }

        int VecFromVec(lua_State* L)
        {
            PushVec(L, Vector(L, 1, "dvec3.from_vec", "argument 1", true));
            return 1;
        }

        int VecToVec(lua_State* L)
        {
            const auto v = Vector(L, 1, "dvec3.to_vec", "argument 1");
            lua_pushvector(L, static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
            return 1;
        }

        int VecUnpack(lua_State* L)
        {
            const auto v = Vector(L, 1, "dvec3.unpack", "argument 1");
            lua_pushnumber(L, v.x);
            lua_pushnumber(L, v.y);
            lua_pushnumber(L, v.z);
            return 3;
        }

        int VecDot(lua_State* L)
        {
            lua_pushnumber(L, Dot(Vector(L, 1, "dvec3.dot", "argument 1"), Vector(L, 2, "dvec3.dot", "argument 2")));
            return 1;
        }

        int VecCross(lua_State* L)
        {
            PushVec(L, Cross(Vector(L, 1, "dvec3.cross", "argument 1"), Vector(L, 2, "dvec3.cross", "argument 2")));
            return 1;
        }

        int VecLength(lua_State* L)
        {
            lua_pushnumber(L, Length(Vector(L, 1, "dvec3.length", "argument 1")));
            return 1;
        }

        int VecLengthSquared(lua_State* L)
        {
            const auto v = Vector(L, 1, "dvec3.length_squared", "argument 1");
            lua_pushnumber(L, Dot(v, v));
            return 1;
        }

        int VecNormalize(lua_State* L)
        {
            const auto v = Vector(L, 1, "dvec3.normalize", "argument 1");
            const double length = Length(v);
            if (length > 0.0) PushVec(L, v / length);
            else PushVec(L, 0.0, 0.0, 0.0);
            return 1;
        }

        int VecDistance(lua_State* L)
        {
            const auto a = Vector(L, 1, "dvec3.distance", "argument 1");
            lua_pushnumber(L, Length(Vector(L, 2, "dvec3.distance", "argument 2") - a));
            return 1;
        }

        int VecLerp(lua_State* L)
        {
            const auto a = Vector(L, 1, "dvec3.lerp", "argument 1");
            const auto b = Vector(L, 2, "dvec3.lerp", "argument 2");
            const double t = Number(L, 3, "dvec3.lerp", "t");
            PushVec(L, a + (b - a) * t);
            return 1;
        }

        int VecAdd(lua_State* L)
        {
            const Vec* a = VecAt(L, 1);
            const Vec* b = VecAt(L, 2);
            if (!a || !b) BadOperands(L, "+");
            PushVec(L, a->X + b->X, a->Y + b->Y, a->Z + b->Z);
            return 1;
        }

        int VecSub(lua_State* L)
        {
            const Vec* a = VecAt(L, 1);
            const Vec* b = VecAt(L, 2);
            if (!a || !b) BadOperands(L, "-");
            PushVec(L, a->X - b->X, a->Y - b->Y, a->Z - b->Z);
            return 1;
        }

        int VecMul(lua_State* L)
        {
            const Vec* a = VecAt(L, 1);
            const Vec* b = VecAt(L, 2);
            if (a && b) PushVec(L, a->X * b->X, a->Y * b->Y, a->Z * b->Z);
            else if (a && lua_type(L, 2) == LUA_TNUMBER)
            {
                const double s = lua_tonumber(L, 2);
                PushVec(L, a->X * s, a->Y * s, a->Z * s);
            }
            else if (b && lua_type(L, 1) == LUA_TNUMBER)
            {
                const double s = lua_tonumber(L, 1);
                PushVec(L, s * b->X, s * b->Y, s * b->Z);
            }
            else BadOperands(L, "*");
            return 1;
        }

        int VecDiv(lua_State* L)
        {
            const Vec* a = VecAt(L, 1);
            const Vec* b = VecAt(L, 2);
            if (a && b) PushVec(L, a->X / b->X, a->Y / b->Y, a->Z / b->Z);
            else if (a && lua_type(L, 2) == LUA_TNUMBER)
            {
                const double s = lua_tonumber(L, 2);
                PushVec(L, a->X / s, a->Y / s, a->Z / s);
            }
            else BadOperands(L, "/");
            return 1;
        }

        int VecUnm(lua_State* L)
        {
            const auto v = Vector(L, 1, "dvec3", "operand");
            PushVec(L, -v.x, -v.y, -v.z);
            return 1;
        }

        int VecEq(lua_State* L)
        {
            const Vec* a = VecAt(L, 1);
            const Vec* b = VecAt(L, 2);
            lua_pushboolean(L, a && b && a->X == b->X && a->Y == b->Y && a->Z == b->Z);
            return 1;
        }

        int VecLen(lua_State* L)
        {
            lua_pushnumber(L, Length(Vector(L, 1, "dvec3", "operand")));
            return 1;
        }

        int VecToString(lua_State* L)
        {
            const auto v = Vector(L, 1, "dvec3", "operand");
            const std::string text = std::format("dvec3({}, {}, {})", v.x, v.y, v.z);
            lua_pushlstring(L, text.data(), text.size());
            return 1;
        }

        int VecIndex(lua_State* L)
        {
            const Vec* v = VecAt(L, 1);
            if (lua_type(L, 2) != LUA_TSTRING) Fail(L, "dvec3 members are read by name, got {}", Describe(L, 2));
            std::size_t size = 0;
            const char* key = lua_tolstring(L, 2, &size);
            if (v && size == 1)
            {
                switch (key[0])
                {
                case 'x': lua_pushnumber(L, v->X); return 1;
                case 'y': lua_pushnumber(L, v->Y); return 1;
                case 'z': lua_pushnumber(L, v->Z); return 1;
                default: break;
                }
            }
            lua_pushvalue(L, 2);
            lua_rawget(L, lua_upvalueindex(1));
            if (lua_isnil(L, -1)) Fail(L, "dvec3 has no member '{}'", key);
            return 1;
        }

        int VecNewIndex(lua_State* L)
        {
            Fail(L, "dvec3 is immutable; build a new value with dvec3.new");
        }

        int QuatNew(lua_State* L)
        {
            if (lua_gettop(L) == 0)
            {
                PushQuat(L, 0.0, 0.0, 0.0, 1.0);
                return 1;
            }
            PushQuat(L, Number(L, 1, "dquat.new", "x"), Number(L, 2, "dquat.new", "y"), Number(L, 3, "dquat.new", "z"),
                     Number(L, 4, "dquat.new", "w"));
            return 1;
        }

        int QuatFromAxisAngle(lua_State* L)
        {
            const auto axis = Vector(L, 1, "dquat.from_axis_angle", "axis", true);
            const double angle = Number(L, 2, "dquat.from_axis_angle", "angle");
            const double length = Length(axis);
            if (!(length > 0.0)) Fail(L, "dquat.from_axis_angle: axis must not be zero");
            const double half = angle * 0.5;
            const double s = std::sin(half) / length;
            PushQuat(L, axis.x * s, axis.y * s, axis.z * s, std::cos(half));
            return 1;
        }

        int QuatFromEuler(lua_State* L)
        {
            glm::dvec3 degrees;
            if (lua_type(L, 1) == LUA_TNUMBER)
            {
                degrees = {Number(L, 1, "dquat.from_euler", "x"), Number(L, 2, "dquat.from_euler", "y"),
                           Number(L, 3, "dquat.from_euler", "z")};
            }
            else
            {
                degrees = Vector(L, 1, "dquat.from_euler", "euler degrees", true);
            }
            const glm::dquat q(glm::radians(degrees));
            PushQuat(L, q);
            return 1;
        }

        int QuatToEuler(lua_State* L)
        {
            const auto q = Normalized(Rotation(L, 1, "dquat.to_euler", "argument 1"));
            PushVec(L, glm::degrees(glm::eulerAngles(q)));
            return 1;
        }

        int QuatMulFn(lua_State* L)
        {
            PushQuat(L, Multiply(Rotation(L, 1, "dquat.mul", "argument 1"), Rotation(L, 2, "dquat.mul", "argument 2")));
            return 1;
        }

        int QuatRotate(lua_State* L)
        {
            PushVec(L, Rotate(Rotation(L, 1, "dquat.rotate", "argument 1"), Vector(L, 2, "dquat.rotate", "argument 2")));
            return 1;
        }

        int QuatSlerp(lua_State* L)
        {
            const auto a = Rotation(L, 1, "dquat.slerp", "argument 1");
            auto b = Rotation(L, 2, "dquat.slerp", "argument 2");
            const double t = Number(L, 3, "dquat.slerp", "t");
            double d = QuatDot(a, b);
            if (d < 0.0)
            {
                b = {-b.w, -b.x, -b.y, -b.z};
                d = -d;
            }
            double wa = 1.0 - t;
            double wb = t;
            if (d < kParallel)
            {
                const double theta = std::acos(d);
                const double s = std::sin(theta);
                wa = std::sin((1.0 - t) * theta) / s;
                wb = std::sin(t * theta) / s;
            }
            PushQuat(L, Normalized({wa * a.w + wb * b.w, wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z}));
            return 1;
        }

        int QuatInverse(lua_State* L)
        {
            const auto q = Rotation(L, 1, "dquat.inverse", "argument 1");
            const double norm = QuatDot(q, q);
            if (!(norm > 0.0)) PushQuat(L, 0.0, 0.0, 0.0, 1.0);
            else PushQuat(L, -q.x / norm, -q.y / norm, -q.z / norm, q.w / norm);
            return 1;
        }

        int QuatConjugate(lua_State* L)
        {
            const auto q = Rotation(L, 1, "dquat.conjugate", "argument 1");
            PushQuat(L, -q.x, -q.y, -q.z, q.w);
            return 1;
        }

        int QuatNormalize(lua_State* L)
        {
            PushQuat(L, Normalized(Rotation(L, 1, "dquat.normalize", "argument 1")));
            return 1;
        }

        int QuatDotFn(lua_State* L)
        {
            lua_pushnumber(L, QuatDot(Rotation(L, 1, "dquat.dot", "argument 1"), Rotation(L, 2, "dquat.dot", "argument 2")));
            return 1;
        }

        int QuatLengthFn(lua_State* L)
        {
            const auto q = Rotation(L, 1, "dquat.length", "argument 1");
            lua_pushnumber(L, std::sqrt(QuatDot(q, q)));
            return 1;
        }

        int QuatMulOp(lua_State* L)
        {
            const Quat* a = QuatAt(L, 1);
            if (!a) BadOperands(L, "*");
            if (QuatAt(L, 2)) return QuatMulFn(L);
            if (VecAt(L, 2)) return QuatRotate(L);
            BadOperands(L, "*");
        }

        int QuatEq(lua_State* L)
        {
            const Quat* a = QuatAt(L, 1);
            const Quat* b = QuatAt(L, 2);
            lua_pushboolean(L, a && b && a->X == b->X && a->Y == b->Y && a->Z == b->Z && a->W == b->W);
            return 1;
        }

        int QuatToString(lua_State* L)
        {
            const auto q = Rotation(L, 1, "dquat", "operand");
            const std::string text = std::format("dquat({}, {}, {}, {})", q.x, q.y, q.z, q.w);
            lua_pushlstring(L, text.data(), text.size());
            return 1;
        }

        int QuatIndex(lua_State* L)
        {
            const Quat* q = QuatAt(L, 1);
            if (lua_type(L, 2) != LUA_TSTRING) Fail(L, "dquat members are read by name, got {}", Describe(L, 2));
            std::size_t size = 0;
            const char* key = lua_tolstring(L, 2, &size);
            if (q && size == 1)
            {
                switch (key[0])
                {
                case 'x': lua_pushnumber(L, q->X); return 1;
                case 'y': lua_pushnumber(L, q->Y); return 1;
                case 'z': lua_pushnumber(L, q->Z); return 1;
                case 'w': lua_pushnumber(L, q->W); return 1;
                default: break;
                }
            }
            lua_pushvalue(L, 2);
            lua_rawget(L, lua_upvalueindex(1));
            if (lua_isnil(L, -1)) Fail(L, "dquat has no member '{}'", key);
            return 1;
        }

        int QuatNewIndex(lua_State* L)
        {
            Fail(L, "dquat is immutable; build a new value with dquat.new");
        }

        template <std::size_t Index>
        void DirectField(void* data, void* result)
        {
            lua_userdatadirectfield_setnumber(result, static_cast<const double*>(data)[Index]);
        }

        struct Entry
        {
            const char* Name;
            lua_CFunction Function;
        };

        void Install(lua_State* L, const char* global, const int tag, const std::span<const Entry> functions,
                     const std::span<const Entry> metamethods, const lua_CFunction index, const lua_CFunction newIndex)
        {
            lua_newtable(L);
            for (const auto& entry : functions)
            {
                lua_pushcclosurek(L, entry.Function, entry.Name, 0, nullptr);
                lua_setfield(L, -2, entry.Name);
            }
            lua_newtable(L);
            lua_pushvalue(L, -2);
            lua_pushcclosurek(L, index, "__index", 1, nullptr);
            lua_setfield(L, -2, "__index");
            lua_pushcclosurek(L, newIndex, "__newindex", 0, nullptr);
            lua_setfield(L, -2, "__newindex");
            for (const auto& entry : metamethods)
            {
                lua_pushcclosurek(L, entry.Function, entry.Name, 0, nullptr);
                lua_setfield(L, -2, entry.Name);
            }
            lua_pushstring(L, global);
            lua_setfield(L, -2, "__type");
            lua_pushstring(L, "locked");
            lua_setfield(L, -2, "__metatable");
            lua_setuserdatametatable(L, tag);
            lua_setglobal(L, global);
        }
    }

    bool LuauNativeCall::IsDVec3(const int index) const { return VecAt(Lua(m_State), index) != nullptr; }
    bool LuauNativeCall::IsDQuat(const int index) const { return QuatAt(Lua(m_State), index) != nullptr; }

    std::optional<glm::dvec3> LuauNativeCall::ToDVec3(const int index) const
    {
        lua_State* L = Lua(m_State);
        if (const auto* v = VecAt(L, index)) return glm::dvec3(v->X, v->Y, v->Z);
        if (lua_type(L, index) == LUA_TVECTOR)
        {
            const float* f = lua_tovector(L, index);
            return glm::dvec3(f[0], f[1], f[2]);
        }
        return std::nullopt;
    }

    std::optional<glm::dquat> LuauNativeCall::ToDQuat(const int index) const
    {
        if (const auto* q = QuatAt(Lua(m_State), index)) return glm::dquat(q->W, q->X, q->Y, q->Z);
        return std::nullopt;
    }

    std::string LuauNativeCall::TypeName(const int index) const { return Describe(Lua(m_State), index); }

    void LuauNativeCall::PushDVec3(const glm::dvec3& value) { PushVec(Lua(m_State), value); }
    void LuauNativeCall::PushDQuat(const glm::dquat& value) { PushQuat(Lua(m_State), value); }

    void RegisterDMathBindings(LuauVm& vm)
    {
        lua_State* L = Lua(vm.NativeState());
        constexpr std::array<Entry, 13> vecFunctions{{
            {"new", VecNew}, {"from_vec", VecFromVec}, {"to_vec", VecToVec}, {"unpack", VecUnpack}, {"dot", VecDot},
            {"cross", VecCross}, {"length", VecLength}, {"length_squared", VecLengthSquared}, {"normalize", VecNormalize},
            {"distance", VecDistance}, {"lerp", VecLerp}, {"add", VecAdd}, {"sub", VecSub},
        }};
        constexpr std::array<Entry, 8> vecMeta{{
            {"__add", VecAdd}, {"__sub", VecSub}, {"__mul", VecMul}, {"__div", VecDiv}, {"__unm", VecUnm},
            {"__eq", VecEq}, {"__len", VecLen}, {"__tostring", VecToString},
        }};
        constexpr std::array<Entry, 12> quatFunctions{{
            {"new", QuatNew}, {"from_axis_angle", QuatFromAxisAngle}, {"from_euler", QuatFromEuler},
            {"to_euler", QuatToEuler}, {"mul", QuatMulFn}, {"rotate", QuatRotate}, {"slerp", QuatSlerp},
            {"inverse", QuatInverse}, {"conjugate", QuatConjugate}, {"normalize", QuatNormalize}, {"dot", QuatDotFn},
            {"length", QuatLengthFn},
        }};
        constexpr std::array<Entry, 3> quatMeta{{{"__mul", QuatMulOp}, {"__eq", QuatEq}, {"__tostring", QuatToString}}};
        Install(L, "dvec3", kVecTag, vecFunctions, vecMeta, VecIndex, VecNewIndex);
        Install(L, "dquat", kQuatTag, quatFunctions, quatMeta, QuatIndex, QuatNewIndex);
        lua_registeruserdatadirectfieldget(L, kVecTag, "x", DirectField<0>);
        lua_registeruserdatadirectfieldget(L, kVecTag, "y", DirectField<1>);
        lua_registeruserdatadirectfieldget(L, kVecTag, "z", DirectField<2>);
        lua_registeruserdatadirectfieldget(L, kQuatTag, "x", DirectField<0>);
        lua_registeruserdatadirectfieldget(L, kQuatTag, "y", DirectField<1>);
        lua_registeruserdatadirectfieldget(L, kQuatTag, "z", DirectField<2>);
        lua_registeruserdatadirectfieldget(L, kQuatTag, "w", DirectField<3>);
    }
}
