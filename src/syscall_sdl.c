/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#if !RV32_HAS(SDL)
#error "Do not manage to build this file unless you enable SDL support."
#endif

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <SDL.h>
#if RV32_HAS(SDL_MIXER)
#include <SDL_mixer.h>
#endif

#include "riscv.h"
#include "riscv_private.h"

/* The DSITMBK sound effect in DOOM1.WAD uses a sample rate of 22050, but since
 * the game is played in single-player mode, it is acceptable to stick with
 * 11025.
 *
 * In Quake, most sound effects have a sample rate of 11025.
 */
#define SAMPLE_RATE 11025

/* Most audio device supports stereo */
#define CHANNEL_USED 2

#define CHUNK_SIZE 2048

#define MUSIC_MAX_SIZE 65536

/* Max size of sound is around 18000 bytes */
#define SFX_SAMPLE_SIZE 32768

#define R 1
#define W 0

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
#define get_offset_by_addr(addr) (addr) & (MASK(RV_PG_SHIFT))
static uint32_t offset;
static uint32_t curr_page_data_size;
static uint32_t remain_size;
static uint32_t curr_offset;
static uint32_t curr_data_size;

#define GET_DATA_FROM_RANDOM_PAGE(source_vaddr, dest)                    \
    do {                                                                 \
        while (remain_size > 0) {                                        \
            uint32_t screen_addr =                                       \
                rv->io.mem_translate(rv, source_vaddr + curr_offset, R); \
            offset = get_offset_by_addr(screen_addr);                    \
            curr_page_data_size = RV_PG_SIZE - offset;                   \
            curr_data_size = remain_size <= curr_page_data_size          \
                                 ? remain_size                           \
                                 : curr_page_data_size;                  \
            memory_read(attr->mem, dest + curr_offset, screen_addr,      \
                        sizeof(uint8_t) * curr_data_size);               \
            remain_size -= curr_data_size;                               \
            curr_offset += curr_data_size;                               \
        }                                                                \
    } while (0)

#define GET_VIDEO_DATA_FROM_RANDOM_PAGE(source_vaddr, dest) \
    GET_DATA_FROM_RANDOM_PAGE(source_vaddr, dest)
#define GET_SFX_DATA_FROM_RANDOM_PAGE(source_vaddr, dest) \
    GET_DATA_FROM_RANDOM_PAGE(source_vaddr, dest)
#define GET_MUSIC_DATA_FROM_RANDOM_PAGE(source_vaddr, dest) \
    GET_DATA_FROM_RANDOM_PAGE(source_vaddr, dest)
#endif

/* sound-related request type */
enum {
    INIT_AUDIO,
    SHUTDOWN_AUDIO,
    PLAY_MUSIC,
    PLAY_SFX,
    SET_MUSIC_VOLUME,
    STOP_MUSIC,
};

typedef struct sound {
    uint8_t *data;
    size_t size;
    int looping;
    int volume;
} sound_t;

/* SDL-mixer-related and music-related variables */
static pthread_t music_thread;
static uint8_t *music_midi_data;
#if RV32_HAS(SDL_MIXER)
static Mix_Music *mid;

/* SDL-mixer-related and sfx-related variables */
static pthread_t sfx_thread;
static Mix_Chunk *sfx_chunk;
#endif
static uint8_t *sfx_samples;
static uint32_t nr_sfx_samples;
static int chan;

/* Used to properly destroy audio, compatible to process VM emulation */
static bool audio_init = false;
static bool sfx_thread_init = false;
static bool music_thread_init = false;

typedef struct {
    void *data;
    int size;
} musicinfo_t;

typedef struct {
    void *data;
    int size;
} sfxinfo_t;

enum {
    KEY_EVENT = 0,
    MOUSE_MOTION_EVENT = 1,
    MOUSE_BUTTON_EVENT = 2,
    QUIT_EVENT = 3,
};

typedef struct {
    uint32_t keycode;
    uint8_t state;
} key_event_t;

typedef struct {
    int32_t x, y, xrel, yrel;
} mouse_motion_t;

typedef struct {
    uint8_t button;
    uint8_t state;
} mouse_button_t;

typedef struct {
    uint32_t type;
    union {
        key_event_t key_event;
        union {
            mouse_motion_t motion;
            mouse_button_t button;
        } mouse;
    };
} event_t;

typedef struct {
    uint32_t base;
    size_t end;
} event_queue_t;

