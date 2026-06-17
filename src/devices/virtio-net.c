/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#include "utils.h"
#include "virtio.h"

#define VNET_FEATURES_0 0
#define VNET_FEATURES_1 1 /* VIRTIO_F_VERSION_1 */

#define VNET_QUEUE_NUM_MAX 1024
#define VNET_QUEUE_COUNT 2
#define VNET_QUEUE (vnet->queues[vnet->queue_sel])

enum {
    VNET_QUEUE_RX = 0,
    VNET_QUEUE_TX = 1,
};

#define VNET_LINK_UP 1
#define VNET_HDR_SIZE 12

typedef struct __attribute__((packed)) {
    uint8_t mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
} virtio_net_config_t;

static size_t vnet_min(size_t a, size_t b)
{
    return a < b ? a : b;
}

static void virtio_net_set_fail(virtio_net_state_t *vnet)
{
    vnet->status |= VIRTIO_STATUS_DEVICE_NEEDS_RESET;
    if (vnet->status & VIRTIO_STATUS_DRIVER_OK)
        vnet->interrupt_status |= VIRTIO_INT_CONF_CHANGE;
}

static bool vnet_guest_range_ok(uint64_t addr, uint64_t len)
{
    if (len == 0)
        return true;

    if (addr >= MEM_SIZE)
        return false;

    if (len > MEM_SIZE - addr)
        return false;

    return true;
}

static bool vnet_word_range_ok(uint32_t word_index, uint64_t words)
{
    uint64_t byte_addr = (uint64_t) word_index * sizeof(uint32_t);
    uint64_t byte_len = words * sizeof(uint32_t);

    return vnet_guest_range_ok(byte_addr, byte_len);
}

static bool vnet_check_word_range(virtio_net_state_t *vnet,
                                  uint32_t word_index,
                                  uint64_t words)
{
    if (!vnet_word_range_ok(word_index, words)) {
        virtio_net_set_fail(vnet);
        return false;
    }

    return true;
}

