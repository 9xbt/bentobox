#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

int main() {
    int fb = open("/dev/fb0", O_RDWR);
    if (fb == -1) {
        perror("failed to open framebuffer");
        exit(EXIT_FAILURE);
    }
    void *fb_mem = mmap(NULL, 100000, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    memset(fb_mem, 0xFF, 100000);

    close(fb);
    puts("fbtest: Success!");
    return EXIT_SUCCESS;
}