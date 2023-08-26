#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sound request */
enum {
    INIT_SOUND,
    SHUTDOWN_SOUND,
    PLAY_MUSIC,
    PLAY_SFX,
    SET_MUSIC_VOLUME,
    STOP_MUSIC,
};

typedef struct {
    void *data;
    int size;
} musicinfo_t;

typedef struct {
    void *data;
    int size;
} sfxinfo_t;

#define MUSIC_MAX_SIZE 65536
#define SFX_MAX_SIZE 32768

sfxinfo_t *sfx;
musicinfo_t *music;

static char *sfx_src;
static char *music_src;
static int music_delay = 30;
static int music_looping = 0; /* FIXME: parsing looping CLI argument */
static int music_volume = 3;
static int sfx_volume = 15;

enum {
    MUSIC = 1,
    SFX = 2,
    INCREASING_MUSIC_VOLUME = 4,
    SFX_REPEAT = 8,
};
static int flag = 0;

void play_music()
{
    int request_type = PLAY_MUSIC;

    register int a0 asm("a0") = request_type;
    register int a1 asm("a1") = (uintptr_t) music;
    register int a2 asm("a2") = music_volume;
    register int a3 asm("a3") = music_looping;
    register int a7 asm("a7") = 0xD00D;

    asm volatile("scall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a7));
}

void stop_music()
{
    int request_type = STOP_MUSIC;

    register int a0 asm("a0") = request_type;
    register int a7 asm("a7") = 0xD00D;

    asm volatile("scall" : "+r"(a0) : "r"(a7));
}

void play_sfx()
{
    int request_type = PLAY_SFX;

    register int a0 asm("a0") = request_type;
    register int a1 asm("a1") = (uintptr_t) sfx;
    register int a2 asm("a2") = sfx_volume;
    register int a7 asm("a7") = 0xD00D;

    asm volatile("scall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7));
}

void set_music_volume()
{
    int request_type = SET_MUSIC_VOLUME;

    register int a0 asm("a0") = request_type;
    register int a1 asm("a1") = music_volume;
    register int a7 asm("a7") = 0xD00D;

    asm volatile("scall" : "+r"(a0) : "r"(a1), "r"(a7));
}

/*
 * when emulating the Doom, sfx is in lump
 * so sfx_src should be a valid sfx lump
 */
static void load_sfx()
{
    sfx = malloc(sizeof(sfxinfo_t));
    assert(sfx);
    sfx->data = malloc(sizeof(uint8_t) * SFX_MAX_SIZE);
    assert(sfx->data);
    sfx->size = 5669; /* FIXME: hardcoded size */

    FILE *sfx_file = fopen(sfx_src, "rb");
    assert(sfx_file);
    fread(sfx->data, 5669, 1, sfx_file); /* FIXME: hardcoded size */
    fclose(sfx_file);
}

static void unload_sfx()
{
    free(sfx->data);
    free(sfx);
}

/* when emulating the Doom, music is in lump */
static void load_music()
{
    music = malloc(sizeof(musicinfo_t));
    assert(music);
    music->data = malloc(sizeof(uint8_t) * MUSIC_MAX_SIZE);
    assert(music->data);
    music->size = 17283; /* FIXME: hardcoded size */

    FILE *music_file = fopen(music_src, "rb");
    assert(music_file);
    fread(music->data, 17283, 1, music_file); /* FIXME: hardcoded size */
    fclose(music_file);
}

static void unload_music()
{
    free(music->data);
    free(music);
}

void init_sound()
{
    int request_type = INIT_SOUND;

    register int a0 asm("a0") = request_type;
    register int a7 asm("a7") = 0xBABE;

    asm volatile("scall" : "+r"(a0) : "r"(a7));
}

void shutdown_sound()
{
    int request_type = SHUTDOWN_SOUND;

    register int a0 asm("a0") = request_type;
    register int a7 asm("a7") = 0xBABE;

    asm volatile("scall" : "+r"(a0) : "r"(a7));
}

/* use '=' to prevent ate by emulator */
void usage(const char *prog)
{
    fprintf(
        stderr,
        "Usage: [path of rv32emu] %s [options]\n"
        "Options:\n"
        "  =m  [MUS format file]: convert MUS format to MIDI format and play "
        "the music\n"
        "  =s  [WAV format file]: play the sound effect\n"
        "  =d  [music delay]    : the larger the more longer of playing the "
        "music\n"
        "  =mv [volume]         : set volume of music which in range [0 - 15], "
        "default is 8\n"
        "  =sv [volume]         : set volume of sfx which in range [0 - 15], "
        "default is 15\n"
        "  =upmv                : increasing music volume slowly to show "
        "'set_music_volume' effect, \n"
        "                         note: delay must be larger enough to see the "
        "effect\n"
        "  =srep                : repeat sfx sound during the play of music\n"
        "  =h                   : show this usage\n",
        prog);
    exit(1);
}

/* FIXME: more robust parser of CLI arguments */
static bool parse_args(int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "=m")) {
            music_src = argv[i + 1];
            flag |= MUSIC;
        } else if (!strcmp(argv[i], "=s")) {
            sfx_src = argv[i + 1];
            flag |= SFX;
        } else if (!strcmp(argv[i], "=d")) {
            music_delay = (int) strtol(argv[i + 1], NULL, 10);
        } else if (!strcmp(argv[i], "=upmv")) {
            flag |= INCREASING_MUSIC_VOLUME;
        } else if (!strcmp(argv[i], "=srep")) {
            flag |= SFX_REPEAT;
        } else if (!strcmp(argv[i], "=mv")) {
            music_volume = (int) strtol(argv[i + 1], NULL, 10);
        } else if (!strcmp(argv[i], "=sv")) {
            sfx_volume = (int) strtol(argv[i + 1], NULL, 10);
        } else if (!strcmp(argv[i], "=h")) {
            return false;
        }
    }

    return true;
}

/* FIXME: rough simulation of sleep */
static void busy_loop(int n)
{
    int delay = n * 10000000;
    int interval = delay / (15 - music_volume);

    for (int i = 0; i < delay; i++) {
        if (flag & INCREASING_MUSIC_VOLUME && flag & MUSIC) {
            if (!(i % interval)) {
                if (music_volume < 15) {
                    music_volume++;
                    set_music_volume();
                }
                if (flag & SFX_REPEAT && flag & SFX)
                    play_sfx();
            }
        }
    }
}

static void do_play_sound()
{
    if (flag & MUSIC)
        load_music();
    if (flag & SFX)
        load_sfx();

    if (flag & MUSIC) {
        if (flag & SFX)
            play_sfx();
        play_music();
        busy_loop(music_delay);
        stop_music();
    } else if (flag & SFX) {
        play_sfx();
        busy_loop(2);
    }

    if (flag & MUSIC)
        unload_music();
    if (flag & SFX)
        unload_sfx();
}

int main(int argc, char *argv[])
{
    if (!parse_args(argc, argv))
        usage(argv[0]);

    if (!(flag & MUSIC || flag & SFX)) {
        fprintf(stderr, "At least a music or sound effect should be given\n");
        usage(argv[0]);
    }

    init_sound();
    do_play_sound();
    shutdown_sound();

    return 0;
}
