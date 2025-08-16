#include <stdint.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/tsc.h>
#include <kernel/arch/x86_64/io.h>

#define from_bcd(value) ((value >> 4) * 10 + (value & 0xf))
#define is_leap_year(year) (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
#define leap_years(year) ((year - 1) / 4) - ((year - 1) / 100) + ((year - 1) / 400) - (1969 / 4) + (1969 / 100) - (1969 / 400);
#define century 20 /** TODO: parse century register from FADT */

enum {
    CMOS_ADDRESS = 0x70,
    CMOS_DATA    = 0x71
};

enum {
	CMOS_SECOND = 0x00,
	CMOS_MINUTE = 0x02,
	CMOS_HOUR   = 0x04,
	CMOS_DAY    = 0x07,
	CMOS_MONTH  = 0x08,
	CMOS_YEAR   = 0x09
};

static const uint16_t days_before_month[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

static void rtc_dump(uint16_t *buffer, uint16_t count) {
    for (uint16_t i = 0; i < count; ++i) {
        outb(CMOS_ADDRESS, i);
        buffer[i] = inb(CMOS_DATA);
    }
}

uint64_t now(void) {
    uint16_t dump[10];
    rtc_dump(dump, 10);

    uint64_t year = century * 100 + from_bcd(dump[CMOS_YEAR]);
    uint64_t month = from_bcd(dump[CMOS_MONTH]);
    uint64_t day = from_bcd(dump[CMOS_DAY]);
    uint64_t hour = from_bcd(dump[CMOS_HOUR]);
    uint64_t minute = from_bcd(dump[CMOS_MINUTE]);
    uint64_t second = from_bcd(dump[CMOS_SECOND]);

    uint64_t years_since_epoch = year - 1970;
    uint64_t leap_years = leap_years(year);
    uint64_t days_since_epoch = years_since_epoch * 365 + leap_years + days_before_month[month - 1] + (day - 1) + ((is_leap_year(year) && month > 2) ? 1 : 0);

    uint64_t unixtime = days_since_epoch * 86400 + hour * 3600 + minute * 60 + second;
    return unixtime;
}

void gettimeofday(long *sec, long *nsec) {
    if (sec) *sec = now();
    if (nsec) {
        if (hpet) hpet_read_time(NULL, nsec);
        else if (tsc_period) tsc_read_time(NULL, nsec);
    }
}

void uptime(long *sec, long *nsec) {
    if (hpet) hpet_read_time(sec, nsec);
    else if (tsc_period) tsc_read_time(sec, nsec);
}
