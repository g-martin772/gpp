module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Reflection;

import std;

namespace YAML
{
    template <>
    struct convert<glm::vec2>
    {
        static Node encode(const glm::vec2& v)
        {
            Node node;
            node.push_back(v.x);
            node.push_back(v.y);
            node.SetStyle(EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::vec2& out)
        {
            if (!node.IsSequence() || node.size() != 2) return false;
            out.x = node[0].as<float>();
            out.y = node[1].as<float>();
            return true;
        }
    };

    template <>
    struct convert<glm::vec3>
    {
        static Node encode(const glm::vec3& v)
        {
            Node node;
            node.push_back(v.x);
            node.push_back(v.y);
            node.push_back(v.z);
            node.SetStyle(EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::vec3& out)
        {
            if (!node.IsSequence() || node.size() != 3) return false;
            out.x = node[0].as<float>();
            out.y = node[1].as<float>();
            out.z = node[2].as<float>();
            return true;
        }
    };

    template <>
    struct convert<glm::vec4>
    {
        static Node encode(const glm::vec4& v)
        {
            Node node;
            node.push_back(v.x);
            node.push_back(v.y);
            node.push_back(v.z);
            node.push_back(v.w);
            node.SetStyle(EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::vec4& out)
        {
            if (!node.IsSequence() || node.size() != 4) return false;
            out.x = node[0].as<float>();
            out.y = node[1].as<float>();
            out.z = node[2].as<float>();
            out.w = node[3].as<float>();
            return true;
        }
    };

    template <>
    struct convert<glm::quat>
    {
        static Node encode(const glm::quat& q)
        {
            Node node;
            node.push_back(q.x);
            node.push_back(q.y);
            node.push_back(q.z);
            node.push_back(q.w);
            node.SetStyle(EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::quat& out)
        {
            if (!node.IsSequence() || node.size() != 4) return false;
            out.x = node[0].as<float>();
            out.y = node[1].as<float>();
            out.z = node[2].as<float>();
            out.w = node[3].as<float>();
            return true;
        }
    };
}

namespace GPP
{
    export using FieldValue = std::variant<std::monostate, bool, int, float, glm::vec2, glm::vec3, glm::vec4,
                                           std::string, std::uint64_t>;

    export enum class FieldType { Bool, Int, Float, Vec2, Vec3, Vec4, String, Entity, Enum };

    export enum class FieldKind { Plain, Color, Direction, Angle, AssetPath, EntityRef, Multiline };

    export struct FieldMeta
    {
        std::string Label;
        std::string Category;
        float Min = 0.0f;
        float Max = 0.0f;
        float Speed = 0.0f;
        FieldKind Kind = FieldKind::Plain;
        bool ReadOnly = false;
        std::vector<std::string> Options;
        std::string Format;
        std::string VisibleField;
        std::uint32_t VisibleMask = 0;
    };

    namespace Reflect
    {
        template <typename>
        inline constexpr bool kAlwaysFalse = false;

        template <typename M>
        constexpr FieldType TypeOf()
        {
            if constexpr (std::is_same_v<M, bool>) return FieldType::Bool;
            else if constexpr (std::is_same_v<M, int>) return FieldType::Int;
            else if constexpr (std::is_same_v<M, float>) return FieldType::Float;
            else if constexpr (std::is_same_v<M, glm::vec2>) return FieldType::Vec2;
            else if constexpr (std::is_same_v<M, glm::vec3>) return FieldType::Vec3;
            else if constexpr (std::is_same_v<M, glm::vec4>) return FieldType::Vec4;
            else if constexpr (std::is_same_v<M, glm::quat>) return FieldType::Vec3;
            else if constexpr (std::is_same_v<M, std::string>) return FieldType::String;
            else if constexpr (std::is_same_v<M, std::uint64_t>) return FieldType::Entity;
            else if constexpr (std::is_enum_v<M>) return FieldType::Enum;
            else static_assert(kAlwaysFalse<M>, "unsupported field type");
        }

        template <typename M>
        FieldValue ToValue(const M& v)
        {
            if constexpr (std::is_same_v<M, glm::quat>) return glm::degrees(glm::eulerAngles(v));
            else if constexpr (std::is_enum_v<M>) return static_cast<int>(v);
            else return FieldValue{std::in_place_type<M>, v};
        }

        template <typename X>
        bool Extract(const FieldValue& v, X& out)
        {
            if (const auto* p = std::get_if<X>(&v))
            {
                out = *p;
                return true;
            }
            if constexpr (std::is_same_v<X, bool>)
            {
                if (const auto* i = std::get_if<int>(&v)) { out = *i != 0; return true; }
                if (const auto* f = std::get_if<float>(&v)) { out = *f != 0.0f; return true; }
            }
            else if constexpr (std::is_same_v<X, float>)
            {
                if (const auto* i = std::get_if<int>(&v)) { out = static_cast<float>(*i); return true; }
            }
            else if constexpr (std::is_same_v<X, int>)
            {
                if (const auto* f = std::get_if<float>(&v)) { out = static_cast<int>(*f); return true; }
            }
            return false;
        }

