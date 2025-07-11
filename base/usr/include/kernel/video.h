#pragma once
#include <stdint.h>
#include <stdatomic.h>
#include <ioctls.h>
#include <kernel/multiboot.h>
#include <kernel/3rdparty/flanterm.h>

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

void framebuffer_setfont(const char *fontdata, size_t fontlen);
void framebuffer_get_winsize(struct winsize *ws);
void framebuffer_initialize(void);