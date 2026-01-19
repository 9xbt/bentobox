#include <X11/Xlib.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>

int main() {
    Display *dpy = XOpenDisplay(NULL);
    Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                                      0, 0, 400, 300, 0, 0, 0xffffff);
    XMapWindow(dpy, win);
    XFlush(dpy);
    
    printf("Window created. Close it from TWM.\n");
    
    int fd = ConnectionNumber(dpy);
    printf("Socket fd: %d\n", fd);
    
    struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLHUP | POLLERR | POLLRDHUP };
    
    while (1) {
        int ret = poll(&pfd, 1, 500);
        if (ret < 0) {
            printf("poll error: %d\n", errno);
            break;
        }
        if (ret == 0) {
            printf(".\n");
            continue;
        }
        
        printf("poll: revents=0x%x IN=%d HUP=%d ERR=%d RDHUP=%d\n",
               pfd.revents,
               !!(pfd.revents & POLLIN),
               !!(pfd.revents & POLLHUP),
               !!(pfd.revents & POLLERR),
               !!(pfd.revents & POLLRDHUP));
        
        if (pfd.revents & POLLIN) {
            char buf[4096];
            int n = read(fd, buf, sizeof(buf));
            printf("read() returned %d (errno=%d)\n", n, errno);
            if (n == 0) {
                printf("EOF on socket - peer closed connection\n");
                break;
            }
            if (n < 0) {
                printf("read error\n");
                break;
            }
        }
        
        if (pfd.revents & (POLLHUP | POLLERR | POLLRDHUP)) {
            printf("Connection terminated\n");
            break;
        }
    }
    
    printf("Exited\n");
    return 0;
}