enum {
    RELATIVE_MODE_SUBMISSION = 0,
    WINDOW_TITLE_SUBMISSION = 1,
};

typedef struct {
    uint8_t enabled;
} mouse_submission_t;

typedef struct {
    uint32_t title;
    uint32_t size;
} title_submission_t;

typedef struct {
    uint32_t type;
    union {
        mouse_submission_t mouse;
        title_submission_t title;
    };
} submission_t;

typedef struct {
    uint32_t base;
    size_t start;
} submission_queue_t;

/* SDL-related variables */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

/* Event queue specific variables */
static uint32_t queues_capacity;
static uint32_t event_count;
static uint32_t deferred_submissions = 0;
static event_queue_t event_queue = {
    .base = 0,
    .end = 0,
};
static submission_queue_t submission_queue = {
    .base = 0,
    .start = 0,
};

static submission_t submission_pop(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    submission_t submission;
    memory_read(
        attr->mem, (void *) &submission,
        submission_queue.base + submission_queue.start * sizeof(submission_t),
        sizeof(submission_t));
    ++submission_queue.start;
    submission_queue.start &= queues_capacity - 1;
    return submission;
}

static void event_push(riscv_t *rv, event_t event)
{
    vm_attr_t *attr = PRIV(rv);
    if (!memory_write(attr->mem,
                      event_queue.base + event_queue.end * sizeof(event_t),
                      (void *) &event, sizeof(event_t)))
        return;
    ++event_queue.end;
    event_queue.end &= queues_capacity - 1;

    uint32_t count;
    count = rv->io.mem_read_w(rv, event_count);
    count += 1;
    rv->io.mem_write_w(rv, event_count, count);
}

static inline uint32_t round_pow2(uint32_t x)
{
    if (x <= 1)
        return 1;
#if defined(__GNUC__) || defined(__clang__)
    x = 1 << (32 - __builtin_clz(x - 1));
#else
    /* Bit Twiddling Hack */
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;
#endif
    return x;
}

void syscall_submit_queue(riscv_t *rv);

/* check if SDL needs to be set up and run the event loop */
static bool check_sdl(riscv_t *rv, int width, int height)
{
    if (!window) { /* check if video has been initialized. */
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
            rv_log_fatal("Failed to call SDL_Init()");
            exit(EXIT_FAILURE);
        }
        window = SDL_CreateWindow("rv32emu", SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, width, height,
                                  SDL_WINDOW_RESIZABLE);
        if (!window) {
            rv_log_fatal("Window could not be created! SDL_Error: %s",
                         SDL_GetError());
            exit(EXIT_FAILURE);
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);

        if (deferred_submissions) {
            syscall_submit_queue(rv);
            deferred_submissions = 0;
        }
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) { /* Run event handler */
        switch (event.type) {
        case SDL_QUIT: {
            event_t new_event = {
                .type = QUIT_EVENT,
            };
            event_push(rv, new_event);
            return false;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            if (event.key.repeat)
                break;
            event_t new_event = {
                .type = KEY_EVENT,
            };
            key_event_t key_event = {
                .keycode = event.key.keysym.sym,
                .state = (bool) (event.key.state == SDL_PRESSED),
            };
            memcpy(&new_event.key_event, &key_event, sizeof(key_event));
            event_push(rv, new_event);
            break;
        }
        case SDL_MOUSEMOTION: {
            event_t new_event = {
                .type = MOUSE_MOTION_EVENT,
            };
            mouse_motion_t mouse_motion = {
                .x = event.motion.x,
                .y = event.motion.y,
                .xrel = event.motion.xrel,
                .yrel = event.motion.yrel,
            };
            memcpy(&new_event.mouse.motion, &mouse_motion,
                   sizeof(mouse_motion));
            event_push(rv, new_event);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            event_t new_event = {
                .type = MOUSE_BUTTON_EVENT,
            };
            mouse_button_t mouse_button = {
                .button = event.button.button,
                .state = (bool) (event.button.state == SDL_PRESSED),
            };
            memcpy(&new_event.mouse.button, &mouse_button,
                   sizeof(mouse_button));
            event_push(rv, new_event);
            break;
        }
        default:
            break;
        }
    }
    return true;
}

