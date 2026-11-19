#include <doctest/doctest.h>

#include "Assets/FMaterial.hpp"
#include "Assets/FMaterialInstance.hpp"
#include "Assets/FMaterialSerializer.hpp"
#include "GPU/WorldRenderFixture.hpp"
#include "RHI/FTexture.hpp"
#include "RenderCore/FLightProbeGrid.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Decals: blended geometry laid over the level's surfaces (EAlphaMode::Decal). Whole frames of the
// small scene of FWorldRenderFixture, with a decal on its floor or hanging in its air.

namespace Leon {

    namespace {

        struct FDecalFixture : FWorldRenderFixture {
            /** Kept so a test can change a decal's material while it is in the world. */
            std::vector<TRef<FMaterial>> Materials;
            TRef<FTexture2D> AlphaSquare;

            FDecalFixture() {
                if (!bReady)
                    return;
                UsePlainPostProcess();
                World->GetWorldRenderer()->SetPlanarReflectionEnabled(false);
            }

            /** White, solid in the middle half of the image and empty around it. */
            TRef<FTexture2D> GetAlphaSquare() {
                if (AlphaSquare)
                    return AlphaSquare;
                constexpr uint32_t size = 64;
                std::vector<uint8_t> pixels(size * size * 4, 255);
                for (uint32_t y = 0; y < size; ++y) {
                    for (uint32_t x = 0; x < size; ++x) {
                        const bool bInside = x >= size / 4 && x < size * 3 / 4 && y >= size / 4 && y < size * 3 / 4;
                        pixels[(y * size + x) * 4 + 3] = bInside ? 255 : 0;
                    }
                }
                AlphaSquare = FTexture2D::Create(size, size);
                AlphaSquare->SetData(pixels.data(), static_cast<uint32_t>(pixels.size()));
                return AlphaSquare;
            }

            /** A matt decal material of one colour. bInAlphaSquare: only the middle half of its quad is there. */
            TRef<FMaterial> MakeDecalMaterial(const char* InName, const glm::vec3& InColor, bool bInAlphaSquare = false) {
                const TRef<FMaterial> material = FMaterial::Create(InName);
                material->SetAlbedoColor(InColor);
                material->SetRoughness(0.9f);
                material->SetAlphaMode(EAlphaMode::Decal);
                if (bInAlphaSquare)
                    material->SetAlbedoMap(GetAlphaSquare());
                Materials.push_back(material);
                return material;
            }

            /** A quad with a decal material. Movable and without a lightmap, as a level places them. */
            FMeshComponent& AddDecal(const char* InName, const glm::vec3& InPosition, const glm::vec3& InRotation,
                                     float InWidth, float InHeight, const TRef<FMaterial>& InMaterial) {
                AActor* actor = World->SpawnActor<AActor>(InName);
                actor->SetActorLocation(InPosition);
                actor->SetActorRotation(InRotation);
                auto& mesh = actor->AddComponent<FMeshComponent>(FMeshPrimitives::CreateQuad(InWidth, InHeight));
                mesh.MeshType = "Quad";
                mesh.MeshWidth = InWidth;
                mesh.MeshHeight = InHeight;
                mesh.Mobility = EComponentMobility::Movable;
                actor->AddComponent<FMaterialComponent>(InMaterial->CreateInstance());
                return mesh;
            }

            /** A decal lying on the floor (the top of the floor is y = 0), InLift above it. */
            FMeshComponent& AddFloorDecal(const char* InName, float InX, float InZ, float InWidth, float InDepth,
                                          const TRef<FMaterial>& InMaterial, float InLift = 0.0f) {
                return AddDecal(InName, {InX, InLift, InZ}, {-90.0f, 0.0f, 0.0f}, InWidth, InDepth, InMaterial);
            }

