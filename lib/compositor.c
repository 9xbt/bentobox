#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>

#include <bentobox/compositor.h>

static int cc_sockfd = -1;

static int cc_initialize(void) {
    if (cc_sockfd != -1)
        return 0;
    if ((cc_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        return -1;

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };
    strcpy(addr.sun_path, CC_SOCKET);
    if (connect(cc_sockfd, (const struct sockaddr *)&addr, sizeof addr) < 0)
        return -1;

    return 0;
}

cc_window *cc_create_window(int width, int height, const char *name) {
    if (cc_initialize() == -1)
        return NULL;

    cc_window_create_packet packet = {
        .hdr.length = sizeof packet,
        .hdr.type = CC_CREATE_WINDOW,
        .width = width,
        .height = height
    };
    packet.hdr.length = sizeof packet;
    strncpy(packet.name, name, sizeof packet.name);
    
    if (send(cc_sockfd, &packet, sizeof packet, 0) < 0)
        return NULL;
    
    cc_window_create_ack ack;
    if (recv(cc_sockfd, &ack, sizeof ack, 0) < 0)
        return NULL;
    
    if (ack.hdr.type != CC_CREATE_WINDOW_ACK)
        return NULL;
    
    cc_window *w = malloc(sizeof(*w));
    w->wid = ack.wid;
    w->width = width;
    w->height = height;
    w->x = w->y = w->z = -1;
    w->cv = malloc(sizeof(*w->cv));
    w->cv->width = width;
    w->cv->height = height;
    w->cv->pitch = width * sizeof(int);
    w->cv->buffer = malloc(w->cv->pitch * height);
    return w;
}