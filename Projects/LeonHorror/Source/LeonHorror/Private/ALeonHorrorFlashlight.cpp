#include "LeonHorror/ALeonHorrorFlashlight.hpp"

#include "Engine/Components.hpp"
#include "Renderer/FLight.hpp"

namespace Leon {

    ALeonHorrorFlashlight::ALeonHorrorFlashlight(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorFlashlight");
        SetCanEverTick(false);
    }

    void ALeonHorrorFlashlight::PostInitializeComponents() {
        if (HasComponent<FSpotLightComponent>())
            return;
        auto& spot = AddComponent<FSpotLightComponent>();
        spot.Mobility = ELightMobility::Movable;
        spot.bEnabled = bOn;
        spot.Light.Color = glm::vec3(1.0f, 0.95f, 0.84f);
        spot.Light.Intensity = 16.0f;
        spot.Light.Radius = 22.0f;
        spot.Light.CutOff = 15.0f;
        spot.Light.OuterCutOff = 24.0f;
    }

    void ALeonHorrorFlashlight::SetOn(bool bInOn) {
        bOn = bInOn;
        if (HasComponent<FSpotLightComponent>())
            GetComponent<FSpotLightComponent>().bEnabled = bOn;
    }

    void ALeonHorrorFlashlight::UpdateFromView(const glm::vec3& InEyeLocation, const glm::vec3& InForward) {
        const float length = glm::length(InForward);
        const glm::vec3 forward = length > 1e-5f ? InForward / length : glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        right = glm::length(right) > 1e-5f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);

        // Chest height, a hand's width to the right and ahead of the body, so the mesh does not shadow it.
        const glm::vec3 position = InEyeLocation + forward * 0.45f + right * 0.22f - glm::vec3(0.0f, 0.25f, 0.0f);
        const glm::vec3 rotation = SpotLightEulerFromWorldDirection(forward);
        SetActorLocation(position);
        SetActorRotation(rotation);
        if (HasComponent<FSpotLightComponent>()) {
            // The renderer derives these from the transform every frame; set them too so the state is
            // correct before the first render and in headless runs.
            auto& spot = GetComponent<FSpotLightComponent>();
            SyncSpotLightFromTransform(spot.Light, position, rotation);
        }
    }

    glm::vec3 ALeonHorrorFlashlight::GetLightDirection() const {
        return HasComponent<FSpotLightComponent>() ? GetComponent<FSpotLightComponent>().Light.Direction
                                                   : glm::vec3(0.0f, -1.0f, 0.0f);
    }

    glm::vec3 ALeonHorrorFlashlight::GetLightPosition() const {
        return HasComponent<FSpotLightComponent>() ? GetComponent<FSpotLightComponent>().Light.Position
                                                   : GetActorLocation();
    }

} // namespace Leon
