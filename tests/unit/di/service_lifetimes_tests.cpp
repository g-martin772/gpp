#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct ICounterService : IService
    {
        virtual int Id() const = 0;
    };

    class CounterService : public ICounterService
    {
    public:
        CounterService()
            : m_Id(++s_NextId)
        {
        }

        int Id() const override { return m_Id; }

    private:
        inline static std::atomic<int> s_NextId{0};
        int m_Id{0};
    };
}

TEST_CASE ("Singleton resolves the same instance from root and scopes", "[di][lifetime][singleton]")
{
   ServiceCollection services;
   services.AddSingleton<ICounterService, CounterService>();

   auto provider = services.Build();
   const auto rootFirst = provider.GetRequiredService<ICounterService>();
   const auto rootSecond = provider.GetRequiredService<ICounterService>();
   CHECK(rootFirst == rootSecond);

   auto scopeA = provider.CreateScope();
   auto scopeB = provider.CreateScope();
   const auto fromScopeA = scopeA.GetProvider().GetRequiredService<ICounterService>();
   const auto fromScopeB = scopeB.GetProvider().GetRequiredService<ICounterService>();
   CHECK(rootFirst == fromScopeA);
   CHECK(rootFirst == fromScopeB);
}

TEST_CASE ("Transient resolves a new instance each time", "[di][lifetime][transient]")
{
   ServiceCollection services;
   services.AddTransient<ICounterService, CounterService>();

   auto provider = services.Build();
   const auto first = provider.GetRequiredService<ICounterService>();
   const auto second = provider.GetRequiredService<ICounterService>();

   CHECK(first != second);
   CHECK(first->Id() != second->Id());
}

TEST_CASE ("Scoped resolves one instance per scope", "[di][lifetime][scoped]")
{
   ServiceCollection services;
   services.AddScoped<ICounterService, CounterService>();

   auto provider = services.Build();
   CHECK(provider.GetService<ICounterService>() == nullptr);

   auto scopeA = provider.CreateScope();
   auto scopeB = provider.CreateScope();

   const auto aFirst = scopeA.GetProvider().GetRequiredService<ICounterService>();
   const auto aSecond = scopeA.GetProvider().GetRequiredService<ICounterService>();
   const auto bFirst = scopeB.GetProvider().GetRequiredService<ICounterService>();

   CHECK(aFirst == aSecond);
   CHECK(aFirst != bFirst);
   CHECK(aFirst->Id() != bFirst->Id());
}
