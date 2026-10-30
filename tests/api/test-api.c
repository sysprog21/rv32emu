/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <rv32emu.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* RV32E passes syscall numbers in t0; other builds use a7. */
#ifdef API_TEST_RV32E
#define SYSCALL_REG 5
#define REGISTER_COUNT 16
#else
#define SYSCALL_REG 17
#define REGISTER_COUNT 32
#endif

static void put32(unsigned char *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = value >> (8 * i);
}

/* A tiny RV32 ELF requiring no cross compiler or private headers. */
static void make_guest(const char *path)
{
    unsigned char elf[100] = {0};
    memcpy(elf, "\177ELF\1\1\1", 7);
    elf[16] = 2;   /* ET_EXEC */
    elf[18] = 243; /* EM_RISCV */
    put32(elf + 20, 1);
    put32(elf + 24, 0x1000);
    put32(elf + 28, 52);
    elf[40] = 52;
    elf[42] = 32;
    elf[44] = 1;
    put32(elf + 52, 1); /* PT_LOAD */
    put32(elf + 56, 84);
    put32(elf + 60, 0x1000);
    put32(elf + 64, 0x1000);
    put32(elf + 68, 16);
    put32(elf + 72, 16);
    put32(elf + 76, 5);
    put32(elf + 80, 4);
    put32(elf + 84, 0x00700513);                      /* li a0, 7 */
    put32(elf + 88, 0x05d00013 | (SYSCALL_REG << 7)); /* li syscall reg, 93 */
    put32(elf + 92, 0x00000073);                      /* ecall */
    put32(elf + 96, 0x0000006f);                      /* j . */
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(elf, 1, sizeof(elf), f) == sizeof(elf));
    assert(!fclose(f));
}

/* All checks use the installed-header contract, never private structures. */
static void test_invalid_options(const char *path)
{
    rv32emu_options_t options;
    rv32emu_t *emu = NULL;
    rv32emu_options_init(NULL);
    rv32emu_options_init(&options);
    assert(options.memory_size == (64U << 20));
    assert(!options.argc && !options.argv);
    const uint64_t bad_sizes[] = {0, 16383, 16385, UINT64_C(0x100000001)};
    for (unsigned i = 0; i < sizeof(bad_sizes) / sizeof(*bad_sizes); i++) {
        options.memory_size = bad_sizes[i];
        assert(rv32emu_create(path, &options, &emu) ==
               RV32EMU_INVALID_ARGUMENT);
        assert(!emu);
    }
    rv32emu_options_init(&options);
    options.argc = -1;
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_INVALID_ARGUMENT);
    options.argc = 129;
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_INVALID_ARGUMENT);
    options.argc = 1;
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_INVALID_ARGUMENT);
    const char *args[] = {NULL};
    options.argv = args;
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_INVALID_ARGUMENT);
    char oversized[2049];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    args[0] = oversized;
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_create("", NULL, &emu) == RV32EMU_INVALID_ARGUMENT);
    for (int status = RV32EMU_OK; status <= RV32EMU_INVALID_STATE; status++) {
        const char *message = rv32emu_status_string((rv32emu_status_t) status);
        assert(message && *message && strcmp(message, "unknown status"));
    }
    assert(!strcmp(rv32emu_status_string((rv32emu_status_t) 999),
                   "unknown status"));
}

static void test_argument_limits(const char *path)
{
    rv32emu_options_t options;
    rv32emu_options_init(&options);
    options.memory_size = 16384;
    const char *args[128];
    for (unsigned i = 0; i < 128; i++)
        args[i] = "";
    options.argc = 128;
    options.argv = args;
    rv32emu_t *emu = NULL;
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_OK);
    rv32emu_destroy(emu);
    char maximum[2048];
    memset(maximum, 'a', sizeof(maximum) - 1);
    maximum[sizeof(maximum) - 1] = 0;
    args[0] = maximum;
    options.argc = 1;
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_OK);
    rv32emu_destroy(emu);
    options.argc = 2; /* Each terminating NUL counts toward the byte limit. */
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_INVALID_ARGUMENT);
    assert(!emu);
}

static uint32_t read_word(rv32emu_t *emu, uint32_t address)
{
    unsigned char bytes[4];
    assert(rv32emu_read_memory(emu, address, bytes, sizeof(bytes)) ==
           RV32EMU_OK);
    return bytes[0] | (uint32_t) bytes[1] << 8 | (uint32_t) bytes[2] << 16 |
           (uint32_t) bytes[3] << 24;
}

