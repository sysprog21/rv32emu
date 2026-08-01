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

#if RV32EMU_NET_HAS_TAP || RV32EMU_NET_HAS_SLIRP
typedef int (*netdev_init_fn_t)(netdev_t *netdev);
#endif

static void netdev_reset(netdev_t *netdev)
{
    if (!netdev)
        return;

    netdev->name = NULL;
    netdev->type = NETDEV_IMPL_NONE;
    netdev->op = NULL;
}

#if RV32EMU_NET_HAS_TAP || RV32EMU_NET_HAS_SLIRP
static bool netdev_setup(netdev_t *netdev,
                         const char *name,
                         netdev_impl_t type,
                         size_t options_size,
                         netdev_init_fn_t init_fn)
{
    netdev->name = name;
    netdev->type = type;
    netdev->op = calloc(1, options_size);

    if (!netdev->op) {
        netdev_reset(netdev);
        return false;
    }

    if (init_fn(netdev) < 0) {
        free(netdev->op);
        netdev_reset(netdev);
        return false;
    }

    return true;
}
#endif

#if RV32EMU_NET_HAS_TAP
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

static const char *netdev_default_backend(void)
{
#if RV32EMU_NET_HAS_TAP
    return "tap";
#elif RV32EMU_NET_HAS_SLIRP
    return "user";
#else
    return NULL;
#endif
}

bool netdev_init(netdev_t *netdev, const char *net_type)
{
    if (!netdev)
        return false;

    netdev_reset(netdev);

    const char *requested = net_type ? net_type : netdev_default_backend();
    if (!requested) {
        rv_log_error("no virtio-net backend was compiled");
        return false;
    }

#if RV32EMU_NET_HAS_TAP
    if (!strcmp(requested, "tap")) {
        return netdev_setup(netdev, "tap", NETDEV_IMPL_TAP,
                            sizeof(net_tap_options_t), net_init_tap);
    }
#endif

#if RV32EMU_NET_HAS_SLIRP
    if (!strcmp(requested, "user")) {
        return netdev_setup(netdev, "user", NETDEV_IMPL_USER,
                            sizeof(net_user_options_t), net_init_user);
    }
#endif

    rv_log_error("unsupported virtio-net backend: %s", requested);
    return false;
}

void netdev_delete(netdev_t *netdev)
{
    if (!netdev || !netdev->op)
        return;

    switch (netdev->type) {
#if RV32EMU_NET_HAS_TAP
    case NETDEV_IMPL_TAP: {
        net_tap_options_t *tap = (net_tap_options_t *) netdev->op;
        if (tap->tap_fd >= 0)
            close(tap->tap_fd);
        break;
    }
#endif

#if RV32EMU_NET_HAS_SLIRP
    case NETDEV_IMPL_USER:
        net_slirp_cleanup((net_user_options_t *) netdev->op);
        break;
#endif

    case NETDEV_IMPL_NONE:
    default:
        break;
    }

    free(netdev->op);
    netdev_reset(netdev);
}
