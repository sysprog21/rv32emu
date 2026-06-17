/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>

typedef struct netdev netdev_t;

typedef enum {
    NETDEV_IMPL_tap,
} netdev_impl_t;

typedef struct {
    int tap_fd;
} net_tap_options_t;

struct netdev {
    char *name;
    netdev_impl_t type;
    void *op;
};

bool netdev_init(netdev_t *netdev, const char *net_type);

void netdev_delete(netdev_t *netdev);
