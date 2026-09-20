/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include "netdev.h"

#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)

#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int net_init_tap(netdev_t *netdev)
{
    net_tap_options_t *tap = (net_tap_options_t *) netdev->op;

    tap->tap_fd = -1;
    tap->tap_fd = open("/dev/net/tun", O_RDWR);
    if (tap->tap_fd < 0) {
        rv_log_error("failed to open TAP device: %s", strerror(errno));
        return -1;
    }

    struct ifreq ifreq = {
        .ifr_flags = IFF_TAP | IFF_NO_PI,
    };

    strncpy(ifreq.ifr_name, "tap%d", sizeof(ifreq.ifr_name) - 1);
    ifreq.ifr_name[sizeof(ifreq.ifr_name) - 1] = '\0';

    if (ioctl(tap->tap_fd, TUNSETIFF, &ifreq) < 0) {
        rv_log_error("failed to allocate TAP device: %s", strerror(errno));
        close(tap->tap_fd);
        tap->tap_fd = -1;
        return -1;
    }

    rv_log_warn("allocated TAP interface: %s", ifreq.ifr_name);

    int flags = fcntl(tap->tap_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(tap->tap_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
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
    if (!netdev)
        return false;

    if (netdev->op)
        netdev_delete(netdev);

    netdev->name = NULL;
    netdev->type = NETDEV_IMPL_NONE;
    netdev->op = NULL;

    const char *requested = net_type ? net_type : "tap";
    if (strcmp(requested, "tap")) {
        rv_log_error("unsupported virtio-net backend: %s", requested);
        return false;
    }

    netdev->name = "tap";
    netdev->type = NETDEV_IMPL_TAP;
    netdev->op = calloc(1, sizeof(net_tap_options_t));
    if (!netdev->op) {
        netdev->type = NETDEV_IMPL_NONE;
        netdev->name = NULL;
        return false;
    }

    if (net_init_tap(netdev) < 0) {
        free(netdev->op);
        netdev->op = NULL;
        netdev->type = NETDEV_IMPL_NONE;
        netdev->name = NULL;
        return false;
    }

    return true;
}

void netdev_delete(netdev_t *netdev)
{
    if (!netdev)
        return;

    if (netdev->op && netdev->type == NETDEV_IMPL_TAP) {
        net_tap_options_t *tap = (net_tap_options_t *) netdev->op;
        if (tap->tap_fd >= 0)
            close(tap->tap_fd);
    }

    free(netdev->op);
    netdev->op = NULL;
    netdev->type = NETDEV_IMPL_NONE;
    netdev->name = NULL;
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
    if (!netdev)
        return;

    free(netdev->op);
    netdev->op = NULL;
    netdev->type = NETDEV_IMPL_NONE;
    netdev->name = NULL;
}

#endif
