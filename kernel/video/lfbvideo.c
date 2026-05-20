#include <limine.h>
#include <stddef.h>
#include <stddef.h>
#include <kernel/ringbuffer.h>
#include <kernel/lfbvideo.h>
#include <kernel/termios.h>
#include <kernel/version.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/log.h>
#include <kernel/psf.h>
#include <kernel/mmu.h>
#include <flanterm_backends/fb.h>
#define FLANTERM_IN_FLANTERM
#include <flanterm_private.h>
#include <flanterm_backends/fb_private.h>
#include <flanterm.h>

struct limine_framebuffer *framebuffer;
struct flanterm_context *ft_ctx;
static void *font = NULL;

spinlock_t flanterm_lock = 0;

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

static void *ft_malloc(size_t n) {
    return kmalloc(n);
}

static void ft_free(void *ptr, size_t n) {
    (void)n;
    kfree(ptr);
}

void framebuffer_initialize(void) {
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        return;
    }

    framebuffer = framebuffer_request.response->framebuffers[0];

    ft_ctx = flanterm_fb_init(
        ft_malloc,
        ft_free,
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

    log_initialize();
    log_register_sink(write);
}

void framebuffer_get_winsize(struct winsize *ws) {
    if (!ws || !ft_ctx || !framebuffer)
        return;

    ws->ws_row = ft_ctx->rows;
    ws->ws_col = ft_ctx->cols;
    ws->ws_xpixel = framebuffer->width;
    ws->ws_ypixel = framebuffer->height;
}

void framebuffer_get_vinfo(struct fb_var_screeninfo *vinfo) {
    vinfo->xres = framebuffer->width;
    vinfo->yres = framebuffer->height;
    vinfo->xres_virtual = framebuffer->pitch / (framebuffer->bpp / 8);
    vinfo->yres_virtual = framebuffer->height;
    vinfo->bits_per_pixel = framebuffer->bpp;
    vinfo->red.offset = framebuffer->red_mask_shift;
    vinfo->red.length = framebuffer->red_mask_size;
    vinfo->green.offset = framebuffer->green_mask_shift;
    vinfo->green.length = framebuffer->green_mask_size;
    vinfo->blue.offset = framebuffer->blue_mask_shift;
    vinfo->blue.length = framebuffer->blue_mask_size;
}

void framebuffer_get_finfo(struct fb_fix_screeninfo *finfo) {
    strcpy(finfo->id, __kernel_name);
    finfo->smem_start = (unsigned long)framebuffer->address;
    finfo->smem_len = framebuffer->pitch * framebuffer->height;
    finfo->type = FB_TYPE_PACKED_PIXELS;
    finfo->visual = FB_VISUAL_TRUECOLOR;
    finfo->line_length = framebuffer->pitch;
    finfo->accel = FB_ACCEL_NONE;
}

void framebuffer_get_font_size(size_t *width, size_t *height) {
    struct flanterm_fb_context *fb_ctx = (struct flanterm_fb_context *)ft_ctx;
    *width = fb_ctx->font_width;
    *height = fb_ctx->font_height;
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

void framebuffer_setfont(const void *fontdata, size_t fontlen) {
    if (font)
        kfree(font);
    font = kmalloc(fontlen);
    memcpy(font, fontdata, fontlen);

    struct psf1_header *psf1 = font;
    struct psf2_header *psf2 = font;
    void *bitmap = NULL;
    int width = 8, height = 16;

    if (psf1->magic[0] == 0x36 &&
        psf1->magic[1] == 0x04) {
        bitmap = font + sizeof(struct psf1_header);
    } else if (
        psf2->magic[0] == 0x72 &&
        psf2->magic[1] == 0xb5 &&
        psf2->magic[2] == 0x4a &&
        psf2->magic[3] == 0x86) {
        height = psf2->height;
        width = psf2->width;
        bitmap = font + psf2->header_size;
    } else {
        bitmap = font;
    }

    struct flanterm_fb_context *fb_ctx = (struct flanterm_fb_context *)ft_ctx;

    size_t x = fb_ctx->cursor_x, y = fb_ctx->cursor_y, grid_size = fb_ctx->grid_size;
    struct flanterm_fb_char *grid = kmalloc(grid_size);
    memcpy(grid, fb_ctx->grid, grid_size);

    flanterm_deinit(ft_ctx, ft_free);
    ft_ctx = flanterm_fb_init(
        ft_malloc,
        ft_free,
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
        bitmap, width, height, 1,
        0, 0,
        0
    );
    fb_ctx = (struct flanterm_fb_context *)ft_ctx;

    memcpy(fb_ctx->grid, grid, MIN(grid_size, fb_ctx->grid_size));
    fb_ctx->cursor_x = x, fb_ctx->cursor_y = y;
    ft_ctx->full_refresh(ft_ctx);
    kfree(grid);
}