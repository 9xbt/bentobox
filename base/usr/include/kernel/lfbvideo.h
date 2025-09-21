#pragma once
#include <kernel/termios.h>
#include <flanterm.h>

extern struct flanterm_context *ft_ctx;

void framebuffer_initialize(void);
void framebuffer_get_winsize(struct winsize *ws);