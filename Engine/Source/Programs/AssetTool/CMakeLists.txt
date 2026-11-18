file(GLOB_RECURSE ASSET_TOOL_SOURCES
    "Source/*.cpp"
)

add_executable(AssetTool
    ${ASSET_TOOL_SOURCES}
)

target_include_directories(AssetTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    "${LEON_ENGINE_ROOT}/Engine/Source/Programs/Common/Include"
    "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb"
    "${LEON_ENGINE_ROOT}/Engine/ThirdParty/ufbx"
)

target_link_libraries(AssetTool PRIVATE
    ToolsCommon
    LeonEngineCore
    LeonEnginePipeline
    glm::glm
)
