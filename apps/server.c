// resol

#define SOCKET_NAME "/tmp/resol.sock"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

int main() {
    int ret;
    int listen_sock;
    int data_sock;
    
    listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("failed to create socket");
        return listen_sock;
    }
    printf("server: created socket\n");

    unlink(SOCKET_NAME);

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };
    strcpy(addr.sun_path, SOCKET_NAME);

    ret = bind(listen_sock, (const struct sockaddr *)&addr, sizeof addr);
    if (ret < 0) {
        perror("failed to bind socket");
        return ret;
    }
    printf("server: bound socket\n");

    printf("server: listening\n");
    ret = listen(listen_sock, 20);
    if (ret < 0) {
        perror("listen");
        return ret;
    }

    char buf[16];
    for (;;) {
        data_sock = accept(listen_sock, NULL, NULL);
        if (data_sock < 0) {
            perror("accept");
            continue;
        }
        printf("server: accepted connection\n");

        ret = recv(data_sock, buf, sizeof buf, 0);
        if (ret < 0) {
            perror("recv");
            continue;
        }
        printf("server: received %d bytes: %16s\n", ret, buf);
    }
}