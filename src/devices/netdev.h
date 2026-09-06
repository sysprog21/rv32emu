/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/uio.h>

/*
 * Networking backends:
 *
 * Linux:
 *   - tap  : kernel TAP device
 *   - user : user-mode SLIRP
 *
 * macOS:
 *   - user : user-mode SLIRP
 *
 * Emscripten:
 * * Networking backends:
 *
 *
 *   - virtio-net networking backends are disabled
 */
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#define RV32EMU_NET_HAS_TAP 1
#else
#define RV32EMU_NET_HAS_TAP 0
#endif

#if !defined(__EMSCRIPTEN__)
#define RV32EMU_NET_HAS_SLIRP 1
#else
#define RV32EMU_NET_HAS_SLIRP 0
#endif

#define RV32EMU_NET_HAS_VMNET 0

typedef struct netdev netdev_t;

typedef enum {
    NETDEV_IMPL_none = 0,
#if RV32EMU_NET_HAS_TAP
    NETDEV_IMPL_tap,
#endif
#if RV32EMU_NET_HAS_SLIRP
    NETDEV_IMPL_user,
#endif
} netdev_impl_t;

#if RV32EMU_NET_HAS_TAP
typedef struct {
    int tap_fd;
} net_tap_options_t;
#endif

#if RV32EMU_NET_HAS_SLIRP
#define SLIRP_PKT_MAX 16384
#define SLIRP_READ_SIDE 0
#define SLIRP_WRITE_SIDE 1

typedef struct {
    void *slirp;
    int guest_to_host_channel[2];
    int host_to_guest_channel[2];
    struct pollfd *pfd;
    int pfd_len;
    int pfd_size;
    void *timer;
} net_user_options_t;

int net_slirp_init(net_user_options_t *usr);
void net_slirp_cleanup(net_user_options_t *usr);
int net_slirp_poll(net_user_options_t *usr);
int net_slirp_read(net_user_options_t *usr);
#endif

struct netdev {
    const char *name;
    netdev_impl_t type;
    void *op;
};

bool netdev_init(netdev_t *netdev, const char *net_type);

void netdev_delete(netdev_t *netdev);
