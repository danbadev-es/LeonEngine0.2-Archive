#include "RHI/IRenderAPI.hpp"
#include "RHI/IRenderDriver.hpp"

namespace Leon {

    ERenderAPI IRenderAPI::CurrentAPI = ERenderAPI::Null;

    TScope<IRenderAPI> IRenderAPI::Create() {
        if (auto driver = FRenderDriverRegistry::GetActiveDriver()) {
            return driver->CreateRenderAPI();
        }
        return nullptr;
    }

} // namespace Leon
