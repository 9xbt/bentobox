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
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <stdarg.h>

#include <bentobox/compositor.h>
#include <bentobox/list.h>

#define TARGET_FPS 120
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)

#define DECOR_WIDTH  6
#define DECOR_HEIGHT 24

char *name;
bool dirty = false;
FILE *console;
cc_canvas *back_fb, *mid_fb, *front_fb;
list_t *clients;

cc_canvas *titlebar, *close_button, *max_button, *min_button;

void render_decorations(cc_canvas *cv);

cc_canvas *canvas_new(int width, int height, void *buffer) {
    cc_canvas *cv = malloc(sizeof(*cv));
    cv->width = width;
    cv->height = height;
    cv->pitch = width * sizeof(int);
    cv->buffer = buffer ?: malloc(cv->pitch * height);
    return cv;
}

cc_client *client_new(int socket) {
    cc_client *c = malloc(sizeof(*c));
    c->socket = socket;
    c->windows = list_create();
    return c;
}

cc_window *window_new(char *name, int width, int height) {
    cc_window *w = malloc(sizeof(*w));
    w->name = strdup(name);
    w->x = w->y = w->z = 0;
    w->width = width + DECOR_WIDTH;
    w->height = height + DECOR_HEIGHT;
    w->cv = canvas_new(w->width, w->height, NULL);
    render_decorations(w->cv);
    return w;
}

void line(cc_canvas *cv, int x1, int y1, int x2, int y2, uint32_t color) {
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

void rectangle(cc_canvas *cv, long x, long y, size_t width, size_t height, uint32_t color) {
    line(cv, x, y, x + width - 1, y, color);
    line(cv, x, y, x, y + height - 1, color);
    line(cv, x + width - 1, y, x + width - 1, y + height - 1, color);
    line(cv, x, y + height - 1, x + width - 1+ 1, y + height - 1, color);
}

void rectangle_filled(cc_canvas *cv, int x, int y, int width, int height, uint32_t color) {
    for (int yy = y; yy < y + height; yy++) {
        for (int xx = x; xx < x + width; xx++) {
            plot(cv, xx, yy, color);
        }
    }
}

void blit(int x, int y, cc_canvas *from, cc_canvas *to) {
    for (int yy = 0; yy < from->height; yy++) {
        memcpy(&to->buffer[(y + yy) * to->width + x], &from->buffer[yy * from->width], from->width * sizeof(int));
    }
}

void copy_region(int x, int y, int width, int height, cc_canvas *from, cc_canvas *to) {
    for (int yy = 0; yy < height; yy++) {
        memcpy(&to->buffer[(y + yy) * to->width + x], &from->buffer[(y + yy) * from->width + x], width * sizeof(int));
    }
}

void swap_colors(cc_canvas *cv) {
    for (size_t i = 0; i < cv->width * cv->height; i++) {
        cv->buffer[i] = (cv->buffer[i] & 0xFF000000) | ((cv->buffer[i] & 0xFF) << 16) | (cv->buffer[i] & 0xFF00) | ((cv->buffer[i] & 0xFF0000) >> 16);
    }
}

void render_decorations(cc_canvas *cv) {
    rectangle(cv, 0, 0, cv->width, cv->height, 0xff303e43);
    rectangle(cv, 1, 1, cv->width - 2, cv->height - 2, 0xffced7df);
    rectangle(cv, 2, 20, cv->width - 4, cv->height - 22, 0xff637a8a);
    for (int x = 2; x < cv->width - 2; x++) {
        blit(x, 2, titlebar, cv);
    }
    blit(cv->width - 5 - close_button->width, 4, close_button, cv);
    blit(cv->width - 5 - close_button->width - max_button->width, 4, max_button, cv);
    blit(cv->width - 5 - close_button->width - max_button->width - min_button->width, 4, min_button, cv);
}

void render_windows(void) {
    memset(back_fb->buffer, 0, back_fb->pitch * back_fb->height);
    foreach(i, clients) {
        cc_client *c = i->value;
        foreach(j, c->windows) {
            cc_window *w = j->value;
            blit(w->x, w->y, w->cv, back_fb);
        }
    }
    swap(back_fb, mid_fb);
    dirty = true;
}

void render_mouse(int x, int y, int lx, int ly) {
    copy_region(lx, ly, 10, 10, mid_fb, front_fb);
    rectangle_filled(front_fb, x, y, 10, 10, 0xffffffff);
}

void error(const char *s) {
    fprintf(stderr, "%s: %s", name, s);
}

void log_msg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    fprintf(console, "\033[93m%s:\033[0m ", name);
    vfprintf(console, fmt, args);

    va_end(args);
    fflush(console);
}

cc_canvas *load_asset(const char *path, int width, int height) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        error("failed to open ");
        perror(path);
        exit(EXIT_FAILURE);
    }
    cc_canvas *cv = canvas_new(width, height, NULL);
    if (read(fd, cv->buffer, width * height * sizeof(int)) < 0) {
        error("");
        perror("read");
        exit(EXIT_FAILURE);
    }
    swap_colors(cv);
    return cv;
}

void add_client(cc_client *c) {
    list_insert(clients, c);
}

void add_window(cc_client *c, cc_window *w) {
    list_insert(c->windows, w);
    render_windows();
}

void handle_packet(cc_client *c, cc_packet *p) {
    switch (p->type) {
        case CC_CREATE_WINDOW: {
            cc_window_create_packet *packet = (cc_window_create_packet *)p;
            cc_window *w = window_new(packet->name, packet->width, packet->height);
            add_window(c, w);
            break;
        }
        default:
            log_msg("unknown packet type %d\n", p->type);
            break;
    }
}

