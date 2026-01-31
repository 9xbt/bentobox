#pragma once
#include <stdint.h>
#include <stddef.h>

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define NCCS 32

typedef uint32_t tcflag_t;
typedef uint8_t cc_t;
typedef uint32_t speed_t;

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

#define BRKINT  0x00000002
#define ICRNL   0x00000100
#define IXON    0x00000400

#define OPOST   0x00000001
#define ONLCR   0x00000002

#define CS8     0x00000030
#define CREAD   0x00000800

#define ISIG    0x00000001
#define ICANON  0x00000002
#define ECHO    0x00000008
#define ECHOE   0x00000010
#define ECHOK   0x00000020
#define IEXTEN  0x00008000

#define VINTR   0
#define VQUIT   1
#define VERASE  2
#define VKILL   3
#define VEOF    4
#define VTIME   5
#define VMIN    6
#define VSTART  7
#define VSTOP   8
#define VSUSP   9

#define TCGETS  	0x5401
#define TCSETS  	0x5402
#define TCSETSW 	0x5403
#define TCXONC		0x540A
#define TIOCSCTTY	0x540E

#define TIOCGWINSZ 	0x5413
#define TIOCSWINSZ 	0x5414
#define TIOCGPGRP  	0x540F
#define TIOCSPGRP  	0x5410

#define TIOCGPTN   	0x80045430
#define TIOCSPTLCK 	0x40045431