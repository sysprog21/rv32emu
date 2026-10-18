/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

/* Private to syscall_sdl.c. Linux shared builds load the mixer on first use,
 * so command-line guests do not pay for its optional codec dependencies.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#define MIXER_FUNCTIONS(_) \
    _(QuickLoad_RAW)       \
    _(PlayChannelTimed)    \
    _(Volume)              \
    _(HaltMusic)           \
    _(FreeMusic)           \
    _(LoadMUSType_RW)      \
    _(VolumeMusic)         \
    _(PlayMusic)           \
    _(PlayingMusic)        \
    _(Init)                \
    _(OpenAudio)           \
    _(Quit)                \
    _(HaltChannel)         \
    _(FreeChunk)           \
    _(CloseAudio)

#define MIXER_DECLARE(name) static __typeof__(&Mix_##name) mixer_##name;
MIXER_FUNCTIONS(MIXER_DECLARE)
#undef MIXER_DECLARE

static bool mixer_loaded;

static void load_mixer_once(void)
{
    /* Keep the handle alive for audio callbacks and process teardown. Global
     * visibility matches the direct link, including codecs loaded by mixer.
     */
    void *handle = dlopen("libSDL2_mixer-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        rv_log_fatal("Cannot load SDL2_mixer: %s", dlerror());
        exit(EXIT_FAILURE);
    }
#define MIXER_LOAD(name)                                                   \
    mixer_##name = (__typeof__(mixer_##name)) dlsym(handle, "Mix_" #name); \
    if (!mixer_##name) {                                                   \
        rv_log_fatal("Cannot load Mix_%s: %s", #name, dlerror());          \
        exit(EXIT_FAILURE);                                                \
    }
    MIXER_FUNCTIONS(MIXER_LOAD)
#undef MIXER_LOAD
    __atomic_store_n(&mixer_loaded, true, __ATOMIC_RELEASE);
}
#undef MIXER_FUNCTIONS

static void load_mixer(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, load_mixer_once);
}

/* Every path that creates mixer state loads the library first. */
static inline bool mixer_is_loaded(void)
{
    return __atomic_load_n(&mixer_loaded, __ATOMIC_ACQUIRE);
}

#define Mix_QuickLoad_RAW mixer_QuickLoad_RAW
#define Mix_PlayChannelTimed mixer_PlayChannelTimed
#define Mix_Volume mixer_Volume
#define Mix_HaltMusic mixer_HaltMusic
#define Mix_FreeMusic mixer_FreeMusic
#define Mix_LoadMUSType_RW mixer_LoadMUSType_RW
#define Mix_VolumeMusic mixer_VolumeMusic
#define Mix_PlayMusic mixer_PlayMusic
#define Mix_PlayingMusic mixer_PlayingMusic
#define Mix_Init mixer_Init
#define Mix_OpenAudio mixer_OpenAudio
#define Mix_Quit mixer_Quit
#define Mix_HaltChannel mixer_HaltChannel
#define Mix_FreeChunk mixer_FreeChunk
#define Mix_CloseAudio mixer_CloseAudio

/* SDL_mixer 2.6 exports this as a function; older headers use the same macro.
 * Use the older entry point to keep the supported runtime ABI unchanged.
 */
#undef Mix_PlayChannel
#define Mix_PlayChannel(channel, chunk, loops) \
    Mix_PlayChannelTimed(channel, chunk, loops, -1)
