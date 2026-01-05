#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int fd;
    struct rtc_time new_time;
    int year = 0;

    /* TODO: support month and date */
    /* Handle CLI argument */
    if (argc == 2) {
        year = atoi(argv[1]);
    } else if (argc > 2) {
        fprintf(stderr, "Usage: %s [year]\n", argv[0]);
        fprintf(stderr, "Example: %s 1972\n", argv[0]);
        return 1;
    }

    printf("Opening /dev/rtc0...\n");
    fd = open("/dev/rtc0", O_RDWR);
    if (fd == -1) {
        perror("Failed to open /dev/rtc0");
        return 1;
    }

    if (year > 0) {
        /* Manually set RTC to <year>-01-01 00:00:00 */
        new_time.tm_sec = 0;
        new_time.tm_min = 0;
        new_time.tm_hour = 0;
        new_time.tm_mday = 1;
        new_time.tm_mon = 0;            /* January = 0 */
        new_time.tm_year = year - 1900; /* tm_year is years since 1900 */
        new_time.tm_wday = 0;
        new_time.tm_yday = 0;
        new_time.tm_isdst = 0;

        printf("Setting RTC time to: %04d-01-01 00:00:00 (UTC)\n", year);
    } else {
        /* No year provided, set RTC to current UTC time */
        time_t now = time(NULL);
        struct tm *utc_tm = gmtime(&now);
        if (!utc_tm) {
            perror("gmtime failed");
            close(fd);
            return 1;
        }

        new_time.tm_sec = utc_tm->tm_sec;
        new_time.tm_min = utc_tm->tm_min;
        new_time.tm_hour = utc_tm->tm_hour;
        new_time.tm_mday = utc_tm->tm_mday;
        new_time.tm_mon = utc_tm->tm_mon;   /* 0-11 */
        new_time.tm_year = utc_tm->tm_year; /* years since 1900 */
        new_time.tm_wday = utc_tm->tm_wday;
        new_time.tm_yday = utc_tm->tm_yday;
        new_time.tm_isdst = 0;

        printf(
            "Setting RTC time to current UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
            new_time.tm_year + 1900, new_time.tm_mon + 1, new_time.tm_mday,
            new_time.tm_hour, new_time.tm_min, new_time.tm_sec);
    }

    /* Trigger the goldfish_rtc_set_time kernel driver */
    if (ioctl(fd, RTC_SET_TIME, &new_time) == -1) {
        perror("RTC_SET_TIME ioctl failed");
        close(fd);
        return 1;
    }

    printf("RTC time successfully updated!\n\n");
    close(fd);

    /* Immediately read and print /proc/driver/rtc */
    printf("Reading /proc/driver/rtc to verify...\n\n");
    FILE *proc_file = fopen("/proc/driver/rtc", "r");
    if (!proc_file) {
        perror("Failed to open /proc/driver/rtc");
        return 1;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), proc_file)) {
        printf("%s", buffer);
    }

    fclose(proc_file);
    return 0;
}
