export module GPP.Core:DI.HostedService;
import :DI.Service;
import :Threading.Task;

namespace GPP
{
    export struct IHostedService : public IService {
        virtual ~IHostedService() = default;

        virtual Task<void> StartAsync(std::stop_token stopToken) = 0;
        virtual Task<void> StopAsync() = 0;
    };
}
