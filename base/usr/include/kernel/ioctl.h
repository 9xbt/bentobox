#pragma once

#include <sys/termios.h>
#include <asm-generic/ioctls.h>
#include <linux/kd.h>
#include <linux/fb.h>

#define TIOCGNAME       0x5483
#define KDFONTOP        0x4B72
#define PIO_UNIMAP	    0x4B67
#define PIO_UNIMAPCLR   0x4B68

#define KD_FONT_OP_SET          0
#define KD_FONT_OP_GET          1
#define KD_FONT_OP_SET_DEFAULT  2
#define KD_FONT_OP_COPY         3

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};;