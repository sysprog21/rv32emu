/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include "netdev.h"

#if RV32EMU_NET_HAS_SLIRP

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "minislirp/src/libslirp.h"
#include "utils.h"

typedef struct {
    Slirp *slirp;
    SlirpTimerId id;
    void *cb_opaque;
    int64_t expire_time_ms;
} rv_slirp_timer_t;

static int64_t monotonic_clock_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int64_t monotonic_clock_ms(void)
{
    return monotonic_clock_ns() / 1000000LL;
}

static ssize_t net_slirp_send_packet(const void *buf, size_t len, void *opaque)
{
    net_user_options_t *usr = (net_user_options_t *) opaque;

    if (!usr || usr->guest_to_host_channel[SLIRP_WRITE_SIDE] < 0)
        return 0;

    return write(usr->host_to_guest_channel[SLIRP_WRITE_SIDE], buf, len);
}

static void net_slirp_guest_error(const char *msg, void *opaque)
{
    (void) opaque;

    rv_log_warn("SLIRP guest error: %s", msg ? msg : "(null)");
}

static int64_t net_slirp_clock_get_ns(void *opaque)
{
    (void) opaque;

    return monotonic_clock_ns();
}

static void net_slirp_init_completed(Slirp *slirp, void *opaque)
{
    net_user_options_t *usr = (net_user_options_t *) opaque;

    usr->slirp = slirp;
}

static void *net_slirp_timer_new_opaque(SlirpTimerId id,
                                        void *cb_opaque,
                                        void *opaque)
{
    net_user_options_t *usr = (net_user_options_t *) opaque;
    rv_slirp_timer_t *timer = calloc(1, sizeof(*timer));

    if (!timer)
        return NULL;

    timer->slirp = (Slirp *) usr->slirp;
    timer->id = id;
    timer->cb_opaque = cb_opaque;
    timer->expire_time_ms = -1;

    usr->timer = timer;
    return timer;
}

static void net_slirp_timer_free(void *timer, void *opaque)
{
    net_user_options_t *usr = (net_user_options_t *) opaque;

    if (usr && usr->timer == timer)
        usr->timer = NULL;

    free(timer);
}

static void net_slirp_timer_mod(void *timer, int64_t expire_time, void *opaque)
{
    (void) opaque;

    if (!timer)
        return;

    rv_slirp_timer_t *t = (rv_slirp_timer_t *) timer;
    t->expire_time_ms = expire_time;
}

static void net_slirp_register_poll_sock(slirp_os_socket fd, void *opaque)
{
    (void) fd;
    (void) opaque;
}

static void net_slirp_unregister_poll_sock(slirp_os_socket fd, void *opaque)
{
    (void) fd;
    (void) opaque;
}

static void net_slirp_notify(void *opaque)
{
    (void) opaque;
}

static const SlirpCb slirp_cb = {
    .send_packet = net_slirp_send_packet,
    .guest_error = net_slirp_guest_error,
    .clock_get_ns = net_slirp_clock_get_ns,
    .init_completed = net_slirp_init_completed,
    .timer_new_opaque = net_slirp_timer_new_opaque,
    .timer_free = net_slirp_timer_free,
    .timer_mod = net_slirp_timer_mod,
    .register_poll_socket = net_slirp_register_poll_sock,
    .unregister_poll_socket = net_slirp_unregister_poll_sock,
    .notify = net_slirp_notify,
};

static short slirp_to_poll_events(int events)
{
    short ret = 0;

    if (events & SLIRP_POLL_IN)
        ret |= POLLIN;
    if (events & SLIRP_POLL_OUT)
        ret |= POLLOUT;
    if (events & SLIRP_POLL_PRI)
        ret |= POLLPRI;
    if (events & SLIRP_POLL_ERR)
        ret |= POLLERR;
    if (events & SLIRP_POLL_HUP)
        ret |= POLLHUP;

    return ret;
}

static int poll_to_slirp_revents(short revents)
{
    int ret = 0;

    if (revents & POLLIN)
        ret |= SLIRP_POLL_IN;
    if (revents & POLLOUT)
        ret |= SLIRP_POLL_OUT;
    if (revents & POLLPRI)
        ret |= SLIRP_POLL_PRI;
    if (revents & POLLERR)
        ret |= SLIRP_POLL_ERR;
    if (revents & POLLHUP)
        ret |= SLIRP_POLL_HUP;

    return ret;
}

static int slirp_add_poll_socket(slirp_os_socket fd, int events, void *opaque)
{
    net_user_options_t *usr = (net_user_options_t *) opaque;

    if (usr->pfd_len >= usr->pfd_size) {
        int new_size = usr->pfd_size ? usr->pfd_size * 2 : 16;
        struct pollfd *new_pfd =
            realloc(usr->pfd, (size_t) new_size * sizeof(*usr->pfd));

        if (!new_pfd)
            return -1;

        usr->pfd = new_pfd;
        usr->pfd_size = new_size;
    }

    int idx = usr->pfd_len++;
    usr->pfd[idx].fd = fd;
    usr->pfd[idx].events = slirp_to_poll_events(events);
    usr->pfd[idx].revents = 0;

    return idx;
}