void syscall_draw_frame(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    /* draw_frame(base, width, height) */
    const uint32_t screen = rv_get_reg(rv, rv_reg_a0);
    const int width = rv_get_reg(rv, rv_reg_a1);
    const int height = rv_get_reg(rv, rv_reg_a2);

    if (!check_sdl(rv, width, height))
        return;

    uint32_t total_size = width * height * 4;
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    static uint8_t tmp_buf[256 * RV_PG_SIZE];
    uint8_t *tmp_buf_ptr = &tmp_buf[0];
    uint32_t screen_vaddr = screen;
    remain_size = total_size;
    curr_offset = 0;
    memset(tmp_buf_ptr, 0, sizeof(tmp_buf));

    GET_VIDEO_DATA_FROM_RANDOM_PAGE(screen_vaddr, tmp_buf_ptr);
#endif
    int pitch = 0;
    void *pixels_ptr;
    if (SDL_LockTexture(texture, NULL, &pixels_ptr, &pitch))
        exit(EXIT_FAILURE);
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    memcpy(pixels_ptr, tmp_buf_ptr, total_size);
#else
    memory_read(attr->mem, pixels_ptr, screen, total_size);
#endif
    SDL_UnlockTexture(texture);

    int actual_width, actual_height;
    SDL_GetWindowSize(window, &actual_width, &actual_height);
    SDL_RenderCopy(renderer, texture, NULL,
                   &(SDL_Rect){0, 0, actual_width, actual_height});
    SDL_RenderPresent(renderer);
}

void syscall_setup_queue(riscv_t *rv)
{
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    /*
     * The guestOS might exit and execute the SDL-based program again
     * thus clearing the queue is required to avoid using the
     * access the old events.
     */
    event_queue.base = event_queue.end = 0;
    submission_queue.base = submission_queue.start = 0;
#endif

    /* setup_queue(base, capacity, event_count) */
    uint32_t base = rv_get_reg(rv, rv_reg_a0);
    queues_capacity = rv_get_reg(rv, rv_reg_a1);
    event_count = rv_get_reg(rv, rv_reg_a2);

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    uint32_t submission_queue_addr =
        rv->io.mem_translate(rv, base + sizeof(event_t) * queues_capacity, R);

    uint32_t event_queue_addr = rv->io.mem_translate(rv, base, R);

    /* now the bases are the gPA and host can access directly */
    event_queue.base = event_queue_addr;
    submission_queue.base = submission_queue_addr;
#else
    event_queue.base = base;
    submission_queue.base = base + sizeof(event_t) * queues_capacity;
#endif
    queues_capacity = round_pow2(queues_capacity);
}

void syscall_submit_queue(riscv_t *rv)
{
    /* submit_queue(count) */
    uint32_t count = rv_get_reg(rv, rv_reg_a0);

    if (!window) {
        deferred_submissions += count;
        return;
    }

    if (deferred_submissions)
        count = deferred_submissions;

    while (count--) {
        submission_t submission = submission_pop(rv);

        char *title;
        switch (submission.type) {
        case RELATIVE_MODE_SUBMISSION:
            SDL_SetRelativeMouseMode(submission.mouse.enabled);
            break;
        case WINDOW_TITLE_SUBMISSION:
            title = malloc(submission.title.size + 1);
            if (unlikely(!title))
                return;

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
            uint32_t addr = rv->io.mem_translate(rv, submission.title.title, R);
            memory_read(PRIV(rv)->mem, (uint8_t *) title, addr,
                        submission.title.size);
#else
            memory_read(PRIV(rv)->mem, (uint8_t *) title,
                        submission.title.title, submission.title.size);
#endif
            title[submission.title.size] = 0;

            SDL_SetWindowTitle(window, title);
            free(title);
            break;
        }
    }
}

/* Portions Copyright (C) 2021-2022 Steve Clark
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would
 *    be appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/* This is a simple MUS to MIDI converter designed for programs such as DOOM
 * that utilize MIDI for sound storage.
 *
 * The sfx_handler can also manage Quake's sound effects since they are all in
 * WAV format.
 */

typedef PACKED(struct {
    char id[4];
    uint16_t score_len;
    uint16_t score_start;
}) mus_header_t;

typedef PACKED(struct {
    char id[4];
    int length;
    uint16_t type;
    uint16_t ntracks;
    uint16_t ticks;
}) midi_header_t;