            /** Where a point of the world lands in the picture, in pixels. */
            glm::vec2 PixelOf(const glm::vec3& InWorldPoint) const {
                const glm::vec4 clip = Camera.GetViewProjectionMatrix() * glm::vec4(InWorldPoint, 1.0f);
                REQUIRE(clip.w > 0.0f);
                return {(clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(kWidth),
                        (0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(kHeight)};
            }

            glm::ivec3 ColorAt(const FImage& InImage, const glm::vec3& InWorldPoint) const {
                const glm::vec2 pixel = PixelOf(InWorldPoint);
                REQUIRE(pixel.x >= 0.0f);
                REQUIRE(pixel.x < static_cast<float>(kWidth));
                REQUIRE(pixel.y >= 0.0f);
                REQUIRE(pixel.y < static_cast<float>(kHeight));
                const uint8_t* color = InImage.At(static_cast<uint32_t>(pixel.x), static_cast<uint32_t>(pixel.y));
                return {color[0], color[1], color[2]};
            }

            /** The rectangle of the picture that holds the given points, InMargin pixels wider on every side. */
            struct FPixelBox {
                glm::vec2 Min{1.0e9f};
                glm::vec2 Max{-1.0e9f};
                bool Contains(uint32_t InX, uint32_t InY) const {
                    const glm::vec2 pixel(static_cast<float>(InX) + 0.5f, static_cast<float>(InY) + 0.5f);
                    return pixel.x >= Min.x && pixel.x <= Max.x && pixel.y >= Min.y && pixel.y <= Max.y;
                }
            };
            FPixelBox BoxOf(const std::vector<glm::vec3>& InPoints, float InMargin) const {
                FPixelBox box;
                for (const glm::vec3& point : InPoints) {
                    const glm::vec2 pixel = PixelOf(point);
                    box.Min = glm::min(box.Min, pixel - glm::vec2(InMargin));
                    box.Max = glm::max(box.Max, pixel + glm::vec2(InMargin));
                }
                return box;
            }

            /** Where the view ray of a pixel meets the plane y = 0; false when it does not (the sky). */
            bool FloorPointOf(uint32_t InX, uint32_t InY, glm::vec3& OutPoint) const {
                const glm::mat4 clipToWorld = glm::inverse(Camera.GetViewProjectionMatrix());
                const glm::vec2 ndc((static_cast<float>(InX) + 0.5f) / static_cast<float>(kWidth) * 2.0f - 1.0f,
                                    1.0f - (static_cast<float>(InY) + 0.5f) / static_cast<float>(kHeight) * 2.0f);
                glm::vec4 nearPoint = clipToWorld * glm::vec4(ndc, -1.0f, 1.0f);
                glm::vec4 farPoint = clipToWorld * glm::vec4(ndc, 1.0f, 1.0f);
                nearPoint /= nearPoint.w;
                farPoint /= farPoint.w;
                const glm::vec3 direction = glm::vec3(farPoint - nearPoint);
                if (direction.y > -1.0e-6f)
                    return false;
                const float along = -nearPoint.y / direction.y;
                if (along < 0.0f || along > 1.0f)
                    return false;
                OutPoint = glm::vec3(nearPoint) + direction * along;
                return true;
            }

            static uint32_t CountDifferent(const FImage& InA, const FImage& InB) {
                uint32_t different = 0;
                for (uint32_t y = 0; y < kHeight; ++y) {
                    for (uint32_t x = 0; x < kWidth; ++x) {
                        const uint8_t* a = InA.At(x, y);
                        const uint8_t* b = InB.At(x, y);
                        different += (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) ? 1 : 0;
                    }
                }
                return different;
            }
        };

        const glm::vec3 kBlue(0.05f, 0.05f, 0.9f);
        const glm::vec3 kRed(0.9f, 0.05f, 0.05f);

        bool IsBlue(const glm::ivec3& InColor) {
            return InColor.b > 40 && InColor.b > InColor.r * 2 && InColor.b > InColor.g * 2;
        }
        bool IsRed(const glm::ivec3& InColor) {
            return InColor.r > 40 && InColor.r > InColor.b * 2 && InColor.r > InColor.g * 2;
        }

    } // namespace

