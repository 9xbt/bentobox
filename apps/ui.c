#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <termios.h>

#include "ui/root_weave"

int mouse_x, mouse_y;

size_t fb_size;
uint32_t *back_fb, *front_fb;
struct fb_var_screeninfo vinfo = {};

uint8_t *font;
uint32_t *cursor, *background;

#define invert(c) ((~c & 0x00FFFFFF) | (c & 0xFF000000))
#define swap(src, dest) memcpy(dest, src, fb_size);
#define plot(fb, x, y, c) if ((c) >> 24 && x < vinfo.xres && y < vinfo.yres) fb[y * vinfo.xres + x] = c;
#define rectangle(fb, x, y, w, h, c) _rectangle(fb, x, y, w, h, c);
#define string(fb, x, y, c, s) _string(fb, x + 1, y + 1, invert(c), s); _string(fb, x, y, c, s);
#define image(fb, x, y, w, h, i) _image(fb, x, y, w, h, i);
#define stipple(fb, w, h, fg, bg, s) _stipple(fb, w, h, fg, bg, (uint8_t *)s);

void _rectangle(uint32_t *fb, size_t x, size_t y, size_t width, size_t height, uint32_t color) {
    for (int yy = y; yy < y + height; yy++) {
        for (int xx = x; xx < x + width; xx++) {
            plot(fb, xx, yy, color);
        }
    }
}

void _char(uint32_t *fb, size_t x, size_t y, uint32_t color, char c) {
    for (int yy = y; yy < y + 16; yy++) {
        for (int xx = x; xx < x + 8; xx++) {
            if (font[c * 16 + (yy - y)] & (1 << (7 - (xx - x)))) {
                plot(fb, xx, yy, color);
            }
        }
    }
}

void _string(uint32_t *fb, size_t x, size_t y, uint32_t color, char *str) {
    for (int i = 0; i < strlen(str); i++) {
        _char(fb, x + i * 8, y, color, str[i]);
    }
}

void _image(uint32_t *fb, size_t x, size_t y, size_t width, size_t height, uint32_t *image) {
    for (size_t yy = y; yy < y + height; yy++) {
        for (size_t xx = x; xx < x + width; xx++) {
            size_t img_x = xx - x;
            size_t img_y = yy - y;
            plot(fb, xx, yy, image[img_y * width + img_x]);
        }
    }
}

void _stipple(uint32_t *fb, size_t width, size_t height, uint32_t fg, uint32_t bg, uint8_t *stipple) {
    for (int y = 0; y < vinfo.yres; y++) {
        for (int x = 0; x < vinfo.xres; x++) {
            plot(fb, x, y, ((stipple[(y % height)] >> (x % width)) & 1) ? fg : bg);
        }
    }
}

void update(void) {
    swap(background, back_fb);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    char date[64];
    strftime(date, sizeof date, "%-I:%M:%S %p", tm_info);
    string(back_fb, 10, 10, 0xFFFFFFFF, date);

    image(back_fb, mouse_x, mouse_y, 16, 16, cursor);
    swap(back_fb, front_fb);
}

int main(int argc, char *argv[]) {
    int fb = open("/dev/fb0", O_RDWR);
    if (fb == -1) {
        perror("failed to open framebuffer");
        exit(EXIT_FAILURE);
    }
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("ioctl");
    }
    mouse_x = vinfo.xres / 2;
    mouse_y = vinfo.yres / 2;
    fb_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    if (!(front_fb = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0))) {
        perror("failed to map framebuffer");
        exit(EXIT_FAILURE);
    }
    back_fb = malloc(fb_size);

    int keyboard, mouse;
    if ((keyboard = open("/dev/input/event0", O_RDONLY | O_NONBLOCK)) == -1) {
        perror("failed to open keyboard");
        exit(EXIT_FAILURE);
    }
    if ((mouse = open("/dev/input/event1", O_RDONLY | O_NONBLOCK)) == -1) {
        perror("failed to open mouse");
        exit(EXIT_FAILURE);
    }

    FILE *fptr;
    long size;
    if (!(fptr = fopen("/usr/share/fonts/VGA9.F16", "rb"))) {
        perror("failed to open font");
        exit(EXIT_FAILURE);
    }

    fseek(fptr, 0, SEEK_END);
    size = ftell(fptr);
    font = malloc(size);
    rewind(fptr);
    fread(font, 1, size, fptr);
    fclose(fptr);

    if (!(fptr = fopen("/usr/share/icons/cursor.raw", "rb"))) {
        perror("failed to open cursor");
        exit(EXIT_FAILURE);
    }

    fseek(fptr, 0, SEEK_END);
    size = ftell(fptr);
    cursor = malloc(size);
    rewind(fptr);
    fread(cursor, 1, size, fptr);
    fclose(fptr);

    background = malloc(fb_size);
    stipple(background, root_weave_width, root_weave_height, 0xFFBFBFBF, 0xFF7F7F7F, root_weave_bits);

    //string(background, 10, 10, 0xFFFFFFFF, "Hello, world!");

    update();

    struct input_event ev;
    struct pollfd pfd[] = {
        {
            .fd = mouse,
            .events = POLLIN,
        },
        {
            .fd = keyboard,
            .events = POLLIN,
        }
    };

    for (;;) {
        int ret = poll(pfd, sizeof pfd / sizeof(struct pollfd), 100);

        if (ret == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        if (pfd[0].revents & POLLIN) {
            ssize_t bytes = read(mouse, &ev, sizeof(struct input_event));
            if (bytes >= (ssize_t) sizeof(struct input_event)) {
                if (ev.type == EV_REL) {
                    if (ev.code == REL_X) {
                        mouse_x += ev.value;
                    } else if (ev.code == REL_Y) {
                        mouse_y -= ev.value;
                    }

                    if (mouse_x < 0)
                        mouse_x = 0;
                    if (mouse_y < 0)
                        mouse_y = 0;
                    if (mouse_x >= vinfo.xres)
                        mouse_x = vinfo.xres - 1;
                    if (mouse_y >= vinfo.yres)
                        mouse_y = vinfo.yres - 1;
                }
            }
        }
        if (pfd[1].events & POLLIN) {
            ssize_t bytes = read(keyboard, &ev, sizeof(struct input_event));
            if (bytes >= (ssize_t) sizeof(struct input_event)) {
                if (ev.value == 1) {
                    switch (ev.code) {
                        case KEY_ESC:
                            memset(front_fb, 0, fb_size);
                            exit(EXIT_SUCCESS);
                            break;
                    }
                }
            }
        }
        update();
    }

    return EXIT_FAILURE;
}