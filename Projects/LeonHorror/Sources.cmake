# Single definition of the LeonHorror game sources, shared by the game executable, its tests and the
# editor (which links the game in-process for Play In Editor). Add new files here only.

set(LEONHORROR_ROOT "${CMAKE_CURRENT_LIST_DIR}")

set(LEONHORROR_GAME_SOURCES
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/FLeonHorrorGameModule.cpp"
    "${LEONHORROR_ROOT}/Source/LeonHorror/Private/ALeonHorrorGameMode.cpp"
)

# Explicit list: a GLOB would silently skip new test files until the next configure.
set(LEONHORROR_TEST_SOURCES
    "${LEONHORROR_ROOT}/Tests/Main.cpp"
    "${LEONHORROR_ROOT}/Tests/BootTests.cpp"
    "${LEONHORROR_ROOT}/Tests/AnimationPipelineTests.cpp"
)

function(leonhorror_add_game_library)
    if(TARGET LeonHorrorGame)
        return()
    endif()
    add_library(LeonHorrorGame STATIC ${LEONHORROR_GAME_SOURCES})
    target_include_directories(LeonHorrorGame PUBLIC "${LEONHORROR_ROOT}/Source/LeonHorror/Public")
    target_link_libraries(LeonHorrorGame PUBLIC
        LeonEngineCore
        Leon::Pipeline
        Leon::OpenGL
        Leon::Jolt
    )
    if(COMMAND leon_apply_compile_options)
        leon_apply_compile_options(LeonHorrorGame)
    endif()
endfunction()
