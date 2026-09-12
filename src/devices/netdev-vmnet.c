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
#include <sys/socket.h>
#include <unistd.h>

#include <dispatch/dispatch.h>
#include <vmnet/vmnet.h>

#include "utils.h"

#define VMNET_START_TIMEOUT_NS (10LL * 1000000000LL)

typedef struct {
    uint8_t bytes[6];
} vmnet_mac_t;

static int vmnet_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return -1;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void vmnet_packet_handler(net_vmnet_state_t *state,
                                 const uint8_t *buf,
                                 ssize_t len)
{
    if (!state || !state->running || len <= 0)
        return;

    /*
     * SOCK_DGRAM preserves one Ethernet frame as one message. Both ends of
     * the socketpair are non-blocking, so a full receive queue drops the
     * packet instead of stalling vmnet's serial dispatch queue.
     */
    ssize_t written = send(state->rx_fds[1], buf, (size_t) len, 0);

    if (written < 0) {
        /*
         * A full socket buffer is not a fatal backend error. It simply means
         * the guest is not consuming received packets quickly enough, so drop
         * this frame rather than blocking vmnet's dispatch queue.
         */
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
            return;

        rv_log_error("vmnet: failed to queue received packet: %s",
                     strerror(errno));
        return;
    }

    if (written != len)
        rv_log_error("vmnet: short datagram write: %zd/%zd", written, len);
}

static bool vmnet_store_mac(uint8_t mac[6], xpc_object_t param)
{
    const char *mac_str =
        xpc_dictionary_get_string(param, vmnet_mac_address_key);

    if (!mac_str)
        return false;

    int count = sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0],
                       &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

    if (count != 6) {
        rv_log_error("vmnet: failed to parse MAC address: %s", mac_str);
        return false;
    }

    return true;
}

static vmnet_return_t vmnet_register_packet_callback(net_vmnet_state_t *state,
                                                     interface_ref iface)
{
    return vmnet_interface_set_event_callback(
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

static int vmnet_stop_interface_sync(net_vmnet_state_t *state)
{
    if (!state || !state->iface || !state->queue)
        return 0;

    interface_ref iface = (interface_ref) state->iface;
    dispatch_queue_t queue = (dispatch_queue_t) state->queue;
    dispatch_semaphore_t stop_sem = dispatch_semaphore_create(0);

    if (!stop_sem) {
        rv_log_error("vmnet: failed to create stop semaphore");
        return -1;
    }

    __block vmnet_return_t stop_status = VMNET_FAILURE;

    vmnet_return_t ret =
        vmnet_stop_interface(iface, queue, ^(vmnet_return_t status) {
          stop_status = status;
          dispatch_semaphore_signal(stop_sem);
        });

    if (ret != VMNET_SUCCESS) {
        rv_log_error("vmnet: failed to schedule interface stop: %d", ret);

        /*
         * Drain callbacks that were already queued before returning to the
         * caller. The event callback is cleared before this function is used
         * during normal cleanup.
         */
        dispatch_sync(queue, ^{
                      });

        dispatch_release(stop_sem);
        state->iface = NULL;

        return -1;
    }

    dispatch_semaphore_wait(stop_sem, DISPATCH_TIME_FOREVER);

    /*
     * vmnet callbacks use a serial queue. Enqueueing a synchronous empty block
     * after the stop completion ensures previously queued packet callbacks have
     * finished before their state is released.
     */
    dispatch_sync(queue, ^{
                  });

    dispatch_release(stop_sem);

    state->iface = NULL;

    if (stop_status != VMNET_SUCCESS) {
        rv_log_error("vmnet: failed to stop interface: %d", stop_status);
        return -1;
    }

    return 0;
}

static void vmnet_abort_interface(interface_ref iface, dispatch_queue_t queue)
{
    if (!iface || !queue)
        return;

    vmnet_return_t ret =
        vmnet_stop_interface(iface, queue, ^(vmnet_return_t status) {
          if (status != VMNET_SUCCESS)
              rv_log_error("vmnet: failed to abort interface: %d", status);
        });

    if (ret != VMNET_SUCCESS)
        rv_log_error("vmnet: failed to schedule interface abort: %d", ret);
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

    state->queue = dispatch_queue_create(queue_name, DISPATCH_QUEUE_SERIAL);

    if (!state->queue) {
        rv_log_error("vmnet: failed to create dispatch queue");
        xpc_release(iface_desc);
        return -1;
    }

    dispatch_semaphore_t start_sem = dispatch_semaphore_create(0);

    if (!start_sem) {
        rv_log_error("vmnet: failed to create start semaphore");
        xpc_release(iface_desc);
        return -1;
    }

    __block vmnet_return_t status = VMNET_FAILURE;
    __block vmnet_mac_t mac = {0};

    /*
     * Keep one reference for the completion block. If the bounded wait below
     * times out, the semaphore must remain valid in case vmnet invokes the
     * completion block later.
     */
    dispatch_retain(start_sem);

    interface_ref iface =
        vmnet_start_interface(iface_desc, (dispatch_queue_t) state->queue,
                              ^(vmnet_return_t ret, xpc_object_t param) {
                                status = ret;

                                if (ret == VMNET_SUCCESS)
                                    vmnet_store_mac(mac.bytes, param);

                                dispatch_semaphore_signal(start_sem);
                                dispatch_release(start_sem);
                              });

    /*
     * vmnet_start_interface returning NULL means the request was rejected
     * immediately. In that case no completion handler will signal start_sem.
     */
    if (!iface) {
        rv_log_error("vmnet: failed to start %s interface", mode_name);

        /*
         * Balance both the caller's reference and the reference reserved for
         * the completion block, which will not run for this failure.
         */
        dispatch_release(start_sem);
        dispatch_release(start_sem);

        xpc_release(iface_desc);
        return -1;
    }

    dispatch_time_t deadline =
        dispatch_time(DISPATCH_TIME_NOW, VMNET_START_TIMEOUT_NS);

    if (dispatch_semaphore_wait(start_sem, deadline) != 0) {
        rv_log_error("vmnet: timed out starting %s interface", mode_name);

        /*
         * Drop the caller's semaphore reference. The completion block keeps
         * its own reference so a late callback remains safe.
         *
         * No packet callback has been installed yet, so aborting the interface
         * asynchronously cannot access net_vmnet_state_t after this function
         * returns.
         */
        dispatch_release(start_sem);
        vmnet_abort_interface(iface, (dispatch_queue_t) state->queue);

        xpc_release(iface_desc);
        return -1;
    }

    dispatch_release(start_sem);

    if (status != VMNET_SUCCESS) {
        rv_log_error("vmnet: failed to create %s interface: %d", mode_name,
                     status);

        vmnet_abort_interface(iface, (dispatch_queue_t) state->queue);

        xpc_release(iface_desc);
        return -1;
    }

    /*
     * vmnet_start_interface has returned and the completion handler has
     * finished, so iface is now valid here. Register the packet callback only
     * after this point instead of capturing iface inside the completion block.
     */
    state->iface = iface;
    memcpy(state->mac, mac.bytes, sizeof(state->mac));

    rv_log_warn("vmnet: %s mode started, MAC %02x:%02x:%02x:%02x:%02x:%02x",
                mode_name, state->mac[0], state->mac[1], state->mac[2],
                state->mac[3], state->mac[4], state->mac[5]);

    vmnet_return_t ret = vmnet_register_packet_callback(state, iface);

    if (ret != VMNET_SUCCESS) {
        rv_log_error("vmnet: failed to register packet callback: %d", ret);

        vmnet_stop_interface_sync(state);

        xpc_release(iface_desc);
        return -1;
    }

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

    state->iface = NULL;
    state->queue = NULL;
    state->rx_fds[0] = -1;
    state->rx_fds[1] = -1;
    state->running = false;

    /*
     * Use a datagram socketpair rather than a byte-stream pipe. Each Ethernet
     * frame becomes one datagram, so a failed write cannot leave the receive
     * stream between a length prefix and its payload.
     */
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, state->rx_fds) < 0) {
        rv_log_error("vmnet: failed to create socketpair: %s", strerror(errno));
        return -1;
    }

    /*
     * Both sides must be non-blocking. In particular, the write side runs from
     * vmnet's serial callback queue and must never block when the guest is not
     * consuming RX buffers.
     */
    if (vmnet_set_nonblock(state->rx_fds[0]) < 0 ||
        vmnet_set_nonblock(state->rx_fds[1]) < 0) {
        rv_log_error("vmnet: failed to set socketpair non-blocking: %s",
                     strerror(errno));

        close(state->rx_fds[0]);
        close(state->rx_fds[1]);

        state->rx_fds[0] = -1;
        state->rx_fds[1] = -1;

        return -1;
    }

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
        if (state->queue) {
            dispatch_release((dispatch_queue_t) state->queue);
            state->queue = NULL;
        }

        close(state->rx_fds[0]);
        close(state->rx_fds[1]);

        state->rx_fds[0] = -1;
        state->rx_fds[1] = -1;

        return -1;
    }

    state->running = true;

    return 0;
}