    TEST_SUITE("Shader GPU - Decals") {

        TEST_CASE_FIXTURE(FDecalFixture, "A decal colours the floor where its alpha is and nowhere else") {
            REQUIRE(bReady);
            // Two metres square in front of the box; its texture is solid in the middle metre only.
            const TRef<FMaterial> material = MakeDecalMaterial("Stain", kBlue, true);
            FMeshComponent& decal = AddFloorDecal("Stain", 0.0f, 2.0f, 2.0f, 2.0f, material);

            decal.bVisible = false;
            const FImage bare = Render();
            decal.bVisible = true;
            const FImage stained = Render();

            const glm::vec3 middle(0.0f, 0.0f, 2.0f);
            const glm::ivec3 floorColor = ColorAt(bare, middle);
            const glm::ivec3 decalColor = ColorAt(stained, middle);
            CAPTURE(floorColor.r);
            CAPTURE(floorColor.b);
            CAPTURE(decalColor.r);
            CAPTURE(decalColor.b);
            CHECK_FALSE(IsBlue(floorColor));
            CHECK(IsBlue(decalColor));

            // Inside the quad but outside the square of the texture, the floor is exactly as it was.
            for (const glm::vec3& empty : {glm::vec3(-0.85f, 0.0f, 2.0f), glm::vec3(0.85f, 0.0f, 2.0f),
                                           glm::vec3(0.0f, 0.0f, 1.15f), glm::vec3(0.0f, 0.0f, 2.85f)})
                CHECK(ColorAt(stained, empty) == ColorAt(bare, empty));

            // Nothing in the picture changed but the square (and the texel around it the filter fades over).
            const float reach = 0.5f + 2.0f / 64.0f * 1.5f;
            const FPixelBox square = BoxOf({{-reach, 0.0f, 2.0f - reach}, {reach, 0.0f, 2.0f - reach},
                                            {-reach, 0.0f, 2.0f + reach}, {reach, 0.0f, 2.0f + reach}}, 1.0f);
            uint32_t changedInside = 0;
            uint32_t changedOutside = 0;
            for (uint32_t y = 0; y < kHeight; ++y) {
                for (uint32_t x = 0; x < kWidth; ++x) {
                    const uint8_t* a = bare.At(x, y);
                    const uint8_t* b = stained.At(x, y);
                    if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2])
                        continue;
                    ++(square.Contains(x, y) ? changedInside : changedOutside);
                }
            }
            CHECK(changedInside > 300);
            CHECK(changedOutside == 0);

