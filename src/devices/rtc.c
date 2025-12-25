/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rtc.h"

static uint64_t now_nsec;

uint64_t rtc_get_now_nsec(rtc_t *rtc)
{
    /* TODO:
     * - detects timezone and use the correct UTC offset
     * - a new CLI option should be added to main.c to let user to select
     *   [UTC] or [UTC + offset](localtime) time. E.g., -x rtc:utc or -x
     *   rtc:localtime
     */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) (ts.tv_sec * 1e9) + ts.tv_nsec + rtc->clock_offset;
}

uint32_t rtc_read(rtc_t *rtc, uint32_t addr)
{
    uint32_t rtc_read_val = 0;

    switch (addr) {
    case RTC_TIME_LOW:
        now_nsec = rtc_get_now_nsec(rtc);
        rtc->time_low = (uint32_t) (now_nsec & MASK(32));
        rtc_read_val = rtc->time_low;
        break;
    case RTC_TIME_HIGH:
        /* reuse the now_nsec when reading RTC_TIME_LOW */
        rtc->time_high = (uint32_t) (now_nsec >> 32);
        rtc_read_val = rtc->time_high;
        break;
    case RTC_ALARM_LOW:
        rtc_read_val = rtc->alarm_low;
        break;
    case RTC_ALARM_HIGH:
        rtc_read_val = rtc->alarm_high;
        break;
    case RTC_ALARM_STATUS:
        rtc_read_val = rtc->alarm_status;
        break;
    default:
        rv_log_error("Unsupported RTC read operation, 0x%x", addr);
        break;
    }

    return rtc_read_val;
}

void rtc_write(rtc_t *rtc, uint32_t addr, uint32_t value)
{
    switch (addr) {
    case RTC_TIME_LOW:
        now_nsec = rtc_get_now_nsec(rtc);
        rtc->clock_offset += (uint64_t) (value) - (now_nsec & MASK(32));
        break;
    case RTC_TIME_HIGH:
        /* reuse the now_nsec when writing RTC_TIME_LOW */
        rtc->clock_offset += ((uint64_t) (value) << 32) -
                             (now_nsec & ((uint64_t) (MASK(32)) << 32));
        break;
    case RTC_ALARM_LOW:
        rtc->alarm_low = value;
        break;
    case RTC_ALARM_HIGH:
        rtc->alarm_high = value;
        break;
    case RTC_IRQ_ENABLED:
        rtc->irq_enabled = value;
        break;
    case RTC_CLEAR_ALARM:
        rtc->alarm_status = 0;
        break;
    case RTC_CLEAR_INTERRUPT:
        rtc->interrupt_status = 0;
        break;
    default:
        rv_log_error("Unsupported RTC write operation, 0x%x", addr);
        break;
    }
    return;
}

rtc_t *rtc_new()
{
    rtc_t *rtc = calloc(1, sizeof(rtc_t));
    assert(rtc);

    /*
     * The rtc->time_low/high values can be updated through the RTC_SET_TIME
     * ioctl operation. Therefore, they should be initialized to match the
     * host OS time during initialization.
     */
    now_nsec = rtc_get_now_nsec(rtc);
    rtc->time_low = (uint32_t) (now_nsec & MASK(32));
    rtc->time_high = (uint32_t) (now_nsec >> 32);

    return rtc;
}

void rtc_delete(rtc_t *rtc)
{
    free(rtc);
}
