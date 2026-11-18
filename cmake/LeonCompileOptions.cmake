# Shared MSVC / GCC compile options for Leon targets.
# Usage: include(...) then leon_apply_compile_options(tgt1 tgt2 ...)

# MSVC CRT policy: use the dynamic (DLL) CRT to avoid LNK2001 on operator delete / _Lockit.
# MultiThreadedDebugDLL = /MDd  (Debug)
# MultiThreadedDLL      = /MD   (Release, RelWithDebInfo, MinSizeRel)
# This MUST be set BEFORE any target is created; set it globally here so all
# add_subdirectory calls that follow inherit the same runtime linkage.
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        CACHE STRING "MSVC CRT linkage" FORCE
    )
endif()

option(LEON_WARNINGS_AS_ERRORS "Treat warnings as errors on Leon targets (CI)" OFF)
option(LEON_ENABLE_ASAN "Build everything with the address sanitizer" OFF)

if(LEON_ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

function(leon_apply_compile_options)
    foreach(tgt IN LISTS ARGN)
        if(TARGET ${tgt})
            if(MSVC)
                target_compile_options(${tgt} PRIVATE
                    /W4
                    # Headers included with <...> are third party: their warnings are not ours to fix.
                    /external:anglebrackets
                    /external:W0
                    /permissive-
                    /Zc:__cplusplus
                    /MP
                    $<$<CONFIG:Debug,RelWithDebInfo>:/FS>
                )
                # Explicitly set per-target CRT to match the global policy.
                # /MDd links msvcrtd.lib (Debug DLL CRT); /MD links msvcrt.lib.
                set_target_properties(${tgt} PROPERTIES
                    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
                )
                target_compile_definitions(${tgt} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
                if(LEON_WARNINGS_AS_ERRORS)
                    target_compile_options(${tgt} PRIVATE /WX)
                endif()
            else()
                target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
                if(LEON_WARNINGS_AS_ERRORS)
                    target_compile_options(${tgt} PRIVATE -Werror)
                endif()
            endif()
        endif()
    endforeach()
endfunction()

# /MP on third-party STATIC libs (warnings stay local to each dep).
function(leon_enable_msvc_mp)
    foreach(tgt IN LISTS ARGN)
        if(TARGET ${tgt} AND MSVC)
            target_compile_options(${tgt} PRIVATE /MP $<$<CONFIG:Debug,RelWithDebInfo>:/FS>)
            set_target_properties(${tgt} PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
            )
        endif()
    endforeach()
endfunction()
