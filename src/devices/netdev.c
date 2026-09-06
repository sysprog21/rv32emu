/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include "netdev.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#if RV32EMU_NET_HAS_TAP
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#endif

static void netdev_reset(netdev_t *netdev)
{
    if (!netdev)
        return;

    netdev->name = NULL;
    netdev->type = NETDEV_IMPL_none;
    netdev->op = NULL;
}

#if RV32EMU_NET_HAS_TAP
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
    ifreq.ifr_name[sizeof(ifreq.ifr_name) - 1] = '\0';

    if (ioctl(tap->tap_fd, TUNSETIFF, &ifreq) < 0) {
        rv_log_error("failed to allocate TAP device: %s", strerror(errno));
        close(tap->tap_fd);
        tap->tap_fd = -1;
        return -1;
    }

    rv_log_info("allocated TAP interface: %s", ifreq.ifr_name);

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
#endif

#if RV32EMU_NET_HAS_SLIRP
static int net_init_user(netdev_t *netdev)
{
    net_user_options_t *usr = (net_user_options_t *) netdev->op;

    memset(usr, 0, sizeof(*usr));
    usr->guest_to_host_channel[SLIRP_READ_SIDE] = -1;
    usr->guest_to_host_channel[SLIRP_WRITE_SIDE] = -1;
    usr->host_to_guest_channel[SLIRP_READ_SIDE] = -1;
    usr->host_to_guest_channel[SLIRP_WRITE_SIDE] = -1;

    return net_slirp_init(usr);
}
#endif

#if RV32EMU_NET_HAS_TAP
static const char *netdev_linux_default(void)
{
    return "tap";
}
#endif

#if defined(__APPLE__) && RV32EMU_NET_HAS_SLIRP
static const char *netdev_macos_default(void)
{
    return "user";
}
#endif

bool netdev_init(netdev_t *netdev, const char *net_type)
{
    if (!netdev)
        return false;

    netdev_reset(netdev);

#if defined(__APPLE__) && RV32EMU_NET_HAS_SLIRP
    const char *requested = net_type ? net_type : netdev_macos_default();

    if (!strcmp(requested, "user")) {
        netdev->name = "user";
        netdev->type = NETDEV_IMPL_user;
        netdev->op = calloc(1, sizeof(net_user_options_t));
        if (!netdev->op)
            return false;

        if (net_init_user(netdev) < 0) {
            free(netdev->op);
            netdev->op = NULL;
            return false;
        }

        return true;
    }

    rv_log_error("unsupported virtio-net backend on macOS: %s", requested);
    return false;

#elif RV32EMU_NET_HAS_TAP
    const char *requested = net_type ? net_type : netdev_linux_default();

    if (!strcmp(requested, "tap")) {
        netdev->name = "tap";
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

#if RV32EMU_NET_HAS_SLIRP
    if (!strcmp(requested, "user")) {
        netdev->name = "user";
        netdev->type = NETDEV_IMPL_user;
        netdev->op = calloc(1, sizeof(net_user_options_t));
        if (!netdev->op)
            return false;

        if (net_init_user(netdev) < 0) {
            free(netdev->op);
            netdev->op = NULL;
            return false;
        }

        return true;
    }
#endif

    rv_log_error("unsupported virtio-net backend on Linux: %s", requested);
    return false;

#else
    (void) net_type;
    rv_log_error("virtio-net networking is disabled on this host");
    return false;
#endif
}

void netdev_delete(netdev_t *netdev)
{
    if (!netdev || !netdev->op)
        return;

    switch (netdev->type) {
#if RV32EMU_NET_HAS_TAP
    case NETDEV_IMPL_tap: {
        net_tap_options_t *tap = (net_tap_options_t *) netdev->op;
        if (tap->tap_fd >= 0)
            close(tap->tap_fd);
        break;
    }
#endif

#if RV32EMU_NET_HAS_SLIRP
    case NETDEV_IMPL_user:
        net_slirp_cleanup((net_user_options_t *) netdev->op);
        break;
#endif

    default:
        break;
    }

    free(netdev->op);
    netdev_reset(netdev);
}
