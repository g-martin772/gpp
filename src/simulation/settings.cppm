module;
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Settings;

import std;
import GPP.Core;
import :Reflection;

namespace GPP
{
    export enum class SettingScale { Linear, Log };

    export struct SettingMeta
    {
        std::string Label;
        std::string Group;
        std::string Tooltip;
        std::string Doc;
        double Min = 0.0;
        double Max = 0.0;
        SettingScale Scale = SettingScale::Linear;
        int Cost = 0; // 0-5
        std::string Requires;
        std::vector<std::string> Options;
        std::vector<std::string> OptionLabels;
        std::vector<std::string> OptionRequires;
        std::vector<int> OptionCosts;
        std::string Format;
        // "key", "key=N", "key>=N", "key!=N", clauses joined by '&'
        std::string VisibleWhen;

        [[nodiscard]] const std::string& OptionLabel(std::size_t index) const
        {
            return index < OptionLabels.size() ? OptionLabels[index] : Options[index];
        }
    };

    export template <typename T>
    struct SettingDesc
    {
        std::string Key;
        FieldType Type{FieldType::Float};
        SettingMeta Meta;
        std::function<FieldValue(const T&)> Get;
        std::function<bool(T&, const FieldValue&)> Set;
        std::function<void(const T&, YAML::Node&)> Encode;
        std::function<void(T&, const YAML::Node&)> Decode;

        [[nodiscard]] int CostFor(const T& value) const
        {
            const auto index = SelectedOption(value);
            if (index && !Meta.OptionCosts.empty() && *index < Meta.OptionCosts.size()) return Meta.OptionCosts[*index];
            return Meta.Cost;
        }

        [[nodiscard]] std::string RequiresFor(const T& value) const
        {
            const auto index = SelectedOption(value);
            if (index && *index < Meta.OptionRequires.size() && !Meta.OptionRequires[*index].empty())
                return Meta.OptionRequires[*index];
            return Meta.Requires;
        }

    private:
        [[nodiscard]] std::optional<std::size_t> SelectedOption(const T& value) const
        {
            const auto field = Get(value);
            const auto* index = std::get_if<int>(&field);
            if (!index || *index < 0) return std::nullopt;
            return static_cast<std::size_t>(*index);
        }
    };

    namespace SettingDetail
    {
        inline std::string Lower(std::string_view text)
        {
            std::string out(text);
            std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        inline std::string Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
            return std::string(text);
        }
    }

    export template <typename T, typename M>
    SettingDesc<T> Setting(std::string key, M T::* member, SettingMeta meta = {})
    {
        FieldMeta fieldMeta;
        fieldMeta.Options = meta.Options;
        fieldMeta.Min = meta.Min;
        fieldMeta.Max = meta.Max;

        SettingDesc<T> desc;
        desc.Type = Reflect::TypeOf<M>();
        desc.Get = [member](const T& c) { return Reflect::ToValue<M>(c.*member); };
        desc.Set = [member](T& c, const FieldValue& v) { return Reflect::FromValue<M>(c.*member, v); };
        desc.Encode = [key, member, fieldMeta](const T& c, YAML::Node& node)
        {
            Reflect::ToYaml<M>(node, key, c.*member, fieldMeta);
        };
        desc.Decode = [key, member, fieldMeta](T& c, const YAML::Node& node)
        {
            Reflect::FromYaml<M>(node, key, c.*member, fieldMeta);
        };
        if (meta.Label.empty()) meta.Label = key;
        desc.Key = std::move(key);
        desc.Meta = std::move(meta);
        return desc;
    }

    export template <typename T>
    class SettingRegistry
    {
    public:
        void Add(SettingDesc<T> desc)
        {
            if (Find(desc.Key)) throw std::logic_error("duplicate setting '" + desc.Key + "'");
            if (std::ranges::find(m_Groups, desc.Meta.Group) == m_Groups.end()) m_Groups.push_back(desc.Meta.Group);
            m_Settings.push_back(std::move(desc));
        }

        [[nodiscard]] const std::vector<SettingDesc<T>>& All() const noexcept { return m_Settings; }

        [[nodiscard]] const std::vector<std::string>& Groups() const noexcept { return m_Groups; }

