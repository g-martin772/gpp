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
    std::unique_ptr<T> createInstanceWithDepsImpl(Container& container, std::index_sequence<I...>)
    {
        return std::make_unique<T>(
            container.template GetRequiredService<std::tuple_element_t<I, Tuple>>()...
        );
    }

    template <typename T, typename Tuple, typename Container>
    std::unique_ptr<T> createInstanceWithDeps(Container& container)
    {
        return createInstanceWithDepsImpl<T, Tuple>(container, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    }

    template <typename T, typename Container>
    std::unique_ptr<T> createInstance(Container& container)
    {
        if constexpr (has_dependencies<T>)
        {
            using Deps = typename T::Dependencies;
            return createInstanceWithDeps<T, Deps>(container);
        }
        else
        {
            return std::make_unique<T>();
        }
    }

    export class ServiceCollection
    {
    public:
        template <typename TInterface, typename TImplementation>
            requires std::derived_from<TImplementation, TInterface> && std::derived_from<TInterface, IService>
        void AddSingleton()
        {
            auto typeId = std::type_index(typeid(TInterface));
            m_Descriptors[typeId] = ServiceDescriptor{
                .m_Factory = [](ServiceProvider& provider) -> std::unique_ptr<IService>
                {
                    return std::unique_ptr<IService>(createInstance<TImplementation>(provider));
                }
            };
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
            auto typeId = std::type_index(typeid(TInterface));
            m_Descriptors[typeId] = ServiceDescriptor{
                .m_Factory = factory
            };
        }

        template <typename T> requires std::derived_from<T, IService>
        void AddSingleton(ServiceFactory factory)
        {
            AddSingleton<T, T>(factory);
        }

        ServiceProvider Build()
        {
            return ServiceProvider(std::move(m_Descriptors));
        }

    private:
        std::unordered_map<std::type_index, ServiceDescriptor> m_Descriptors{};
    };
}
