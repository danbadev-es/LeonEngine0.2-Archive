# leon_add_module(<Name>
#     SOURCES <files...>
#     [DEPENDS <other modules...>]        # public: their headers are visible to users of this module
#     [PRIVATE_DEPENDS <modules...>]
#     [LIBRARIES <third-party targets...>]
#     [PRIVATE_LIBRARIES <targets...>])
#
# A module is a static library named LeonModule_<Name> (alias Leon::Module::<Name>) whose public
# headers live in Engine/Source/Runtime/<Name>/Public. It can only include headers of the modules
# it declares in DEPENDS: an undeclared, upward include does not compile.

include_guard(GLOBAL)

function(leon_add_module Name)
    cmake_parse_arguments(ARG "" "ROOT" "SOURCES;DEPENDS;PRIVATE_DEPENDS;LIBRARIES;PRIVATE_LIBRARIES" ${ARGN})
    if(NOT ARG_ROOT)
        set(ARG_ROOT "${LEON_RUNTIME_ROOT}/${Name}")
    endif()
    set(_target "LeonModule_${Name}")
    add_library(${_target} STATIC ${ARG_SOURCES})
    add_library(Leon::Module::${Name} ALIAS ${_target})
    target_include_directories(${_target}
        PUBLIC "${ARG_ROOT}/Public"
        PRIVATE "${ARG_ROOT}/Private"
    )
    target_compile_features(${_target} PUBLIC cxx_std_20)
    foreach(_dep IN LISTS ARG_DEPENDS)
        target_link_libraries(${_target} PUBLIC LeonModule_${_dep})
    endforeach()
    foreach(_dep IN LISTS ARG_PRIVATE_DEPENDS)
        target_link_libraries(${_target} PRIVATE LeonModule_${_dep})
    endforeach()
    if(ARG_LIBRARIES)
        target_link_libraries(${_target} PUBLIC ${ARG_LIBRARIES})
    endif()
    if(ARG_PRIVATE_LIBRARIES)
        target_link_libraries(${_target} PRIVATE ${ARG_PRIVATE_LIBRARIES})
    endif()
    if(COMMAND leon_apply_compile_options)
        leon_apply_compile_options(${_target})
    endif()
endfunction()