        [[nodiscard]] const SettingDesc<T>* Find(std::string_view key) const
        {
            const auto it = std::ranges::find(m_Settings, key, &SettingDesc<T>::Key);
            return it == m_Settings.end() ? nullptr : &*it;
        }

        [[nodiscard]] std::vector<const SettingDesc<T>*> InGroup(std::string_view group) const
        {
            std::vector<const SettingDesc<T>*> out;
            for (const auto& setting : m_Settings)
            {
                if (setting.Meta.Group == group) out.push_back(&setting);
            }
            return out;
        }

        void Clamp(T& value) const
        {
            for (const auto& setting : m_Settings)
            {
                const auto current = setting.Get(value);
                if (setting.Type == FieldType::Enum)
                {
                    if (const auto* index = std::get_if<int>(&current); index && !setting.Meta.Options.empty()
                        && (*index < 0 || static_cast<std::size_t>(*index) >= setting.Meta.Options.size()))
                    {
                        setting.Set(value, FieldValue{0});
                    }
                }
                else if (setting.Meta.Max > setting.Meta.Min)
                {
                    if (const auto* f = std::get_if<float>(&current))
                    {
                        setting.Set(value, FieldValue{static_cast<float>(std::clamp<double>(*f, setting.Meta.Min, setting.Meta.Max))});
                    }
                    else if (const auto* i = std::get_if<int>(&current))
                    {
                        setting.Set(value, FieldValue{static_cast<int>(std::clamp<double>(*i, setting.Meta.Min, setting.Meta.Max))});
                    }
                }
            }
        }

        [[nodiscard]] bool IsVisible(const T& value, const SettingDesc<T>& setting) const
        {
            std::string_view conditions = setting.Meta.VisibleWhen;
            while (!conditions.empty())
            {
                const auto split = conditions.find('&');
                if (!ClauseHolds(value, conditions.substr(0, split))) return false;
                conditions = split == std::string_view::npos ? std::string_view{} : conditions.substr(split + 1);
            }
            return true;
        }

        [[nodiscard]] bool ClauseHolds(const T& value, const std::string_view condition) const
        {
            if (SettingDetail::Trim(condition).empty()) return true;
            const auto operatorAt = condition.find_first_of("=!>");
            const auto key = SettingDetail::Trim(condition.substr(0, operatorAt));
            const auto* other = Find(key);
            if (!other) return true;

            double current = 0.0;
            std::visit([&](const auto& v)
            {
                using V = std::remove_cvref_t<decltype(v)>;
                if constexpr (std::is_arithmetic_v<V>) current = static_cast<double>(v);
            }, other->Get(value));
            if (operatorAt == std::string_view::npos) return current != 0.0;

            auto rest = condition.substr(operatorAt);
            enum class Op { Equal, NotEqual, AtLeast } op = Op::Equal;
            if (rest.starts_with(">=")) { op = Op::AtLeast; rest.remove_prefix(2); }
            else if (rest.starts_with("!=")) { op = Op::NotEqual; rest.remove_prefix(2); }
            else rest.remove_prefix(1);
            double expected = 0.0;
            const auto text = SettingDetail::Trim(rest);
            std::from_chars(text.data(), text.data() + text.size(), expected);
            switch (op)
            {
            case Op::Equal: return current == expected;
            case Op::NotEqual: return current != expected;
            case Op::AtLeast: return current >= expected;
            }
            return true;
        }

        [[nodiscard]] bool Equal(const T& a, const T& b, const SettingDesc<T>& setting) const
        {
            return setting.Get(a) == setting.Get(b);
        }

        [[nodiscard]] std::vector<std::string> Diff(const T& a, const T& b) const
        {
            std::vector<std::string> keys;
            for (const auto& setting : m_Settings)
            {
                if (!Equal(a, b, setting)) keys.push_back(setting.Key);
            }
            return keys;
        }

        [[nodiscard]] YAML::Node Encode(const T& value, const T* baseline = nullptr) const
        {
            YAML::Node node(YAML::NodeType::Map);
            for (const auto& setting : m_Settings)
            {
                if (baseline && Equal(value, *baseline, setting)) continue;
                setting.Encode(value, node);
            }
            return node;
        }

