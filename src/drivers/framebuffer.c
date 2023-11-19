#include <stddef.h>
#include <drivers/framebuffer.h>
#include <kernel/multiboot.h>
#include <kernel/logging.h>
#include <mem/bitmap.h>
#include <mem/pmm.h>
#include <stdarg.h>
#include <sys.h>

extern psf_font_t _binary_fonts_default_psf_start;
extern char _binary_fonts_default_psf_size;
extern char _binary_fonts_default_psf_end;
framebuffer_info_t fbinfo;
psf_font_t *loaded_font;
screen_info_t screen_info = { 0, 0, 0xFFFFFFFF, 0, 0, 0 };
char fb_buf[BUF_SIZE];
size_t fb_buf_pos = 0;
bool FRAMEBUFFER_INITIALIZED = false;
atomic_flag FRAMEBUFFER_LOCK = ATOMIC_FLAG_INIT;

inline static uint8_t *get_glyph(uint8_t sym_num) {
    return (uint8_t*) loaded_font + loaded_font->headersize + sym_num * loaded_font->bytesperglyph;
}

static void map_framebuffer(void) {
    uint32_t num_pages = fbinfo.memory_size / PAGE_SIZE;
    if (fbinfo.memory_size % PAGE_SIZE)
        num_pages++;
    for (uint32_t i = 0; i < num_pages; i++)
        map_addr(fbinfo.phys_addr + i * PAGE_SIZE, FRAMEBUFFER_START + i * PAGE_SIZE, PAGE_TABLE_ENTRY);
    bitmap_rsv_area(fbinfo.phys_addr, num_pages * PAGE_SIZE);
}

extern void putchar(char sym) {
    uint8_t *glyph = get_glyph(sym);
    size_t bytes_per_line = (loaded_font->width + 7) / 8;
    size_t offset = (screen_info.y * loaded_font->height * fbinfo.pitch) +
                    (screen_info.x * loaded_font->width * sizeof(uint32_t));
    for (uint32_t cy = 0, line = offset; cy < loaded_font->height;
        cy++, glyph += bytes_per_line, offset += fbinfo.pitch, line = offset)
        for (uint32_t cx = 0; cx < loaded_font->width; cx++, line += sizeof(fbinfo.bpp / 8))
            *((uint32_t*) (fbinfo.address + line)) = glyph[cx / 8] & (0x80 >> (cx & 7)) ? screen_info.fg : screen_info.bg;
}

static void find_last_char(void) {
    if (screen_info.x == 0) {
        if (screen_info.y == 0)
            return;
        screen_info.x = screen_info.num_cols - 1;
        screen_info.y--;
    } else
        screen_info.x--;
    while (1) {
        size_t offset = (screen_info.y * loaded_font->height * fbinfo.pitch) +
                        (screen_info.x * loaded_font->width * sizeof(uint32_t));
        for (uint32_t cy = 0, line = offset; cy < loaded_font->height;
            cy++, offset += fbinfo.pitch, line = offset)
            for (uint32_t cx = 0; cx < loaded_font->width; cx++, line += sizeof(fbinfo.bpp / 8))
                if (*((uint32_t*) (fbinfo.address + line)) != screen_info.bg)
                    return;
        if (screen_info.x == 0)
            return;
        else
            screen_info.x--;
    }
}

void print_char(char c) {
    switch (c) {
    case '\n':
        screen_info.x = 0;
        screen_info.y++;
        break;
    
    case '\b':
        find_last_char();
        putchar(' ');
        break;

    case '\t':
        for (int i = 0; i < TAB_SIZE; i++)
            print_char(' ');
        break;

    default:
        putchar(c);
        screen_info.x++;
        if (screen_info.x >= screen_info.num_cols) {
            screen_info.x = 0;
            screen_info.y++;
        }
    }
    if (screen_info.y >= screen_info.num_rows)
        clear_screen();
}

void clear_screen(void) {
    for (size_t i = 0; i < fbinfo.memory_size / 4; i++)
        *((uint32_t*) fbinfo.address + i) = screen_info.bg;
    screen_info.x = 0;
    screen_info.y = 0;
}

void init_framebuffer(mb_framebuffer_data_t *fbdata) {
    loaded_font = &_binary_fonts_default_psf_start;
    fbinfo.address = (uint8_t*) FRAMEBUFFER_START;
    fbinfo.bpp = fbdata->framebuffer_bpp;
    fbinfo.height = fbdata->framebuffer_height;
    fbinfo.pitch = fbdata->framebuffer_pitch;
    fbinfo.width = fbdata->framebuffer_width;
    fbinfo.memory_size = fbinfo.height * fbinfo.pitch;
    fbinfo.phys_addr = fbdata->framebuffer_addr;
    screen_info.num_cols = fbinfo.width / loaded_font->width;
    screen_info.num_rows = fbinfo.height / loaded_font->height;
    map_framebuffer();
    FRAMEBUFFER_INITIALIZED = true;
    log(Info, "FRAMEBUFFER", "Initialized framebuffer");
}
