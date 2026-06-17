/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include "netdev.h"

#include <stdbool.h>
#include <string.h>

#include "utils.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)

#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int net_init_tap(netdev_t *netdev)
{
    net_tap_options_t *tap = (net_tap_options_t *) netdev->op;

    tap->tap_fd = open("/dev/net/tun", O_RDWR);
    if (tap->tap_fd < 0) {
        rv_log_error("failed to open TAP device: %s", strerror(errno));
        return -1;
    }

    struct ifreq ifreq = {
        .ifr_flags = IFF_TAP | IFF_NO_PI,
    };

    strncpy(ifreq.ifr_name, "tap%d", sizeof(ifreq.ifr_name) - 1);

    if (ioctl(tap->tap_fd, TUNSETIFF, &ifreq) < 0) {
        rv_log_error("failed to allocate TAP device: %s", strerror(errno));
        close(tap->tap_fd);
        tap->tap_fd = -1;
        return -1;
    }

    rv_log_info("allocated TAP interface: %s", ifreq.ifr_name);

    if (fcntl(tap->tap_fd, F_SETFL,
              fcntl(tap->tap_fd, F_GETFL, 0) | O_NONBLOCK) < 0) {
        rv_log_error("failed to set TAP non-blocking mode: %s",
                     strerror(errno));
        close(tap->tap_fd);
        tap->tap_fd = -1;
        return -1;
    }

    return 0;
}

bool netdev_init(netdev_t *netdev, const char *net_type)
{
    if (!netdev || !net_type || strcmp(net_type, "tap"))
        return false;

    netdev->name = (char *) net_type;
    netdev->type = NETDEV_IMPL_tap;
    netdev->op = calloc(1, sizeof(net_tap_options_t));
    if (!netdev->op)
        return false;

    if (net_init_tap(netdev) < 0) {
        free(netdev->op);
        netdev->op = NULL;
        return false;
    }

    return true;
}

void netdev_delete(netdev_t *netdev)
{
    if (!netdev || !netdev->op)
        return;

    switch (netdev->type) {
    case NETDEV_IMPL_tap: {
        net_tap_options_t *tap = (net_tap_options_t *) netdev->op;
        if (tap->tap_fd >= 0)
            close(tap->tap_fd);
        break;
    }
    default:
        break;
    }

    free(netdev->op);
    netdev->op = NULL;
}

#else

bool netdev_init(netdev_t *netdev, const char *net_type)
{
    (void) netdev;
    (void) net_type;

    rv_log_error("virtio-net TAP backend is only supported on Linux hosts");
    return false;
}

void netdev_delete(netdev_t *netdev)
{
    (void) netdev;
}

#endif
