int main() {
    int *ptr = (int *)0xdeadbeef;
    *ptr = 42;
    return 0;
}