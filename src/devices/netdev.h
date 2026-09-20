/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>

#if RV32_HAS(VIRTIO_NET_TAP) && defined(__linux__) && !defined(__EMSCRIPTEN__)
#define RV32EMU_NET_HAS_TAP 1
#else
#define RV32EMU_NET_HAS_TAP 0
#endif

typedef struct netdev netdev_t;

typedef enum {
    NETDEV_IMPL_NONE = 0,
#if RV32EMU_NET_HAS_TAP
    NETDEV_IMPL_TAP,
#endif
} netdev_impl_t;

#if RV32EMU_NET_HAS_TAP
typedef struct {
    int tap_fd;
} net_tap_options_t;
#endif

struct netdev {
    const char *name;
    netdev_impl_t type;
    void *op;
};

bool netdev_init(netdev_t *netdev, const char *net_type);

void netdev_delete(netdev_t *netdev);
