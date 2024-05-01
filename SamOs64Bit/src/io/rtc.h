#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H
// The CMOS real-time clock: wall-clock time.
//
// Not in the book, which has no notion of the date. The RTC is read
// on demand (a dozen port accesses); it runs in UTC under QEMU's
// default -rtc base=utc.

#include <stdint.h>

struct rtc_time {
    int year;       // full year, e.g. 2026
    int month;      // 1-12
    int day;        // 1-31
    int hour;       // 0-23
    int minute;
    int second;
};

// 0, or -EIO when the clock never settled or read nonsense.
int     rtc_read(struct rtc_time* t);
// Seconds since 1970-01-01 00:00:00 UTC, or -1.
int64_t rtc_unix_time(void);
int64_t rtc_to_unix(const struct rtc_time* t);
// Read the time from `fn` (seconds since 1970, < 0 on error)
// instead of the CMOS: the ports' RTC, set before rtc_init.
void    rtc_set_unix_source(int64_t (*fn)(void));
// Print the time on COM1 once at boot.
void    rtc_init(void);

#endif
