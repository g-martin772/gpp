module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:StateHash;

import std;
import :ComponentRegistry;
import :Scene;

namespace GPP
{
    export struct StateHashOptions
    {
        bool IncludeExtensions = false; // scene extensions carry UI and render settings, off by default
    };

    // Incremental FNV-1a (64 bit); stable across runs and platforms.
    export class StateHasher
    {
    public:
        void Bytes(const void* data, std::size_t size) noexcept
        {
            const auto* p = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i) m_Value = (m_Value ^ p[i]) * 0x100000001B3ull;
        }

        void U64(std::uint64_t value) noexcept { Bytes(&value, sizeof(value)); }
        void F64(double value) noexcept { U64(std::bit_cast<std::uint64_t>(value)); }

        void String(std::string_view text) noexcept
        {
            U64(text.size());
            Bytes(text.data(), text.size());
        }

        [[nodiscard]] std::uint64_t Value() const noexcept { return m_Value; }

    private:
        std::uint64_t m_Value = 0xCBF29CE484222325ull;
    };

    // Order independent over entities (sorted by guid); components through their registered YAML encoding, which prints
    // floats with round-trip precision. Non-serializable components, the scene id, name and dirty flags are excluded.
    export [[nodiscard]] std::uint64_t HashScene(const Scene& scene, StateHashOptions options = {});
}