        std::vector<std::string> Decode(T& value, const YAML::Node& node) const
        {
            std::vector<std::string> unknown;
            if (!node || !node.IsMap()) return unknown;
            for (const auto& entry : node)
            {
                const auto key = entry.first.as<std::string>("");
                const auto* setting = Find(key);
                if (!setting)
                {
                    unknown.push_back(key);
                    continue;
                }
                try
                {
                    setting->Decode(value, node);
                }
                catch (const std::exception&)
                {
                    unknown.push_back(key);
                }
            }
            Clamp(value);
            return unknown;
        }

        [[nodiscard]] std::string ToText(const T& value, const SettingDesc<T>& setting) const
        {
            const auto field = setting.Get(value);
            return std::visit([&](const auto& v) -> std::string
            {
                using V = std::remove_cvref_t<decltype(v)>;
                if constexpr (std::is_same_v<V, bool>) return v ? "true" : "false";
                else if constexpr (std::is_same_v<V, int>)
                {
                    if (setting.Type == FieldType::Enum && v >= 0 && static_cast<std::size_t>(v) < setting.Meta.Options.size())
                        return setting.Meta.Options[static_cast<std::size_t>(v)];
                    return std::to_string(v);
                }
                else if constexpr (std::is_same_v<V, float> || std::is_same_v<V, double>) return std::format("{}", v);
                else if constexpr (std::is_same_v<V, std::string>) return v;
                else return {};
            }, field);
        }

        [[nodiscard]] std::expected<void, std::string> SetFromText(T& value, std::string_view key, std::string_view text) const
        {
            const auto* setting = Find(key);
            if (!setting) return std::unexpected("unknown setting '" + std::string(key) + "'");
            const auto trimmed = SettingDetail::Trim(text);
            const auto lowered = SettingDetail::Lower(trimmed);

            FieldValue parsed;
            switch (setting->Type)
            {
            case FieldType::Bool:
            {
                if (lowered == "true" || lowered == "on" || lowered == "yes" || lowered == "1") parsed = true;
                else if (lowered == "false" || lowered == "off" || lowered == "no" || lowered == "0") parsed = false;
                else return std::unexpected("'" + trimmed + "' is not a boolean");
                break;
            }
            case FieldType::Int:
            {
                int number = 0;
                const auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), number);
                if (result.ec != std::errc{} || result.ptr != trimmed.data() + trimmed.size())
                    return std::unexpected("'" + trimmed + "' is not an integer");
                parsed = number;
                break;
            }
            case FieldType::Float:
            case FieldType::Double:
            {
                double number = 0.0;
                const auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), number);
                if (result.ec != std::errc{} || result.ptr != trimmed.data() + trimmed.size())
                    return std::unexpected("'" + trimmed + "' is not a number");
                parsed = setting->Type == FieldType::Float ? FieldValue{static_cast<float>(number)} : FieldValue{number};
                break;
            }
            case FieldType::Enum:
            {
                int index = -1;
                for (std::size_t i = 0; i < setting->Meta.Options.size(); ++i)
                {
                    if (SettingDetail::Lower(setting->Meta.Options[i]) == lowered) index = static_cast<int>(i);
                }
                if (index < 0)
                {
                    int number = 0;
                    const auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), number);
                    if (result.ec == std::errc{} && result.ptr == trimmed.data() + trimmed.size()) index = number;
                }
                if (index < 0 || static_cast<std::size_t>(index) >= setting->Meta.Options.size())
                {
                    std::string options;
                    for (const auto& option : setting->Meta.Options) options += (options.empty() ? "" : ", ") + option;
                    return std::unexpected("'" + trimmed + "' is not one of: " + options);
                }
                parsed = index;
                break;
            }
            case FieldType::String: parsed = trimmed; break;
            default: return std::unexpected("setting '" + std::string(key) + "' cannot be set from text");
            }

            if (!setting->Set(value, parsed)) return std::unexpected("cannot set '" + std::string(key) + "'");
            Clamp(value);
            return {};
        }

    private:
        std::vector<SettingDesc<T>> m_Settings;
        std::vector<std::string> m_Groups;
    };
}
