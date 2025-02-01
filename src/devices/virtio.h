/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#define VIRTIO_VENDOR_ID 0x12345678
#define VIRTIO_MAGIC_NUMBER 0x74726976
#define VIRTIO_VERSION 2
#define VIRTIO_CONFIG_GENERATE 0

#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64

#define VIRTIO_INT_USED_RING 1
#define VIRTIO_INT_CONF_CHANGE 2

#define VIRTIO_DESC_F_NEXT 1
#define VIRTIO_DESC_F_WRITE 2

#define VIRTIO_BLK_DEV_ID 2
#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_FLUSH 4
#define VIRTIO_BLK_T_GET_ID 8
#define VIRTIO_BLK_T_GET_LIFETIME 10
#define VIRTIO_BLK_T_DISCARD 11
#define VIRTIO_BLK_T_WRITE_ZEROES 13
#define VIRTIO_BLK_T_SECURE_ERASE 14

#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2

/* VirtIO MMIO registers */
#define VIRTIO_REG_LIST                  \
    _(MagicValue, 0x000)        /* R */  \
    _(Version, 0x004)           /* R */  \
    _(DeviceID, 0x008)          /* R */  \
    _(VendorID, 0x00c)          /* R */  \
    _(DeviceFeatures, 0x010)    /* R */  \
    _(DeviceFeaturesSel, 0x014) /* W */  \
    _(DriverFeatures, 0x020)    /* W */  \
    _(DriverFeaturesSel, 0x024) /* W */  \
    _(QueueSel, 0x030)          /* W */  \
    _(QueueNumMax, 0x034)       /* R */  \
    _(QueueNum, 0x038)          /* W */  \
    _(QueueReady, 0x044)        /* RW */ \
    _(QueueNotify, 0x050)       /* W */  \
    _(InterruptStatus, 0x60)    /* R */  \
    _(InterruptACK, 0x064)      /* W */  \
    _(Status, 0x070)            /* RW */ \
    _(QueueDescLow, 0x080)      /* W */  \
    _(QueueDescHigh, 0x084)     /* W */  \
    _(QueueDriverLow, 0x090)    /* W */  \
    _(QueueDriverHigh, 0x094)   /* W */  \
    _(QueueDeviceLow, 0x0a0)    /* W */  \
    _(QueueDeviceHigh, 0x0a4)   /* W */  \
    _(ConfigGeneration, 0x0fc)  /* R */  \
    _(Config, 0x100)            /* RW */

enum {
#define _(reg, addr) VIRTIO_##reg = addr >> 2,
    VIRTIO_REG_LIST
#undef _
};

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

#define IRQ_VBLK_SHIFT 3
#define IRQ_VBLK_BIT (1 << IRQ_VBLK_SHIFT)

typedef struct {
    uint32_t queue_num;
    uint32_t queue_desc;
    uint32_t queue_avail;
    uint32_t queue_used;
    uint16_t last_avail;
    bool ready;
} virtio_blk_queue_t;

typedef struct {
    /* feature negotiation */
    uint32_t device_features_sel;
    uint32_t driver_features;
    uint32_t driver_features_sel;
    /* queue config */
    uint32_t queue_sel;
    virtio_blk_queue_t queues[2];
    /* status */
    uint32_t status;
    uint32_t interrupt_status;
    /* supplied by environment */
    uint32_t *ram;
    uint32_t *disk;
    /* implementation-specific */
    void *priv;
} virtio_blk_state_t;

uint32_t virtio_blk_read(virtio_blk_state_t *vblk, uint32_t addr);

void virtio_blk_write(virtio_blk_state_t *vblk, uint32_t addr, uint32_t value);

uint32_t *virtio_blk_init(virtio_blk_state_t *vblk, char *disk_file);

virtio_blk_state_t *vblk_new();

void vblk_delete(virtio_blk_state_t *vblk);