            // The opacity of the material multiplies the alpha of the texture: half of it leaves the
            // floor showing through, between the two colours.
            material->SetOpacity(0.5f);
            const FImage faint = Render();
            const glm::ivec3 faintColor = ColorAt(faint, middle);
            CAPTURE(faintColor.r);
            CAPTURE(faintColor.b);
            CHECK(faintColor.r > decalColor.r + 10);
            CHECK(faintColor.r < floorColor.r - 10);
            CHECK(ColorAt(faint, {0.85f, 0.0f, 2.0f}) == ColorAt(bare, {0.85f, 0.0f, 2.0f}));
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FDecalFixture, "A decal is lit by the lights and shadowed by what stands in them") {
            REQUIRE(bReady);
            // Next to no light from the sky: what the decal shows is what the lamps give it.
            AActor* environment = World->SpawnActor<AActor>("Environment");
            auto& sky = environment->AddComponent<FSkyboxComponent>();
            sky.bEnabled = false;
            sky.SkyZenithColor = sky.HorizonColor = sky.GroundColor = glm::vec3(0.02f);
            // White, beside the box, where the lamp throws the box's shadow on the floor.
            const TRef<FMaterial> material = MakeDecalMaterial("Chalk", glm::vec3(1.0f));
            AddFloorDecal("Chalk", -1.3f, -0.3f, 1.6f, 1.6f, material);
            const glm::vec3 inShadow(-1.2f, 0.0f, -0.3f);

            FShadowSettings& shadows = World->GetWorldRenderer()->GetShadowSettings();
            shadows.bEnableShadows = false;
            const float lit = LumaAt(Render(), inShadow);
            shadows.bEnableShadows = true;
            const float shadowed = LumaAt(Render(), inShadow);
            SwitchFixtureLightsOff();
            const float unlit = LumaAt(Render(), inShadow);
            CAPTURE(lit);
            CAPTURE(shadowed);
            CAPTURE(unlit);
            CHECK(lit > 0.3f);
            CHECK(shadowed < lit * 0.5f);
            CHECK(unlit < lit * 0.3f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FDecalFixture, "A decal casts no shadow") {
            REQUIRE(bReady);
            // A solid sheet hanging between the lamp and the floor: as a decal it blocks no light.
            const TRef<FMaterial> material = MakeDecalMaterial("Sheet", kBlue);
            const glm::vec3 center(1.0f, 1.2f, 1.5f);
            FMeshComponent& sheet = AddDecal("Sheet", center, {-90.0f, 0.0f, 0.0f}, 1.2f, 1.2f, material);
            World->GetWorldRenderer()->GetShadowSettings().bEnableShadows = true;
            const FPixelBox footprint = BoxOf({center + glm::vec3(-0.6f, 0.0f, -0.6f), center + glm::vec3(0.6f, 0.0f, -0.6f),
                                               center + glm::vec3(-0.6f, 0.0f, 0.6f), center + glm::vec3(0.6f, 0.0f, 0.6f)},
                                              2.0f);
            const auto darkerAroundSheet = [&footprint](const FImage& InWithout, const FImage& InWith) {
                uint32_t darker = 0;
                for (uint32_t y = 0; y < kHeight; ++y) {
                    for (uint32_t x = 0; x < kWidth; ++x) {
                        if (!footprint.Contains(x, y) && InWithout.Luma(x, y) - InWith.Luma(x, y) > 0.02f)
                            ++darker;
                    }
                }
                return darker;
            };

            sheet.bVisible = false;
            const FImage without = Render();
            sheet.bVisible = true;
            const FImage with = Render();
            CHECK(IsBlue(ColorAt(with, center)));
            CHECK(darkerAroundSheet(without, with) == 0);

            // The same sheet as an ordinary blended surface does throw one: the test would notice.
            material->SetAlphaMode(EAlphaMode::Blend);
            CHECK(darkerAroundSheet(without, Render()) > 100);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FDecalFixture, "A decal writes no depth: what is in front covers it, what is drawn after it still shows") {
            REQUIRE(bReady);
            // On the floor, under the box and around it.
            const TRef<FMaterial> floorMaterial = MakeDecalMaterial("Spill", kBlue);
            FMeshComponent& spill = AddFloorDecal("Spill", 0.0f, 0.0f, 3.0f, 3.0f, floorMaterial);
            // Hanging in the air to the left, half transparent, with a pane of green glass behind it.
            const TRef<FMaterial> hangingMaterial = MakeDecalMaterial("Veil", kBlue);
            hangingMaterial->SetOpacity(0.5f);
            const glm::vec3 veilCenter(-1.5f, 1.0f, 3.0f);
            FMeshComponent& veil = AddDecal("Veil", veilCenter, {0.0f, 0.0f, 0.0f}, 0.8f, 0.8f, hangingMaterial);
            const TRef<FMaterial> glassMaterial = FMaterial::Create("Glass");
            glassMaterial->SetAlbedoColor(glm::vec3(0.0f));
            glassMaterial->SetEmissiveColor({0.0f, 1.0f, 0.0f});
            glassMaterial->SetEmissiveIntensity(1.0f);
            glassMaterial->SetOpacity(0.6f);
            glassMaterial->SetAlphaMode(EAlphaMode::Blend);
            Materials.push_back(glassMaterial);
            AActor* glassActor = World->SpawnActor<AActor>("Glass");
            glassActor->SetActorLocation({-1.5f, 1.0f, 2.2f});
            auto& glass = glassActor->AddComponent<FMeshComponent>(FMeshPrimitives::CreateQuad(1.4f, 1.4f));
            glass.MeshType = "Quad";
            glass.MeshWidth = 1.4f;
            glass.MeshHeight = 1.4f;
            glass.Mobility = EComponentMobility::Movable;
            glass.bCastShadows = false;
            glassActor->AddComponent<FMaterialComponent>(glassMaterial->CreateInstance());

            spill.bVisible = false;
            veil.bVisible = false;
            glass.bVisible = false;
            const FImage bare = Render();
            spill.bVisible = true;
            veil.bVisible = true;
            const FImage decals = Render();
            glass.bVisible = true;
            const FImage withGlass = Render();

            // The box stands on the decal and hides the part behind it: its pixels are as without it.
            const glm::vec3 boxFront(0.0f, 0.5f, 0.5f);
            CHECK(ColorAt(decals, boxFront) == ColorAt(bare, boxFront));
            const glm::vec3 boxTop(0.0f, 1.0f, 0.0f);
            CHECK(ColorAt(decals, boxTop) == ColorAt(bare, boxTop));
            CHECK(IsBlue(ColorAt(decals, {0.0f, 0.0f, 1.2f})));

            // The glass is behind the veil and drawn after it. Had the veil written its depth, the
            // glass would be cut away there.
            const glm::ivec3 veilAlone = ColorAt(decals, veilCenter);
            const glm::ivec3 veilOverGlass = ColorAt(withGlass, veilCenter);
            CAPTURE(veilAlone.g);
            CAPTURE(veilOverGlass.g);
            CHECK(veilAlone.b > veilAlone.g + 20);
            CHECK(veilOverGlass.g > veilAlone.g + 40);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FDecalFixture, "A decal lying in the plane of the floor, or a millimetre above it, covers it without a gap") {
            REQUIRE(bReady);
            World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
            const TRef<FMaterial> material = MakeDecalMaterial("Paint", kBlue);

            // Every pixel that looks at the painted floor must be paint: one grey pixel is the floor
            // winning the depth test.
            const auto floorShowing = [this](const FImage& InImage, uint32_t& OutPainted) {
                uint32_t showing = 0;
                OutPainted = 0;
                for (uint32_t y = 0; y < kHeight; ++y) {
                    for (uint32_t x = 0; x < kWidth; ++x) {
                        glm::vec3 point;
                        if (!FloorPointOf(x, y, point) || std::abs(point.x) > 4.8f || std::abs(point.z) > 4.8f)
                            continue;
                        const uint8_t* pixel = InImage.At(x, y);
                        if (IsBlue({pixel[0], pixel[1], pixel[2]}))
                            ++OutPainted;
                        else
                            ++showing;
                    }
                }
                return showing;
            };

            for (const float lift : {0.0f, 0.001f}) {
                CAPTURE(lift);
                FMeshComponent& paint = AddFloorDecal("Paint", 0.0f, 0.0f, 10.0f, 10.0f, material, lift);
                // From above, as the fixture looks at its scene.
                Camera.SetPosition({0.0f, 2.0f, 6.0f});
                Camera.SetRotation(-15.0f, -90.0f);
                uint32_t painted = 0;
                CHECK(floorShowing(Render(), painted) == 0);
                CHECK(painted > 5000);
                // Along the floor, from the height of an ankle: the far end is ten metres away and
                // seen at a degree or two.
                Camera.SetPosition({0.0f, 0.15f, 5.5f});
                Camera.SetRotation(0.0f, -90.0f);
                CHECK(floorShowing(Render(), painted) == 0);
                CHECK(painted > 1500);
                // From across a corner, far from both axes.
                Camera.SetPosition({5.5f, 1.2f, 5.5f});
                Camera.SetRotation(-8.0f, -135.0f);
                CHECK(floorShowing(Render(), painted) == 0);
                CHECK(painted > 5000);
                paint.bVisible = false;
            }
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FDecalFixture, "Where two decals overlap the higher priority is on top, and of equals the later one") {
            REQUIRE(bReady);
            const TRef<FMaterial> red = MakeDecalMaterial("Red", kRed);
            const TRef<FMaterial> blue = MakeDecalMaterial("Blue", kBlue);
            // The red one is in the world first; they share the strip around x = 0.
            AddFloorDecal("Red", -0.4f, 3.0f, 1.6f, 1.2f, red);
            AddFloorDecal("Blue", 0.4f, 3.0f, 1.6f, 1.2f, blue, 0.0005f);
            const glm::vec3 shared(0.0f, 0.0f, 3.0f);
            const glm::vec3 redOnly(-0.9f, 0.0f, 3.0f);
            const glm::vec3 blueOnly(0.9f, 0.0f, 3.0f);

            // Equal priorities: the one that came into the world later lies on top.
            FImage image = Render();
            CHECK(IsBlue(ColorAt(image, shared)));
            CHECK(IsRed(ColorAt(image, redOnly)));
            CHECK(IsBlue(ColorAt(image, blueOnly)));

            // A higher priority wins over the order of the world, although the blue quad is the higher of the two.
            red->SetSortPriority(1);
            image = Render();
            CHECK(IsRed(ColorAt(image, shared)));
            CHECK(IsBlue(ColorAt(image, blueOnly)));

            blue->SetSortPriority(2);
            CHECK(IsBlue(ColorAt(Render(), shared)));

            // Priorities may be negative: below the default.
            blue->SetSortPriority(0);
            red->SetSortPriority(-1);
            CHECK(IsBlue(ColorAt(Render(), shared)));
            blue->SetSortPriority(-2);
            CHECK(IsRed(ColorAt(Render(), shared)));
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FDecalFixture, "A decal has no lightmap: its ambient light is that of the light probes around it") {
            REQUIRE(bReady);
            SwitchFixtureLightsOff();
            AActor* environment = World->SpawnActor<AActor>("Environment");
            auto& sky = environment->AddComponent<FSkyboxComponent>();
            sky.bEnabled = false;
            sky.SkyZenithColor = sky.HorizonColor = sky.GroundColor = glm::vec3(0.25f);
            sky.EnvironmentIntensity = 1.0f;
            const TRef<FMaterial> material = MakeDecalMaterial("Chalk", glm::vec3(1.0f));
            const glm::vec3 middle(0.0f, 0.0f, 3.0f);
            AddFloorDecal("Chalk", middle.x, middle.z, 1.5f, 1.5f, material);

            // Without probes the grey of the sky.
            const glm::ivec3 underSky = ColorAt(Render(), middle);
            CAPTURE(underSky.r);
            CAPTURE(underSky.g);
            CHECK(underSky.r > 30);
            CHECK(std::abs(underSky.r - underSky.b) < 8);

            // A probe where the decal is, with red light coming down on it.
            FLightProbeGrid grid;
            grid.Origin = middle;
            grid.Spacing = glm::vec3(1.0f);
            grid.Resize({1, 1, 1});
            grid.Probes[0].AddRadiance({0.0f, 1.0f, 0.0f}, {3.0f, 0.1f, 0.1f});
            World->SetLightProbes(grid);
            const glm::ivec3 underProbe = ColorAt(Render(), middle);
            CAPTURE(underProbe.r);
            CAPTURE(underProbe.g);
            CHECK(underProbe.r > 100);
            CHECK(underProbe.r > underProbe.g * 2);
            CHECK(NewValidationErrors() == 0);
        }
    }