int main(int argc, char *argv[]) {
    name = argv[0];
    console = fopen("/dev/console", "w");

    int fb = open("/dev/fb0", O_RDWR);
    if (fb == -1) {
        error("failed to open framebuffer: ");
        perror("");
        exit(EXIT_FAILURE);
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        error("");
        perror("ioctl");
        exit(EXIT_FAILURE);
    }

    void *buffer = mmap(0, vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (buffer == MAP_FAILED) {
        error("failed to map framebuffer: ");
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    back_fb  = canvas_new(vinfo.xres, vinfo.yres, NULL);
    mid_fb   = canvas_new(vinfo.xres, vinfo.yres, NULL);
    front_fb = canvas_new(vinfo.xres, vinfo.yres, buffer);

    int mouse = open("/dev/event1", O_RDONLY | O_NONBLOCK);
    if (mouse == -1) {
        error("failed to open mouse: ");
        perror("");
        exit(EXIT_FAILURE);
    }

    titlebar     = load_asset("/usr/share/compositor/titlebar.raw", 1, 18);
    close_button = load_asset("/usr/share/compositor/close.raw", 24, 14);
    max_button   = load_asset("/usr/share/compositor/maximize.raw", 24, 14);
    min_button   = load_asset("/usr/share/compositor/minimize.raw", 24, 14);

    clients = list_create();

    // cc_client *c = client_new(-1, "test", 400, 300);
    // c->x = c->y = 50;
    // render_decorations(c->cv);
    // list_insert(clients, c);

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("failed to create socket: ");
        perror("");
        exit(EXIT_FAILURE);
    }
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    unlink(CC_SOCKET);
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };
    strcpy(addr.sun_path, CC_SOCKET);

    if (bind(sockfd, (const struct sockaddr *)&addr, sizeof addr) < 0) {
        error("failed to bind socket: ");
        perror("");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 20) < 0) {
        error("");
        perror("listen");
        exit(EXIT_FAILURE);
    }

    render_windows();

    struct input_event ev;
    int x = 0, y = 0, last_x = 0, last_y = 0, left = 0, right = 0;

    int drag_off_x = 0, drag_off_y = 0;
    cc_window *drag_target = NULL;

    struct timespec last_frame, current_frame;
    clock_gettime(CLOCK_MONOTONIC, &last_frame);

    struct pollfd mouse_poll = {
        .fd = mouse,
        .events = POLLIN
    };

    int client_sockfd;
    int client_recv_count;
    for (;;) {
        if ((client_sockfd = accept(sockfd, NULL, NULL)) != -1) {
            fcntl(client_sockfd, F_SETFL, O_NONBLOCK);
            add_client(client_new(client_sockfd));
        }

        foreach(i, clients) {
            cc_client *c = i->value;
            cc_packet hdr;
            int ret = recv(c->socket, &hdr, sizeof hdr, 0);
            if (ret < 0) {
                if (errno == EAGAIN)
                    continue;
                error("");
                perror("recv");
                exit(EXIT_FAILURE);
            }
            if (ret < sizeof hdr) {
                log_msg("received bad packet with size %d\n", hdr.length);
                continue;
            }
            cc_packet *packet = malloc(sizeof hdr + hdr.length);
            memcpy(packet, &hdr, sizeof hdr);
            ret = recv(c->socket, (char *)packet + sizeof hdr, hdr.length, 0);
            if (ret < 0) {
                if (errno != EAGAIN)
                    perror("recv");
                free(packet);
                continue;
            }
            if (ret != hdr.length - sizeof hdr) {
                free(packet);
                continue;
            }
            handle_packet(c, packet);
            free(packet);
        }
        
        poll(&mouse_poll, 1, 16);

        ssize_t n = read(mouse, &ev, sizeof(ev));
        if (n < 0) {
            if (errno != EAGAIN) {
                error("");
                perror("read");
                exit(EXIT_FAILURE);
            }
        }
        if (n == sizeof(ev)) {
            if (ev.type == EV_REL) {
                if (ev.code == REL_X) x += ev.value;
                if (ev.code == REL_Y) y -= ev.value;
                if (x < 0) x = 0;
                if (y < 0) y = 0;
                if (x >= vinfo.xres) x = vinfo.xres - 1;
                if (y >= vinfo.yres) y = vinfo.yres - 1;

                if (drag_target) {
                    drag_target->x = x - drag_off_x;
                    drag_target->y = y - drag_off_y;
                }
            } else if (ev.type == EV_KEY) {
                if (ev.code == BTN_LEFT) left = ev.value;
                if (ev.code == BTN_RIGHT) right = ev.value;

                if (left) {
                    foreach(i, clients) {
                        cc_client *c = i->value;
                        foreach(j, c->windows) {
                            cc_window *w = j->value;
                            if (x > w->x && x < w->x + w->width &&
                                y > w->y && y < w->y + w->height) {
                                drag_off_x = x - w->x;
                                drag_off_y = y - w->y;
                                drag_target = w;
                                break; 
                            }
                        }
                    }
                } else {
                    drag_target = NULL;
                }
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &current_frame);
        long elapsed = (current_frame.tv_sec - last_frame.tv_sec) * 1000000000L + (current_frame.tv_nsec - last_frame.tv_nsec);

        if (drag_target && elapsed > FRAME_TIME_NS) {
            clock_gettime(CLOCK_MONOTONIC, &last_frame);
            render_windows();
        }

        if (dirty) {
            swap(mid_fb, front_fb);
            dirty = false;
        }

        render_mouse(x, y, last_x, last_y);

        last_x = x, last_y = y;
    }

    return 0;
}