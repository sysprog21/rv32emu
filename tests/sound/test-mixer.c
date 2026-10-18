/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <SDL.h>
#include <SDL_mixer.h>
/* The checks below perform the calls under test; keep them in every build. */
#undef NDEBUG
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef TEST_DYNAMIC_MIXER
#define rv_log_fatal(...) fprintf(stderr, __VA_ARGS__)
#include "../../src/sdl_mixer_dynamic.h"
static void *load_thread(void *unused)
{
    (void) unused;
    load_mixer();
    return NULL;
}
#endif
int main(void)
{
#ifdef TEST_DYNAMIC_MIXER
    pthread_t threads[4];
    for (int i = 0; i < 4; i++)
        assert(!pthread_create(&threads[i], NULL, load_thread, NULL));
    for (int i = 0; i < 4; i++)
        assert(!pthread_join(threads[i], NULL));
#endif
    assert(SDL_Init(SDL_INIT_AUDIO) == 0);
    for (int round = 0; round < 2; round++) {
        int flags = Mix_Init(MIX_INIT_MID);
        printf("midi=%d\n", (flags & MIX_INIT_MID) != 0);
        assert(Mix_OpenAudio(11025, AUDIO_U8, 2, 2048) == 0);
        Uint8 samples[2048];
        SDL_memset(samples, 128, sizeof(samples));
        Mix_Chunk *chunk = Mix_QuickLoad_RAW(samples, sizeof(samples));
        assert(chunk);
        int channel = Mix_PlayChannel(-1, chunk, -1);
        assert(channel >= 0);
        Mix_Volume(channel, 64);
        assert(Mix_Volume(channel, -1) == 64);
        Mix_VolumeMusic(72);
        assert(Mix_VolumeMusic(-1) == 72);
        static const Uint8 midi[] = {
            'M', 'T', 'h', 'd',  0,   0,   0, 6,    0,    0,  0, 1,
            0,   96,  'M', 'T',  'r', 'k', 0, 0,    0,    12, 0, 0x90,
            60,  64,  96,  0x80, 60,  0,   0, 0xff, 0x2f, 0};
        SDL_RWops *rw = SDL_RWFromConstMem(midi, sizeof(midi));
        assert(rw);
        Mix_Music *music = Mix_LoadMUSType_RW(rw, MUS_MID, SDL_TRUE);
        printf("music=%d\n", music != NULL);
        if (music) {
            assert(Mix_PlayMusic(music, -1) == 0);
            assert(Mix_PlayingMusic());
            Mix_HaltMusic();
            Mix_FreeMusic(music);
        }
        Mix_HaltChannel(-1);
        Mix_FreeChunk(chunk);
        Mix_CloseAudio();
        Mix_Quit();
    }
    SDL_Quit();
    puts("mixer OK");
}
