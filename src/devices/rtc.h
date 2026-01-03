/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

/* Check: https://github.com/torvalds/linux/blob/v6.1/drivers/rtc/rtc-goldfish.c
 */

/* Google Goldfish RTC MMIO registers */
#define RTC_REG_LIST                   \
    _(TIME_LOW, 0x00)        /* R/W */ \
    _(TIME_HIGH, 0x04)       /* R/W */ \
    _(ALARM_LOW, 0x08)       /* R/W */ \
    _(ALARM_HIGH, 0x0c)      /* R/W */ \
    _(IRQ_ENABLED, 0x10)     /* W */   \
    _(CLEAR_ALARM, 0x14)     /* W */   \
    _(ALARM_STATUS, 0x18)    /* R */   \
    _(CLEAR_INTERRUPT, 0x1c) /* W */

enum {
#define _(reg, addr) RTC_##reg = addr,
    RTC_REG_LIST
#undef _
};

typedef struct {
    uint32_t time_low;
    uint32_t time_high;
    uint32_t alarm_low;
    uint32_t alarm_high;
    uint32_t irq_enabled;
    uint32_t alarm_status;
    uint32_t interrupt_status;

    /* Ensure the clock always progresses so RTC_SET_TIME ioctl can set any
     * arbitrary time */
    uint64_t clock_offset;
} rtc_t;

#define IRQ_RTC_SHIFT 2
#define IRQ_RTC_BIT (1 << IRQ_RTC_SHIFT)

#define rtc_alarm_fire(rtc, now_nsec) \
    ((rtc)->irq_enabled &&            \
     ((now_nsec) >=                   \
      ((((uint64_t) (rtc)->alarm_high) << 32) | (rtc)->alarm_low)))

uint64_t rtc_get_now_nsec(rtc_t *rtc);

uint32_t rtc_read(rtc_t *rtc, uint32_t addr);

void rtc_write(rtc_t *rtc, uint32_t addr, uint32_t value);

rtc_t *rtc_new();

void rtc_delete(rtc_t *rtc);
