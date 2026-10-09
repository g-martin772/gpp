export module GPP.Scripting:Luau;

import std;
import glm;

export namespace GPP
{
    using LuauValue = std::variant<std::monostate, bool, int, float, glm::vec2, glm::vec3, glm::vec4, std::string,
                                   std::uint64_t>;

    enum class LuauType { Bool, Int, Float, Vec2, Vec3, Vec4, String, Entity };

    // View over the arguments of a native function call; the Lua state stays private to the VM.
    class LuauNativeCall
    {
    public:
        explicit LuauNativeCall(void* state) : m_State(state) {}

        [[nodiscard]] int Count() const;
        [[nodiscard]] bool IsNil(int index) const;
        [[nodiscard]] bool IsNumber(int index) const;
        [[nodiscard]] bool IsEntity(int index) const;
        [[nodiscard]] double Number(int index, double fallback = 0.0) const;
        [[nodiscard]] std::string String(int index) const;
        [[nodiscard]] std::uint64_t Entity(int index) const;
        // Monostate for nil or a value that does not fit the requested type.
        [[nodiscard]] LuauValue ToValue(int index, LuauType type) const;
        // Infers the value from the Lua type (numbers become floats, or ints when integers is set).
        [[nodiscard]] LuauValue ToValueAuto(int index, bool integers = false) const;

        void PushNil();
        void PushBoolean(bool value);
        void PushNumber(double value);
        void PushString(std::string_view value);
        void PushEntity(std::uint64_t guid);
        void PushValue(const LuauValue& value);
        void PushValues(std::span<const LuauValue> values);
        // Registers the vec2/vec4 metatables, taken from the two tables on the stack.
        void StoreVectorMetatables();

    private:
        void* m_State;
    };

    using LuauNative = std::function<int(LuauNativeCall&)>;

    struct LuauLimits
    {
        std::int64_t MaxSafepoints{20'000'000};
        double MaxSeconds{1.0};
        std::size_t MaxBytes{256ull * 1024 * 1024};
    };

    // Message is the VM's text; Line is the 1-based line in the chunk (0 if unknown), usable with LuauSourceMap.
    struct LuauError
    {
        std::string Message;
        std::string Chunk;
        int Line{0};
    };

    // Maps generated-source lines to caller-defined ids (e.g. graph nodes) so errors and traces can point back.
    class LuauSourceMap
    {
    public:
        void Add(int id) { m_Lines.push_back(id); }
        void Shift(std::size_t lines) { m_Lines.insert(m_Lines.begin(), lines, 0); }
        [[nodiscard]] int Locate(int line) const
        {
            return line >= 1 && static_cast<std::size_t>(line) <= m_Lines.size() ? m_Lines[line - 1] : 0;
        }
        [[nodiscard]] std::size_t Size() const { return m_Lines.size(); }

    private:
        std::vector<int> m_Lines;
    };

    class LuauVm
    {
    public:
        LuauVm();
        ~LuauVm();
        LuauVm(const LuauVm&) = delete;
        LuauVm& operator=(const LuauVm&) = delete;

        void SetLimits(const LuauLimits& limits);
        [[nodiscard]] const LuauLimits& Limits() const;

        // Setup phase, before Seal(): natives live in a global table, trusted chunks run in the main environment.
        void Register(std::string_view table, std::string_view name, LuauNative function);
        [[nodiscard]] std::expected<void, LuauError> RunTrusted(std::string_view source, std::string_view chunkName);
        // Freezes the standard library and globals; removes os/debug.
        void Seal();

        // Compiles and runs the chunk in its own sandboxed environment; the table it returns is kept under the handle.
        [[nodiscard]] std::expected<int, LuauError> Load(std::string_view source, std::string_view chunkName);
        [[nodiscard]] std::expected<void, LuauError> Call(int handle, std::string_view function,
                                                          std::span<const double> args = {});
        void Release(int handle);

        [[nodiscard]] std::size_t BytesUsed() const;

    private:
        friend class LuauNativeCall;
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
