#pragma once

#include "LeonHorror/ALeonHorrorEnemy.hpp"
#include "LeonHorror/ULeonHorrorAnimInstance.hpp"

#include <string>
#include <vector>

namespace Leon {

    /**
     * Everything that tells one kind of enemy from another: how it looks, how it moves, how it
     * fights and what it sounds like. A spawner names one with its Type property.
     */
    struct FLeonHorrorEnemyArchetype {
        std::string Name;
        std::string MeshPath;
        /** The mesh is scaled to stand this tall. */
        float HeightMetres = 1.8f;
        FLeonHorrorEnemyTuning Tuning;
        FLeonHorrorAnimSet Anims;
        /** "/Game/Audio/SFX_Patient": _Idle_01..03, _Alert, _Attack_01..02, _Hurt, _Death. Empty: the plain hit sound. */
        std::string VoicePrefix;
    };

    namespace LeonHorrorArchetypes {

        /** The enemy every map had before there were kinds: the mannequin, with the original tuning. */
        inline constexpr const char* kDefaultEnemy = "Husk";

        /** The archetype of that name, or the default one when there is none. */
        const FLeonHorrorEnemyArchetype& FindEnemy(const std::string& InName);
        std::vector<std::string> EnemyNames();

        /** The mesh the player wears. */
        std::string PlayerMeshPath();

        /**
         * InPreferred when that asset exists in the project, InFallback otherwise. Art arrives after
         * the code that uses it: until a character or a clip is built, the mannequin stands in.
         */
        std::string ExistingAsset(const std::string& InPreferred, const std::string& InFallback);

    } // namespace LeonHorrorArchetypes

} // namespace Leon