static inline uint32_t vnet_preprocess(virtio_net_state_t *vnet, uint32_t addr)
{
#if MEM_SIZE < 0x100000000ULL
    if ((addr >= MEM_SIZE) || (addr & 0b11)) {
#else
    if (addr & 0b11) {
#endif
        virtio_net_set_fail(vnet);
        return 0;
    }

    return addr >> 2;
}

static void virtio_net_update_status(virtio_net_state_t *vnet, uint32_t status)
{
    vnet->status |= status;
    if (status)
        return;

    /* Reset while preserving environment-owned fields. */
    netdev_t peer = vnet->peer;
    uint32_t *ram = vnet->ram;
    void *priv = vnet->priv;

    memset(vnet, 0, sizeof(*vnet));

    vnet->peer = peer;
    vnet->ram = ram;
    vnet->priv = priv;
    vnet->queues[VNET_QUEUE_RX].fd_ready = true;
    vnet->queues[VNET_QUEUE_TX].fd_ready = true;
}

static bool vnet_iovec_write(struct iovec **vecs,
                             size_t *nvecs,
                             const uint8_t *src,
                             size_t n)
{
    while (n > 0 && *nvecs > 0) {
        size_t to_copy = vnet_min(n, (*vecs)->iov_len);

        memcpy((*vecs)->iov_base, src, to_copy);
        src += to_copy;
        n -= to_copy;

        (*vecs)->iov_base = (void *) ((uintptr_t) (*vecs)->iov_base + to_copy);
        (*vecs)->iov_len -= to_copy;

        if ((*vecs)->iov_len == 0) {
            (*vecs)++;
            (*nvecs)--;
        }
    }

    return n > 0;
}

static bool vnet_iovec_read(struct iovec **vecs,
                            size_t *nvecs,
                            uint8_t *dst,
                            size_t n)
{
    while (n > 0 && *nvecs > 0) {
        size_t to_copy = vnet_min(n, (*vecs)->iov_len);

        memcpy(dst, (*vecs)->iov_base, to_copy);
        dst += to_copy;
        n -= to_copy;

        (*vecs)->iov_base = (void *) ((uintptr_t) (*vecs)->iov_base + to_copy);
        (*vecs)->iov_len -= to_copy;

        if ((*vecs)->iov_len == 0) {
            (*vecs)++;
            (*nvecs)--;
        }
    }

    return n > 0;
}

static bool vnet_build_iovs(virtio_net_state_t *vnet,
                            virtio_net_queue_t *queue,
                            uint16_t buffer_idx,
                            bool device_writes,
                            struct iovec *iovs,
                            size_t *niovs)
{
    uint32_t *ram = vnet->ram;
    uint16_t desc_idx = buffer_idx;

    *niovs = 0;

    if (queue->queue_num == 0 || buffer_idx >= queue->queue_num) {
        virtio_net_set_fail(vnet);
        return false;
    }

    for (uint32_t iter = 0; iter < queue->queue_num; iter++) {
        if (desc_idx >= queue->queue_num) {
            virtio_net_set_fail(vnet);
            return false;
        }

        uint32_t desc_addr = queue->queue_desc + desc_idx * 4;
        if (!vnet_check_word_range(vnet, desc_addr, 4))
            return false;

        const struct virtq_desc *desc =
            (const struct virtq_desc *) &ram[desc_addr];

        bool writable = !!(desc->flags & VIRTIO_DESC_F_WRITE);
        if (writable != device_writes) {
            virtio_net_set_fail(vnet);
            return false;
        }

        if (!vnet_guest_range_ok(desc->addr, desc->len)) {
            virtio_net_set_fail(vnet);
            return false;
        }

        iovs[*niovs].iov_base = (void *) ((uintptr_t) ram + desc->addr);
        iovs[*niovs].iov_len = desc->len;
        (*niovs)++;

        if (!(desc->flags & VIRTIO_DESC_F_NEXT))
            return true;

        desc_idx = desc->next;
    }

    /* Descriptor chain loop or too long chain. */
    virtio_net_set_fail(vnet);
    return false;
}

static ssize_t vnet_handle_read(netdev_t *netdev,
                                virtio_net_queue_t *queue,
                                struct iovec *iovs,
                                size_t niovs)
{
    switch (netdev->type) {
    case NETDEV_IMPL_tap: {
        net_tap_options_t *tap = (net_tap_options_t *) netdev->op;
        ssize_t plen = readv(tap->tap_fd, iovs, niovs);

        if (plen < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            queue->fd_ready = false;
            return -1;
        }

        if (plen < 0) {
            rv_log_error("virtio-net: could not read packet: %s",
                         strerror(errno));
            return -1;
        }

        return plen;
    }
    default:
        return -1;
    }
}

static ssize_t vnet_handle_write(netdev_t *netdev,
                                 virtio_net_queue_t *queue,
                                 struct iovec *iovs,
                                 size_t niovs)
{
    switch (netdev->type) {
    case NETDEV_IMPL_tap: {
        net_tap_options_t *tap = (net_tap_options_t *) netdev->op;
        ssize_t plen = writev(tap->tap_fd, iovs, niovs);

        if (plen < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            queue->fd_ready = false;
            return -1;
        }

        if (plen < 0) {
            rv_log_error("virtio-net: could not write packet: %s",
                         strerror(errno));
            return -1;
        }

        return plen;
    }
    default:
        return -1;
    }
}

static bool vnet_get_avail_idx(virtio_net_state_t *vnet,
                               virtio_net_queue_t *queue,
                               uint16_t queue_idx,
                               uint16_t *buffer_idx)
{
    uint32_t avail_addr = queue->queue_avail + 1 + queue_idx / 2;

    if (!vnet_check_word_range(vnet, avail_addr, 1))
        return false;

    *buffer_idx = vnet->ram[avail_addr] >> (16 * (queue_idx % 2));

    return true;
}

static bool vnet_put_used_elem(virtio_net_state_t *vnet,
                               virtio_net_queue_t *queue,
                               uint16_t new_used,
                               uint16_t buffer_idx,
                               uint32_t len)
{
    uint32_t used_addr =
        queue->queue_used + 1 + (new_used % queue->queue_num) * 2;

    if (!vnet_check_word_range(vnet, used_addr, 2))
        return false;

    vnet->ram[used_addr] = buffer_idx;
    vnet->ram[used_addr + 1] = len;

    return true;
}

static void virtio_net_try_rx(virtio_net_state_t *vnet)
{
    uint32_t *ram = vnet->ram;
    virtio_net_queue_t *queue = &vnet->queues[VNET_QUEUE_RX];

    if (vnet->status & VIRTIO_STATUS_DEVICE_NEEDS_RESET)
        return;

    if (!queue->fd_ready)
        return;

    if (!((vnet->status & VIRTIO_STATUS_DRIVER_OK) && queue->ready))
        return virtio_net_set_fail(vnet);

    if (queue->queue_num == 0)
        return virtio_net_set_fail(vnet);

    if (!vnet_check_word_range(vnet, queue->queue_avail, 1))
        return;
    if (!vnet_check_word_range(vnet, queue->queue_used, 1))
        return;

    uint16_t new_avail = ram[queue->queue_avail] >> 16;
    if (new_avail - queue->last_avail > (uint16_t) queue->queue_num) {
        rv_log_error("virtio-net: RX available ring size check failed");
        return virtio_net_set_fail(vnet);
    }

    if (queue->last_avail == new_avail)
        return;

    uint16_t new_used = ram[queue->queue_used] >> 16;

    while (queue->last_avail != new_avail) {
        uint16_t queue_idx = queue->last_avail % queue->queue_num;
        uint16_t buffer_idx;

        if (!vnet_get_avail_idx(vnet, queue, queue_idx, &buffer_idx))
            return;

        struct iovec iovs[VNET_QUEUE_NUM_MAX];
        size_t niovs = 0;

        if (!vnet_build_iovs(vnet, queue, buffer_idx, true, iovs, &niovs))
            return;

        struct iovec *cursor = iovs;
        size_t ncursor = niovs;
        uint8_t virtio_header[VNET_HDR_SIZE] = {0};

        /* num_buffers = 1, matching semu's simple RX path. */
        virtio_header[10] = 1;

        if (vnet_iovec_write(&cursor, &ncursor, virtio_header,
                             sizeof(virtio_header))) {
            rv_log_error("virtio-net: RX buffer too small for virtio header");
            return virtio_net_set_fail(vnet);
        }

        ssize_t plen = vnet_handle_read(&vnet->peer, queue, cursor, ncursor);
        if (plen < 0)
            break;

        if (!vnet_put_used_elem(vnet, queue, new_used, buffer_idx,
                                (uint32_t) plen + sizeof(virtio_header)))
            return;

        queue->last_avail++;
        new_used++;
    }

    ram[queue->queue_used] &= MASK(16);
    ram[queue->queue_used] |= ((uint32_t) new_used) << 16;

    if (!(ram[queue->queue_avail] & 1))
        vnet->interrupt_status |= VIRTIO_INT_USED_RING;
}

static void virtio_net_try_tx(virtio_net_state_t *vnet)
{
    uint32_t *ram = vnet->ram;
    virtio_net_queue_t *queue = &vnet->queues[VNET_QUEUE_TX];

    if (vnet->status & VIRTIO_STATUS_DEVICE_NEEDS_RESET)
        return;

    if (!queue->fd_ready)
        return;

    if (!((vnet->status & VIRTIO_STATUS_DRIVER_OK) && queue->ready))
        return virtio_net_set_fail(vnet);

    if (queue->queue_num == 0)
        return virtio_net_set_fail(vnet);

    if (!vnet_check_word_range(vnet, queue->queue_avail, 1))
        return;
    if (!vnet_check_word_range(vnet, queue->queue_used, 1))
        return;

    uint16_t new_avail = ram[queue->queue_avail] >> 16;
    if (new_avail - queue->last_avail > (uint16_t) queue->queue_num) {
        rv_log_error("virtio-net: TX available ring size check failed");
        return virtio_net_set_fail(vnet);
    }

    uint16_t new_used = ram[queue->queue_used] >> 16;

    while (queue->last_avail != new_avail) {
        uint16_t queue_idx = queue->last_avail % queue->queue_num;
        uint16_t buffer_idx;

        if (!vnet_get_avail_idx(vnet, queue, queue_idx, &buffer_idx))
            return;

        struct iovec iovs[VNET_QUEUE_NUM_MAX];
        size_t niovs = 0;

        if (!vnet_build_iovs(vnet, queue, buffer_idx, false, iovs, &niovs))
            return;

        struct iovec *cursor = iovs;
        size_t ncursor = niovs;
        uint8_t virtio_header[VNET_HDR_SIZE];

        if (vnet_iovec_read(&cursor, &ncursor, virtio_header,
                            sizeof(virtio_header))) {
            rv_log_error("virtio-net: TX buffer too small for virtio header");
            return virtio_net_set_fail(vnet);
        }

        ssize_t plen = vnet_handle_write(&vnet->peer, queue, cursor, ncursor);
        if (plen < 0)
            break;

        if (!vnet_put_used_elem(vnet, queue, new_used, buffer_idx, 0))
            return;

        queue->last_avail++;
        new_used++;
    }

    ram[queue->queue_used] &= MASK(16);
    ram[queue->queue_used] |= ((uint32_t) new_used) << 16;

    if (!(ram[queue->queue_avail] & 1))
        vnet->interrupt_status |= VIRTIO_INT_USED_RING;
}

void virtio_net_refresh_queue(virtio_net_state_t *vnet)
{
    if (!vnet || !vnet->peer.op)
        return;

    if (!(vnet->status & VIRTIO_STATUS_DRIVER_OK) ||
        (vnet->status & VIRTIO_STATUS_DEVICE_NEEDS_RESET))
        return;

    switch (vnet->peer.type) {
    case NETDEV_IMPL_tap: {
        net_tap_options_t *tap = (net_tap_options_t *) vnet->peer.op;
        struct pollfd pfd = {
            .fd = tap->tap_fd,
            .events = POLLIN | POLLOUT,
        };

        poll(&pfd, 1, 0);

        if (pfd.revents & POLLIN) {
            vnet->queues[VNET_QUEUE_RX].fd_ready = true;
            virtio_net_try_rx(vnet);
        }

        if (pfd.revents & POLLOUT) {
            vnet->queues[VNET_QUEUE_TX].fd_ready = true;
            virtio_net_try_tx(vnet);
        }

        break;
    }
    default:
        break;
    }
}

static uint32_t virtio_net_config_read(virtio_net_state_t *vnet, uint32_t addr)
{
    virtio_net_config_t *cfg = (virtio_net_config_t *) vnet->priv;
    uint32_t offset = (addr - VIRTIO_Config) * sizeof(uint32_t);
    uint32_t value = 0;

    if (offset >= sizeof(*cfg))
        return 0;

    memcpy(&value, (uint8_t *) cfg + offset,
           vnet_min(sizeof(value), sizeof(*cfg) - offset));

    return value;
}

static void virtio_net_config_write(virtio_net_state_t *vnet,
                                    uint32_t addr,
                                    uint32_t value)
{
    virtio_net_config_t *cfg = (virtio_net_config_t *) vnet->priv;
    uint32_t offset = (addr - VIRTIO_Config) * sizeof(uint32_t);

    if (offset >= sizeof(*cfg))
        return;

    memcpy((uint8_t *) cfg + offset, &value,
           vnet_min(sizeof(value), sizeof(*cfg) - offset));
}

uint32_t virtio_net_read(virtio_net_state_t *vnet, uint32_t addr)
{
    addr = addr >> 2;

#define _(reg) VIRTIO_##reg
    switch (addr) {
    case _(MagicValue):
        return VIRTIO_MAGIC_NUMBER;
    case _(Version):
        return VIRTIO_VERSION;
    case _(DeviceID):
        return VIRTIO_NET_DEV_ID;
    case _(VendorID):
        return VIRTIO_VENDOR_ID;
    case _(DeviceFeatures):
        return vnet->device_features_sel == 0
                   ? VNET_FEATURES_0 | vnet->device_features
                   : (vnet->device_features_sel == 1 ? VNET_FEATURES_1 : 0);
    case _(QueueNumMax):
        return VNET_QUEUE_NUM_MAX;
    case _(QueueReady):
        return (uint32_t) VNET_QUEUE.ready;
    case _(InterruptStatus):
        return vnet->interrupt_status;
    case _(Status):
        return vnet->status;
    case _(ConfigGeneration):
        return VIRTIO_CONFIG_GENERATE;
    default:
        if (addr >= _(Config))
            return virtio_net_config_read(vnet, addr);
        return 0;
    }
#undef _
}

void virtio_net_write(virtio_net_state_t *vnet, uint32_t addr, uint32_t value)
{
    addr = addr >> 2;

#define _(reg) VIRTIO_##reg
    switch (addr) {
    case _(DeviceFeaturesSel):
        vnet->device_features_sel = value;
        break;
    case _(DriverFeatures):
        if (vnet->driver_features_sel == 0)
            vnet->driver_features = value;
        break;
    case _(DriverFeaturesSel):
        vnet->driver_features_sel = value;
        break;
    case _(QueueSel):
        if (value < VNET_QUEUE_COUNT)
            vnet->queue_sel = value;
        else
            virtio_net_set_fail(vnet);
        break;
    case _(QueueNum):
        if (value > 0 && value <= VNET_QUEUE_NUM_MAX)
            VNET_QUEUE.queue_num = value;
        else
            virtio_net_set_fail(vnet);
        break;
    case _(QueueReady):
        VNET_QUEUE.ready = value & 1;
        if (value & 1) {
            if (!vnet_check_word_range(vnet, VNET_QUEUE.queue_avail, 1))
                return;
            VNET_QUEUE.last_avail = vnet->ram[VNET_QUEUE.queue_avail] >> 16;
            VNET_QUEUE.fd_ready = true;
        }
        break;
    case _(QueueDescLow):
        VNET_QUEUE.queue_desc = vnet_preprocess(vnet, value);
        break;
    case _(QueueDescHigh):
        if (value)
            virtio_net_set_fail(vnet);
        break;
    case _(QueueDriverLow):
        VNET_QUEUE.queue_avail = vnet_preprocess(vnet, value);
        break;
    case _(QueueDriverHigh):
        if (value)
            virtio_net_set_fail(vnet);
        break;
    case _(QueueDeviceLow):
        VNET_QUEUE.queue_used = vnet_preprocess(vnet, value);
        break;
    case _(QueueDeviceHigh):
        if (value)
            virtio_net_set_fail(vnet);
        break;
    case _(QueueNotify):
        switch (value) {
        case VNET_QUEUE_RX:
            virtio_net_try_rx(vnet);
            break;
        case VNET_QUEUE_TX:
            virtio_net_try_tx(vnet);
            break;
        default:
            virtio_net_set_fail(vnet);
            break;
        }
        break;
    case _(InterruptACK):
        vnet->interrupt_status &= ~value;
        break;
    case _(Status):
        virtio_net_update_status(vnet, value);
        break;
    default:
        if (addr >= _(Config))
            virtio_net_config_write(vnet, addr, value);
        break;
    }
#undef _
}

bool virtio_net_init(virtio_net_state_t *vnet, const char *net_type)
{
    if (!netdev_init(&vnet->peer, net_type)) {
        rv_log_error("virtio-net: failed to initialize net backend: %s",
                     net_type ? net_type : "(null)");
        return false;
    }

    return true;
}

virtio_net_state_t *vnet_new(void)
{
    virtio_net_state_t *vnet = calloc(1, sizeof(*vnet));
    assert(vnet);

    virtio_net_config_t *cfg = calloc(1, sizeof(*cfg));
    assert(cfg);

    cfg->mac[0] = 0x52;
    cfg->mac[1] = 0x54;
    cfg->mac[2] = 0x00;
    cfg->mac[3] = 0x12;
    cfg->mac[4] = 0x34;
    cfg->mac[5] = 0x56;
    cfg->status = VNET_LINK_UP;
    cfg->max_virtqueue_pairs = 1;
    cfg->mtu = 1500;

    vnet->priv = cfg;
    vnet->queues[VNET_QUEUE_RX].fd_ready = true;
    vnet->queues[VNET_QUEUE_TX].fd_ready = true;

    return vnet;
}

void vnet_delete(virtio_net_state_t *vnet)
{
    if (!vnet)
        return;

    netdev_delete(&vnet->peer);
    free(vnet->priv);
    free(vnet);
}
