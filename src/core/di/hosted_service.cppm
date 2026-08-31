export module GPP.Core:DI.HostedService;
import :DI.Service;

namespace GPP
{
    export struct IHostedService : public IService {
        virtual ~IHostedService() = default;

        virtual void StartAsync(std::stop_token stopToken) = 0;
        virtual void StopAsync() = 0;
    };
}