static const char magic_mus[4] = {
    'M',
    'U',
    'S',
    0x1a,
};
static const char magic_midi[4] = {
    'M',
    'T',
    'h',
    'd',
};
static const char magic_track[4] = {
    'M',
    'T',
    'r',
    'k',
};
static const uint8_t magic_end_of_track[4] = {
    0x00,
    0xff,
    0x2f,
    0x00,
};

static const int controller_map[16] = {
    -1, 0, 1, 7, 10, 11, 91, 93, 64, 67, 120, 123, 126, 127, 121, -1,
};

static uint8_t *midi_data;
static int midi_size;

static uint8_t *mus_pos;
static int mus_end_of_track;

static uint8_t delta_bytes[4];
static int delta_cnt;

/* maintain a list of channel volume */
static uint8_t mus_channel[16];

/* main conversion routine for MUS to MIDI */
static int convert(void)
{
    uint8_t data, last, channel;
    uint8_t event[3] = {0};
    int count = 0;
    uint8_t *midi_data_tmp;

    data = *mus_pos++;
    last = data & 0x80;
    channel = data & 0xf;

    switch (data & 0x70) {
    case 0x00:
        event[0] = 0x80;
        event[1] = *mus_pos++ & 0x7f;
        event[2] = mus_channel[channel];
        count = 3;
        break;

    case 0x10:
        event[0] = 0x90;
        data = *mus_pos++;
        event[1] = data & 0x7f;
        event[2] = data & 0x80 ? *mus_pos++ : mus_channel[channel];
        mus_channel[channel] = event[2];
        count = 3;
        break;

    case 0x20:
        event[0] = 0xe0;
        event[1] = (*mus_pos & 0x01) << 6;
        event[2] = *mus_pos++ >> 1;
        count = 3;
        break;

    case 0x30:
        event[0] = 0xb0;
        event[1] = controller_map[*mus_pos++ & 0xf];
        event[2] = 0x7f;
        count = 3;
        break;

    case 0x40:
        data = *mus_pos++;
        if (data == 0) {
            event[0] = 0xc0;
            event[1] = *mus_pos++;
            count = 2;
            break;
        }
        event[0] = 0xb0;
        event[1] = controller_map[data & 0xf];
        event[2] = *mus_pos++;
        count = 3;
        break;

    case 0x50:
        return 0;

    case 0x60:
        mus_end_of_track = 1;
        return 0;

    case 0x70:
        mus_pos++;
        return 0;
    }

    if (channel == 9)
        channel = 15;
    else if (channel == 15)
        channel = 9;

    event[0] |= channel;

    midi_data_tmp = realloc(midi_data, midi_size + delta_cnt + count);
    if (unlikely(!midi_data_tmp)) {
        free(midi_data);
        return -ENOMEM;
    }
    midi_data = midi_data_tmp;

    memcpy(midi_data + midi_size, &delta_bytes, delta_cnt);
    midi_size += delta_cnt;
    memcpy(midi_data + midi_size, &event, count);
    midi_size += count;

    if (last) {
        delta_cnt = 0;
        do {
            data = *mus_pos++;
            delta_bytes[delta_cnt] = data;
            delta_cnt++;
        } while (data & 128);
    } else {
        delta_bytes[0] = 0;
        delta_cnt = 1;
    }

    return 0;
}

