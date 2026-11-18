include("${CMAKE_CURRENT_SOURCE_DIR}/Sources.cmake")
leontournament_add_game_library()

add_executable(LeonTournament Source/LeonTournament/Main.cpp)
target_link_libraries(LeonTournament PRIVATE LeonTournamentGame Leon::Launch)

leon_register_game(
    NAME LeonTournament
    PROJECT_FILE "Games/LeonTournament/LeonTournament.lproject"
    TARGET LeonTournamentGame
    REGISTER_FUNCTION "LE_RegisterGameModule"
    DECLARATION "extern \"C\" void LE_RegisterGameModule(Leon::UClassRegistry& InRegistry, Leon::FGameModuleHooks& OutHooks)"
)

if(LEON_BUILD_TESTS)
    file(GLOB LEON_TOURNAMENT_TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp")
    add_executable(LeonTournamentTests ${LEON_TOURNAMENT_TEST_SRCS})
    target_include_directories(LeonTournamentTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty/doctest"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty"
    )
    target_link_libraries(LeonTournamentTests PRIVATE
        LeonTournamentGame
        doctest
        glad
        glfw
        glm
        stb_image
    )
    add_test(NAME LeonTournamentTests COMMAND LeonTournamentTests WORKING_DIRECTORY "${LEON_ENGINE_ROOT}")
endif()

leon_apply_compile_options(LeonTournament)
if(TARGET LeonTournamentTests)
    leon_apply_compile_options(LeonTournamentTests)
endif()
