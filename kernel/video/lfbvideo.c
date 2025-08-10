#include <kernel/arch/x86_64/serial.h>
#include <kernel/arch/x86_64/vga.h>
#include <kernel/multiboot.h>
#include <kernel/assert.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/video.h>
#include <kernel/mmu.h>
#include <kernel/psf.h>

#define FLANTERM_IN_FLANTERM
#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>

struct framebuffer framebuffer;
struct flanterm_context *ft_ctx = NULL;

static int alloc_n = 0, grid_size;
struct flanterm_fb_context *fb_ctx;
struct flanterm_fb_char *grid;
static void *font = NULL;

static void *ft_malloc(size_t count) {
    void *ptr = kmalloc(count);
    /* dirty hacks */
    if (alloc_n == 0) {
        fb_ctx = ptr;
    }
    if (alloc_n == 3) {
        grid = ptr;
        grid_size = count;
    }
    alloc_n++;
    return ptr;
}

static void ft_free(void *ptr, size_t count) {
    kfree(ptr);
}

void framebuffer_setfont(const char *fontdata, size_t fontlen) {
    if (!ft_ctx) return;
    if (font) kfree(font);
    font = kmalloc(fontlen);
    memcpy(font, fontdata, fontlen);

    struct psf1_header *psf1 = font;
    struct psf2_header *psf2 = font;
    void *vga = NULL;
    int font_width = 8, font_height = 16;

    if (psf1->magic[0] == 0x36 &&
        psf1->magic[1] == 0x04) {
        vga = font + sizeof(struct psf1_header);
    } else if (
        psf2->magic[0] == 0x72 &&
        psf2->magic[1] == 0xb5 &&
        psf2->magic[2] == 0x4a &&
        psf2->magic[3] == 0x86) {
        font_height = psf2->height;
        font_width = psf2->width;
        vga = font + psf2->header_size;
    } else {
        vga = font;
    }

    size_t x = fb_ctx->cursor_x, y = fb_ctx->cursor_y;
    struct flanterm_fb_char *copy = kmalloc(grid_size);
    memcpy(copy, grid, grid_size);

    alloc_n = 0;
    flanterm_deinit(ft_ctx, ft_free);
    ft_ctx = flanterm_fb_init(
        ft_malloc,
        ft_free,
        (uint32_t *)framebuffer.addr,
        framebuffer.fb->common.framebuffer_width,
        framebuffer.fb->common.framebuffer_height,
        framebuffer.fb->common.framebuffer_pitch,
        framebuffer.fb->framebuffer_red_mask_size,
        framebuffer.fb->framebuffer_red_field_position,
        framebuffer.fb->framebuffer_green_mask_size,
        framebuffer.fb->framebuffer_green_field_position,
        framebuffer.fb->framebuffer_blue_mask_size,
        framebuffer.fb->framebuffer_blue_field_position,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        vga, font_width, font_height, 1,
        0, 0,
        0
    );

    for (int i = 0; i < grid_size / (signed)sizeof(struct flanterm_fb_char); i++) {
        if (copy[i].c) {
            uint32_t bg = ((copy[i].bg >> 24) & 0xFF) ? 0 : copy[i].bg;
            uint32_t fg = copy[i].fg;
            printf("\033[48;2;%d;%d;%dm\033[38;2;%d;%d;%dm%c",
                (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF,
                (fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF,
                copy[i].c);
        }
    }

    kfree(copy);
    printf("\033[%d;%dH\n", y, x);
}

void framebuffer_get_winsize(struct winsize *ws) {
    if (!ws) return;
    if (!ft_ctx) {
        ws->ws_row = 25;
        ws->ws_col = 80;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return;
    }

    ws->ws_row = ft_ctx->rows;
    ws->ws_col = ft_ctx->cols;
    ws->ws_xpixel = framebuffer.width;
    ws->ws_ypixel = framebuffer.height;
}

void framebuffer_initialize(void) {
#ifdef __x86_64__
    extern void *mboot;
    struct multiboot_tag_framebuffer *fb = mboot2_find_tag(mboot, MULTIBOOT_TAG_TYPE_FRAMEBUFFER);

    if (!fb || fb->common.framebuffer_addr == 0xB8000) {
        dprintf(6, "%s:%d: framebuffer not found\n", __FILE__, __LINE__);
        vga_enable_cursor();
        return;
    }
    dprintf(6, "%s:%d: found framebuffer at 0x%p\n", __FILE__, __LINE__, fb->common.framebuffer_addr);

    mmu_map_pages((ALIGN_UP((fb->common.framebuffer_pitch * fb->common.framebuffer_height), PAGE_SIZE) / PAGE_SIZE), VIRTUAL(fb->common.framebuffer_addr), (void *)fb->common.framebuffer_addr, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    framebuffer.addr = (uint64_t)VIRTUAL(fb->common.framebuffer_addr);
    framebuffer.width = fb->common.framebuffer_width;
    framebuffer.height = fb->common.framebuffer_height;
    framebuffer.pitch = fb->common.framebuffer_pitch;
    framebuffer.fb = fb;

    vinfo.xres = framebuffer.width;
    vinfo.yres = framebuffer.height;
    vinfo.xres_virtual = framebuffer.width;
    vinfo.yres_virtual = framebuffer.height;
    vinfo.bits_per_pixel = fb->common.framebuffer_bpp;
    vinfo.red.offset = fb->framebuffer_red_field_position;
    vinfo.red.length = fb->framebuffer_red_mask_size;
    vinfo.green.offset = fb->framebuffer_green_field_position;
    vinfo.green.length = fb->framebuffer_green_mask_size;
    vinfo.blue.offset = fb->framebuffer_blue_field_position;
    vinfo.blue.length = fb->framebuffer_blue_mask_size;

    ft_ctx = flanterm_fb_init(
        ft_malloc,
        ft_free,
        (uint32_t *)framebuffer.addr,
        fb->common.framebuffer_width,
        fb->common.framebuffer_height,
        fb->common.framebuffer_pitch,
        fb->framebuffer_red_mask_size,
        fb->framebuffer_red_field_position,
        fb->framebuffer_green_mask_size,
        fb->framebuffer_green_field_position,
        fb->framebuffer_blue_mask_size,
        fb->framebuffer_blue_field_position,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, 0, 0, 1,
        0, 0,
        0
    );

    flanterm_write(ft_ctx, serial_ringbuffer, strlen(serial_ringbuffer));
#else
    unimplemented;
#endif
}