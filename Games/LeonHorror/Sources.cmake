# Single definition of the LeonHorror game sources, shared by the game executable, its tests and the
# editor (which links the game in-process for Play In Editor). Add new files here only.

set(LEONHORROR_ROOT "${CMAKE_CURRENT_LIST_DIR}")

set(LEONHORROR_GAME_SOURCES
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/FLeonHorrorGameModule.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorGameMode.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorCharacter.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorEnemy.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorFlashlight.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorHeldItem.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorInteractable.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorPlayerController.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorPower.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorHUD.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ULeonHorrorInventoryComponent.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/FLeonHorrorArchetypes.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/FLeonHorrorInputState.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/FLeonHorrorSaveGame.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ULeonHorrorAnimInstance.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ULeonHorrorCombatComponent.cpp"
)

# Explicit list: a GLOB would silently skip new test files until the next configure.
set(LEONHORROR_TEST_SOURCES
    "${LEONHORROR_ROOT}/Tests/Main.cpp"
    "${LEONHORROR_ROOT}/Tests/BootTests.cpp"
    "${LEONHORROR_ROOT}/Tests/ClinicMapTests.cpp"
    "${LEONHORROR_ROOT}/Tests/AnimationPipelineTests.cpp"
    "${LEONHORROR_ROOT}/Tests/CharacterTests.cpp"
    "${LEONHORROR_ROOT}/Tests/InteractionTests.cpp"
    "${LEONHORROR_ROOT}/Tests/ScreenTests.cpp"
    "${LEONHORROR_ROOT}/Tests/CombatTests.cpp"
    "${LEONHORROR_ROOT}/Tests/EnemyTests.cpp"
    "${LEONHORROR_ROOT}/Tests/SaveTests.cpp"
    "${LEONHORROR_ROOT}/Tests/HeldItemTests.cpp"
    "${LEONHORROR_ROOT}/Tests/HudTests.cpp"
    "${LEONHORROR_ROOT}/Tests/PowerTests.cpp"
    "${LEONHORROR_ROOT}/Tests/SliceMapTests.cpp"
    "${LEONHORROR_ROOT}/Tests/RenderTests.cpp"
)

function(leonhorror_add_game_library)
    if(TARGET LeonHorrorGame)
        return()
    endif()
    add_library(LeonHorrorGame STATIC ${LEONHORROR_GAME_SOURCES})
    target_include_directories(LeonHorrorGame PUBLIC "${LEONHORROR_ROOT}/Source/LeonHorror/Public")
    target_link_libraries(LeonHorrorGame PUBLIC
        LeonEngineCore
        Leon::Jolt
    )
    if(COMMAND leon_apply_compile_options)
        leon_apply_compile_options(LeonHorrorGame)
    endif()
endfunction()
