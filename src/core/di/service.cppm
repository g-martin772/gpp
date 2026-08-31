export module GPP.Core:DI.Service;
import std;

namespace GPP
{
    export struct IService {
        virtual ~IService() = default;
    };

    export template<typename T>
    concept Service = std::is_base_of_v<IService, T>;
}