# Splinter Cell: Blacklist for PS5: ReXGlue (thirdparty/rexglue-sdk, branch ps5-port, mounted
# at /work/rexglue-sdk by dev.sh), its Xenos GPU plugin on Vulkan and the recompiled game
# (generated/default) compiled as one static PS5 program. ps5/tools/ps5_package.sh then links it
# with RADV and packages the app; ps5/build.sh does both.
#
# The game's code is generated on the host beforehand (`rexglue codegen blacklist_manifest.toml`):
# a cross build can't run the PS5 build of the rexglue tool, so generated/rexglue.cmake isn't used.
cmake_minimum_required(VERSION 3.25)
project(blacklist_ps5 LANGUAGES C CXX)

if(NOT PS5)
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=ps5/cmake/ps5-toolchain.cmake")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)
add_compile_definitions(FMT_USE_CONSTEVAL=0)

set(REXGLUE_USE_VULKAN ON CACHE BOOL "" FORCE)
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

get_filename_component(SCB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(REXSDK_DIR "/work/rexglue-sdk" CACHE PATH "ReXGlue source tree (branch ps5-port)")
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)

set(SCB_GENERATED "${SCB_ROOT}/generated/default")
if(NOT EXISTS "${SCB_GENERATED}/sources.cmake")
    message(FATAL_ERROR "No recompiled code in ${SCB_GENERATED}: run "
                        "`rexglue codegen blacklist_manifest.toml` in ${SCB_ROOT} first")
endif()
include("${SCB_GENERATED}/sources.cmake")

# The recompiled game: no debug info (it would only grow the link).
add_library(blacklist_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(blacklist_recomp PRIVATE ${SCB_ROOT} ${SCB_ROOT}/src ${SCB_GENERATED})
target_link_libraries(blacklist_recomp PRIVATE rex::runtime)
target_precompile_headers(blacklist_recomp PRIVATE "${SCB_GENERATED}/blacklist_pch.h")
target_compile_options(blacklist_recomp PRIVATE -g0)
rexglue_apply_target_settings(blacklist_recomp)
# The game's own files: mfmsr/mtmsrd without a hardware fence (the generated pch's
# REX_PS5_GUEST_CODE block), as gta4-ps5 builds its guest code.
set(SCB_GUEST_SOURCES ${GENERATED_SOURCES})
list(FILTER SCB_GUEST_SOURCES INCLUDE REGEX "blacklist_recomp\\.[0-9]+\\.cpp$")
set_property(SOURCE ${SCB_GUEST_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS REX_PS5_GUEST_CODE=1)

add_executable(blacklist
    ${SCB_ROOT}/src/main.cpp
    ${SCB_ROOT}/src/blacklist_app.cpp
    ${SCB_ROOT}/src/disc_launcher.cpp
    ${SCB_ROOT}/src/hd_texture_pack.cpp
    ${SCB_ROOT}/src/launcher_settings.cpp
)
target_include_directories(blacklist PRIVATE ${SCB_ROOT} ${SCB_ROOT}/src ${SCB_GENERATED})
# rex_app.cpp (added by rexglue_configure_target) and the launcher include imgui, which
# rexruntime links privately: its headers only, as its code is already in rexruntime.
target_include_directories(blacklist PRIVATE
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(blacklist PRIVATE
    blacklist_recomp
    rex::runtime
    rexgpu-xenos
    SceUserService ScePad SceAudioOut SceSystemService SceVideoOut
)
rexglue_configure_target(blacklist)
