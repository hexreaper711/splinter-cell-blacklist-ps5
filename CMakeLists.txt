# blacklist - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(blacklist LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(BLACKLIST_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(blacklist WIN32 ${BLACKLIST_SOURCES})
else()
    add_executable(blacklist ${BLACKLIST_SOURCES})
endif()

rexglue_setup_target(blacklist)
