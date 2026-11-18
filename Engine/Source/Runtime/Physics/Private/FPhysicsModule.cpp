#include "Physics/IPhysicsScene.hpp"

namespace Leon {

    namespace {
        FPhysicsModule::FCreateScene& FactorySlot() {
            static FPhysicsModule::FCreateScene factory;
            return factory;
        }
    } // namespace

    void FPhysicsModule::Register(FCreateScene InFactory) {
        FactorySlot() = std::move(InFactory);
    }

    void FPhysicsModule::Unregister() {
        FactorySlot() = {};
    }

    bool FPhysicsModule::IsRegistered() {
        return static_cast<bool>(FactorySlot());
    }

    TRef<IPhysicsScene> FPhysicsModule::CreateScene() {
        return FactorySlot() ? FactorySlot()() : nullptr;
    }

} // namespace Leon
