export module GPP.Core:Notifications;

import std;

namespace GPP
{
    export enum class NotificationKind { Info, Success, Warning, Error };

    export class ProgressTask {
    public:
        struct Snapshot {
            std::string Title;
            std::string Detail;
            float Fraction = -1.0f; // < 0: unknown
            bool Finished = false;
            bool Ok = true;
            double ElapsedSeconds = 0.0;
        };

        explicit ProgressTask(std::string title, const std::size_t total = 0)
            : m_Title(std::move(title)), m_Total(total), m_Start(std::chrono::steady_clock::now()) {}

        void SetTotal(const std::size_t total) {
            std::scoped_lock lock(m_Mutex);
            m_Total = total;
        }

        void SetDetail(std::string detail) {
            std::scoped_lock lock(m_Mutex);
            m_Detail = std::move(detail);
        }

        void Advance(const std::size_t steps = 1) {
            std::scoped_lock lock(m_Mutex);
            m_Done += steps;
        }

        void SetFraction(const float fraction) {
            std::scoped_lock lock(m_Mutex);
            m_Fraction = std::clamp(fraction, 0.0f, 1.0f);
        }

        void Finish(const bool ok = true) {
            std::scoped_lock lock(m_Mutex);
            m_Finished = true;
            m_Ok = ok;
            m_End = std::chrono::steady_clock::now();
        }

        [[nodiscard]] bool IsFinished() const {
            std::scoped_lock lock(m_Mutex);
            return m_Finished;
        }

        [[nodiscard]] Snapshot Read() const {
            std::scoped_lock lock(m_Mutex);
            Snapshot snapshot;
            snapshot.Title = m_Title;
            snapshot.Detail = m_Detail;
            if (m_Total > 0) snapshot.Fraction = std::min(1.0f, static_cast<float>(m_Done) / static_cast<float>(m_Total));
            else snapshot.Fraction = m_Fraction;
            snapshot.Finished = m_Finished;
            snapshot.Ok = m_Ok;
            const auto end = m_Finished ? m_End : std::chrono::steady_clock::now();
            snapshot.ElapsedSeconds = std::chrono::duration<double>(end - m_Start).count();
            return snapshot;
        }

    private:
        mutable std::mutex m_Mutex;
        std::string m_Title;
        std::string m_Detail;
        std::size_t m_Total = 0;
        std::size_t m_Done = 0;
        float m_Fraction = -1.0f;
        bool m_Finished = false;
        bool m_Ok = true;
        std::chrono::steady_clock::time_point m_Start;
        std::chrono::steady_clock::time_point m_End;
    };

    export struct Notification {
        std::uint64_t Id = 0;
        NotificationKind Kind = NotificationKind::Info;
        std::string Title;
        std::string Message;
        float DurationSeconds = 4.0f;
        float AgeSeconds = 0.0f;
    };

    export class NotificationCenter {
    public:
        static constexpr std::size_t kMaxToasts = 32;

        static constexpr float DefaultDuration(const NotificationKind kind) noexcept {
            switch (kind) {
            case NotificationKind::Warning: return 7.0f;
            case NotificationKind::Error: return 10.0f;
            default: return 4.0f;
            }
        }

        std::uint64_t Post(const NotificationKind kind, std::string title, std::string message = {},
                           const float durationSeconds = 0.0f, std::string key = {}) {
            std::scoped_lock lock(m_Mutex);
            Slot slot;
            slot.Key = std::move(key);
            slot.Toast.Kind = kind;
            slot.Toast.Title = std::move(title);
            slot.Toast.Message = std::move(message);
            slot.Toast.DurationSeconds = durationSeconds > 0.0f ? durationSeconds : DefaultDuration(kind);
            if (!slot.Key.empty()) {
                if (const auto existing = std::ranges::find(m_Toasts, slot.Key, &Slot::Key); existing != m_Toasts.end()) {
                    slot.Toast.Id = existing->Toast.Id;
                    *existing = std::move(slot);
                    return existing->Toast.Id;
                }
            }
            slot.Toast.Id = ++m_LastId;
            const auto id = slot.Toast.Id;
            m_Toasts.push_back(std::move(slot));
            if (m_Toasts.size() > kMaxToasts) m_Toasts.erase(m_Toasts.begin());
            return id;
        }

        std::shared_ptr<ProgressTask> BeginTask(std::string title, const std::size_t total = 0) {
            auto task = std::make_shared<ProgressTask>(std::move(title), total);
            std::scoped_lock lock(m_Mutex);
            m_Tasks.push_back(task);
            return task;
        }

        void Dismiss(const std::uint64_t id) {
            std::scoped_lock lock(m_Mutex);
            std::erase_if(m_Toasts, [id](const Slot& slot) { return slot.Toast.Id == id; });
        }

        void Tick(const float deltaSeconds, const std::uint64_t heldId = 0) {
            std::scoped_lock lock(m_Mutex);
            for (auto& slot : m_Toasts) {
                if (slot.Toast.Id == heldId) continue;
                slot.Toast.AgeSeconds += deltaSeconds;
            }
            std::erase_if(m_Toasts, [](const Slot& slot) { return slot.Toast.AgeSeconds >= slot.Toast.DurationSeconds; });
            std::erase_if(m_Tasks, [](const std::shared_ptr<ProgressTask>& task) { return task->IsFinished(); });
        }

        [[nodiscard]] std::vector<Notification> Toasts() const {
            std::scoped_lock lock(m_Mutex);
            std::vector<Notification> result;
            result.reserve(m_Toasts.size());
            for (const auto& slot : m_Toasts) result.push_back(slot.Toast);
            return result;
        }

        [[nodiscard]] std::vector<std::shared_ptr<ProgressTask>> Tasks() const {
            std::scoped_lock lock(m_Mutex);
            return m_Tasks;
        }

    private:
        struct Slot {
            Notification Toast;
            std::string Key;
        };

        mutable std::mutex m_Mutex;
        std::vector<Slot> m_Toasts;
        std::vector<std::shared_ptr<ProgressTask>> m_Tasks;
        std::uint64_t m_LastId = 0;
    };
}
