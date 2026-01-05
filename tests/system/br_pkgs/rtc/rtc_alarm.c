#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define PROC_RTC_PATH "/proc/driver/rtc"

/* Helper function to dump /proc/driver/rtc */
void read_proc_rtc(const char *msg)
{
    FILE *fp = fopen(PROC_RTC_PATH, "r");
    if (!fp) {
        perror("Failed to open /proc/driver/rtc");
        return;
    }

    printf("\n=== %s ===\n", msg);
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        printf("%s", line);
    }
    printf("===========================\n\n");

    fclose(fp);
}

int main()
{
    int fd;
    struct rtc_time rtc_tm;
    unsigned long data;

    printf("Opening /dev/rtc0...\n");
    fd = open("/dev/rtc0", O_RDONLY);
    if (fd == -1) {
        perror("Failed to open /dev/rtc0");
        return 1;
    }

    /* Step 1. Read /proc/driver/rtc before setting alarm */
    read_proc_rtc("Initial /proc/driver/rtc");

    /* Step 2. Read current RTC time via ioctl */
    if (ioctl(fd, RTC_RD_TIME, &rtc_tm) == -1) {
        perror("RTC_RD_TIME ioctl failed");
        close(fd);
        return 1;
    }

    printf("Current RTC time: %04d-%02d-%02d %02d:%02d:%02d (UTC)\n",
           rtc_tm.tm_year + 1900, rtc_tm.tm_mon + 1, rtc_tm.tm_mday,
           rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);

    /* Step 3. Set alarm 5 seconds from now */
    int delay = 5;
    rtc_tm.tm_sec += delay;
    if (rtc_tm.tm_sec >= 60) {
        rtc_tm.tm_sec -= 60;
        rtc_tm.tm_min++;
        if (rtc_tm.tm_min >= 60) {
            rtc_tm.tm_min = 0;
            rtc_tm.tm_hour = (rtc_tm.tm_hour + 1) % 24;
        }
    }

    printf("Setting alarm for %d seconds later...\n", delay);
    if (ioctl(fd, RTC_ALM_SET, &rtc_tm) == -1) {
        perror("RTC_ALM_SET ioctl failed");
        close(fd);
        return 1;
    }

    /* Step 4. Enable alarm interrupt */
    if (ioctl(fd, RTC_AIE_ON, 0) == -1) {
        perror("RTC_AIE_ON ioctl failed");
        close(fd);
        return 1;
    }

    /* Step 5. Read /proc/driver/rtc right after enabling alarm */
    read_proc_rtc("After enabling alarm");

    printf("Alarm enabled. Waiting for it to fire...\n");

    /* Step 6. Block until the alarm interrupt occurs */
    if (read(fd, &data, sizeof(unsigned long)) == -1) {
        perror("read() failed");
        close(fd);
        return 1;
    }

    printf(">>> Alarm Fired! <<<\n");

    /* Step 7. Read /proc/driver/rtc after alarm fired */
    read_proc_rtc("After alarm fired");

    /* Step 8. Disable the alarm interrupt */
    if (ioctl(fd, RTC_AIE_OFF, 0) == -1) {
        perror("RTC_AIE_OFF ioctl failed");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
