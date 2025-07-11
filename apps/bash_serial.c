#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int main() {
    int file = open("/dev/ttyS0", O_RDWR | O_NOCTTY);
    if (file < 0) {
        perror("failed to open /dev/ttyS0");
        exit(1);
    }
    
    close(0);
    close(1);
    close(2);
    
    if (dup2(file, 0) < 0 ||
        dup2(file, 1) < 0 ||
        dup2(file, 2) < 0) {
        perror("dup2");
        exit(1);
    }
    close(file);
    
    execl("/bin/bash", "bash", NULL);
    
    perror("execl");
    exit(1);
}