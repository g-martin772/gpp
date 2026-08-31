export module GPP.Core:DI.Container;
import std;
import :DI.Service;
import :DI.Provider;

namespace GPP
{
    template <typename T>
    auto has_dependencies_impl(int) -> decltype(typename T::Dependencies{}, std::true_type{});

    template <typename T>
    std::false_type has_dependencies_impl(...);

    template <typename T>
    constexpr bool has_dependencies = decltype(has_dependencies_impl<T>(0))::value;

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
            ServiceFactory factory)
        {
            auto typeId = std::type_index(typeid(TInterface));
            m_Descriptors[typeId] = ServiceDescriptor{
                .factory = std::move(factory),
                .lifetime = lifetime
            };
        }

        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void Add(ServiceLifetime lifetime)
        {
            Add<TInterface>(
                lifetime,
                [](ServiceProvider& provider) -> std::shared_ptr<IService>
                {
                    return std::shared_ptr<IService>(createInstance<TImplementation>(provider));
                });
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

        ServiceProvider Build()
        {
            return ServiceProvider(&m_Descriptors);
        }

    private:
        ServiceDescriptorMap m_Descriptors{};
    };
}
