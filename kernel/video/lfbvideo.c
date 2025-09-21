#include <limine.h>
#include <stddef.h>
#include <stddef.h>
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