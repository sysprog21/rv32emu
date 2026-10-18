/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "devices/rtc.h"

int main(void)
{
    rtc_t rtc = {0};
    const uint64_t alarm = UINT64_MAX - 16;
    rtc_write(&rtc, RTC_ALARM_HIGH, alarm >> 32);
    assert(!rtc_read(&rtc, RTC_ALARM_STATUS));
    rtc_write(&rtc, RTC_ALARM_LOW, (uint32_t) alarm);
    assert(rtc_read(&rtc, RTC_ALARM_STATUS));
    rtc_write(&rtc, RTC_IRQ_ENABLED, 1);
    rtc_update(&rtc, alarm - 1);
    assert(!rtc.interrupt_status);
    rtc_update(&rtc, alarm);
    assert(rtc.interrupt_status);
    assert(!rtc_read(&rtc, RTC_ALARM_STATUS));
    rtc_write(&rtc, RTC_CLEAR_INTERRUPT, 1);
    rtc_update(&rtc, alarm + 1);
    assert(!rtc.interrupt_status);

    rtc_write(&rtc, RTC_ALARM_LOW, (uint32_t) alarm);
    rtc_write(&rtc, RTC_CLEAR_ALARM, 1);
    rtc_update(&rtc, alarm + 1);
    assert(!rtc.interrupt_status);
    /* Expiration remains pending while masked, without rearming on enable. */
    rtc_write(&rtc, RTC_IRQ_ENABLED, 0);
    rtc_write(&rtc, RTC_ALARM_HIGH, 0);
    rtc_write(&rtc, RTC_ALARM_LOW, 0);
    assert(rtc.interrupt_status);
    assert(!rtc.alarm_status);
    rtc_write(&rtc, RTC_IRQ_ENABLED, 3);
    assert(rtc.irq_enabled == 1);
    rtc_write(&rtc, RTC_CLEAR_INTERRUPT, 1);
    rtc_update(&rtc, UINT64_MAX);
    assert(!rtc.interrupt_status);
    puts("RTC alarm state OK");
    return 0;
}
