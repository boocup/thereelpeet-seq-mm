cmake_minimum_required(VERSION 3.19)

project(TheReelPeetSeqMM LANGUAGES C CXX ASM)

set(METAMODULE_PLUGIN_STATIC_LIBC ON CACHE BOOL
    "Use static/prebuilt libc for MetaModule plugins" FORCE)

set(METAMODULE_SDK_BUILD_PLUGIN_LIBC OFF CACHE BOOL
    "Build plugin-libc from source" FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/../MyModule-metamodule/sdk/plugin.cmake)

add_library(thereelpeet-seq STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/TheReelPeet.cpp
)

target_include_directories(thereelpeet-seq PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}
)

create_plugin(
    SOURCE_LIB      thereelpeet-seq
    SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/res
    DESTINATION     ${CMAKE_CURRENT_BINARY_DIR}/out
    PLUGIN_NAME     thereelpeet-seq-mm
    PLUGIN_JSON     ${CMAKE_CURRENT_LIST_DIR}/plugin.json
)
