/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/*
 * vmnet.framework based network backend for macOS.
 *
 * Three operation modes are implemented:
 *
 *   RV32EMU_VMNET_SHARED
 *       Shared networking with NAT.
 *
 *   RV32EMU_VMNET_HOST
 *       Host-only private networking.
 *
 *   RV32EMU_VMNET_BRIDGED
 *       Bridged networking through a host physical interface.
 *
 * The current rv32emu frontend selects shared mode for "vnet:vmnet".
 */

#include "netdev.h"

#if RV32EMU_NET_HAS_VMNET

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dispatch/dispatch.h>
#include <vmnet/vmnet.h>

#include "utils.h"

static void vmnet_packet_handler(net_vmnet_state_t *state,
                                 uint8_t *buf,
                                 ssize_t len)
{
    if (len <= 0)
        return;

    pthread_mutex_lock(&state->lock);

    /*
     * Preserve the packet boundary while passing packets through a byte
     * stream pipe. Store the packet length first and then the packet data.
     */
    uint32_t pkt_len = (uint32_t) len;

    if (write(state->pipe_fds[1], &pkt_len, sizeof(pkt_len)) !=
        (ssize_t) sizeof(pkt_len)) {
        rv_log_error("vmnet: failed to write packet size to pipe");
        pthread_mutex_unlock(&state->lock);
        return;
    }

    ssize_t written = write(state->pipe_fds[1], buf, (size_t) len);

    if (written != len) {
        rv_log_error("vmnet: failed to write packet to pipe: %zd/%zd", written,
                     len);
    }

    pthread_mutex_unlock(&state->lock);
}

static void vmnet_store_mac(net_vmnet_state_t *state, xpc_object_t param)
{
    const char *mac_str =
        xpc_dictionary_get_string(param, vmnet_mac_address_key);

    if (!mac_str)
        return;

    int count = sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &state->mac[0],
                       &state->mac[1], &state->mac[2], &state->mac[3],
                       &state->mac[4], &state->mac[5]);

    if (count != 6)
        rv_log_error("vmnet: failed to parse MAC address: %s", mac_str);
}

static void vmnet_register_packet_callback(net_vmnet_state_t *state,
                                           interface_ref iface)
{
    vmnet_interface_set_event_callback(
        iface, VMNET_INTERFACE_PACKETS_AVAILABLE,
        (dispatch_queue_t) state->queue,
        ^(interface_event_t event_id, xpc_object_t event) {
          (void) event_id;
          (void) event;

          struct vmpktdesc pkts[32];
          uint8_t bufs[32][VMNET_PKT_MAX];
          struct iovec iovs[32];

          int pkt_cnt = (int) (sizeof(pkts) / sizeof(pkts[0]));

          for (int i = 0; i < pkt_cnt; i++) {
              iovs[i].iov_base = bufs[i];
              iovs[i].iov_len = VMNET_PKT_MAX;

              pkts[i].vm_pkt_size = VMNET_PKT_MAX;
              pkts[i].vm_pkt_iov = &iovs[i];
              pkts[i].vm_pkt_iovcnt = 1;
              pkts[i].vm_flags = 0;
          }

          int received = pkt_cnt;

          vmnet_return_t ret = vmnet_read(iface, pkts, &received);

          if (ret != VMNET_SUCCESS) {
              rv_log_error("vmnet: read failed: %d", ret);
              return;
          }

          for (int i = 0; i < received; i++) {
              vmnet_packet_handler(state, bufs[i],
                                   (ssize_t) pkts[i].vm_pkt_size);
          }
        });
}

static int vmnet_init_interface(net_vmnet_state_t *state,
                                uint64_t mode,
                                const char *mode_name,
                                const char *queue_name,
                                const char *iface_name)
{
    xpc_object_t iface_desc = xpc_dictionary_create(NULL, NULL, 0);

    if (!iface_desc) {
        rv_log_error("vmnet: failed to create interface descriptor");
        return -1;
    }

    xpc_dictionary_set_uint64(iface_desc, vmnet_operation_mode_key, mode);

    /*
     * Bridged mode needs the host physical interface to bridge to.
     * Shared and host modes leave iface_name as NULL.
     */
    if (mode == VMNET_BRIDGED_MODE && iface_name && iface_name[0] != '\0') {
        xpc_dictionary_set_string(iface_desc, vmnet_shared_interface_name_key,
                                  iface_name);
    }

    state->sem = dispatch_semaphore_create(0);
    state->queue = dispatch_queue_create(queue_name, DISPATCH_QUEUE_SERIAL);

    if (!state->sem || !state->queue) {
        rv_log_error("vmnet: failed to create dispatch objects");
        xpc_release(iface_desc);
        return -1;
    }

    __block interface_ref iface = NULL;
    __block vmnet_return_t status = VMNET_FAILURE;

    iface = vmnet_start_interface(
        iface_desc, (dispatch_queue_t) state->queue,
        ^(vmnet_return_t ret, xpc_object_t param) {
          status = ret;

          if (ret == VMNET_SUCCESS) {
              vmnet_store_mac(state, param);

              rv_log_warn(
                  "vmnet: %s mode started, MAC "
                  "%02x:%02x:%02x:%02x:%02x:%02x",
                  mode_name, state->mac[0], state->mac[1], state->mac[2],
                  state->mac[3], state->mac[4], state->mac[5]);

              vmnet_register_packet_callback(state, iface);
          }

          dispatch_semaphore_signal((dispatch_semaphore_t) state->sem);
        });

    dispatch_semaphore_wait((dispatch_semaphore_t) state->sem,
                            DISPATCH_TIME_FOREVER);

    if (status != VMNET_SUCCESS) {
        rv_log_error("vmnet: failed to create %s interface: %d", mode_name,
                     status);

        xpc_release(iface_desc);
        return -1;
    }

    state->iface = iface;

    xpc_release(iface_desc);

    return 0;
}

