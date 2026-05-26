/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "virtio.h"

#define VRNG_FEATURES_0 0
#define VRNG_FEATURES_1 1 /* VIRTIO_F_VERSION_1 */
#define VRNG_QUEUE_NUM_MAX 1024
#define VRNG_QUEUE (vrng->queues[vrng->queue_sel])

static void virtio_rng_set_fail(virtio_rng_state_t *vrng)
{
    vrng->status |= VIRTIO_STATUS_DEVICE_NEEDS_RESET;
    if (vrng->status & VIRTIO_STATUS_DRIVER_OK)
        vrng->interrupt_status |= VIRTIO_INT_CONF_CHANGE;
}

static inline uint32_t vrng_preprocess(virtio_rng_state_t *vrng, uint32_t addr)
{
#if MEM_SIZE < 0x100000000ULL
    if ((addr >= MEM_SIZE) || (addr & 0b11)) {
#else
    if (addr & 0b11) {
#endif
        virtio_rng_set_fail(vrng);
        return 0;
    }

    return addr >> 2;
}

static void virtio_rng_update_status(virtio_rng_state_t *vrng, uint32_t status)
{
    vrng->status |= status;
    if (status)
        return;

    /* Reset while preserving environment-owned fields. */
    uint32_t *ram = vrng->ram;
    int rng_fd = vrng->rng_fd;
    memset(vrng, 0, sizeof(*vrng));
    vrng->ram = ram;
    vrng->rng_fd = rng_fd;
}

static void virtio_queue_notify_handler(virtio_rng_state_t *vrng, int index)
{
    uint32_t *ram = vrng->ram;
    virtio_rng_queue_t *queue = &vrng->queues[index];

    if (vrng->status & VIRTIO_STATUS_DEVICE_NEEDS_RESET)
        return;

    if (!((vrng->status & VIRTIO_STATUS_DRIVER_OK) && queue->ready))
        return virtio_rng_set_fail(vrng);

    /* Check for new buffers */
    uint16_t new_avail = ram[queue->queue_avail] >> 16;
    if (new_avail - queue->last_avail > (uint16_t) queue->queue_num) {
        rv_log_error("Size check fail");
        return virtio_rng_set_fail(vrng);
    }

    uint16_t new_used = ram[queue->queue_used] >> 16;
    while (queue->last_avail != new_avail) {
        uint16_t queue_idx = queue->last_avail % queue->queue_num;

        /* Since each buffer index occupies 2 bytes but the memory is aligned
         * with 4 bytes, and the first element of the available queue is stored
         * at ram[queue->queue_avail + 1], to acquire the buffer index, it
         * requires the following array index calculation and bit shifting.
         * Check also the `struct virtq_avail` on the spec.
         */
        uint16_t buffer_idx = ram[queue->queue_avail + 1 + queue_idx / 2] >>
                              (16 * (queue_idx % 2));

        if (buffer_idx >= queue->queue_num)
            return virtio_rng_set_fail(vrng);

        struct virtq_desc *desc = (struct virtq_desc *) &vrng
                                      ->ram[queue->queue_desc + buffer_idx * 4];

        if (!(desc->flags & VIRTIO_DESC_F_WRITE))
            return virtio_rng_set_fail(vrng);

        if (desc->addr >= MEM_SIZE || desc->len > MEM_SIZE - desc->addr)
            return virtio_rng_set_fail(vrng);

        void *entropy_buf = (void *) ((uintptr_t) vrng->ram + desc->addr);
        ssize_t total = read(vrng->rng_fd, entropy_buf, desc->len);
        if (total < 0)
            total = 0;

        /* Write used element information (`struct virtq_used_elem`) to the used
         * queue */
        uint32_t vq_used_addr =
            queue->queue_used + 1 + (new_used % queue->queue_num) * 2;
        ram[vq_used_addr] = buffer_idx;
        ram[vq_used_addr + 1] = (uint32_t) total;

        queue->last_avail++;
        new_used++;
    }

    ram[queue->queue_used] &= MASK(16);
    ram[queue->queue_used] |= ((uint32_t) new_used) << 16;

    if (!(ram[queue->queue_avail] & 1))
        vrng->interrupt_status |= VIRTIO_INT_USED_RING;
}

uint32_t virtio_rng_read(virtio_rng_state_t *vrng, uint32_t addr)
{
    addr = addr >> 2;
#define _(reg) VIRTIO_##reg
    switch (addr) {
    case _(MagicValue):
        return VIRTIO_MAGIC_NUMBER;
    case _(Version):
        return VIRTIO_VERSION;
    case _(DeviceID):
        return VIRTIO_RNG_DEV_ID;
    case _(VendorID):
        return VIRTIO_VENDOR_ID;
    case _(DeviceFeatures):
        return vrng->device_features_sel == 0
                   ? VRNG_FEATURES_0 | vrng->device_features
                   : (vrng->device_features_sel == 1 ? VRNG_FEATURES_1 : 0);
    case _(QueueNumMax):
        return VRNG_QUEUE_NUM_MAX;
    case _(QueueReady):
        return (uint32_t) VRNG_QUEUE.ready;
    case _(InterruptStatus):
        return vrng->interrupt_status;
    case _(Status):
        return vrng->status;
    case _(ConfigGeneration):
        return VIRTIO_CONFIG_GENERATE;
    default:
        return 0;
    }
#undef _
}

void virtio_rng_write(virtio_rng_state_t *vrng, uint32_t addr, uint32_t value)
{
    addr = addr >> 2;
#define _(reg) VIRTIO_##reg
    switch (addr) {
    case _(DeviceFeaturesSel):
        vrng->device_features_sel = value;
        break;
    case _(DriverFeatures):
        if (vrng->driver_features_sel == 0)
            vrng->driver_features = value;
        break;
    case _(DriverFeaturesSel):
        vrng->driver_features_sel = value;
        break;
    case _(QueueSel):
        if (value < ARRAY_SIZE(vrng->queues))
            vrng->queue_sel = value;
        else
            virtio_rng_set_fail(vrng);
        break;
    case _(QueueNum):
        if (value > 0 && value <= VRNG_QUEUE_NUM_MAX)
            VRNG_QUEUE.queue_num = value;
        else
            virtio_rng_set_fail(vrng);
        break;
    case _(QueueReady):
        VRNG_QUEUE.ready = value & 1;
        if (value & 1)
            VRNG_QUEUE.last_avail = vrng->ram[VRNG_QUEUE.queue_avail] >> 16;
        break;
    case _(QueueDescLow):
        VRNG_QUEUE.queue_desc = vrng_preprocess(vrng, value);
        break;
    case _(QueueDescHigh):
        if (value)
            virtio_rng_set_fail(vrng);
        break;
    case _(QueueDriverLow):
        VRNG_QUEUE.queue_avail = vrng_preprocess(vrng, value);
        break;
    case _(QueueDriverHigh):
        if (value)
            virtio_rng_set_fail(vrng);
        break;
    case _(QueueDeviceLow):
        VRNG_QUEUE.queue_used = vrng_preprocess(vrng, value);
        break;
    case _(QueueDeviceHigh):
        if (value)
            virtio_rng_set_fail(vrng);
        break;
    case _(QueueNotify):
        if (value < ARRAY_SIZE(vrng->queues))
            virtio_queue_notify_handler(vrng, value);
        else
            virtio_rng_set_fail(vrng);
        break;
    case _(InterruptACK):
        vrng->interrupt_status &= ~value;
        break;
    case _(Status):
        virtio_rng_update_status(vrng, value);
        break;
    default:
        break;
    }
#undef _
}

bool virtio_rng_init(virtio_rng_state_t *vrng)
{
    vrng->rng_fd = open("/dev/random", O_RDONLY);
    if (vrng->rng_fd < 0) {
        rv_log_error("Could not open /dev/random: %s", strerror(errno));
        return false;
    }

    return true;
}

virtio_rng_state_t *vrng_new(void)
{
    virtio_rng_state_t *vrng = calloc(1, sizeof(*vrng));
    assert(vrng);
    vrng->rng_fd = -1;
    return vrng;
}

void vrng_delete(virtio_rng_state_t *vrng)
{
    if (!vrng)
        return;
    if (vrng->rng_fd >= 0)
        close(vrng->rng_fd);
    free(vrng);
}
