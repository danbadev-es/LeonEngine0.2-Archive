#include <doctest/doctest.h>

#include "Assets/FAnimRuntime.hpp"
#include "Assets/UAnimSequence.hpp"
#include "Assets/UBlendSpace.hpp"
#include "Assets/USkeleton.hpp"
#include "Assets/USkeletalMesh.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/USkeletalMeshComponent.hpp"
#include "Engine/UWorld.hpp"
#include "Engine/Components.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Leon;
namespace fs = std::filesystem;

namespace {

    TRef<USkeleton> MakeTwoBoneSkeleton() {
        auto skel = USkeleton::Create("TwoBone");
        FSkeletonBone root;
        root.Name = "root";
        root.ParentIndex = -1;
        root.RestLocal.Translation = glm::vec3(0.0f);
        root.InverseBindPose = glm::mat4(1.0f);

        FSkeletonBone child;
        child.Name = "spine";
        child.ParentIndex = 0;
        child.RestLocal.Translation = glm::vec3(0.0f, 1.0f, 0.0f);
        child.InverseBindPose = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)));

        skel->GetBones() = {root, child};
        skel->RebuildLookup();
        return skel;
    }

    TRef<UAnimSequence> MakeSpinAnim(const TRef<USkeleton>& InSkel) {
        auto seq = UAnimSequence::Create("Spin");
        seq->SetDuration(1.0f);
        seq->SetSampleRate(30.0f);
        FAnimBoneTrack track;
        track.BoneName = "spine";
        track.RotationKeys.push_back({0.0f, glm::quat(1, 0, 0, 0)});
        track.RotationKeys.push_back({1.0f, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0))});
        seq->GetTracks().push_back(track);
        seq->LinkSkeleton(InSkel);
        return seq;
    }

} // namespace

