#pragma once

#define POLLIN   0x001
#define POLLOUT  0x004
#define POLLNVAL 0x020

struct pollfd {
	int fd;
	short events;
	short revents;
};