#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <bentobox/compositor.h>

int main() {
    int sockfd;

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };
    strcpy(addr.sun_path, CC_SOCKET);
    if (connect(sockfd, (const struct sockaddr *)&addr, sizeof addr) < 0) {
        perror(CC_SOCKET);
        exit(EXIT_FAILURE);
    }

    cc_window_create_packet packet = {
        .hdr.length = sizeof packet,
        .hdr.type = CC_CREATE_WINDOW,
        .width = 200,
        .height = 150,
        .name = "Hello, world!"
    };
    packet.hdr.length = sizeof packet;
    
    if (send(sockfd, &packet, sizeof packet, 0) < 0) {
        perror("send");
        exit(EXIT_FAILURE);
    }
}