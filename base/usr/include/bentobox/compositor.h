#pragma once
#include <stdint.h>

#include <bentobox/list.h>

#define CC_SOCKET "/tmp/compositor.sock"

#define swap(src, dest) memcpy(dest->buffer, src->buffer, src->pitch * src->height);
#define plot(cv, x, y, c) if ((c) >> 24 && x < cv->width && y < cv->height) cv->buffer[y * cv->width + x] = c;

typedef enum {
    CC_NONE,
    CC_CREATE_WINDOW
} cc_packet_type;

typedef struct {
    int width;
    int height;
    int pitch;
    uint32_t *buffer;
} cc_canvas;

typedef struct {
    int socket;
    list_t *windows;
} cc_client;

typedef struct {
    char *name;
    int x, y, z;
    int width;
    int height;
    cc_canvas *cv;
} cc_window;

typedef struct {
    uint16_t length;
    uint8_t type;
} cc_packet;

typedef struct {
    cc_packet hdr;
    int width;
    int height;
    char name[64];
} cc_window_create_packet;