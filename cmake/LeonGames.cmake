# leon_register_game(
#     NAME <ProjectName>                 # as in the .lproject
#     PROJECT_FILE <path>                # relative to the repository root
#     [TARGET <static library>]          # the game's code; omit for a project the editor only lists
#     [REGISTER_FUNCTION <c++ name>]     # void(UClassRegistry&, FGameModuleHooks&)
#     [HEADER <include path>]            # header declaring REGISTER_FUNCTION
#     [DECLARATION <c++ declaration>])   # used instead of HEADER when there is no header; no trailing semicolon
#
# Games call this from their CMakeLists. The editor links every registered TARGET and generates a
# table from the entries, so neither the editor sources nor its CMake name a game.

include_guard(GLOBAL)

function(leon_register_game)
    cmake_parse_arguments(ARG "" "NAME;PROJECT_FILE;TARGET;REGISTER_FUNCTION;HEADER;DECLARATION" "" ${ARGN})
    if(NOT ARG_NAME OR NOT ARG_PROJECT_FILE)
        message(FATAL_ERROR "leon_register_game: NAME and PROJECT_FILE are required")
    endif()
    if(ARG_TARGET AND NOT ARG_REGISTER_FUNCTION)
        message(FATAL_ERROR "leon_register_game(${ARG_NAME}): TARGET needs REGISTER_FUNCTION")
    endif()
    get_property(_names GLOBAL PROPERTY LEON_GAME_NAMES)
    if(ARG_NAME IN_LIST _names)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY LEON_GAME_NAMES "${ARG_NAME}")
    foreach(_field PROJECT_FILE TARGET REGISTER_FUNCTION HEADER DECLARATION)
        set_property(GLOBAL PROPERTY "LEON_GAME_${ARG_NAME}_${_field}" "${ARG_${_field}}")
    endforeach()
endfunction()