static int vmnet_init_shared(net_vmnet_state_t *state)
{
    return vmnet_init_interface(state, VMNET_SHARED_MODE, "shared",
                                "org.rv32emu.vmnet.shared", NULL);
}

static int vmnet_init_host(net_vmnet_state_t *state)
{
    return vmnet_init_interface(state, VMNET_HOST_MODE, "host",
                                "org.rv32emu.vmnet.host", NULL);
}

static int vmnet_init_bridged(net_vmnet_state_t *state, const char *iface_name)
{
    return vmnet_init_interface(state, VMNET_BRIDGED_MODE, "bridged",
                                "org.rv32emu.vmnet.bridged", iface_name);
}

static void vmnet_set_errno(vmnet_return_t ret)
{
    switch (ret) {
    case VMNET_BUFFER_EXHAUSTED:
        errno = EAGAIN;
        break;

    case VMNET_PACKET_TOO_BIG:
        errno = EMSGSIZE;
        break;

    default:
        errno = EIO;
        break;
    }
}

int net_vmnet_init(netdev_t *netdev,
                   rv32emu_vmnet_mode_t mode,
                   const char *iface_name)
{
    if (!netdev || !netdev->op)
        return -1;

    net_vmnet_state_t *state = (net_vmnet_state_t *) netdev->op;

    state->pipe_fds[0] = -1;
    state->pipe_fds[1] = -1;

    if (pipe(state->pipe_fds) < 0) {
        rv_log_error("vmnet: failed to create pipe: %s", strerror(errno));
        return -1;
    }

    /*
     * virtio-net polls the read side. Do not block when no received packet
     * is currently available.
     */
    int flags = fcntl(state->pipe_fds[0], F_GETFL, 0);

    if (flags < 0 ||
        fcntl(state->pipe_fds[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        rv_log_error("vmnet: failed to set pipe non-blocking mode: %s",
                     strerror(errno));

        close(state->pipe_fds[0]);
        close(state->pipe_fds[1]);

        state->pipe_fds[0] = -1;
        state->pipe_fds[1] = -1;

        return -1;
    }

    int mutex_ret = pthread_mutex_init(&state->lock, NULL);

    if (mutex_ret != 0) {
        rv_log_error("vmnet: failed to initialize mutex: %s",
                     strerror(mutex_ret));

        close(state->pipe_fds[0]);
        close(state->pipe_fds[1]);

        state->pipe_fds[0] = -1;
        state->pipe_fds[1] = -1;

        return -1;
    }

    state->running = true;

    int ret = -1;

    switch (mode) {
    case RV32EMU_VMNET_SHARED:
        ret = vmnet_init_shared(state);
        break;

    case RV32EMU_VMNET_HOST:
        ret = vmnet_init_host(state);
        break;

    case RV32EMU_VMNET_BRIDGED:
        ret = vmnet_init_bridged(state, iface_name);
        break;

    default:
        rv_log_error("vmnet: unknown operation mode: %d", (int) mode);
        break;
    }

    if (ret < 0) {
        state->running = false;

        if (state->queue) {
            dispatch_release((dispatch_queue_t) state->queue);
            state->queue = NULL;
        }

        if (state->sem) {
            dispatch_release((dispatch_semaphore_t) state->sem);
            state->sem = NULL;
        }

        close(state->pipe_fds[0]);
        close(state->pipe_fds[1]);

        state->pipe_fds[0] = -1;
        state->pipe_fds[1] = -1;

        pthread_mutex_destroy(&state->lock);

        return -1;
    }

    return 0;
}

ssize_t net_vmnet_read(net_vmnet_state_t *state, uint8_t *buf, size_t len)
{
    if (!state || !buf) {
        errno = EINVAL;
        return -1;
    }

    uint32_t pkt_len;

    ssize_t n = read(state->pipe_fds[0], &pkt_len, sizeof(pkt_len));

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;

        rv_log_error("vmnet: failed to read packet size: %s", strerror(errno));

        return -1;
    }

    if (n != (ssize_t) sizeof(pkt_len)) {
        rv_log_error("vmnet: partial read of packet size");

        errno = EIO;
        return -1;
    }

    if (pkt_len > len) {
        rv_log_error("vmnet: packet too large: %u > %zu", pkt_len, len);

        /*
         * The packet cannot fit into the supplied buffer. Drain it to
         * preserve framing for the following packet.
         */
        uint8_t tmp[VMNET_PKT_MAX];
        size_t remaining = pkt_len;

        while (remaining > 0) {
            size_t chunk = remaining > sizeof(tmp) ? sizeof(tmp) : remaining;

            ssize_t drained = read(state->pipe_fds[0], tmp, chunk);

            if (drained < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;

                break;
            }

            if (drained == 0)
                break;

            remaining -= (size_t) drained;
        }

        errno = EMSGSIZE;
        return -1;
    }

    ssize_t total = 0;

    while ((size_t) total < pkt_len) {
        n = read(state->pipe_fds[0], buf + total, pkt_len - (size_t) total);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;

            rv_log_error("vmnet: failed to read packet data: %s",
                         strerror(errno));

            return -1;
        }

        if (n == 0) {
            errno = EIO;
            return -1;
        }

        total += n;
    }

    return total;
}

