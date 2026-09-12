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

#include "feature.h"

/*
 * Networking backends:
 *
 * Linux:
 *   - tap  : kernel TAP device
 *   - user : user-mode SLIRP
 *
 * macOS:
 *   - user  : user-mode SLIRP
 *   - vmnet : Apple vmnet.framework
 *
 * Emscripten:
 *   - virtio-net networking backends are disabled
 */
#if RV32_HAS(VIRTIO_NET_TAP) && defined(__linux__) && !defined(__EMSCRIPTEN__)
#define RV32EMU_NET_HAS_TAP 1
#else
#define RV32EMU_NET_HAS_TAP 0
#endif

#if RV32_HAS(VIRTIO_NET_USER) && !defined(__EMSCRIPTEN__)
#define RV32EMU_NET_HAS_SLIRP 1
#else
#define RV32EMU_NET_HAS_SLIRP 0
#endif

/*
 * vmnet.framework uses Apple Blocks. Keep the backend restricted to
 * macOS builds using Clang.
 */
#if RV32_HAS(VIRTIO_NET_VMNET) && defined(__APPLE__) && defined(__clang__) && \
    !defined(__EMSCRIPTEN__)
#define RV32EMU_NET_HAS_VMNET 1
#else
#define RV32EMU_NET_HAS_VMNET 0
#endif

typedef struct netdev netdev_t;

typedef enum {
    NETDEV_IMPL_NONE = 0,
#if RV32EMU_NET_HAS_TAP
    NETDEV_IMPL_TAP,
#endif
#if RV32EMU_NET_HAS_SLIRP
    NETDEV_IMPL_USER,
#endif
#if RV32EMU_NET_HAS_VMNET
    NETDEV_IMPL_VMNET,
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

struct rv_slirp_timer;

typedef struct {
    void *slirp;
    int guest_to_host_channel[2];
    int host_to_guest_channel[2];
    struct pollfd *pfd;
    int pfd_len;
    int pfd_size;
    struct rv_slirp_timer *timers;
} net_user_options_t;

int net_slirp_init(net_user_options_t *usr);
void net_slirp_cleanup(net_user_options_t *usr);
int net_slirp_poll(net_user_options_t *usr);
int net_slirp_read(net_user_options_t *usr);
#endif

#if RV32EMU_NET_HAS_VMNET

#include <pthread.h>

#define VMNET_PKT_MAX 2048

typedef struct {
    /*
     * Keep Apple-specific types opaque here so the rest of rv32emu does
     * not need to include vmnet.framework or libdispatch headers.
     */
    void *iface; /* interface_ref */
    void *queue; /* dispatch_queue_t */
    void *sem;   /* dispatch_semaphore_t */

    /*
     * vmnet callbacks are asynchronous. Received Ethernet packets are
     * forwarded into this pipe so virtio-net can poll them.
     */
    int pipe_fds[2];

    uint8_t mac[6];

    pthread_mutex_t lock;
    bool running;
} net_vmnet_state_t;

typedef net_vmnet_state_t net_vmnet_options_t;

/*
 * Keep the three modes implemented by semu.
 *
 * The current rv32emu frontend uses only RV32EMU_VMNET_SHARED for
 * "vnet:vmnet". Host and bridged modes remain available internally for
 * future frontend support.
 */
typedef enum {
    RV32EMU_VMNET_SHARED = 0,
    RV32EMU_VMNET_HOST = 1,
    RV32EMU_VMNET_BRIDGED = 2,
} rv32emu_vmnet_mode_t;

int net_vmnet_init(netdev_t *netdev,
                   rv32emu_vmnet_mode_t mode,
                   const char *iface_name);

ssize_t net_vmnet_read(net_vmnet_state_t *state, uint8_t *buf, size_t len);

ssize_t net_vmnet_write(net_vmnet_state_t *state,
                        const uint8_t *buf,
                        size_t len);

ssize_t net_vmnet_writev(net_vmnet_state_t *state,
                         const struct iovec *iov,
                         size_t iovcnt);

int net_vmnet_get_fd(net_vmnet_state_t *state);

void net_vmnet_cleanup(net_vmnet_state_t *state);

#endif

struct netdev {
    const char *name;
    netdev_impl_t type;
    void *op;
};

bool netdev_init(netdev_t *netdev, const char *net_type);

void netdev_delete(netdev_t *netdev);
