#include <stdio.h>
#include <unistd.h>

int main() {
    printf("Forking\n");

    pid_t pid = fork();

    if (pid == 0) {
        printf("Child!\n");
    } else {
        printf("Parent!\n");
    }
    return 0;
}