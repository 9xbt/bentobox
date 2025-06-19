#include <kernel/arch/x86_64/vga.h>
#include <kernel/mmu.h>
#include <kernel/lfb.h>
#include <kernel/psf.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/assert.h>
#include <kernel/flanterm.h>
#include <kernel/multiboot.h>

struct framebuffer lfb;
struct flanterm_context *ft_ctx;

static int alloc_n = 0, grid_size;
struct flanterm_fb_context *fb_ctx;
struct flanterm_fb_char *grid;
static void *font = NULL;

static void *lfb_malloc(size_t count) {
    void *ptr = kmalloc(count);
    /* dirty hacks*/
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

static void lfb_free(void *ptr, size_t count) {
    kfree(ptr);
}

void lfb_initialize(void) {
#ifdef __x86_64__
    extern void *mboot;
    struct multiboot_tag_framebuffer *fb = mboot2_find_tag(mboot, MULTIBOOT_TAG_TYPE_FRAMEBUFFER);

    if (!fb || fb->common.framebuffer_addr == 0xB8000) {
        dprintf("%s:%d: framebuffer not found\n", __FILE__, __LINE__);
        vga_disable_cursor();
        vga_copy_to_text();
        return;
    }
    dprintf("%s:%d: found framebuffer at 0x%p\n", __FILE__, __LINE__, fb->common.framebuffer_addr);

    mmu_map_pages((ALIGN_UP((fb->common.framebuffer_pitch * fb->common.framebuffer_height), PAGE_SIZE) / PAGE_SIZE), VIRTUAL(fb->common.framebuffer_addr), (void *)fb->common.framebuffer_addr, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    lfb.addr = (uint64_t)VIRTUAL(fb->common.framebuffer_addr);
    lfb.width = fb->common.framebuffer_width;
    lfb.height = fb->common.framebuffer_height;
    lfb.pitch = fb->common.framebuffer_pitch;
    lfb.fb = fb;

    ft_ctx = flanterm_fb_init(
        lfb_malloc,
        lfb_free,
        (uint32_t *)lfb.addr,
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

    vga_copy_to_framebuffer();
#else
    unimplemented;
#endif
}

void lfb_change_font(const char *path) {
    struct vfs_node *file = vfs_open(NULL, path, true);
    if (!file) return;

    if (font) kfree(font);
    font = kmalloc(file->size);
    vfs_read(file, font, 0, file->size);

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
    flanterm_deinit(ft_ctx, lfb_free);
    ft_ctx = flanterm_fb_init(
        lfb_malloc,
        lfb_free,
        (uint32_t *)lfb.addr,
        lfb.fb->common.framebuffer_width,
        lfb.fb->common.framebuffer_height,
        lfb.fb->common.framebuffer_pitch,
        lfb.fb->framebuffer_red_mask_size,
        lfb.fb->framebuffer_red_field_position,
        lfb.fb->framebuffer_green_mask_size,
        lfb.fb->framebuffer_green_field_position,
        lfb.fb->framebuffer_blue_mask_size,
        lfb.fb->framebuffer_blue_field_position,
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
    vfs_close(file);

    printf("\033[%d;%dH\n", y, x);
}

void lfb_get_ws(struct winsize *ws) {
    if (!ws) return;

    ws->ws_row = ft_ctx->rows;
    ws->ws_col = ft_ctx->cols;
    ws->ws_xpixel = lfb.width;
    ws->ws_ypixel = lfb.height;
}