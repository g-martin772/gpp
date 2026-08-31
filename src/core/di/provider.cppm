export module GPP.Core:DI.Provider;
import std;
import :DI.Service;

namespace GPP
{
    export class ServiceProvider;

    export using ServiceFactory = std::function<std::unique_ptr<IService>(ServiceProvider&)>;

    export struct ServiceDescriptor {
         ServiceFactory m_Factory;
    };

    class ServiceProvider {
    public:
        explicit ServiceProvider(std::unordered_map<std::type_index, ServiceDescriptor> descriptors)
            : m_Descriptors(std::move(descriptors)) {}

        template<typename TService>
            requires std::derived_from<TService, IService>
        TService* GetRequiredService() {
            auto typeId = std::type_index(typeid(TService));

            // check of the uniquely-owned Singleton cache
            {
                std::scoped_lock lock(m_Mutex);
                if (auto it = m_SingletonCache.find(typeId); it != m_SingletonCache.end()) {
                    return static_cast<TService*>(it->second.get());
                }
            }

            // fetch descriptor
            auto descIt = m_Descriptors.find(typeId);
            if (descIt == m_Descriptors.end()) {
                throw std::runtime_error("Service not registered: " + std::string(typeid(TService).name()));
            }

            // create the service
            std::unique_ptr<IService> instance = descIt->second.m_Factory(*this);
            TService* rawPtr = static_cast<TService*>(instance.get());

            // re-check and cache
            {
                std::scoped_lock lock(m_Mutex);
                if (auto it = m_SingletonCache.find(typeId); it != m_SingletonCache.end()) {
                    return static_cast<TService*>(it->second.get());
                }
                m_SingletonCache[typeId] = std::move(instance);
            }

            return rawPtr;
        }

    private:
        std::unordered_map<std::type_index, ServiceDescriptor> m_Descriptors{};
        std::unordered_map<std::type_index, std::unique_ptr<IService>> m_SingletonCache{};
        std::mutex m_Mutex{};
    };
}