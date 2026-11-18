#include "RHI/IRenderDriver.hpp"

namespace Leon {

    namespace {
        TScope<IRenderDriver>& ActiveDriver() {
            static TScope<IRenderDriver> driver;
            return driver;
        }
    } // namespace

    void FRenderDriverRegistry::RegisterDriver(TScope<IRenderDriver> InDriver) {
        ActiveDriver() = std::move(InDriver);
    }

    IRenderDriver* FRenderDriverRegistry::GetActiveDriver() {
        return ActiveDriver().get();
    }

} // namespace Leon