static int slirp_get_revents(int idx, void *opaque)
{
    net_user_options_t *usr = (net_user_options_t *) opaque;

    if (idx < 0 || idx >= usr->pfd_len)
        return 0;

    return poll_to_slirp_revents(usr->pfd[idx].revents);
}

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return -1;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_fd(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static Slirp *slirp_create(net_user_options_t *usr)
{
    SlirpConfig cfg;

    memset(&cfg, 0, sizeof(cfg));

    cfg.version = SLIRP_CHECK_VERSION(4, 8, 0)   ? 6
                  : SLIRP_CHECK_VERSION(4, 7, 0) ? 4
                                                 : 1;
    cfg.restricted = 0;

    cfg.in_enabled = true;
    inet_pton(AF_INET, "10.0.2.0", &cfg.vnetwork);
    inet_pton(AF_INET, "255.255.255.0", &cfg.vnetmask);
    inet_pton(AF_INET, "10.0.2.2", &cfg.vhost);
    inet_pton(AF_INET, "10.0.2.15", &cfg.vdhcp_start);
    inet_pton(AF_INET, "10.0.2.3", &cfg.vnameserver);

    cfg.in6_enabled = true;
    inet_pton(AF_INET6, "fd00::", &cfg.vprefix_addr6);
    cfg.vprefix_len = 64;
    inet_pton(AF_INET6, "fd00::2", &cfg.vhost6);
    inet_pton(AF_INET6, "fd00::3", &cfg.vnameserver6);

    cfg.vhostname = "slirp";
    cfg.if_mtu = 1500;
    cfg.if_mru = 1500;
    cfg.disable_host_loopback = false;

    return slirp_new(&cfg, &slirp_cb, usr);
}

int net_slirp_init(net_user_options_t *usr)
{
    if (!usr)
        return -1;

    usr->slirp = slirp_create(usr);
    if (!usr->slirp) {
        rv_log_error("failed to create SLIRP instance");
        return -1;
    }

    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, usr->host_to_guest_channel) < 0) {
        rv_log_error("failed to create SLIRP host-to-guest channel: %s",
                     strerror(errno));
        goto fail;
    }

    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, usr->guest_to_host_channel) < 0) {
        rv_log_error("failed to create SLIRP guest-to-host channel: %s",
                     strerror(errno));
        goto fail;
    }

    if (set_nonblock(usr->guest_to_host_channel[SLIRP_READ_SIDE]) < 0 ||
        set_nonblock(usr->guest_to_host_channel[SLIRP_WRITE_SIDE]) < 0 ||
        set_nonblock(usr->host_to_guest_channel[SLIRP_READ_SIDE]) < 0 ||
        set_nonblock(usr->host_to_guest_channel[SLIRP_WRITE_SIDE]) < 0) {
        rv_log_error("failed to set SLIRP channels to non-blocking mode: %s",
                     strerror(errno));
        goto fail;
    }

    return 0;

fail:
    net_slirp_cleanup(usr);
    return -1;
}

void net_slirp_cleanup(net_user_options_t *usr)
{
    if (!usr)
        return;

    close_fd(&usr->guest_to_host_channel[SLIRP_READ_SIDE]);
    close_fd(&usr->guest_to_host_channel[SLIRP_WRITE_SIDE]);
    close_fd(&usr->host_to_guest_channel[SLIRP_READ_SIDE]);
    close_fd(&usr->host_to_guest_channel[SLIRP_WRITE_SIDE]);

    if (usr->slirp) {
        slirp_cleanup((Slirp *) usr->slirp);
        usr->slirp = NULL;
    }

    free(usr->pfd);
    usr->pfd = NULL;
    usr->pfd_len = 0;
    usr->pfd_size = 0;
    usr->timer = NULL;
}

int net_slirp_read(net_user_options_t *usr)
{
    uint8_t pkt[SLIRP_PKT_MAX];
    ssize_t plen =
        recv(usr->guest_to_host_channel[SLIRP_READ_SIDE], pkt, sizeof(pkt), 0);

    if (plen < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;

        rv_log_error("failed to read packet from virtio-net for SLIRP: %s",
                     strerror(errno));
        return -1;
    }

    if (plen == 0)
        return 0;

    slirp_input((Slirp *) usr->slirp, pkt, (int) plen);
    return (int) plen;
}

static void net_slirp_handle_timer(net_user_options_t *usr)
{
    rv_slirp_timer_t *timer = (rv_slirp_timer_t *) usr->timer;

    if (!timer || timer->expire_time_ms < 0)
        return;

    if (timer->expire_time_ms <= monotonic_clock_ms()) {
        timer->expire_time_ms = -1;
        slirp_handle_timer((Slirp *) usr->slirp, timer->id, timer->cb_opaque);
    }
}

int net_slirp_poll(net_user_options_t *usr)
{
    if (!usr || !usr->slirp)
        return -1;

    net_slirp_handle_timer(usr);

    usr->pfd_len = 0;

    uint32_t timeout = 0;
    slirp_pollfds_fill_socket((Slirp *) usr->slirp, &timeout,
                              slirp_add_poll_socket, usr);

    if (usr->guest_to_host_channel[SLIRP_READ_SIDE] >= 0)
        slirp_add_poll_socket(usr->guest_to_host_channel[SLIRP_READ_SIDE],
                              SLIRP_POLL_IN | SLIRP_POLL_HUP, usr);

    if (usr->pfd_len == 0)
        return 0;

    int ret = poll(usr->pfd, (nfds_t) usr->pfd_len, 0);
    if (ret < 0) {
        if (errno == EINTR)
            return 0;

        rv_log_error("SLIRP poll failed: %s", strerror(errno));
        return -1;
    }

    if (usr->guest_to_host_channel[SLIRP_READ_SIDE] >= 0) {
        for (int i = 0; i < usr->pfd_len; i++) {
            if (usr->pfd[i].fd == usr->guest_to_host_channel[SLIRP_READ_SIDE] &&
                (usr->pfd[i].revents & POLLIN)) {
                net_slirp_read(usr);
                break;
            }
        }
    }

    slirp_pollfds_poll((Slirp *) usr->slirp, ret < 0, slirp_get_revents, usr);
    return 0;
}

#endif /* RV32EMU_NET_HAS_SLIRP */
