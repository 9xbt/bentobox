#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    int pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return 1;
    }
    if (pid == 0) {
        puts("Hi from child");
        return 0;
    } else {
        puts("Hi from parent");
        //int status;
        //wait(&status);
        return 0;
    }
}