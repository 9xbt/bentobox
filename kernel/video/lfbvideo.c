#include <limine.h>
#include <stddef.h>
#include <stddef.h>
#include <kernel/lfbvideo.h>
#include <kernel/termios.h>
#include <flanterm_backends/fb.h>
#define FLANTERM_IN_FLANTERM
#include <flanterm_private.h>
#include <flanterm.h>

struct limine_framebuffer *framebuffer;
struct flanterm_context *ft_ctx;

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

void framebuffer_initialize(void) {
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        return;
    }

    framebuffer = framebuffer_request.response->framebuffers[0];

    ft_ctx = flanterm_fb_init(
        NULL,
        NULL,
        framebuffer->address,
        framebuffer->width,
        framebuffer->height,
        framebuffer->pitch,
        framebuffer->red_mask_size,
        framebuffer->red_mask_shift,
        framebuffer->green_mask_size,
        framebuffer->green_mask_shift,
        framebuffer->blue_mask_size,
        framebuffer->blue_mask_shift,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, 0, 0, 1,
        0, 0,
        0
    );
}

void framebuffer_get_winsize(struct winsize *ws) {
    if (!ws) return;

    ws->ws_row = ft_ctx->rows;
    ws->ws_col = ft_ctx->cols;
    ws->ws_xpixel = framebuffer->width;
    ws->ws_ypixel = framebuffer->height;
}

void framebuffer_get_vinfo(struct fb_var_screeninfo *vinfo) {
    vinfo->xres = framebuffer->width;
    vinfo->yres = framebuffer->height;
    vinfo->xres_virtual = framebuffer->width;
    vinfo->yres_virtual = framebuffer->height;
    vinfo->bits_per_pixel = framebuffer->bpp;
    vinfo->red.offset = framebuffer->red_mask_shift;
    vinfo->red.length = framebuffer->red_mask_size;
    vinfo->green.offset = framebuffer->green_mask_shift;
    vinfo->green.length = framebuffer->green_mask_size;
    vinfo->blue.offset = framebuffer->blue_mask_shift;
    vinfo->blue.length = framebuffer->blue_mask_size;
}

void framebuffer_draw_cursor(int x, int y) {
    static int last_x = -1, last_y = -1;

    if (last_x >= 0 && last_y >= 0) {
        for (int dy = 0; dy < 10; dy++) {
            for (int dx = 0; dx < 10; dx++) {
                if (last_x + dx < (int)framebuffer->width && last_y + dy < (int)framebuffer->height) {
                    uint32_t *pixel = &((uint32_t *)framebuffer->address)[(last_y + dy) * framebuffer->width + (last_x + dx)];
                    *pixel ^= 0xFFFFFF;
                }
            }
        }
    }
    
    if (x >= 0 && y >= 0) {
        for (int dy = 0; dy < 10; dy++) {
            for (int dx = 0; dx < 10; dx++) {
                if (x + dx < (int)framebuffer->width && y + dy < (int)framebuffer->height) {
                    uint32_t *pixel = &((uint32_t *)framebuffer->address)[(y + dy) * framebuffer->width + (x + dx)];
                    *pixel ^= 0xFFFFFF;
                }
            }
        }
    }
    
    last_x = x, last_y = y;
}