TEST_SUITE("Skeletal Animation") {

    TEST_CASE("Skeleton lookup and upper-body mask") {
        auto skel = MakeTwoBoneSkeleton();
        CHECK(skel->FindBoneIndex("spine") == 1);
        CHECK(skel->FindBoneIndex("mixamorig:spine") == 1);
        auto mask = skel->BuildUpperBodyMask();
        REQUIRE(mask.size() == 2);
        CHECK(mask[0] == doctest::Approx(0.0f));
        CHECK(mask[1] == doctest::Approx(1.0f));
    }

    TEST_CASE("Rest pose skinning palette is identity for bind") {
        auto skel = MakeTwoBoneSkeleton();
        FPose rest;
        FAnimRuntime::RestPose(*skel, rest);
        std::vector<glm::mat4> component;
        FAnimRuntime::LocalToComponent(*skel, rest, component);
        std::vector<glm::mat4> palette;
        FAnimRuntime::BuildSkinningPalette(*skel, component, palette);
        REQUIRE(palette.size() == 2);
        glm::mat4 ident(1.0f);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK(palette[1][c][r] == doctest::Approx(ident[c][r]).epsilon(1e-4f));
    }

    TEST_CASE("Sequence sample and pose blend are deterministic") {
        auto skel = MakeTwoBoneSkeleton();
        auto seq = MakeSpinAnim(skel);
        FPose a, b, c;
        FAnimRuntime::SampleSequence(*seq, *skel, 0.5f, true, a);
        FAnimRuntime::SampleSequence(*seq, *skel, 0.5f, true, b);
        CHECK(a.LocalTransforms[1].Rotation.w == doctest::Approx(b.LocalTransforms[1].Rotation.w));
        FAnimRuntime::BlendPoses(a, a, 0.3f, c);
        CHECK(c.LocalTransforms[1].Rotation.w == doctest::Approx(a.LocalTransforms[1].Rotation.w));
    }

    TEST_CASE("BlendSpace 1D interpolates neighboring samples") {
        auto skel = MakeTwoBoneSkeleton();
        auto idle = MakeSpinAnim(skel);
        auto run = MakeSpinAnim(skel);
        auto bs = UBlendSpace::Create("Locomotion");
        bs->Set2D(false);
        bs->SetAxisRange({0.0f, 0.0f}, {600.0f, 0.0f});
        bs->AddSample({0.0f, 0.0f}, idle);
        bs->AddSample({300.0f, 0.0f}, run);
        std::vector<float> w;
        bs->EvaluateWeights({150.0f, 0.0f}, w);
        REQUIRE(w.size() == 2);
        CHECK(w[0] == doctest::Approx(0.5f));
        CHECK(w[1] == doctest::Approx(0.5f));
    }

    TEST_CASE("BlendSpace 2D IDW and .lblend roundtrip") {
        auto skel = MakeTwoBoneSkeleton();
        auto a = MakeSpinAnim(skel);
        auto b = MakeSpinAnim(skel);
        auto bs = UBlendSpace::Create("Loco2D");
        bs->Set2D(true);
        bs->SetAxisRange({0.0f, -180.0f}, {600.0f, 180.0f});
        bs->SetSkeletonPath("/Game/Skeletons/Test.lskeleton");
        a->SetAssetPath("/Game/Animations/A.lanim");
        b->SetAssetPath("/Game/Animations/B.lanim");
        bs->AddSample({0.0f, 0.0f}, a);
        bs->AddSample({300.0f, 90.0f}, b);
        std::vector<float> w;
        bs->EvaluateWeights({0.0f, 0.0f}, w);
        REQUIRE(w.size() == 2);
        CHECK(w[0] > w[1]);

        const std::string path = "build/test.lblend";
        CHECK(bs->SaveToFile(path));
        auto loaded = UBlendSpace::Create("Loaded");
        CHECK(loaded->LoadFromFile(path));
        CHECK(loaded->Is2D());
        CHECK(loaded->GetSamples().size() == 2);
        CHECK(loaded->GetSamples()[1].SequencePath == "/Game/Animations/B.lanim");
        fs::remove(path);
    }

    TEST_CASE("BlendSpace 2D prefers local direction neighbors over opposite heading") {
        auto skel = MakeTwoBoneSkeleton();
        auto fwd = MakeSpinAnim(skel);
        auto right = MakeSpinAnim(skel);
        auto back = MakeSpinAnim(skel);
        fwd->SetAssetPath("/Game/Animations/Fwd.lanim");
        right->SetAssetPath("/Game/Animations/Right.lanim");
        back->SetAssetPath("/Game/Animations/Back.lanim");
        auto bs = UBlendSpace::Create("EightDir");
        bs->Set2D(true);
        bs->SetAxisRange({0.0f, -180.0f}, {600.0f, 180.0f});
        bs->AddSample({500.0f, 0.0f}, fwd);
        bs->AddSample({500.0f, 90.0f}, right);
        bs->AddSample({500.0f, 180.0f}, back);
        std::vector<float> w;
        bs->EvaluateWeights({500.0f, 45.0f}, w);
        REQUIRE(w.size() == 3);
        CHECK(w[0] + w[1] > w[2] * 4.0f);
        CHECK(w[0] > 0.2f);
        CHECK(w[1] > 0.2f);
    }

    TEST_CASE("Layered blend applies bone mask") {
        auto skel = MakeTwoBoneSkeleton();
        FPose base = skel->GetRestPose();
        FPose overlay = skel->GetRestPose();
        overlay.LocalTransforms[1].Translation = glm::vec3(0.0f, 5.0f, 0.0f);
        std::vector<float> mask = {0.0f, 1.0f};
        FPose out;
        FAnimRuntime::LayeredBlend(base, overlay, mask, out);
        CHECK(out.LocalTransforms[0].Translation.y == doctest::Approx(0.0f));
        CHECK(out.LocalTransforms[1].Translation.y == doctest::Approx(5.0f));
    }

    TEST_CASE("Binary roundtrip .lskeleton .lskeletalmesh .lanim") {
        auto skel = MakeTwoBoneSkeleton();
        fs::create_directories("build");
        const std::string skelPath = "build/test.lskeleton";
        CHECK(skel->SaveToFile(skelPath));
        auto loadedSkel = USkeleton::Create("Loaded");
        CHECK(loadedSkel->LoadFromFile(skelPath));
        CHECK(loadedSkel->GetNumBones() == 2);
        CHECK(loadedSkel->GetBones()[1].Name == "spine");

        auto mesh = USkeletalMesh::Create("Skinned");
        mesh->SetSkeleton(skel);
        FSkinnedMeshVertex v;
        v.Position = glm::vec3(0, 1, 0);
        v.BoneIndices = glm::ivec4(1, 0, 0, 0);
        v.BoneWeights = glm::vec4(1, 0, 0, 0);
        mesh->GetVertices() = {v, v, v};
        mesh->GetIndices() = {0, 1, 2};
        FSkeletalSubmesh sub;
        sub.Name = "Body";
        sub.IndexCount = 3;
        sub.VertexCount = 3;
        mesh->GetSubmeshes().push_back(sub);
        mesh->CalculateBounds();
        const std::string meshPath = "build/test.lskeletalmesh";
        CHECK(mesh->SaveToFile(meshPath));
        auto loadedMesh = USkeletalMesh::Create("LoadedMesh");
        CHECK(loadedMesh->LoadFromFile(meshPath));
        CHECK(loadedMesh->GetVertices().size() == 3);
        CHECK(loadedMesh->GetVertices()[0].BoneIndices.x == 1);

        auto seq = MakeSpinAnim(skel);
        const std::string animPath = "build/test.lanim";
        CHECK(seq->SaveToFile(animPath));
        auto loadedAnim = UAnimSequence::Create("LoadedAnim");
        CHECK(loadedAnim->LoadFromFile(animPath));
        CHECK(loadedAnim->GetTracks().size() == 1);
        CHECK(loadedAnim->GetDuration() == doctest::Approx(1.0f));

        fs::remove(skelPath);
        fs::remove(meshPath);
        fs::remove(animPath);
    }

    TEST_CASE("AnimInstance graph uses generic parameters") {
        auto skel = MakeTwoBoneSkeleton();
        auto seq = MakeSpinAnim(skel);
        auto inst = MakeRef<UAnimInstance>("Inst");
        inst->Initialize(skel);

        FAnimState idle;
        idle.Name = "Idle";
        idle.Sequence = seq;
        idle.bLoop = true;
        inst->GetStateMachine().AddState(idle);

        FAnimState move;
        move.Name = "Locomotion";
        move.Sequence = seq;
        move.bLoop = true;
        inst->GetStateMachine().AddState(move);
        inst->GetStateMachine().AddTransition({"Idle", "Locomotion", "FloatGreater:Speed:15", 0.12f});
        inst->GetStateMachine().AddTransition({"Locomotion", "Idle", "FloatLessEqual:Speed:15", 0.12f});
        inst->GetStateMachine().SetDefaultState("Idle");
        inst->GetStateMachine().Reset();

        inst->SetFloat("Speed", 0.0f);
        inst->NativeUpdateAnimation(0.016f);
        CHECK(inst->GetStateMachine().GetCurrentState() == "Idle");
        inst->SetFloat("Speed", 80.0f);
        inst->NativeUpdateAnimation(0.2f);
        CHECK(inst->GetStateMachine().GetCurrentState() == "Locomotion");
        FPose pose;
        inst->Evaluate(pose);
        CHECK(pose.Num() == 2);
    }

    TEST_CASE("State machine transition conditions: one case per condition kind") {
        auto skel = MakeTwoBoneSkeleton();
        auto seq = MakeSpinAnim(skel);

        // Builds a two-state machine A -> B guarded by InCondition and returns the anim instance.
        auto makeInstance = [&](const std::string& InCondition) {
            auto inst = MakeRef<UAnimInstance>("Inst");
            inst->Initialize(skel);
            FAnimState a;
            a.Name = "A";
            a.Sequence = seq;
            FAnimState b;
            b.Name = "B";
            b.Sequence = seq;
            inst->GetStateMachine().AddState(a);
            inst->GetStateMachine().AddState(b);
            inst->GetStateMachine().AddTransition({"A", "B", InCondition, 0.0f});
            inst->GetStateMachine().SetDefaultState("A");
            inst->GetStateMachine().Reset();
            return inst;
        };

        SUBCASE("Bool") {
            auto inst = makeInstance("Bool:bGo");
            inst->NativeUpdateAnimation(0.016f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "A");
            inst->SetBool("bGo", true);
            inst->NativeUpdateAnimation(0.016f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "B");
        }

        SUBCASE("NotBool") {
            auto inst = makeInstance("NotBool:bHold");
            inst->SetBool("bHold", true);
            inst->NativeUpdateAnimation(0.016f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "A");
            inst->SetBool("bHold", false);
            inst->NativeUpdateAnimation(0.016f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "B");
        }

        SUBCASE("FloatGreater") {
            auto inst = makeInstance("FloatGreater:Speed:1.5");
            inst->SetFloat("Speed", 1.5f);
            inst->NativeUpdateAnimation(0.016f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "A");
            inst->SetFloat("Speed", 1.6f);
            inst->NativeUpdateAnimation(0.016f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "B");
        }

        SUBCASE("FloatLessEqual") {
            auto inst = makeInstance("FloatLessEqual:Speed:0.2");
            inst->SetFloat("Speed", 0.3f);
            inst->NativeUpdateAnimation(0.016f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "A");
            inst->SetFloat("Speed", 0.2f);
            inst->NativeUpdateAnimation(0.016f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "B");
        }

        SUBCASE("TimeGreater waits for the threshold instead of firing on the first frame") {
            auto inst = makeInstance("TimeGreater:0.5");
            inst->NativeUpdateAnimation(0.1f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "A");
            inst->NativeUpdateAnimation(0.3f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "A");
            inst->NativeUpdateAnimation(0.2f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "B");
        }

        SUBCASE("Unknown condition never fires") {
            auto inst = makeInstance("Nonsense:1");
            inst->NativeUpdateAnimation(1.0f);
            CHECK(inst->GetStateMachine().GetCurrentState() == "A");
        }
    }

    TEST_CASE("One-shot animation blends in, plays once, blends out and removes itself") {
        auto skel = MakeTwoBoneSkeleton();
        auto seq = MakeSpinAnim(skel); // 1 s: spine turns 0 -> 90 degrees about Y
        auto inst = MakeRef<UAnimInstance>("Inst");
        inst->Initialize(skel);

        const auto spineYawDegrees = [&]() {
            FPose pose;
            inst->Evaluate(pose);
            REQUIRE(pose.Num() == 2);
            return glm::degrees(glm::angle(pose.LocalTransforms[1].Rotation));
        };

        CHECK_FALSE(inst->IsPlayingOneShot());
        CHECK(spineYawDegrees() == doctest::Approx(0.0f).epsilon(0.01));

        FAnimOneShotParams params;
        params.BlendIn = 0.2f;
        params.BlendOut = 0.2f;
        inst->PlayOneShot(seq, params);
        CHECK(inst->IsPlayingOneShot());
        CHECK(inst->GetOneShotWeight() == doctest::Approx(0.0f));

        inst->NativeUpdateAnimation(0.1f); // half way through the blend-in
        CHECK(inst->GetOneShotWeight() == doctest::Approx(0.5f));
        // The clip is at 9 degrees and half blended against the rest pose.
        CHECK(spineYawDegrees() == doctest::Approx(4.5f).epsilon(0.05));

        inst->NativeUpdateAnimation(0.4f); // t = 0.5: fully in
        CHECK(inst->GetOneShotWeight() == doctest::Approx(1.0f));
        CHECK(spineYawDegrees() == doctest::Approx(45.0f).epsilon(0.02));

        inst->NativeUpdateAnimation(0.4f); // t = 0.9: half way through the blend-out
        CHECK(inst->GetOneShotWeight() == doctest::Approx(0.5f).epsilon(0.02));

        inst->NativeUpdateAnimation(0.2f); // past the end
        CHECK_FALSE(inst->IsPlayingOneShot());
        CHECK(inst->GetOneShotWeight() == doctest::Approx(0.0f));
        CHECK(spineYawDegrees() == doctest::Approx(0.0f).epsilon(0.01)); // does not hold the last frame
    }

    TEST_CASE("Stopping a one-shot early fades it out instead of popping") {
        auto skel = MakeTwoBoneSkeleton();
        auto seq = MakeSpinAnim(skel);
        auto inst = MakeRef<UAnimInstance>("Inst");
        inst->Initialize(skel);

        FAnimOneShotParams params;
        params.BlendIn = 0.0f;
        params.BlendOut = 0.2f;
        inst->PlayOneShot(seq, params);
        inst->NativeUpdateAnimation(0.3f);
        REQUIRE(inst->GetOneShotWeight() == doctest::Approx(1.0f));

        inst->StopOneShot();
        CHECK(inst->IsPlayingOneShot());
        CHECK(inst->GetOneShotWeight() == doctest::Approx(1.0f));
        inst->NativeUpdateAnimation(0.1f);
        CHECK(inst->GetOneShotWeight() == doctest::Approx(0.5f).epsilon(0.02));
        inst->NativeUpdateAnimation(0.15f);
        CHECK_FALSE(inst->IsPlayingOneShot());

        SUBCASE("a zero blend-out stops immediately") {
            inst->PlayOneShot(seq, params);
            inst->NativeUpdateAnimation(0.3f);
            inst->StopOneShot(0.0f);
            CHECK_FALSE(inst->IsPlayingOneShot());
        }
    }

    TEST_CASE("An upper-body one-shot leaves bones outside the mask untouched") {
        auto skel = MakeTwoBoneSkeleton(); // mask: root 0, spine 1
        auto seq = UAnimSequence::Create("Both");
        seq->SetDuration(1.0f);
        seq->SetSampleRate(30.0f);
        for (const char* bone : {"root", "spine"}) {
            FAnimBoneTrack track;
            track.BoneName = bone;
            track.RotationKeys.push_back({0.0f, glm::angleAxis(glm::radians(60.0f), glm::vec3(0, 1, 0))});
            track.RotationKeys.push_back({1.0f, glm::angleAxis(glm::radians(60.0f), glm::vec3(0, 1, 0))});
            seq->GetTracks().push_back(track);
        }
        seq->LinkSkeleton(skel);

        auto inst = MakeRef<UAnimInstance>("Inst");
        inst->Initialize(skel);
        FAnimOneShotParams params;
        params.BlendIn = 0.0f;
        params.BlendOut = 0.0f;

        SUBCASE("full body moves both bones") {
            inst->PlayOneShot(seq, params);
            inst->NativeUpdateAnimation(0.5f);
            FPose pose;
            inst->Evaluate(pose);
            CHECK(glm::degrees(glm::angle(pose.LocalTransforms[0].Rotation)) == doctest::Approx(60.0f).epsilon(0.02));
            CHECK(glm::degrees(glm::angle(pose.LocalTransforms[1].Rotation)) == doctest::Approx(60.0f).epsilon(0.02));
        }

        SUBCASE("upper body only moves the spine") {
            params.bUpperBodyOnly = true;
            inst->PlayOneShot(seq, params);
            inst->NativeUpdateAnimation(0.5f);
            FPose pose;
            inst->Evaluate(pose);
            CHECK(glm::degrees(glm::angle(pose.LocalTransforms[0].Rotation)) == doctest::Approx(0.0f).epsilon(0.01));
            CHECK(glm::degrees(glm::angle(pose.LocalTransforms[1].Rotation)) == doctest::Approx(60.0f).epsilon(0.02));
        }
    }

    TEST_CASE("ACharacter writes anim rep state from movement") {
        auto world = UWorld::Create("AnimWorld");
        auto* character = world->SpawnActor<ACharacter>("Hero");
        REQUIRE(character);
        character->SetFloorZ(0.0f);
        character->SetEyeHeight(1.7f);
        character->SetActorLocation({0.0f, 1.7f, 0.0f});
        character->Tick(0.016f);
        CHECK(character->GetMesh() != nullptr);
        CHECK(character->HasComponent<FSkinnedMeshRenderState>());
        CHECK(character->GetAnimRepState().Speed >= 0.0f);
    }

    TEST_CASE("ATestCharacter derives ACharacter without game systems") {
        class ATestCharacter : public ACharacter {
        public:
            ATestCharacter(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "TestCharacter")
                : ACharacter(InHandle, InWorld, InName) {}
        };
        auto world = UWorld::Create("TestCharWorld");
        auto* character = world->SpawnActor<ATestCharacter>("Tester");
        REQUIRE(character);
        character->Tick(0.016f);
        CHECK(character->GetMesh() != nullptr);
        auto anim = character->GetMesh()->GetOrCreateAnimInstance();
        REQUIRE(anim);
        character->UpdateAnimInstance(*anim);
        CHECK(anim->GetFloat("Speed") >= 0.0f);
        CHECK_FALSE(anim->GetBool("bIsDead"));
    }

    TEST_CASE("GetBoneLocation and GetSocketLocation") {
        auto world = UWorld::Create("SocketWorld");
        auto* character = world->SpawnActor<ACharacter>("Hero");
        REQUIRE(character);
        auto mesh = character->GetMesh();
        REQUIRE(mesh);
        auto skel = MakeTwoBoneSkeleton();
        auto skm = USkeletalMesh::Create("TwoBoneMesh");
        skm->SetSkeleton(skel);
        mesh->SetSkeletalMesh(skm);
        FSkeletalMeshSocket socket;
        socket.SocketName = "muzzle";
        socket.BoneName = "spine";
        socket.RelativeLocation = glm::vec3(0.0f, 0.25f, 0.0f);
        mesh->AddSocket(socket);
        glm::vec3 boneLoc{0.0f};
        glm::vec3 socketLoc{0.0f};
        REQUIRE(mesh->GetBoneLocation("spine", boneLoc));
        REQUIRE(mesh->GetSocketLocation("muzzle", socketLoc));
        CHECK(socketLoc.y == doctest::Approx(boneLoc.y + 0.25f).epsilon(0.15f));
    }
}
