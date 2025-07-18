#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main() {
    const char *device = "/dev/input/event1";
    struct input_event ev;
    int fd;

    fd = open(device, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    int x = 0, y = 0;
    for (;;) {
        ssize_t bytes = read(fd, &ev, sizeof(struct input_event));
        if (bytes < (ssize_t) sizeof(struct input_event)) {
            continue;
        }

        if (ev.type == EV_REL) {
            if (ev.code == REL_X) {
                x += ev.value;
            } else if (ev.code == REL_Y) {
                y += ev.value;
            }
        }
        printf("X: %d Y: %d\n", x, y);
    }

    close(fd);
    return EXIT_SUCCESS;
}