static void test_access_errors(rv32emu_t *emu)
{
    uint32_t value = 0x12345678;
    int code = -1;
    assert(rv32emu_get_reg(NULL, 0, &value) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_get_reg(emu, 0, NULL) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_get_reg(emu, UINT_MAX, &value) == RV32EMU_INVALID_ARGUMENT);
    assert(value == 0x12345678);
    assert(rv32emu_set_reg(NULL, 0, 0) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_set_reg(emu, UINT_MAX, 0) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_get_pc(NULL, &value) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_get_pc(emu, NULL) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_set_pc(NULL, 0) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_halt(NULL) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_exit_code(NULL, &code) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_exit_code(emu, NULL) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_step(emu, UINT32_MAX) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_read_memory(NULL, 0, &value, 4) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_write_memory(NULL, 0, &value, 4) ==
           RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_read_memory(emu, 0, NULL, 1) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_write_memory(emu, 0, NULL, 1) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_read_memory(emu, 0, &value, SIZE_MAX) ==
           RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_write_memory(emu, 0, &value, SIZE_MAX) ==
           RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_write_memory(emu, 16384, NULL, 0) == RV32EMU_OK);
    assert(rv32emu_read_memory(emu, 16385, NULL, 0) ==
           RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_write_memory(emu, 16385, NULL, 0) ==
           RV32EMU_INVALID_ARGUMENT);
    unsigned char sentinel[] = {0x5a, 0xa5};
    assert(rv32emu_write_memory(emu, 16383, sentinel, 1) == RV32EMU_OK);
    assert(rv32emu_write_memory(emu, 16383, sentinel + 1, 2) ==
           RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_read_memory(emu, 16383, sentinel, 2) ==
           RV32EMU_INVALID_ARGUMENT);
    assert(sentinel[0] == 0x5a && sentinel[1] == 0xa5);
    sentinel[0] = 0;
    assert(rv32emu_read_memory(emu, 16383, sentinel, 1) == RV32EMU_OK);
    assert(sentinel[0] ==
           0x5a); /* Failed write did not alter the valid prefix. */
}

#ifdef API_TEST_SDL
#include <SDL.h>

static Uint32 guest_window_id;

static int SDLCALL watch_window(void *host, SDL_Event *event)
{
    if (event->type == SDL_WINDOWEVENT &&
        event->window.windowID != SDL_GetWindowID(host))
        guest_window_id = event->window.windowID;
    return 1;
}

static void guest_syscall(rv32emu_t *emu,
                          uint32_t nr,
                          uint32_t a0,
                          uint32_t a1,
                          uint32_t a2)
{
    assert(rv32emu_set_reg(emu, SYSCALL_REG, nr) == RV32EMU_OK);
    assert(rv32emu_set_reg(emu, 10, a0) == RV32EMU_OK);
    assert(rv32emu_set_reg(emu, 11, a1) == RV32EMU_OK);
    assert(rv32emu_set_reg(emu, 12, a2) == RV32EMU_OK);
    assert(rv32emu_set_pc(emu, 0x1008) == RV32EMU_OK);
    assert(rv32emu_step(emu, 1) == RV32EMU_OK);
}

