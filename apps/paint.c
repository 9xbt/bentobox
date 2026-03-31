#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define swap(src, dest) memcpy(dest, src, fb_size);
#define plot(cv, x, y, c) if ((c) >> 24 && x < vinfo.xres && y < vinfo.yres) cv[y * vinfo.xres_virtual + x] = c;
#define rectangle(cv, x, y, w, h, c) _rectangle(cv, x, y, w, h, c);

struct fb_var_screeninfo vinfo;
size_t fb_size;
uint32_t *back_fb;

void _rectangle(uint32_t *cv, long x, long y, long width, long height, uint32_t color) {
    for (long yy = y; yy < y + height; yy++) {
        for (long xx = x; xx < x + width; xx++) {
            plot(cv, xx, yy, color);
        }
    }
}

int main() {
    int fb = open("/dev/fb0", O_RDWR);
    if (fb == -1) {
        perror("failed to open framebuffer");
        exit(EXIT_FAILURE);
    }
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("ioctl"); 
    }

    fb_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    if ((back_fb = mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0)) == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    int mouse = open("/dev/input/event1", O_RDONLY);
    if (mouse == -1) {
        perror("failed to open mouse");
        exit(EXIT_FAILURE);
    }

    struct input_event ev;
    int x = 0, y = 0, last_x = 0, last_y = 0, left = 0;
    for (;;) {
        ssize_t n = read(mouse, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN)
                continue;
            perror("read");
            break;
        }
        if (n != sizeof(ev))
            continue;

        if (ev.type == EV_REL) {
            if (ev.code == REL_X) x += ev.value;
            if (ev.code == REL_Y) y -= ev.value;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= vinfo.xres) x = vinfo.xres - 1;
            if (y >= vinfo.yres) y = vinfo.yres - 1;
        } else if (ev.type == EV_KEY) {
            if (ev.code == BTN_LEFT) left = ev.value;
        }

        if (!left)
            rectangle(back_fb, last_x, last_y, 10, 10, 0xFF000000);
        rectangle(back_fb, x, y, 10, 10, 0xFFFFFFFF);

        last_x = x, last_y = y;
    }

    close(fb);
    return 0;
}