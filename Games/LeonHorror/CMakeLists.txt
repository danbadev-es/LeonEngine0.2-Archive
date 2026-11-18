include("${CMAKE_CURRENT_SOURCE_DIR}/Sources.cmake")
leonhorror_add_game_library()

add_executable(LeonHorror Source/LeonHorror/Main.cpp)
target_include_directories(LeonHorror PRIVATE
    "${LEON_ENGINE_ROOT}/Engine/Plugins/Physics/Jolt/include"
)
target_link_libraries(LeonHorror PRIVATE LeonHorrorGame)

# The Project product builds without the engine test stack, so doctest is added here.
if(NOT TARGET doctest)
    add_subdirectory("${LEON_ENGINE_ROOT}/Engine/ThirdParty/doctest" "${CMAKE_BINARY_DIR}/_leon_tp_doctest")
endif()

add_executable(LeonHorrorTests ${LEONHORROR_TEST_SOURCES})
target_include_directories(LeonHorrorTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests"
    "${LEON_ENGINE_ROOT}/Engine/Plugins/RHI/OpenGL/include"
    "${LEON_ENGINE_ROOT}/Engine/Plugins/Physics/Jolt/include"
    "${LEON_ENGINE_ROOT}/Engine/ThirdParty/glad/include"
    "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb"
    "${LEON_ENGINE_ROOT}/Engine/ThirdParty/doctest"
    "${LEON_ENGINE_ROOT}/Engine/ThirdParty"
)
target_link_libraries(LeonHorrorTests PRIVATE
    LeonHorrorGame
    doctest
    glad
    glfw
    glm
    stb_image
)
add_test(NAME LeonHorrorTests COMMAND LeonHorrorTests WORKING_DIRECTORY "${LEON_ENGINE_ROOT}")
