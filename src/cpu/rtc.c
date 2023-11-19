#include <cpu/rtc.h>
#include <io/stdio.h>
#include <drivers/framebuffer.h>
#include <io/ports.h>
#include <cpu/ioapic.h>
#include <kernel/logging.h>

#define RTC_SECONDS 0
#define RTC_MINUTES 2
#define RTC_HOURS 4
#define RTC_WEEKDAY 6
#define RTC_DOM 7
#define RTC_MONTH 8
#define RTC_YEAR 9
#define RTC_CENTURY 0x32
#define RTC_STATUS_A 0xA
#define RTC_STATUS_B 0xB
#define DISABLE_NMI 0x80
#define TIME_LENGTH 8
#define DATE_LENGTH 8

char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                   "August", "September", "October", "November", "December" };
char *days_of_week[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Friday", "Saturday" };

time_t global_time;
int bcd;

uint8_t read_cmos_register(uint8_t port_num) {
    outb(0x70, port_num);
    return inb(0x71);
}

void write_cmos_register(uint8_t port_num, uint8_t val) {
    outb(0x70, port_num);
    outb(0x71, val);
}

inline uint32_t bcd2bin(uint32_t bcd_num) {
    return ((bcd_num >> 4) * 10) + (bcd_num & 0xF);
}

// Month is 1-12
// Copied and pasted from wikipedia article
inline uint8_t get_dow(uint16_t y, uint8_t m, uint8_t dom) {
    return (dom +=m < 3 ? y-- : y - 2, 23 * m / 9 + dom + 4 + y / 4 - y / 100 + y / 400) % 7 + 1;
}

void init_rtc(void) {
    uint8_t status = read_cmos_register(0xB);
    status |= 0x2 | 0x10; // 24 hour mode and update ended interrupt
    status &= ~0x20; // No periodic interrupt and no alarm interrupt
    status &= ~0x40;
    bcd = !(status & 0x4);
    write_cmos_register(0xB, status);
    log(Info, "RTC", "Initialized RTC timer (in %s mode)", bcd ? "BCD" : "binary");
    read_cmos_register(0xC);
    set_irq_mask(8, false);
}

void rtc_handler(void) {
    uint8_t status = read_cmos_register(0xC);
    if (status & 0b10000) {
        if (bcd) {
            uint8_t century = bcd2bin(read_cmos_register(0x32));
            global_time.century = century != 0 ? century : 20;
            global_time.year = bcd2bin(read_cmos_register(0x9));
            global_time.month = bcd2bin(read_cmos_register(0x8));
            global_time.dom = bcd2bin(read_cmos_register(0x7));
            global_time.hour = bcd2bin(read_cmos_register(0x4));
            global_time.minute = bcd2bin(read_cmos_register(0x2));
            global_time.second = bcd2bin(read_cmos_register(0));
        } else {
            uint8_t century = read_cmos_register(0x32);
            global_time.century = century != 0 ? century : 20;
            global_time.year = read_cmos_register(0x9);
            global_time.month = read_cmos_register(0x8);
            global_time.dom = read_cmos_register(0x7);
            global_time.hour = read_cmos_register(0x4);
            global_time.minute = read_cmos_register(0x2);
            global_time.second = read_cmos_register(0);
        }
        global_time.dow = get_dow(global_time.century * 100 + global_time.year, global_time.month, global_time.dom);
        // HH:MM:SS
        // MM/DD/YY
        uint32_t old_color = screen_info.fg;
        screen_info.fg = 0x579cf7;
        printf_at(screen_info.num_cols - TIME_LENGTH, 0, "%s%u:%s%u:%s%u",
                    global_time.hour < 10 ? "0" : "", global_time.hour,
                    global_time.minute < 10 ? "0" : "", global_time.minute,
                    global_time.second < 10 ? "0" : "", global_time.second);
        printf_at(screen_info.num_cols - DATE_LENGTH, 1, "%s%u/%s%u/%s%u",
                    global_time.month < 10 ? "0" : "", global_time.month,
                    global_time.dom < 10 ? "0" : "", global_time.dom,
                    global_time.year < 10 ? "0" : "", global_time.year);
        screen_info.fg = old_color;
    }
}
