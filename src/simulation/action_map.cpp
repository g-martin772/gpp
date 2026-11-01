module;
#include <yaml-cpp/yaml.h>
module GPP.Simulation;

import :ActionMap;
import std;

namespace GPP
{
    namespace
    {
        std::string Lower(std::string_view text)
        {
            std::string result(text);
            std::ranges::transform(result, result.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        std::string Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
            return std::string(text);
        }

        InputAction Make(std::string name, std::initializer_list<InputBinding> bindings)
        {
            return {std::move(name), std::vector<InputBinding>(bindings)};
        }
    }

    ActionMap ActionMap::Defaults()
    {
        ActionMap map;
        map.m_Actions = {
            Make("move_x", {{"key:d", 1.0f}, {"key:a", -1.0f}}),
            Make("move_y", {{"key:e", 1.0f}, {"key:q", -1.0f}}),
            Make("move_z", {{"key:w", 1.0f}, {"key:s", -1.0f}}),
            Make("jump", {{"key:space", 1.0f}}),
            Make("boost", {{"key:shift", 1.0f}}),
            Make("fire", {{"mouse:left", 1.0f}}),
            Make("aim", {{"mouse:right", 1.0f}}),
        };
        return map;
    }

    std::string ActionMap::NormalizeSource(const std::string_view source) { return Lower(Trim(source)); }

    bool ActionMap::ValidName(const std::string_view name)
    {
        return !name.empty() && std::ranges::all_of(name, [](const unsigned char c) { return std::isalnum(c) || c == '_' || c == '.'; });
    }

    std::expected<InputBinding, std::string> ActionMap::ParseBinding(std::string_view text)
    {
        InputBinding binding;
        auto rest = Trim(text);
        std::string_view view = rest;
        if (const auto star = view.find('*'); star != std::string_view::npos)
        {
            const auto number = Trim(view.substr(0, star));
            const auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), binding.Scale);
            if (number.empty() || ec != std::errc{} || end != number.data() + number.size() || !std::isfinite(binding.Scale))
            {
                return std::unexpected("bad scale in binding '" + std::string(text) + "'");
            }
            view.remove_prefix(star + 1);
        }
        else if (!view.empty() && view.front() == '-')
        {
            binding.Scale = -1.0f;
            view.remove_prefix(1);
        }
        binding.Source = NormalizeSource(view);
        const auto colon = binding.Source.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 == binding.Source.size())
        {
            return std::unexpected("binding '" + std::string(text) + "' must look like key:w or mouse:left");
        }
        return binding;
    }

    std::string ActionMap::FormatBinding(const InputBinding& binding)
    {
        if (binding.Scale == 1.0f) return binding.Source;
        if (binding.Scale == -1.0f) return "-" + binding.Source;
        return std::format("{}*{}", binding.Scale, binding.Source);
    }

    std::expected<ActionMap, std::string> ActionMap::FromYaml(const std::string& yaml)
    {
        try
        {
            const auto root = YAML::Load(yaml);
            if (!root.IsMap()) return std::unexpected("Input: expected a mapping");
            const auto actions = root["Actions"];
            if (!actions) return ActionMap{};
            if (!actions.IsMap()) return std::unexpected("Input.Actions: expected a mapping of action to bindings");
            ActionMap map;
            for (const auto& pair : actions)
            {
                const auto name = pair.first.as<std::string>();
                if (!ValidName(name)) return std::unexpected("Input.Actions: invalid action name '" + name + "'");
                std::vector<InputBinding> bindings;
                const auto list = pair.second;
                if (!list.IsNull() && !list.IsScalar() && !list.IsSequence())
                {
                    return std::unexpected("Input.Actions." + name + ": expected a list of bindings");
                }
                if (!list.IsNull())
                {
                    std::vector<std::string> texts;
                    if (list.IsScalar()) texts.push_back(list.as<std::string>());
                    else for (const auto& item : list) texts.push_back(item.as<std::string>());
                    for (const auto& text : texts)
                    {
                        auto binding = ParseBinding(text);
                        if (!binding) return std::unexpected("Input.Actions." + name + ": " + binding.error());
                        bindings.push_back(std::move(*binding));
                    }
                }
                map.Set(name, std::move(bindings));
            }
            return map;
        }
        catch (const YAML::Exception& error)
        {
            return std::unexpected(std::string("Input: ") + error.what());
        }
    }

    std::string ActionMap::ToYaml() const
    {
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "Actions" << YAML::Value << YAML::BeginMap;
        for (const auto& action : m_Actions)
        {
            out << YAML::Key << action.Name << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (const auto& binding : action.Bindings) out << FormatBinding(binding);
            out << YAML::EndSeq;
        }
        out << YAML::EndMap << YAML::EndMap;
        return out.c_str();
    }

    const InputAction* ActionMap::Find(const std::string_view name) const
    {
        const auto it = std::ranges::find(m_Actions, name, &InputAction::Name);
        return it == m_Actions.end() ? nullptr : &*it;
    }

    void ActionMap::Set(std::string name, std::vector<InputBinding> bindings)
    {
        for (auto& binding : bindings) binding.Source = NormalizeSource(binding.Source);
        if (const auto it = std::ranges::find(m_Actions, name, &InputAction::Name); it != m_Actions.end())
        {
            it->Bindings = std::move(bindings);
            return;
        }
        m_Actions.push_back({std::move(name), std::move(bindings)});
    }

    bool ActionMap::Remove(const std::string_view name)
    {
        return std::erase_if(m_Actions, [&](const InputAction& action) { return action.Name == name; }) > 0;
    }

    void ActionMap::Overlay(const ActionMap& other)
    {
        for (const auto& action : other.m_Actions) Set(action.Name, action.Bindings);
    }

    void ActionRouter::SetTarget(std::shared_ptr<SimulationInput> target)
    {
        std::scoped_lock lock(m_Mutex);
        m_Target = std::move(target);
    }

    void ActionRouter::Push(const InputAction& action)
    {
        if (!m_Target) return;
        float sum = 0.0f;
        for (const auto& binding : action.Bindings)
        {
            if (const auto it = m_Sources.find(binding.Source); it != m_Sources.end()) sum += it->second * binding.Scale;
        }
        m_Target->Set(action.Name, std::clamp(sum, -1.0f, 1.0f));
    }

    void ActionRouter::SetMap(ActionMap map)
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Target)
        {
            for (const auto& old : m_Map.Actions())
            {
                if (!map.Find(old.Name)) m_Target->Set(old.Name, 0.0f);
            }
        }
        m_Map = std::move(map);
        for (const auto& action : m_Map.Actions()) Push(action);
    }

    ActionMap ActionRouter::Map() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Map;
    }

    void ActionRouter::SetSource(const std::string_view source, const float value)
    {
        const auto name = ActionMap::NormalizeSource(source);
        std::scoped_lock lock(m_Mutex);
        if (value == 0.0f) m_Sources.erase(name);
        else m_Sources[name] = value;
        for (const auto& action : m_Map.Actions())
        {
            if (std::ranges::any_of(action.Bindings, [&](const InputBinding& b) { return b.Source == name; })) Push(action);
        }
    }

    void ActionRouter::AddMouse(const glm::vec2 delta)
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Target) m_Target->AddMouse(delta);
    }

    void ActionRouter::Release()
    {
        std::scoped_lock lock(m_Mutex);
        m_Sources.clear();
        if (m_Target) m_Target->Clear();
    }
}
