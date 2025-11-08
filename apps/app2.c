#include <stdio.h>
#include <stdlib.h>

#include <bentobox/compositor.h>

int main() {
    cc_window *win = cc_create_window(200, 150, "Hello, world!");
    if (!win) {
        perror("cc_create_window");
        exit(EXIT_FAILURE);
    }

    printf("Created window with ID %u\n", win->wid);

    return 0;
}