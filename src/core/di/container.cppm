export module GPP.Core:DI.Container;
import std;
import :DI.Service;
import :DI.Provider;
import :Application.Config;

namespace GPP
{
    template <typename T, typename = void>
    struct has_dependencies_impl : std::false_type {};

    template <typename T>
    struct has_dependencies_impl<T, std::void_t<typename T::Dependencies>> : std::true_type {};

    template <typename T>
    constexpr bool has_dependencies = has_dependencies_impl<T>::value;

    template <typename T, typename Tuple, std::size_t... I, typename Container>
    std::shared_ptr<T> createInstanceWithDepsImpl(Container& container, std::index_sequence<I...>)
    {
        auto resolveDep = [&container]<typename TDep>() -> decltype(auto)
        {
            if constexpr (std::is_pointer_v<TDep>)
            {
                using TValue = std::remove_pointer_t<TDep>;
                return container.template GetRequiredService<TValue>().get();
            }
            else
            {
                return container.template GetRequiredService<TDep>();
            }
        };

        return std::make_shared<T>(
            resolveDep.template operator()<std::tuple_element_t<I, Tuple>>()...
        );
    }

    template <typename T, typename Tuple, typename Container>
    std::shared_ptr<T> createInstanceWithDeps(Container& container)
    {
        return createInstanceWithDepsImpl<T, Tuple>(container, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    }

    template <typename T, typename Container>
    std::shared_ptr<T> createInstance(Container& container)
    {
        if constexpr (has_dependencies<T>)
        {
            using Deps = typename T::Dependencies;
            return createInstanceWithDeps<T, Deps>(container);
        }
        else
        {
            return std::make_shared<T>();
        }
    }

    export class ServiceCollection
    {
    public:
        template <typename TInterface>
            requires std::derived_from<TInterface, IService>
        void Add(
            ServiceLifetime lifetime,
            ServiceFactory factory,
            bool isHostedService = false)
        {
            auto typeId = std::type_index(typeid(TInterface));
            m_Descriptors[typeId] = ServiceDescriptor{
                .factory = std::move(factory),
                .lifetime = lifetime,
                .isHostedService = isHostedService
            };
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void Add(ServiceLifetime lifetime, bool isHostedService = false)
        {
            Add<TInterface>(
                lifetime,
                [](ServiceProvider& provider) -> std::shared_ptr<IService>
                {
                    return std::shared_ptr<IService>(createInstance<TImplementation>(provider));
                },
                isHostedService);
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddSingleton()
        {
            Add<TInterface, TImplementation>(ServiceLifetime::Singleton);
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddSingleton()
        {
            AddSingleton<T, T>();
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddSingleton(ServiceFactory factory)
        {
            Add<TInterface>(ServiceLifetime::Singleton, std::move(factory));
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddSingleton(ServiceFactory factory)
        {
            AddSingleton<T, T>(factory);
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddHostedService()
        {
            Add<TInterface, TImplementation>(ServiceLifetime::Singleton, true);
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddHostedService()
        {
            AddHostedService<T, T>();
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddHostedService(ServiceFactory factory)
        {
            Add<TInterface>(ServiceLifetime::Singleton, std::move(factory), true);
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddHostedService(ServiceFactory factory)
        {
            AddHostedService<T, T>(factory);
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddTransient()
        {
            Add<TInterface, TImplementation>(ServiceLifetime::Transient);
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddTransient()
        {
            AddTransient<T, T>();
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddTransient(ServiceFactory factory)
        {
            Add<TInterface>(ServiceLifetime::Transient, std::move(factory));
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddTransient(ServiceFactory factory)
        {
            AddTransient<T, T>(factory);
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddScoped()
        {
            Add<TInterface, TImplementation>(ServiceLifetime::Scoped);
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddScoped()
        {
            AddScoped<T, T>();
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddScoped(ServiceFactory factory)
        {
            Add<TInterface>(ServiceLifetime::Scoped, std::move(factory));
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddScoped(ServiceFactory factory)
        {
            AddScoped<T, T>(factory);
        }

        template <typename TOptions> requires std::derived_from<TOptions, IService>
        void Configure(const std::string& sectionPath,
                       std::function<TOptions(const IConfigurationSection &)> binder)
        {
            auto typeId = std::type_index(typeid(TOptions));
            auto binderPtr = std::make_shared<std::function<TOptions(const IConfigurationSection&)>>(std::move(binder));

            m_DeferredConfigurations.push_back([typeId, sectionPath, binderPtr](IConfiguration& config, ServiceDescriptorMap& descriptors)
            {
                auto section = std::shared_ptr<IConfigurationSection>(config.GetSection(sectionPath).release());
                descriptors[typeId] = ServiceDescriptor{
                    .factory = [section = std::move(section), binderPtr](ServiceProvider&) -> std::shared_ptr<IService>
                    {
                        TOptions options = (*binderPtr)(*section);
                        return std::make_shared<TOptions>(std::move(options));
                    },
                    .lifetime = ServiceLifetime::Singleton
                };
            });
        }

        template <typename TOptions> requires std::derived_from<TOptions, IService>  && ConfigurableOptions<TOptions>
        void Configure(const std::string& sectionPath)
        {
            Configure<TOptions>(sectionPath, TOptions::FromConfig);
        }

        void ApplyConfiguration(IConfiguration& config)
        {
            for (auto& deferred : m_DeferredConfigurations)
            {
                deferred(config, m_Descriptors);
            }
        }

        ServiceProvider Build()
        {
            return ServiceProvider(&m_Descriptors);
        }

    private:
        ServiceDescriptorMap m_Descriptors{};
        std::vector<std::function<void(IConfiguration&, ServiceDescriptorMap&)>> m_DeferredConfigurations{};
    };
}
