#pragma once
#include <stdint.h>
#include <stdatomic.h>
#include <kernel/ioctl.h>
#include <kernel/flanterm.h>
#include <kernel/multiboot.h>

struct framebuffer {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    struct multiboot_tag_framebuffer *fb;
};

extern struct framebuffer framebuffer;
extern struct flanterm_context *ft_ctx;
extern struct fb_var_screeninfo vinfo;

void fb_draw_char(struct framebuffer *fb, uint32_t x, uint32_t y, uint8_t c, uint32_t fore, uint32_t back);
void lfb_change_font(const char *path);
void lfb_get_ws(struct winsize *ws);
void framebuffer_initialize(void);