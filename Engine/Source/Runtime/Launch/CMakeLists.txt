# Shared entry point for game executables. Links the backends the engine ships with, so a game's
# main() and CMake never name a render or physics backend.
add_library(LeonLaunch STATIC Private/FGameLauncher.cpp)
add_library(Leon::Launch ALIAS LeonLaunch)
target_include_directories(LeonLaunch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(LeonLaunch
    PUBLIC LeonEngineCore
    PRIVATE Leon::OpenGL Leon::Jolt
)
if(COMMAND leon_apply_compile_options)
    leon_apply_compile_options(LeonLaunch)
endif()
