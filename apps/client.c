// reep

#define SOCKET_NAME "/tmp/resol.sock"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

int main() {
    int ret;
    int recv_sock;
    int data_sock;

    recv_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (recv_sock < 0) {
        perror("failed to create socket");
        return recv_sock;
    }
    printf("client: created socket\n");

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };
    strcpy(addr.sun_path, SOCKET_NAME);

    printf("client: connecting to socket\n");
    ret = connect(recv_sock, (const struct sockaddr *)&addr, sizeof addr);
    if (ret < 0) {
        perror("failed to connect");
        return ret;
    }
    printf("client: connected\n");

    char buf[16] = "Hello, world!";
    ret = send(recv_sock, buf, sizeof buf, 0);
    if (ret < 0) {
        perror("failed to send data");
        return ret;
    }
}