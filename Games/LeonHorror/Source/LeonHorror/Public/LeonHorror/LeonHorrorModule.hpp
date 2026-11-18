#pragma once

#include "Engine/IGameModule.hpp"
#include "Gameplay/UClassRegistry.hpp"

namespace Leon {

    /**
     * Registers every LeonHorror actor class and the game-instance factory.
     *
     * Deliberately not named LE_RegisterGameModule: the editor links several game libraries into one
     * process and that C symbol would collide with the other projects' entry points.
     */
    void RegisterLeonHorrorModule(UClassRegistry& InRegistry, FGameModuleHooks& OutHooks);

} // namespace Leon
