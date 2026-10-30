# [ps5 port] CMake toolchain for a ReXGlue game as a native PS5 homebrew app. From
# gta4-ps5's ps5/cmake/ps5-toolchain.cmake (elripalda, GPL-3.0); ps5/build.sh runs it.
#
#   cmake -S ps5 -B out/ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=ps5/cmake/ps5-toolchain.cmake \
#     -DPS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
#
# Uses the PS5 payload SDK (mihawk-99's fork, as ps5-plume-triangle builds it) for clang,
# headers, libc++ 18 and the kernel stubs.

if(NOT PS5_PAYLOAD_SDK)
  set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}")
endif()
if(NOT PS5_PAYLOAD_SDK OR NOT EXISTS "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
  message(FATAL_ERROR "Set PS5_PAYLOAD_SDK to the PS5 payload SDK root")
endif()
# Pass it through to try_compile projects.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PS5_PAYLOAD_SDK)

include("${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
set(CMAKE_VERBOSE_MAKEFILE OFF CACHE BOOL "" FORCE)

# Zen 2 (the PS5 CPU); emulated TLS (the PS5 ABI); libc++ 18 needs -fexperimental-library for
# std::jthread / stop_token; Linux names mapped to FreeBSD ones (rex/ps5_compat.h, force-included
# by ReXGlue's CMakeLists.txt).
set(PS5_COMMON_FLAGS
  "-march=znver2 -femulated-tls -fno-stack-protector -fno-plt -fdenormal-fp-math=ieee -fvisibility-nodllstorageclass=default")
set(CMAKE_C_FLAGS_INIT "${PS5_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${PS5_COMMON_FLAGS} -fexperimental-library -frtti -fexceptions")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fexperimental-library")

# No desktop windowing / audio / input in SDL on the PS5: the app gets the display, the
# DualSense and audio from the PS5 platform layer instead.
set(REX_PS5 ON CACHE BOOL "" FORCE)