        template <typename M>
        bool FromValue(M& out, const FieldValue& v)
        {
            if constexpr (std::is_same_v<M, glm::quat>)
            {
                glm::vec3 degrees;
                if (!Extract(v, degrees)) return false;
                out = glm::quat(glm::radians(degrees));
                return true;
            }
            else if constexpr (std::is_enum_v<M>)
            {
                int index;
                if (!Extract(v, index)) return false;
                out = static_cast<M>(index);
                return true;
            }
            else
            {
                return Extract(v, out);
            }
        }

        template <typename M>
        void ToYaml(YAML::Node& node, const std::string& name, const M& v, const FieldMeta& meta)
        {
            if constexpr (std::is_enum_v<M>)
            {
                const auto index = static_cast<std::size_t>(v);
                node[name] = index < meta.Options.size() ? meta.Options[index] : std::string{};
            }
            else
            {
                node[name] = v;
            }
        }

        template <typename M>
        void FromYaml(const YAML::Node& node, const std::string& name, M& out, const FieldMeta& meta)
        {
            const auto child = node[name];
            if (!child) return;
            if constexpr (std::is_enum_v<M>)
            {
                const auto text = child.template as<std::string>();
                if (const auto it = std::ranges::find(meta.Options, text); it != meta.Options.end())
                {
                    out = static_cast<M>(it - meta.Options.begin());
                }
            }
            else
            {
                out = child.template as<M>();
            }
        }
    }

    export template <typename T>
    struct FieldDesc
    {
        std::string Name;
        FieldType Type{FieldType::Float};
        FieldMeta Meta;
        bool Serialized = true;
        std::function<FieldValue(const T&)> Get;
        std::function<bool(T&, const FieldValue&)> Set;
        std::function<void(const T&, YAML::Node&)> Encode;
        std::function<void(T&, const YAML::Node&)> Decode;
    };

    export template <typename T, typename M>
    FieldDesc<T> Field(std::string name, M T::* member, FieldMeta meta = {})
    {
        FieldDesc<T> desc;
        desc.Type = Reflect::TypeOf<M>();
        desc.Get = [member](const T& c) { return Reflect::ToValue<M>(c.*member); };
        if (!meta.ReadOnly)
        {
            desc.Set = [member](T& c, const FieldValue& v) { return Reflect::FromValue<M>(c.*member, v); };
        }
        desc.Encode = [name, member, meta](const T& c, YAML::Node& node)
        {
            Reflect::ToYaml<M>(node, name, c.*member, meta);
        };
        desc.Decode = [name, member, meta](T& c, const YAML::Node& node)
        {
            Reflect::FromYaml<M>(node, name, c.*member, meta);
        };
        if (meta.Label.empty()) meta.Label = name;
        desc.Name = std::move(name);
        desc.Meta = std::move(meta);
        return desc;
    }

    // Runtime-only field backed by accessors; never serialized.
    export template <typename T, typename M>
    FieldDesc<T> Computed(std::string name, std::function<M(const T&)> getter,
                          std::function<void(T&, const M&)> setter = nullptr, FieldMeta meta = {})
    {
        FieldDesc<T> desc;
        desc.Type = Reflect::TypeOf<M>();
        desc.Serialized = false;
        desc.Get = [getter](const T& c) { return Reflect::ToValue<M>(getter(c)); };
        if (setter && !meta.ReadOnly)
        {
            desc.Set = [setter](T& c, const FieldValue& v)
            {
                M value{};
                if (!Reflect::FromValue<M>(value, v)) return false;
                setter(c, value);
                return true;
            };
        }
        else
        {
            meta.ReadOnly = true;
        }
        if (meta.Label.empty()) meta.Label = name;
        desc.Name = std::move(name);
        desc.Meta = std::move(meta);
        return desc;
    }

    // The single declaration of a component's fields. Registering it generates YAML, the runtime field
    // list, the add-component factory and (downstream) inspector widgets and graph pins.
    export template <typename T>
    struct ComponentDescription
    {
        std::string DisplayName;
        std::string Note;
        bool Inspectable = true;
        bool GraphExposed = true;
        std::vector<FieldDesc<T>> Fields;
        T Defaults{};
        std::function<void(const T&, YAML::Node&)> EncodeExtra;
        std::function<void(T&, const YAML::Node&)> DecodeExtra;
    };
}