uint8_t *mus2midi(uint8_t *data, int *length)
{
    mus_header_t *mus_hdr = (mus_header_t *) data;
    midi_header_t midi_hdr;
    uint8_t *mid_track_len;
    int track_len;
    uint8_t *midi_data_tmp;

    if (strncmp(mus_hdr->id, magic_mus, 4))
        return NULL;

    if (*length != mus_hdr->score_start + mus_hdr->score_len)
        return NULL;

    midi_size = sizeof(midi_header_t);
    memcpy(midi_hdr.id, magic_midi, 4);
    midi_hdr.length = bswap32(6);
    midi_hdr.type = bswap16(0); /* single track should be type 0 */
    midi_hdr.ntracks = bswap16(1);
    /* maybe, set 140ppqn and set tempo to 1000000µs */
    midi_hdr.ticks =
        bswap16(70); /* 70 ppqn = 140 per second @ tempo = 500000µs (default) */
    midi_data = malloc(midi_size);
    if (unlikely(!midi_data))
        return NULL;
    memcpy(midi_data, &midi_hdr, midi_size);

    midi_data_tmp = realloc(midi_data, midi_size + 8);
    if (unlikely(!midi_data_tmp)) {
        free(midi_data);
        return NULL;
    }
    midi_data = midi_data_tmp;
    memcpy(midi_data + midi_size, magic_track, 4);
    midi_size += 4;
    mid_track_len = midi_data + midi_size;
    midi_size += 4;

    track_len = 0;

    mus_pos = data + mus_hdr->score_start;
    mus_end_of_track = 0;
    delta_bytes[0] = 0;
    delta_cnt = 1;

    for (int i = 0; i < 16; i++)
        mus_channel[i] = 0;

    while (!mus_end_of_track)
        if (unlikely(convert() < 0))
            return NULL;

    /* a final delta time must be added prior to the end of track event */
    midi_data_tmp = realloc(midi_data, midi_size + delta_cnt);
    if (unlikely(!midi_data_tmp)) {
        free(midi_data);
        return NULL;
    }
    midi_data = midi_data_tmp;
    memcpy(midi_data + midi_size, &delta_bytes, delta_cnt);
    midi_size += delta_cnt;

    midi_data_tmp = realloc(midi_data, midi_size + 3);
    if (unlikely(!midi_data_tmp)) {
        free(midi_data);
        return NULL;
    }
    midi_data = midi_data_tmp;
    memcpy(midi_data + midi_size, magic_end_of_track + 1, 3);
    midi_size += 3;

    track_len = bswap32(midi_size - sizeof(midi_header_t) - 8);
    memcpy(mid_track_len, &track_len, 4);

    *length = midi_size;

    return midi_data;
}

#if RV32_HAS(SDL_MIXER)
static void *sfx_handler(void *arg)
{
    sound_t *sfx = (sound_t *) arg;
    uint8_t *ptr = sfx->data;

    if (*ptr & 0x3) { /* Doom WAV format */
        ptr += 2;     /* skip format */
        ptr += 2;     /* skip sample rate since SAMPLE_RATE is defined */
        nr_sfx_samples = *(uint32_t *) ptr;
        ptr += 4;
        ptr += 4;  /* skip pad bytes */
    } else {       /* Normal WAV format*/
        ptr += 44; /* skip RIFF header */
        nr_sfx_samples = sfx->size - 44;
    }

    memcpy(sfx_samples, ptr, sizeof(uint8_t) * nr_sfx_samples);
    sfx_chunk = Mix_QuickLoad_RAW(sfx_samples, nr_sfx_samples);
    if (!sfx_chunk)
        return NULL;

    chan = Mix_PlayChannel(-1, sfx_chunk, 0);
    if (chan == -1)
        return NULL;

    if (*ptr & 0x3) {
        /* Doom, multiplied by 8 because sfx->volume's max is 15 */
        Mix_Volume(chan, sfx->volume * 8);
    } else {
        /* Quake, + 1 mod by 128 because sfx->volume's max is 255 and
         * Mix_Volume's max is 128.
         */
        Mix_Volume(chan, (sfx->volume + 1) % 128);
    }

    return NULL;
}

static void *music_handler(void *arg)
{
    sound_t *music = (sound_t *) arg;
    int looping = music->looping ? -1 : 1;

    free(music_midi_data);

    music_midi_data = mus2midi(music->data, (int *) &music->size);
    if (!music_midi_data) {
        rv_log_error("mus2midi() failed");
        return NULL;
    }

    SDL_RWops *rwops = SDL_RWFromMem(music_midi_data, music->size);
    if (!rwops) {
        rv_log_error("SDL_RWFromMem failed: %s", SDL_GetError());
        return NULL;
    }

    mid = Mix_LoadMUSType_RW(rwops, MUS_MID, SDL_TRUE);
    if (!mid) {
        rv_log_error("Mix_LoadMUSType_RW failed: %s", Mix_GetError());
        return NULL;
    }

    /* multiplied by 8 because sfx->volume's max is 15
     * further setting volume via syscall_set_music_volume
     */
    Mix_VolumeMusic(music->volume * 8);

    if (Mix_PlayMusic(mid, looping) == -1) {
        rv_log_error("Mix_PlayMusic failed: %s", Mix_GetError());
        return NULL;
    }

    return NULL;
}

