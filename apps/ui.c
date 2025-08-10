#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
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

#include "kernel/list.h"
#include "ui/root_weave"
#include "ui/list"

#define FPS_CAP 180

typedef struct canvas {
    long width;
    long height;
    size_t size;
    uint32_t *data;
} canvas_t;

typedef struct window {
    long x, y;
    char *name;
    struct canvas cv;
    struct canvas decor;
} window_t;

int mouse_x, mouse_y;

bool dragging = false;
int drag_offset_x = 0, drag_offset_y = 0;
window_t *drag_target = NULL;

canvas_t back_cv, front_cv;
struct fb_var_screeninfo vinfo = {};

uint8_t *font;
canvas_t cursor, background;

#define invert(c) ((~c & 0x00FFFFFF) | (c & 0xFF000000))
#define swap(src, dest) memcpy((dest)->data, (src)->data, (src)->size);
#define plot(cv, x, y, c) if ((c) >> 24 && x < cv->width && y < cv->height) cv->data[y * cv->width + x] = c;
#define rectangle(cv, x, y, w, h, c) _rectangle_outline(cv, x, y, w, h, c);
#define rectangle_filled(cv, x, y, w, h, c) _rectangle(cv, x, y, w, h, c);
#define string(cv, x, y, c, s) _string(cv, x + 1, y + 1, invert(c), s); _string(cv, x, y, c, s);
#define image(cv, x, y, i) _image(cv, x, y, i);
#define stipple(cv, w, h, fg, bg, s) _stipple(cv, w, h, fg, bg, (uint8_t *)s);
#define line(cv, x1, y1, x2, y2, c) _line(cv, x1, y1, x2, y2, c);

void _rectangle(canvas_t *cv, long x, long y, long width, long height, uint32_t color) {
    for (long yy = y; yy < y + height; yy++) {
        for (long xx = x; xx < x + width; xx++) {
            plot(cv, xx, yy, color);
        }
    }
}

void _char(canvas_t *cv, long x, long y, uint32_t color, char c) {
    for (long yy = y; yy < y + 16; yy++) {
        for (long xx = x; xx < x + 8; xx++) {
            if (font[c * 16 + (yy - y)] & (1 << (7 - (xx - x)))) {
                plot(cv, xx, yy, color);
            }
        }
    }
}

void _string(canvas_t *cv, long x, long y, uint32_t color, char *str) {
    for (size_t i = 0; i < strlen(str); i++) {
        _char(cv, x + i * 8, y, color, str[i]);
    }
}

void _image(canvas_t *cv, long x, long y, canvas_t *image) {
    long sx = 0, sy = 0;
    if (x < 0) sx = -x, x = 0;
    if (y < 0) sy = -y, y = 0;

    for (long yy = y; yy < y + image->height - sy && yy < cv->height; yy++) {
        for (long xx = x; xx < x + image->width - sx && xx < cv->width; xx++) {
            plot(cv, xx, yy, image->data[(yy - y + sy) * image->width + (xx - x + sx)]);
        }
    }
}


void _stipple(canvas_t *cv, size_t width, size_t height, uint32_t fg, uint32_t bg, uint8_t *stipple) {
    for (long y = 0; y < vinfo.yres; y++) {
        for (long x = 0; x < vinfo.xres; x++) {
            plot(cv, x, y, ((stipple[(y % height)] >> (x % width)) & 1) ? fg : bg);
        }
    }
}

void _line(canvas_t *cv, int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;

    do {
        plot(cv, x1, y1, color);

        int e2 = err;

        if (e2 > -dx) {
            err -= dy;
            x1 += sx;
        }

        if (e2 < dy) {
            err += dx;
            y1 += sy;
        }
    } while (x1 != x2 || y1 != y2);
}

void _rectangle_outline(canvas_t *cv, long x, long y, size_t width, size_t height, uint32_t color) {
    width--;
    height--;
    _line(cv, x, y, x + width, y, color);
    _line(cv, x, y, x, y + height, color);
    _line(cv, x + width, y, x + width, y + height, color);
    _line(cv, x, y + height, x + width + 1, y + height, color);
}

canvas_t canvas_create(long width, long height) {
    canvas_t cv;
    cv.width = width;
    cv.height = height;
    cv.size = width * height * 4;
    cv.data = malloc(cv.size);
    return cv;
}

list_t *clients;

