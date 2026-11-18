include("${CMAKE_CURRENT_SOURCE_DIR}/Sources.cmake")
leonhorror_add_game_library()

add_executable(LeonHorror Source/LeonHorror/Main.cpp)
target_link_libraries(LeonHorror PRIVATE LeonHorrorGame Leon::Launch)

leon_register_game(
    NAME LeonHorror
    PROJECT_FILE "Games/LeonHorror/LeonHorror.lproject"
    TARGET LeonHorrorGame
    REGISTER_FUNCTION "Leon::RegisterLeonHorrorModule"
    HEADER "LeonHorror/LeonHorrorModule.hpp"
)

if(LEON_BUILD_TESTS)
    add_executable(LeonHorrorTests ${LEONHORROR_TEST_SOURCES})
    target_include_directories(LeonHorrorTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty/doctest"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty"
    )
    target_link_libraries(LeonHorrorTests PRIVATE
        LeonHorrorGame
        # Importers and the light baker: tests may use them, the game does not.
        Leon::Pipeline
        Leon::Module::RHID3D12
        doctest
        glfw
        glm
        stb_image
    )
    # The suite that draws the start room and compares it with reference pictures is the golden one.
    add_test(NAME LeonHorrorTests.unit
        COMMAND LeonHorrorTests "--test-suite-exclude=LeonHorror rendering"
        WORKING_DIRECTORY "${LEON_ENGINE_ROOT}")
    add_test(NAME LeonHorrorTests.golden
        COMMAND LeonHorrorTests "--test-suite=LeonHorror rendering"
        WORKING_DIRECTORY "${LEON_ENGINE_ROOT}")
    set_tests_properties(LeonHorrorTests.unit PROPERTIES LABELS "unit")
    set_tests_properties(LeonHorrorTests.golden PROPERTIES LABELS "golden")
endif()

leon_apply_compile_options(LeonHorror)
if(TARGET LeonHorrorTests)
    leon_apply_compile_options(LeonHorrorTests)
endif()