    TEST_SUITE("Decal materials") {

        TEST_CASE("the decal mode and the sort priority of a material are written and read back") {
            FMaterial stain("Stain");
            stain.SetAlphaMode(EAlphaMode::Decal);
            stain.SetOpacity(0.75f);
            stain.SetSortPriority(-3);
            std::string text;
            REQUIRE(FMaterialSerializer::SerializeText(text, stain));
            CHECK(text.find("AlphaMode: \"Decal\"") != std::string::npos);
            CHECK(text.find("SortPriority: -3") != std::string::npos);

            const TRef<FMaterial> loaded = FMaterial::Create("Loaded");
            REQUIRE(FMaterialSerializer::DeserializeText(text, *loaded));
            CHECK(loaded->GetAlphaMode() == EAlphaMode::Decal);
            CHECK(loaded->GetOpacity() == doctest::Approx(0.75f));
            CHECK(loaded->GetSortPriority() == -3);
            // A decal is blended over what is there and leaves the depth buffer alone.
            CHECK(loaded->GetPipelineState().bBlend);
            CHECK_FALSE(loaded->GetPipelineState().bDepthWrite);
            CHECK(loaded->CreateInstance()->GetAlphaMode() == EAlphaMode::Decal);
            CHECK(loaded->CreateInstance()->GetSortPriority() == -3);

            // The second trip gives the same text: nothing is lost on the way.
            std::string again;
            REQUIRE(FMaterialSerializer::SerializeText(again, *loaded));
            CHECK(again == text);
        }

        TEST_CASE("a material that says nothing about them is not a decal, has priority 0 and does not write it") {
            FMaterial plain("Plain");
            REQUIRE(FMaterialSerializer::DeserializeText("Material:\n  AlphaMode: \"Blend\"\n  Opacity: 0.5\n", plain));
            CHECK(plain.GetAlphaMode() == EAlphaMode::Blend);
            CHECK(plain.GetSortPriority() == 0);
            std::string text;
            REQUIRE(FMaterialSerializer::SerializeText(text, plain));
            CHECK(text.find("SortPriority") == std::string::npos);
            CHECK(text.find("AlphaMode: \"Blend\"") != std::string::npos);

            // The value is read whatever its case, like the other modes.
            FMaterial lower("Lower");
            REQUIRE(FMaterialSerializer::DeserializeText("Material:\n  AlphaMode: decal\n  SortPriority: 4\n", lower));
            CHECK(lower.GetAlphaMode() == EAlphaMode::Decal);
            CHECK(lower.GetSortPriority() == 4);
        }
    }

} // namespace Leon
