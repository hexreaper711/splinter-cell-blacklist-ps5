# rexaudio - Audio subsystem library
# Audio processing and XMA decoding for Xbox 360 emulation

add_library(rexaudio OBJECT
    
    audio_driver.cpp
    audio_system.cpp
    downmix.cpp
    xma_context.cpp
    xma_decoder.cpp
    xma_register_file.cpp
    # NOP backend (always available)
    nop/nop_audio_system.cpp
    # SDL backend
    sdl/sdl_audio_system.cpp
    sdl/sdl_audio_driver.cpp
    # [ps5 port] sceAudioOut
    $<$<BOOL:${PS5}>:ps5/ps5_audio_driver.cpp>
)
add_library(rex::audio ALIAS rexaudio)

if(PS5)
    # [ps5 port] With the PS5 target, clang's implicit __va_list_tag from the PCH and the one
    # FFmpeg's <stdarg.h> uses in this file don't unify: no PCH here.
    set_source_files_properties(xma_decoder.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

rex_target_pch(rexaudio
    <mutex>
    <rex/math.h>
    <rex/string.h>
    <rex/memory.h>
    <rex/filesystem.h>
    <rex/thread.h>
    <rex/thread/mutex.h>
    <rex/ppc/context.h>
    <rex/system/kernel_state.h>
    <rex/system/xthread.h>
)

target_include_directories(rexaudio PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(rexaudio
    PUBLIC rexcore SDL3::SDL3
    PRIVATE libavcodec libavutil
)
