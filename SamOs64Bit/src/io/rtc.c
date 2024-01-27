// CMOS RTC (MC146818 compatible) at ports 0x70/0x71.
//
// The clock updates its registers once a second and they are not
// consistent while status A bit 7 (update in progress) is set, so a
// read waits for it to clear and is repeated until two reads agree.
// Status B says whether the values are BCD (bit 2 clear) and whether
// hours are 12-hour with bit 7 as PM (bit 1 clear). The century is
// in the CMOS register the FADT names (0x32 under QEMU), else the
// two-digit year is taken as 1970-2069.

#include "io/rtc.h"
#include "io/isa.h"
#include "io/serial.h"
#include "acpi/acpi.h"
#include "lib/spinlock/spinlock.h"
#include "status.h"

// The index/data port pair is one register window: two CPUs reading
// at once (time() and a FAT timestamp) would read each
// other's registers.
static struct spinlock rtc_lock = SPINLOCK_INIT("rtc", LOCK_LEVEL_RTC, 0);

#define CMOS_INDEX   0x70
#define CMOS_DATA    0x71
#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B
#define RTC_UIP      0x80
#define RTC_B_24H    0x02
#define RTC_B_BINARY 0x04

// Bit 7 of the index port gates NMI; keep it clear (NMI enabled).
static uint8_t cmos_read(uint8_t reg){
    isa_write8(CMOS_INDEX, reg & 0x7F);
    return isa_read8(CMOS_DATA);
}

struct rtc_raw {
    uint8_t sec, min, hour, day, month, year, century;
};

static int rtc_read_raw(struct rtc_raw* r, uint8_t century_reg){
    // An update takes under 2 ms; this bounds a dead clock.
    for(int i = 0; cmos_read(RTC_STATUS_A) & RTC_UIP; i++){
        if(i == 1000000){
            return -EIO;
        }
    }
    r->sec     = cmos_read(RTC_SECONDS);
    r->min     = cmos_read(RTC_MINUTES);
    r->hour    = cmos_read(RTC_HOURS);
    r->day     = cmos_read(RTC_DAY);
    r->month   = cmos_read(RTC_MONTH);
    r->year    = cmos_read(RTC_YEAR);
    r->century = century_reg ? cmos_read(century_reg) : 0;
    return 0;
}

static int bcd(uint8_t v){
    return (v & 0x0F) + (v >> 4) * 10;
}

static int rtc_read_locked(struct rtc_time* t){
    uint8_t century_reg = acpi_info()->century_reg;
    struct rtc_raw a, b;
    if(rtc_read_raw(&a, century_reg) < 0){
        return -EIO;
    }
    // Repeat until two reads in a row agree (an update may have
    // started between the UIP check and the last register).
    for(int tries = 0; ; tries++){
        if(tries == 5 || rtc_read_raw(&b, century_reg) < 0){
            return -EIO;
        }
        if(a.sec == b.sec && a.min == b.min && a.hour == b.hour && a.day == b.day &&
           a.month == b.month && a.year == b.year && a.century == b.century){
            break;
        }
        a = b;
    }

    uint8_t status_b = cmos_read(RTC_STATUS_B);
    bool pm = !(status_b & RTC_B_24H) && (b.hour & 0x80);
    b.hour &= 0x7F;
    if(status_b & RTC_B_BINARY){
        t->second = b.sec;
        t->minute = b.min;
        t->hour   = b.hour;
        t->day    = b.day;
        t->month  = b.month;
        t->year   = b.year;
    } else {
        t->second = bcd(b.sec);
        t->minute = bcd(b.min);
        t->hour   = bcd(b.hour);
        t->day    = bcd(b.day);
        t->month  = bcd(b.month);
        t->year   = bcd(b.year);
        b.century = bcd(b.century);
    }
    if(!(status_b & RTC_B_24H)){
        // 12-hour clock: 12 AM is 0, 12 PM is 12.
        t->hour = (t->hour % 12) + (pm ? 12 : 0);
    }
    if(century_reg && b.century >= 19 && b.century <= 99){
        t->year += b.century * 100;
    } else {
        t->year += t->year < 70 ? 2000 : 1900;
    }
    if(t->month < 1 || t->month > 12 || t->day < 1 || t->day > 31 ||
       t->hour > 23 || t->minute > 59 || t->second > 60){
        return -EIO;
    }
    return 0;
}

int rtc_read(struct rtc_time* t){
    spin_lock(&rtc_lock);
    int res = rtc_read_locked(t);
    spin_unlock(&rtc_lock);
    return res;
}

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm,
// valid for any Gregorian year).
int64_t rtc_to_unix(const struct rtc_time* t){
    int64_t y = t->year - (t->month <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t mp  = (t->month + 9) % 12;          // March = 0
    int64_t doy = (153 * mp + 2) / 5 + t->day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + t->hour * 3600 + t->minute * 60 + t->second;
}

int64_t rtc_unix_time(void){
    struct rtc_time t;
    if(rtc_read(&t) < 0){
        return -1;
    }
    return rtc_to_unix(&t);
}

static void rtc_put2(int v){
    char s[3] = { '0' + (v / 10) % 10, '0' + v % 10, 0 };
    serial_write(s);
}

void rtc_init(void){
    struct rtc_time t;
    if(rtc_read(&t) < 0){
        serial_write("rtc: no valid time\n");
        return;
    }
    serial_write("rtc: ");
    rtc_put2(t.year / 100);
    rtc_put2(t.year);
    serial_write("-");
    rtc_put2(t.month);
    serial_write("-");
    rtc_put2(t.day);
    serial_write(" ");
    rtc_put2(t.hour);
    serial_write(":");
    rtc_put2(t.minute);
    serial_write(":");
    rtc_put2(t.second);
    serial_write(" UTC, unix ");
    char buf[21];
    int i = sizeof(buf) - 1;
    uint64_t v = (uint64_t)rtc_to_unix(&t);
    buf[i] = 0;
    do {
        buf[--i] = '0' + v % 10;
        v /= 10;
    } while(v);
    serial_write(&buf[i]);
    serial_write("\n");
}
