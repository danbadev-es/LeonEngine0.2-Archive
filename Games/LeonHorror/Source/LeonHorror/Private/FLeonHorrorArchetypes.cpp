#include "LeonHorror/FLeonHorrorArchetypes.hpp"

#include "Core/FProjectPaths.hpp"

#include <filesystem>

namespace Leon {

    namespace {

        constexpr const char* kMannequinMesh = "/Game/SkeletalMeshes/YBot.lskeletalmesh";

        FLeonHorrorAnimSet MannequinEnemyClips() {
            FLeonHorrorAnimSet clips;
            clips.Idle = "/Game/Animations/EnemyIdle.lanim";
            clips.Walk = "/Game/Animations/EnemyShamble.lanim";
            clips.Attack = "/Game/Animations/EnemyAttack.lanim";
            clips.Hit = "/Game/Animations/HitReact.lanim";
            clips.Death = "/Game/Animations/DeathFromTheFront.lanim";
            return clips;
        }

        /** A clip of the kind's own when it has been authored, the mannequin's otherwise. */
        FLeonHorrorAnimSet ClipsFor(const std::string& InKind) {
            const FLeonHorrorAnimSet base = MannequinEnemyClips();
            const auto own = [&](const char* InClip, const std::string& InFallback) {
                return LeonHorrorArchetypes::ExistingAsset("/Game/Animations/" + InKind + InClip + ".lanim", InFallback);
            };
            FLeonHorrorAnimSet clips;
            clips.Idle = own("Idle", base.Idle);
            clips.Walk = own("Walk", base.Walk);
            clips.Attack = own("Attack", base.Attack);
            clips.Hit = own("Hit", base.Hit);
            clips.Death = own("Death", base.Death);
            return clips;
        }

        std::vector<FLeonHorrorEnemyArchetype> BuildEnemies() {
            std::vector<FLeonHorrorEnemyArchetype> all;

            FLeonHorrorEnemyArchetype husk;
            husk.Name = LeonHorrorArchetypes::kDefaultEnemy;
            husk.MeshPath = kMannequinMesh;
            husk.HeightMetres = 1.82f;
            husk.Anims = MannequinEnemyClips();
            all.push_back(husk);

            // Thin and quick: it notices from further to the side, closes fast and hits often, but a
            // blow stops it for a long moment and it does not take many.
            FLeonHorrorEnemyArchetype patient;
            patient.Name = "Patient";
            patient.MeshPath = LeonHorrorArchetypes::ExistingAsset("/Game/SkeletalMeshes/SK_Patient.lskeletalmesh", kMannequinMesh);
            patient.HeightMetres = 1.72f;
            patient.Tuning.Health = 70.0f;
            patient.Tuning.WanderSpeed = 1.1f;
            patient.Tuning.InvestigateSpeed = 2.2f;
            patient.Tuning.ChaseSpeed = 3.4f;
            patient.Tuning.SightRadius = 13.0f;
            patient.Tuning.SightHalfAngleDegrees = 65.0f;
            patient.Tuning.AttackDamage = 16.0f;
            patient.Tuning.AttackWindup = 0.35f;
            patient.Tuning.AttackCooldown = 1.1f;
            patient.Tuning.StaggerSeconds = 0.8f;
            patient.Anims = ClipsFor("Patient");
            patient.Anims.WalkSpeedCm = 130.0f;
            patient.VoicePrefix = "/Game/Audio/SFX_Patient";
            all.push_back(patient);

            // Big and slow: it can be walked away from, it shrugs off a hit, and the blow it winds up
            // for so long takes a third of a life.
            FLeonHorrorEnemyArchetype orderly;
            orderly.Name = "Orderly";
            orderly.MeshPath = LeonHorrorArchetypes::ExistingAsset("/Game/SkeletalMeshes/SK_Orderly.lskeletalmesh", kMannequinMesh);
            orderly.HeightMetres = 1.95f;
            orderly.Tuning.Health = 220.0f;
            orderly.Tuning.WanderSpeed = 0.8f;
            orderly.Tuning.InvestigateSpeed = 1.5f;
            orderly.Tuning.ChaseSpeed = 2.3f;
            orderly.Tuning.SightRadius = 16.0f;
            orderly.Tuning.SightHalfAngleDegrees = 45.0f;
            orderly.Tuning.AttackRange = 1.7f;
            orderly.Tuning.AttackReach = 2.3f;
            orderly.Tuning.AttackDamage = 38.0f;
            orderly.Tuning.AttackWindup = 0.7f;
            orderly.Tuning.AttackCooldown = 2.2f;
            orderly.Tuning.StaggerSeconds = 0.25f;
            orderly.Tuning.MemorySeconds = 10.0f;
            orderly.Anims = ClipsFor("Orderly");
            orderly.Anims.WalkSpeedCm = 95.0f;
            orderly.VoicePrefix = "/Game/Audio/SFX_Orderly";
            all.push_back(orderly);
            return all;
        }

    } // namespace

    namespace LeonHorrorArchetypes {

        std::string ExistingAsset(const std::string& InPreferred, const std::string& InFallback) {
            std::error_code ec;
            if (std::filesystem::exists(FProjectPaths::ResolveVirtualPath(InPreferred), ec))
                return InPreferred;
            return InFallback;
        }

        const FLeonHorrorEnemyArchetype& FindEnemy(const std::string& InName) {
            // Rebuilt when another project is opened: whether an asset exists depends on the project,
            // and a table kept from an earlier one would name the wrong files.
            static std::vector<FLeonHorrorEnemyArchetype> enemies;
            static std::string builtFor;
            const std::string project = FProjectPaths::ProjectContentDir();
            if (enemies.empty() || builtFor != project) {
                enemies = BuildEnemies();
                builtFor = project;
            }
            for (const FLeonHorrorEnemyArchetype& enemy : enemies) {
                if (enemy.Name == InName)
                    return enemy;
            }
            return enemies.front();
        }

        std::vector<std::string> EnemyNames() {
            return {kDefaultEnemy, "Patient", "Orderly"};
        }

        std::string PlayerMeshPath() {
            return ExistingAsset("/Game/SkeletalMeshes/SK_Protagonist.lskeletalmesh", kMannequinMesh);
        }

    } // namespace LeonHorrorArchetypes

} // namespace Leon
