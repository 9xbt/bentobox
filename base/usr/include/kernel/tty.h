#pragma once
#include <stdbool.h>

void tty_enqueue(int c);
long tty_dequeue(bool block);