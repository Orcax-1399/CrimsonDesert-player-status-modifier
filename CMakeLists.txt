cmake_minimum_required(VERSION 3.20)

project(player-status-modifier LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/safetyhook)

add_library(player-status-modifier SHARED
    src/config.cpp
    src/config_watcher.cpp
    src/dllmain.cpp
    src/hooks.cpp
    src/hooks/durability_hooks.cpp
    src/hooks/economy_hooks.cpp
    src/hooks/install_hooks.cpp
    src/hooks/player_hooks.cpp
    src/key_listener.cpp
    src/logger.cpp
    src/mod_logic.cpp
    src/ptrchain.cpp
    src/ptrchain_resources.cpp
    src/position_control.cpp
        src/runtime/actor_resolve.cpp
        src/runtime/damage_logic.cpp
        src/runtime/durability_logic.cpp
        src/runtime/mount_resolver.cpp
        src/runtime/runtime_state.cpp
        src/runtime/stat_logic.cpp
        src/scanner.cpp
)

target_include_directories(player-status-modifier PRIVATE src)
target_link_libraries(player-status-modifier PRIVATE safetyhook)

target_compile_definitions(player-status-modifier PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(player-status-modifier PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
    )
endif()

set_target_properties(player-status-modifier PROPERTIES
    OUTPUT_NAME "player-status-modifier"
    SUFFIX ".asi"
)