static void play_sfx(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    const uint32_t sfxinfo_addr = (uint32_t) rv_get_reg(rv, rv_reg_a1);
    int volume = rv_get_reg(rv, rv_reg_a2);

    sfxinfo_t sfxinfo;
    uint8_t sfx_data[SFX_SAMPLE_SIZE];
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    uint32_t addr = rv->io.mem_translate(rv, sfxinfo_addr, R);
    memory_read(attr->mem, (uint8_t *) &sfxinfo, addr, sizeof(sfxinfo_t));

    uint32_t sfx_data_offset =
        *((uint32_t *) ((uint8_t *) attr->mem->mem_base + addr));
    uint32_t sfx_data_size =
        *(uint32_t *) ((uint8_t *) attr->mem->mem_base + addr + 4);
    uint32_t sfx_data_vaddr = sfx_data_offset;
    uint8_t *sfx_data_ptr = &sfx_data[0];
    remain_size = sfx_data_size;
    curr_offset = 0;

    GET_SFX_DATA_FROM_RANDOM_PAGE(sfx_data_vaddr, sfx_data_ptr);
#else
    memory_read(attr->mem, (uint8_t *) &sfxinfo, sfxinfo_addr,
                sizeof(sfxinfo_t));

    /* The data and size in the application must be positioned in the first two
     * fields of the structure. This ensures emulator compatibility with
     * various applications when accessing different sfxinfo_t instances.
     */
    uint32_t sfx_data_offset = *((uint32_t *) &sfxinfo);
    uint32_t sfx_data_size = *(uint32_t *) ((uint32_t *) &sfxinfo + 1);
    memory_read(attr->mem, sfx_data, sfx_data_offset,
                sizeof(uint8_t) * sfx_data_size);
#endif

    sound_t sfx = {
        .data = sfx_data,
        .size = sfx_data_size,
        .volume = volume,
    };
#if RV32_HAS(SDL_MIXER)
    pthread_create(&sfx_thread, NULL, sfx_handler, &sfx);
    sfx_thread_init = true;
#endif
    /* FIXME: In web browser runtime, web workers in thread pool do not reap
     * after sfx_handler return, thus we have to join them. sfx_handler does not
     * contain infinite loop,so do not worry to be stalled by it */
#ifdef __EMSCRIPTEN__
    pthread_join(sfx_thread, NULL);
#endif
}

static void play_music(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    const uint32_t musicinfo_addr = (uint32_t) rv_get_reg(rv, rv_reg_a1);
    int volume = rv_get_reg(rv, rv_reg_a2);
    int looping = rv_get_reg(rv, rv_reg_a3);

    musicinfo_t musicinfo;
    uint8_t music_data[MUSIC_MAX_SIZE];
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    uint32_t addr = rv->io.mem_translate(rv, musicinfo_addr, R);
    memory_read(attr->mem, (uint8_t *) &musicinfo, addr, sizeof(musicinfo_t));

    /* The data and size in the application must be positioned in the first two
     * fields of the structure. This ensures emulator compatibility with
     * various applications when accessing different musicinfo_t instances.
     */
    uint32_t music_data_offset =
        *((uint32_t *) ((uint8_t *) attr->mem->mem_base + addr));
    uint32_t music_data_size =
        *(uint32_t *) ((uint8_t *) attr->mem->mem_base + addr + 4);
    uint32_t music_data_vaddr = music_data_offset;
    uint8_t *music_data_ptr = &music_data[0];
    remain_size = music_data_size;
    curr_offset = 0;

    GET_MUSIC_DATA_FROM_RANDOM_PAGE(music_data_vaddr, music_data_ptr);
#else
    memory_read(attr->mem, (uint8_t *) &musicinfo, musicinfo_addr,
                sizeof(musicinfo_t));

    /* The data and size in the application must be positioned in the first two
     * fields of the structure. This ensures emulator compatibility with
     * various applications when accessing different sfxinfo_t instances.
     */
    uint32_t music_data_offset = *((uint32_t *) &musicinfo);
    uint32_t music_data_size = *(uint32_t *) ((uint32_t *) &musicinfo + 1);
    memory_read(attr->mem, music_data, music_data_offset, music_data_size);
#endif

    sound_t music = {
        .data = music_data,
        .size = music_data_size,
        .looping = looping,
        .volume = volume,
    };
#if RV32_HAS(SDL_MIXER)
    pthread_create(&music_thread, NULL, music_handler, &music);
    music_thread_init = true;
#endif
    /* FIXME: In web browser runtime, web workers in thread pool do not reap
     * after music_handler return, thus we have to join them. music_handler does
     * not contain infinite loop,so do not worry to be stalled by it */
#ifdef __EMSCRIPTEN__
    pthread_join(music_thread, NULL);
#endif
}