static void test_sdl_lifecycle(const char *path)
{
    assert(!SDL_setenv("SDL_VIDEODRIVER", "dummy", 1));
    assert(!SDL_setenv("SDL_AUDIODRIVER", "dummy", 1));
    assert(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
    Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
    assert(!SDL_InitSubSystem(flags));
    SDL_Window *host = SDL_CreateWindow("host", 0, 0, 8, 8, 0);
    assert(host);
    Uint32 host_id = SDL_GetWindowID(host);
    SDL_AddEventWatch(watch_window, host);

    /* First destroy a guest with deferred work but no SDL resources. Then
     * advance both ring cursors and verify each replacement starts at zero.
     */
    for (unsigned pass = 0; pass < 4; pass++) {
        rv32emu_t *emu = NULL;
        assert(rv32emu_create(path, NULL, &emu) == RV32EMU_OK);
        unsigned char submission[24] = {0};
        put32(submission, 1); /* WINDOW_TITLE_SUBMISSION */
        put32(submission + 4, 0x3000);
        put32(submission + 8, 5);
        put32(submission + 12, 1);
        put32(submission + 16, 0x3010);
        put32(submission + 20, 5);
        /* Two event slots of 20 bytes each precede submissions. */
        assert(rv32emu_write_memory(emu, 0x2028, submission,
                                    sizeof(submission)) == RV32EMU_OK);
        assert(rv32emu_write_memory(emu, 0x3000, "fresh", 5) == RV32EMU_OK);
        assert(rv32emu_write_memory(emu, 0x3010, "stale", 5) == RV32EMU_OK);
        guest_syscall(emu, 0xC0DE, 0x2000, 2, 0x2100);
        guest_syscall(emu, 0xFEED, pass ? 1 : 2, 0, 0);
        if (pass) {
            guest_syscall(emu, 0xBEEF, 0x4000, 8, 8);
            SDL_Window *guest = SDL_GetWindowFromID(guest_window_id);
            assert(guest && guest != host);
            assert(!strcmp(SDL_GetWindowTitle(guest), "fresh"));
            SDL_Event key = {0};
            key.type = SDL_KEYDOWN;
            key.key.windowID = guest_window_id;
            key.key.state = SDL_PRESSED;
            key.key.keysym.sym = SDLK_a;
            assert(SDL_PushEvent(&key) == 1);
            guest_syscall(emu, 0xBEEF, 0x4000, 8, 8);
            assert(read_word(emu, 0x2100) == 1);
            assert(read_word(emu, 0x2004) == SDLK_a);
        }
        rv32emu_destroy(emu);
        if (pass)
            assert(!SDL_GetWindowFromID(guest_window_id));
        assert(SDL_WasInit(flags) == flags);
        assert(SDL_GetWindowFromID(host_id) == host);
        assert(!strcmp(SDL_GetWindowTitle(host), "host"));
    }
    /* Audio-only guests also acquire their own subsystem reference, even
     * when the host already initialized audio. Repeated init is harmless.
     */
    for (unsigned pass = 0; pass < 2; pass++) {
        rv32emu_t *emu = NULL;
        assert(rv32emu_create(path, NULL, &emu) == RV32EMU_OK);
        guest_syscall(emu, 0xBABE, 0, 0, 0);
        guest_syscall(emu, 0xBABE, 0, 0, 0);
        guest_syscall(emu, 0xBABE, 1, 0, 0);
        guest_syscall(emu, 0xBABE, 0, 0, 0);
        rv32emu_destroy(emu);
        assert(SDL_WasInit(flags) == flags);
        assert(SDL_GetWindowFromID(host_id) == host);
    }
    SDL_DelEventWatch(watch_window, host);
    SDL_DestroyWindow(host);
    SDL_QuitSubSystem(flags);
    assert(!SDL_WasInit(flags));
}
#endif

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *path = argv[1];
    rv32emu_t *emu = NULL, *second = NULL;
    rv32emu_options_t options;
    rv32emu_options_init(&options);
    options.memory_size = 16384;
    assert(rv32emu_create(NULL, NULL, &emu) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_create(path, NULL, NULL) == RV32EMU_INVALID_ARGUMENT);
    assert(rv32emu_step(NULL, 1) == RV32EMU_INVALID_ARGUMENT);
    rv32emu_destroy(NULL);
    /* An existing non-ELF file must fail without terminating this process. */
    FILE *bad = fopen(path, "wb");
    assert(bad);
    assert(fputs("not an ELF", bad) >= 0);
    assert(!fclose(bad));
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_CREATE_FAILED);
    assert(!emu);
    assert(!remove(path));
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_CREATE_FAILED);
    make_guest(path);
    test_invalid_options(path);
    test_argument_limits(path);
    char greeting[] = "hello";
    const char *args[] = {"guest", greeting};
    options.argc = 2;
    options.argv = args;
    for (int repeat = 0; repeat < 3; repeat++) {
        assert(rv32emu_create(path, &options, &emu) == RV32EMU_OK);
        greeting[0] = 'X'; /* Creation must copy arguments into guest RAM. */
        assert(rv32emu_create(path, &options, &second) == RV32EMU_BUSY);
        assert(!second);
        test_access_errors(emu);
        uint32_t value, sp;
        assert(rv32emu_get_pc(emu, &value) == RV32EMU_OK && value == 0x1000);
        assert(rv32emu_set_pc(emu, 1) == RV32EMU_INVALID_ARGUMENT);
        assert(rv32emu_set_reg(emu, 0, 42) == RV32EMU_OK);
        assert(rv32emu_get_reg(emu, 0, &value) == RV32EMU_OK && !value);
        assert(rv32emu_get_reg(emu, 32, &value) == RV32EMU_INVALID_ARGUMENT);
        assert(rv32emu_get_reg(emu, REGISTER_COUNT, &value) ==
               RV32EMU_INVALID_ARGUMENT);
        assert(rv32emu_set_reg(emu, REGISTER_COUNT, 1) ==
               RV32EMU_INVALID_ARGUMENT);
        assert(rv32emu_set_reg(emu, REGISTER_COUNT - 1, 42) == RV32EMU_OK);
        assert(rv32emu_get_reg(emu, REGISTER_COUNT - 1, &value) == RV32EMU_OK &&
               value == 42);
        assert(rv32emu_set_reg(emu, 10, 123) == RV32EMU_OK);
        assert(rv32emu_get_reg(emu, 10, &value) == RV32EMU_OK && value == 123);
        assert(rv32emu_get_reg(emu, 2, &sp) == RV32EMU_OK);
        unsigned char data[12];
        assert(read_word(emu, sp) == 2); /* argc */
        uint32_t arg0 = read_word(emu, sp + 4);
        assert(rv32emu_read_memory(emu, arg0, data, 6) == RV32EMU_OK);
        assert(!memcmp(data, "guest", 6));
        assert((sp & 15) == 0);
        assert(read_word(emu, sp + 12) == 0); /* argv[argc] */
        uint32_t arg1 = read_word(emu, sp + 8);
        assert(rv32emu_read_memory(emu, arg1, data, 6) == RV32EMU_OK);
        assert(!memcmp(data, "hello", 6));
        greeting[0] = 'h';
        assert(rv32emu_write_memory(emu, 0x2000, "data", 5) == RV32EMU_OK);
        assert(rv32emu_read_memory(emu, 0x2000, data, 5) == RV32EMU_OK);
        assert(!memcmp(data, "data", 5));
        assert(rv32emu_read_memory(emu, 16384, NULL, 0) == RV32EMU_OK);
        assert(rv32emu_read_memory(emu, UINT32_MAX, data, 12) ==
               RV32EMU_INVALID_ARGUMENT);
        assert(rv32emu_write_memory(emu, 16383, "xx", 2) ==
               RV32EMU_INVALID_ARGUMENT);
        int code = -1;
        assert(rv32emu_exit_code(emu, &code) == RV32EMU_INVALID_STATE);
        assert(rv32emu_step(emu, 0) == RV32EMU_INVALID_ARGUMENT);
        rv32emu_status_t status = RV32EMU_OK;
        for (int i = 0; i < 100 && status == RV32EMU_OK; i++)
            status = rv32emu_step(emu, 1);
        assert(status == RV32EMU_HALTED);
        assert(rv32emu_exit_code(emu, &code) == RV32EMU_OK && code == 7);
        assert(rv32emu_step(emu, 1) == RV32EMU_HALTED);
        assert(rv32emu_write_memory(emu, 0, "x", 1) == RV32EMU_INVALID_STATE);
        rv32emu_destroy(emu);
    }
    /* A hot native loop must yield repeatedly and accept state changes. */
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_OK);
    unsigned char loop[4];
    put32(loop, 0x0000006f);
    assert(rv32emu_write_memory(emu, 0x1000, loop, 4) == RV32EMU_OK);
    for (int slice = 0; slice < 100; slice++)
        assert(rv32emu_step(emu, 1000) == RV32EMU_OK);
    /* Redirect a hot loop to the original exit sequence via public state. */
    assert(rv32emu_set_reg(emu, 10, 23) == RV32EMU_OK);
    assert(rv32emu_set_pc(emu, 0x1004) == RV32EMU_OK);
    assert(rv32emu_step(emu, 100) == RV32EMU_HALTED);
    int redirected_code = -1;
    assert(rv32emu_exit_code(emu, &redirected_code) == RV32EMU_OK);
    assert(redirected_code == 23);
    rv32emu_destroy(emu);
    /* Also destroy while yielded, before any halt or PC edit clears state. */
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_OK);
    assert(rv32emu_write_memory(emu, 0x1000, loop, 4) == RV32EMU_OK);
    assert(rv32emu_step(emu, 1000) == RV32EMU_OK);
    rv32emu_destroy(emu);
    assert(rv32emu_create(path, &options, &emu) == RV32EMU_OK);
    assert(rv32emu_step(emu, 100) == RV32EMU_HALTED);
    rv32emu_destroy(emu);
    assert(rv32emu_create(path, NULL, &emu) == RV32EMU_OK);
    uint32_t default_sp;
    assert(rv32emu_get_reg(emu, 2, &default_sp) == RV32EMU_OK);
    assert(read_word(emu, default_sp) == 1);
    assert(read_word(emu, default_sp + 8) == 0);
    char default_arg[2048];
    assert(strlen(path) < sizeof(default_arg));
    assert(rv32emu_read_memory(emu, read_word(emu, default_sp + 4), default_arg,
                               strlen(path) + 1) == RV32EMU_OK);
    assert(!strcmp(default_arg, path));
    assert(rv32emu_halt(emu) == RV32EMU_OK);
    int halted_code = -1;
    assert(rv32emu_exit_code(emu, &halted_code) == RV32EMU_OK);
    assert(halted_code == 0);
    assert(rv32emu_step(emu, 1) == RV32EMU_HALTED);
    rv32emu_destroy(emu);
#ifdef API_TEST_SDL
    test_sdl_lifecycle(path);
#endif
    assert(!remove(path));
    puts("Public API tests passed");
    return 0;
}