window_t *spawn(char *name, long width, long height) {
    window_t *client = malloc(sizeof(window_t));
    client->y = 0;
    client->y = 0;
    client->name = malloc(strlen(name + 1));
    strcpy(client->name, name);
    client->cv = canvas_create(width - 2, height - 23);
    client->decor = canvas_create(width, height);

    rectangle_filled(&client->cv, 0, 0, width, height, 0xFFEEEEEE);

    rectangle_filled(&client->decor, 0, 0, width, 23, 0xFFFFFFFF);
    rectangle(&client->decor, 0, 0, width, height, 0xFF000000);
    string(&client->decor, 5 + 16, 5, 0xFF000000, name);

    list_insert(clients, client);
    return client;
}

void kill(window_t *client) {
    free(client->name);
    free(client->cv.data);
    free(client->decor.data);
    free(client);
    list_remove_value(clients, client);
}

void update(void) {
    swap(&background, &back_cv);

    time_t now_time = time(NULL);
    struct tm *tm_info = localtime(&now_time);

    char date[64];
    strftime(date, sizeof date, "%-I:%M:%S %p", tm_info);
    string(&back_cv, 10, 10, 0xFFFFFFFF, date);

    foreach(item, clients) {
        window_t *client = item->value;
        image(&back_cv, client->x + 1, client->y + 23, &client->cv);
        image(&back_cv, client->x, client->y, &client->decor);
    }

    image(&back_cv, mouse_x, mouse_y, &cursor);
    swap(&back_cv, &front_cv);
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
    size_t fb_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    
    front_cv.width = vinfo.xres;
    front_cv.height = vinfo.yres;
    front_cv.size = fb_size;
    if (!(front_cv.data = mmap(NULL, front_cv.size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0))) {
        perror("failed to map framebuffer");
        exit(EXIT_FAILURE);
    }
    back_cv.width = vinfo.xres;
    back_cv.height = vinfo.yres;
    back_cv.size = fb_size;
    back_cv.data = malloc(fb_size);

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
    cursor.width = 16;
    cursor.height = 16;
    cursor.size = size;
    cursor.data = malloc(size);
    rewind(fptr);
    fread(cursor.data, 1, size, fptr);
    fclose(fptr);

    background.width = vinfo.xres;
    background.height = vinfo.yres;
    background.size = fb_size;
    background.data = malloc(fb_size);
    stipple(&background, root_weave_width, root_weave_height, 0xFFBFBFBF, 0xFF7F7F7F, root_weave_bits);

    clients = list_create();

    window_t *hello = spawn("bentobox", 240, 160);
    string(&hello->cv, 5, 0, 0xFF000000, "Hello, world!");

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
        if (poll(pfd, sizeof pfd / sizeof(struct pollfd), 16) == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        while (pfd[0].revents & POLLIN) {
            ssize_t bytes = read(mouse, &ev, sizeof(struct input_event));
            if (bytes < (ssize_t) sizeof(struct input_event)) break;

            if (ev.type == EV_REL) {
                if (ev.code == REL_X) mouse_x += ev.value;
                else if (ev.code == REL_Y) mouse_y -= ev.value;

                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x >= (int)vinfo.xres) mouse_x = vinfo.xres - 1;
                if (mouse_y >= (int)vinfo.yres) mouse_y = vinfo.yres - 1;
                if (dragging && drag_target) {
                    drag_target->x = mouse_x - drag_offset_x;
                    drag_target->y = mouse_y - drag_offset_y;
                }
            } else if (ev.type == EV_KEY && ev.code == BTN_LEFT) {
                if (ev.value == 1) {
                    foreach(item, clients) {
                        window_t *client = item->value;
                        if (mouse_x >= client->x && mouse_x < client->x + client->cv.width &&
                            mouse_y >= client->y && mouse_y < client->y + client->cv.height) {
                            drag_target = client;
                            drag_offset_x = mouse_x - client->x;
                            drag_offset_y = mouse_y - client->y;
                            dragging = true;
                            break;
                        }
                    }
                } else if (ev.value == 0) {
                    dragging = true;
                    drag_target = NULL;
                }
            }
        }

        while (pfd[1].revents & POLLIN) {
            ssize_t bytes = read(keyboard, &ev, sizeof(struct input_event));
            if (bytes < (ssize_t) sizeof(struct input_event)) break;
            
            if (ev.value == 1) {
                switch (ev.code) {
                    case KEY_ESC:
                        memset(front_cv.data, 0, fb_size);
                        exit(EXIT_SUCCESS);
                }
            }
        }
        update();
    }

    return EXIT_FAILURE;
}