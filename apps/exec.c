#include <stdio.h>
#include <unistd.h>

int main() {
    printf("Exec\n");

    execl("/bin/hello", 0, NULL, NULL);
    return 0;
}