#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    printf("Forking\n");

    pid_t pid = fork();

    if (pid == 0) {
        printf("Child!\n");
        return 42;
    } else {
        printf("Parent!\n");
        int status;
        wait(&status);
        printf("Child exited with status %d.\n", status);
    }
    return 0;
}