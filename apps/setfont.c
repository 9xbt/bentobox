#include <bentobox/setfont.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

char *name;

void load_font(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror(name);
        exit(EXIT_FAILURE);
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror(name);
        close(fd);
        exit(EXIT_FAILURE);
    }

    void *fontdata = malloc(st.st_size);
    if (read(fd, fontdata, st.st_size) < 0) {
        perror(name);
        free(fontdata);
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);

    struct bb_font_op font_op = {
        .fontlen = st.st_size,
        .fontdata = fontdata
    };

    if (ioctl(STDOUT_FILENO, BBLOADFONT, &font_op) < 0) {
        perror(name);
        free(fontdata);
        exit(EXIT_FAILURE);
    }

    free(fontdata);
}

void print_usage(void) {
    printf(
        "Usage: %s [OPTIONS...] FILE\n"
        "Load the console font\n"
        "\n"
        "Options:\n"
        "     --help      Show this help\n",
        name);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    name = argv[0];
    
    if (argc < 2) {
        print_usage();
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))
            print_usage();
    }

    for (int i = 1; i < argc; i++) {
        load_font(argv[i]);
    }
}