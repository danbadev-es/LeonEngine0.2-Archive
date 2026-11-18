cmake_minimum_required(VERSION 3.21)

project(LeonEngine2
    VERSION 0.15.0
    LANGUAGES C CXX
)

# One build tree for everything: engine, plugins, tools, tests, editor and games.
#   cmake --preset dev-debug
#   cmake --build --preset dev-debug --target LeonHorror
# Every dependency is fetched and compiled once per tree (see CMakePresets.json).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC: every target, third party included, uses the DLL C runtime. Must be set before any target.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        CACHE STRING "MSVC CRT: DLL runtime (/MDd or /MD)" FORCE)
endif()

set(LEON_ENGINE_ROOT "${CMAKE_SOURCE_DIR}" CACHE PATH "LeonEngine2 repository root")

# OFF while the engine has no GPU backend: the editor draws through ImGui, which needs one.
option(LEON_BUILD_EDITOR "Build the editor" ON)
option(LEON_BUILD_TOOLS "Build AssetTool, LightmassTool and ProjectTool" ON)
option(LEON_BUILD_TESTS "Build the test executables" ON)
set(LEON_GAMES "Sandbox;LeonTournament;LeonHorror" CACHE STRING "Game folders under Games/ or Samples/ to build")
set(LEON_EXTERNAL_GAME_DIRS "" CACHE STRING "Absolute paths of game projects that live outside this repository")

# Executables land in <tree>/bin. The editor gets its own folder because it stages a copy of the
# engine resources next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(LEON_BUILD_TESTS)
    enable_testing()
endif()

set(LEON_STACK_WITH_ENET ON)
set(LEON_STACK_WITH_TOOLS ${LEON_BUILD_TOOLS})
set(LEON_STACK_WITH_TESTS ${LEON_BUILD_TESTS})
include("${LEON_ENGINE_ROOT}/cmake/LeonEngineStack.cmake")
include("${LEON_ENGINE_ROOT}/cmake/LeonGames.cmake")

foreach(_game IN LISTS LEON_GAMES)
    if(EXISTS "${LEON_ENGINE_ROOT}/Games/${_game}/CMakeLists.txt")
        add_subdirectory("${LEON_ENGINE_ROOT}/Games/${_game}" "${CMAKE_BINARY_DIR}/_games/${_game}")
    elseif(EXISTS "${LEON_ENGINE_ROOT}/Samples/${_game}/CMakeLists.txt")
        add_subdirectory("${LEON_ENGINE_ROOT}/Samples/${_game}" "${CMAKE_BINARY_DIR}/_games/${_game}")
    else()
        message(WARNING "LEON_GAMES lists '${_game}' but neither Games/${_game} nor Samples/${_game} has a CMakeLists.txt")
    endif()
endforeach()

foreach(_dir IN LISTS LEON_EXTERNAL_GAME_DIRS)
    get_filename_component(_name "${_dir}" NAME)
    if(EXISTS "${_dir}/CMakeLists.txt")
        add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/_games/external_${_name}")
    else()
        message(WARNING "LEON_EXTERNAL_GAME_DIRS lists '${_dir}' but it has no CMakeLists.txt")
    endif()
endforeach()

# After the games: the editor links every game that registered itself.
if(LEON_BUILD_EDITOR)
    include("${LEON_ENGINE_ROOT}/cmake/EditorDependencies.cmake")
    add_subdirectory("${LEON_ENGINE_ROOT}/Engine/Source/Editor" "${CMAKE_BINARY_DIR}/_editor")
endif()
