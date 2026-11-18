# Single definition of the LeonTournament game sources, shared by the game executable, its tests
# and the editor (which links the game in-process for Play In Editor). Add new files here only.

set(LEON_TOURNAMENT_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(LEON_TOURNAMENT_PUBLIC "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Public")

set(LEON_TOURNAMENT_GAME_SOURCES
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentGameMode.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/FLeonTournamentArenaBuilder.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/FLeonTournamentDamageRules.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentGameState.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Characters/ALeonTournamentCharacter.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentPlayerController.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentPlayerState.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentBotController.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/FLeonTournamentKillFeed.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Combat/ALeonTournamentWeapon.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Combat/FLeonTournamentWeaponVfx.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Combat/ALeonTournamentProjectile.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Combat/ALeonTournamentPickup.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentFlag.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentFlagBase.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentCaptureTheFlagGameMode.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ALeonTournamentHUD.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Characters/ALeonTournamentDummy.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentAnimLabGameMode.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentRenderLabGameMode.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ALeonTournamentTransitionGameMode.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Characters/ULeonTournamentAnimInstance.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Combat/ULeonTournamentCombatComponent.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/ULeonTournamentGameInstance.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/Game/FLeonTournamentGameModule.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/FLeonTournamentUILayout.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/FLeonTournamentCrosshairTextures.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ULeonTournamentMainMenuWidget.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ULeonTournamentLobbyWidget.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ULeonTournamentHUDWidget.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ULeonTournamentScoreboardWidget.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ULeonTournamentPauseWidget.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ULeonTournamentMatchEndWidget.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ULeonTournamentLoadingOverlayWidget.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ULeonTournamentRenderLabWidget.cpp"
    "${LEON_TOURNAMENT_ROOT}/Source/LeonTournament/Private/UI/ALeonTournamentTransitionHUD.cpp"
)

function(leontournament_add_game_library)
    if(TARGET LeonTournamentGame)
        return()
    endif()
    add_library(LeonTournamentGame STATIC ${LEON_TOURNAMENT_GAME_SOURCES})
    target_include_directories(LeonTournamentGame PUBLIC
        "${LEON_TOURNAMENT_PUBLIC}"
        "${LEON_TOURNAMENT_PUBLIC}/Types"
        "${LEON_TOURNAMENT_PUBLIC}/UI"
        "${LEON_TOURNAMENT_PUBLIC}/Game"
        "${LEON_TOURNAMENT_PUBLIC}/Characters"
        "${LEON_TOURNAMENT_PUBLIC}/Combat"
    )
    target_link_libraries(LeonTournamentGame PUBLIC
        LeonEngineCore
        Leon::Pipeline
        Leon::OpenGL
        Leon::Jolt
        Leon::ENet
    )
    if(COMMAND leon_apply_compile_options)
        leon_apply_compile_options(LeonTournamentGame)
    endif()
endfunction()