ssize_t net_vmnet_read(net_vmnet_state_t *state, uint8_t *buf, size_t len)
{
    if (!state || !buf) {
        errno = EINVAL;
        return -1;
    }

    /*
     * One recv() consumes exactly one datagram. If no packet is available the
     * non-blocking socket returns EAGAIN/EWOULDBLOCK.
     */
    ssize_t n = recv(state->rx_fds[0], buf, len, 0);

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        rv_log_error("vmnet: failed to receive packet: %s", strerror(errno));

    return n;
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

    return state->rx_fds[0];
}

void net_vmnet_cleanup(net_vmnet_state_t *state)
{
    if (!state)
        return;

    /*
     * Packet callbacks may already be queued when shutdown begins. Mark the
     * backend stopped first so such callbacks discard their packets rather
     * than writing more data into the socketpair.
     */
    state->running = false;

    if (state->iface) {
        interface_ref iface = (interface_ref) state->iface;

        /*
         * Prevent vmnet from scheduling new packet callbacks before stopping
         * the interface.
         */
        vmnet_return_t ret = vmnet_interface_set_event_callback(
            iface, VMNET_INTERFACE_PACKETS_AVAILABLE, NULL, NULL);

        if (ret != VMNET_SUCCESS)
            rv_log_error("vmnet: failed to clear packet callback: %d", ret);

        /*
         * Wait for vmnet_stop_interface's completion and then drain the serial
         * queue before any state used by packet callbacks is destroyed.
         */
        vmnet_stop_interface_sync(state);
    }

    if (state->rx_fds[0] >= 0) {
        close(state->rx_fds[0]);
        state->rx_fds[0] = -1;
    }

    if (state->rx_fds[1] >= 0) {
        close(state->rx_fds[1]);
        state->rx_fds[1] = -1;
    }

    if (state->queue) {
        dispatch_release((dispatch_queue_t) state->queue);
        state->queue = NULL;
    }
}

#endif /* RV32EMU_NET_HAS_VMNET */