static void stop_music()
{
    if (Mix_PlayingMusic())
        Mix_HaltMusic();
}

static void set_music_volume(riscv_t *rv)
{
    int volume = rv_get_reg(rv, rv_reg_a1);

    /* multiplied by 8 because volume's max is 15 */
    Mix_VolumeMusic(volume * 8);
}
#endif /* RV32_HAS(SDL_MIXER) */

static void init_audio(void)
{
    if (!(SDL_WasInit(-1) & SDL_INIT_AUDIO)) {
        if (SDL_Init(SDL_INIT_AUDIO) != 0) {
            rv_log_fatal("Failed to call SDL_Init()");
            exit(EXIT_FAILURE);
        }
    }

    /* sfx samples buffer */
    sfx_samples = malloc(SFX_SAMPLE_SIZE);
    if (unlikely(!sfx_samples)) {
        rv_log_fatal("Failed to allocate memory for buffer");
        exit(EXIT_FAILURE);
    }

#if RV32_HAS(SDL_MIXER)
    /* Initialize SDL2 Mixer */
    if (Mix_Init(MIX_INIT_MID) != MIX_INIT_MID) {
        rv_log_fatal("Mix_Init failed: %s", Mix_GetError());
        exit(EXIT_FAILURE);
    }
    if (Mix_OpenAudio(SAMPLE_RATE, AUDIO_U8, CHANNEL_USED, CHUNK_SIZE) == -1) {
        rv_log_fatal("Mix_OpenAudio failed: %s", Mix_GetError());
        Mix_Quit();
        exit(EXIT_FAILURE);
    }
#endif
    audio_init = true;
}

static void shutdown_audio()
{
    /*
     * sfx_thread and music_thread might contain invalid identifiers.
     * Additionally, there is no built-in method to verify the validity
     * of a pthread_t type. Therefore, the sfx_thread_init and music_thread_init
     * flags are used for validation, ensuring that pthread_join always operates
     * on a valid pthread_t identifier.
     */

#if RV32_HAS(SDL_MIXER)
    if (music_thread_init) {
        stop_music();
        pthread_join(music_thread, NULL);
        Mix_FreeMusic(mid);
        free(music_midi_data);
        music_midi_data = NULL;
    }

    if (sfx_thread_init) {
        pthread_join(sfx_thread, NULL);
        Mix_HaltChannel(-1);
        Mix_FreeChunk(sfx_chunk);
        free(sfx_samples);
        sfx_samples = NULL;
    }

    Mix_CloseAudio();
    Mix_Quit();
#endif

    audio_init = sfx_thread_init = music_thread_init = false;
}

void sdl_video_audio_cleanup()
{
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    /*
     * The sfx_or_music_thread_init flag might not be set if a quick ctrl-c
     * occurs while the audio configuration is being initialized. Therefore,
     * need to destroy the audio settings by checking audio_init flag.
     */
    bool sfx_or_music_thread_init = sfx_thread_init | music_thread_init;
    if (sfx_or_music_thread_init || (!sfx_or_music_thread_init && audio_init))
        shutdown_audio();
    SDL_Quit();
}

void syscall_setup_audio(riscv_t *rv)
{
    /* setup_audio(request) */
    const int request = rv_get_reg(rv, rv_reg_a0);

    switch (request) {
    case INIT_AUDIO:
        init_audio();
        break;
    case SHUTDOWN_AUDIO:
        shutdown_audio();
        break;
    default:
        rv_log_error("Unknown sound request: %d", request);
        break;
    }
}

void syscall_control_audio(riscv_t *rv)
{
    /* control_audio(request) */
    const int request = rv_get_reg(rv, rv_reg_a0);

    switch (request) {
    case PLAY_MUSIC:
#if RV32_HAS(SDL_MIXER)
        play_music(rv);
#endif
        break;
    case PLAY_SFX:
#if RV32_HAS(SDL_MIXER)
        play_sfx(rv);
#endif
        break;
    case SET_MUSIC_VOLUME:
#if RV32_HAS(SDL_MIXER)
        set_music_volume(rv);
#endif
        break;
    case STOP_MUSIC:
#if RV32_HAS(SDL_MIXER)
        stop_music();
#endif
        break;
    default:
        rv_log_error("Unknown sound control request: %d", request);
        break;
    }
}
