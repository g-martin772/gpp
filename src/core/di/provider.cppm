export module GPP.Core:DI.Provider;
import std;
import :DI.Service;
import :Types;
import :DI.HostedService;

namespace GPP
{
    export class ServiceProvider;
    export class ServiceScope;

    export using ServiceFactory = std::function<std::shared_ptr<IService>(ServiceProvider&)>;

    export enum class ServiceLifetime : $u8
    {
        Singleton,
        Scoped,
        Transient
    };

    export struct ServiceDescriptor
    {
        ServiceFactory factory;
        ServiceLifetime lifetime;
        bool isHostedService = false;
    };

    export using ServiceDescriptorMap = std::unordered_map<std::type_index, ServiceDescriptor>;
    export using ServiceCacheMap = std::unordered_map<std::type_index, std::shared_ptr<IService>>;

    class ServiceProvider : public IService
    {
    public:
        ServiceProvider(const ServiceProvider&) = delete;
        ServiceProvider& operator=(const ServiceProvider&) = delete;

        ServiceProvider(ServiceProvider&& other) noexcept
            : m_Descriptors(std::exchange(other.m_Descriptors, nullptr)),
              m_Cache(std::move(other.m_Cache)),
              m_ParentProvider(std::exchange(other.m_ParentProvider, nullptr))
        {
        }

        ServiceProvider& operator=(ServiceProvider&& other) noexcept
        {
            if (this != &other)
            {
                m_Descriptors = std::exchange(other.m_Descriptors, nullptr);
                m_Cache = std::move(other.m_Cache);
                m_ParentProvider = std::exchange(other.m_ParentProvider, nullptr);
            }

            return *this;
        }

        explicit ServiceProvider(ServiceDescriptorMap* descriptors)
            : m_Descriptors(descriptors)
        {
        }

        explicit ServiceProvider(ServiceProvider* parent)
            : m_Descriptors(parent->m_Descriptors), m_ParentProvider(parent)
        {
        }

        template <typename TService> requires std::derived_from<TService, IService>
        std::shared_ptr<TService> GetServiceByTypeIndex(std::type_index typeId)
        {
            // fetch descriptor
            auto descIt = m_Descriptors->find(typeId);
            if (descIt == m_Descriptors->end())
            {
                return nullptr;
            }

            switch (descIt->second.lifetime)
            {
            case ServiceLifetime::Singleton:
                {
                    if (m_ParentProvider)
                    {
                        return m_ParentProvider->GetService<TService>();
                    }

                    // check cache
                    {
                        std::scoped_lock lock(m_Mutex);
                        if (auto it = m_Cache.find(typeId); it != m_Cache.end())
                        {
                            return std::static_pointer_cast<TService>(it->second);
                        }
                    }

                    // create the service
                    std::shared_ptr<IService> instance = descIt->second.factory(*this);

                    // re-check and cache
                    {
                        std::scoped_lock lock(m_Mutex);
                        if (auto it = m_Cache.find(typeId); it != m_Cache.end())
                        {
                            return std::static_pointer_cast<TService>(it->second);
                        }
                        m_Cache[typeId] = instance;
                    }

                    return std::static_pointer_cast<TService>(instance);
                }
            case ServiceLifetime::Scoped:
                {
                    if (IsRootProvider())
                    {
                        return nullptr;
                        //throw std::runtime_error("Cannot resolve scoped service from root provider: " + std::string(typeid(TService).name()));
                    }

                    // check cache
                    {
                        std::scoped_lock lock(m_Mutex);
                        if (auto it = m_Cache.find(typeId); it != m_Cache.end())
                        {
                            return std::static_pointer_cast<TService>(it->second);
                        }
                    }

                    // check parent tree
                    if (auto service = m_ParentProvider->GetService<TService>(); service != nullptr)
                    {
                        return service;
                    }

                    // create the service
                    std::shared_ptr<IService> instance = descIt->second.factory(*this);

                    // re-check and cache
                    {
                        std::scoped_lock lock(m_Mutex);
                        if (auto it = m_Cache.find(typeId); it != m_Cache.end())
                        {
                            return std::static_pointer_cast<TService>(it->second);
                        }
                        m_Cache[typeId] = instance;
                    }

                    return std::static_pointer_cast<TService>(instance);
                }
            case ServiceLifetime::Transient:
                {
                    return std::static_pointer_cast<TService>(descIt->second.factory(*this));
                }
            default:
                return nullptr;
            }
        }

        template <typename TService> requires std::derived_from<TService, IService>
        std::shared_ptr<TService> GetService()
        {
            if constexpr (std::same_as<TService, ServiceProvider>)
            {
                return std::shared_ptr<ServiceProvider>(this, [](ServiceProvider*) {});
            }

            const auto typeId = std::type_index(typeid(TService));
            return GetServiceByTypeIndex<TService>(typeId);
        }

        template <typename TService> requires std::derived_from<TService, IService>
        std::shared_ptr<TService> GetRequiredService()
        {
            std::shared_ptr<TService> service = GetService<TService>();

            if (!service)
            {
                throw std::runtime_error("Could not resolve required service: " + std::string(typeid(TService).name()));
            }

            return service;
        }

        bool IsRootProvider() const noexcept
        {
            return m_ParentProvider == nullptr;
        }

        std::vector<std::shared_ptr<IHostedService>> GetHostedServices()
        {
            std::vector<std::shared_ptr<IHostedService>> hostedServices;

            for (const auto& [typeId, descriptor] : *m_Descriptors)
            {
                //if (std::is_base_of_v<IHostedService, std::remove_pointer_t<std::remove_reference_t<decltype(*descriptor.factory(*this))>>>)
                if (descriptor.isHostedService)
                {
                    auto service = GetServiceByTypeIndex<IHostedService>(typeId);
                    if (service)
                    {
                        hostedServices.push_back(service);
                    }
                }
            }

            return hostedServices;
        }

        ServiceScope CreateScope();

    private:
        ServiceDescriptorMap* m_Descriptors{nullptr};
        ServiceCacheMap m_Cache{};
        ServiceProvider* m_ParentProvider{nullptr};
        std::mutex m_Mutex{};
    };

    export class ServiceScope {
    public:
        explicit ServiceScope(std::unique_ptr<ServiceProvider> scopedProvider)
            : m_ScopedProvider(std::move(scopedProvider)) {}

        ~ServiceScope() = default;

        ServiceProvider& GetProvider() noexcept { return *m_ScopedProvider; }
    private:
        std::unique_ptr<ServiceProvider> m_ScopedProvider;
    };

    inline ServiceScope ServiceProvider::CreateScope()
    {
        return ServiceScope(std::make_unique<ServiceProvider>(this));
    }
}