ssize_t net_vmnet_write(net_vmnet_state_t *state,
                        const uint8_t *buf,
                        size_t len)
{
    if (!state || !buf || !state->running || !state->iface) {
        errno = ENODEV;
        return -1;
    }

    struct iovec iov = {
        .iov_base = (void *) buf,
        .iov_len = len,
    };

    struct vmpktdesc pkt = {
        .vm_pkt_size = len,
        .vm_pkt_iov = &iov,
        .vm_pkt_iovcnt = 1,
        .vm_flags = 0,
    };

    int pkt_cnt = 1;

    vmnet_return_t ret =
        vmnet_write((interface_ref) state->iface, &pkt, &pkt_cnt);

    if (ret != VMNET_SUCCESS) {
        vmnet_set_errno(ret);
        return -1;
    }

    if (pkt_cnt <= 0) {
        errno = EAGAIN;
        return -1;
    }

    return (ssize_t) len;
}

ssize_t net_vmnet_writev(net_vmnet_state_t *state,
                         const struct iovec *iov,
                         size_t iovcnt)
{
    if (!state || !iov || !state->running || !state->iface) {
        errno = ENODEV;
        return -1;
    }

    size_t total_len = 0;

    for (size_t i = 0; i < iovcnt; i++)
        total_len += iov[i].iov_len;

    struct vmpktdesc pkt = {
        .vm_pkt_size = total_len,
        .vm_pkt_iov = (struct iovec *) iov,
        .vm_pkt_iovcnt = iovcnt,
        .vm_flags = 0,
    };

    int pkt_cnt = 1;

    vmnet_return_t ret =
        vmnet_write((interface_ref) state->iface, &pkt, &pkt_cnt);

    if (ret != VMNET_SUCCESS) {
        vmnet_set_errno(ret);
        return -1;
    }

    if (pkt_cnt <= 0) {
        errno = EAGAIN;
        return -1;
    }

    return (ssize_t) total_len;
}

int net_vmnet_get_fd(net_vmnet_state_t *state)
{
    if (!state)
        return -1;

    return state->pipe_fds[0];
}

void net_vmnet_cleanup(net_vmnet_state_t *state)
{
    if (!state)
        return;

    state->running = false;

    if (state->iface) {
        vmnet_stop_interface(
            (interface_ref) state->iface, (dispatch_queue_t) state->queue,
            ^(vmnet_return_t ret) {
              if (ret != VMNET_SUCCESS) {
                  rv_log_error("vmnet: failed to stop interface: %d", ret);
              }
            });

        state->iface = NULL;
    }

    if (state->queue) {
        dispatch_release((dispatch_queue_t) state->queue);
        state->queue = NULL;
    }

    if (state->sem) {
        dispatch_release((dispatch_semaphore_t) state->sem);
        state->sem = NULL;
    }

    if (state->pipe_fds[0] >= 0) {
        close(state->pipe_fds[0]);
        state->pipe_fds[0] = -1;
    }

    if (state->pipe_fds[1] >= 0) {
        close(state->pipe_fds[1]);
        state->pipe_fds[1] = -1;
    }

    pthread_mutex_destroy(&state->lock);
}

#endif /* RV32EMU_NET_HAS_VMNET */
