#include <kernel/string.h>
#include <kernel/lfbvideo.h>

void puts(char *s) {
	flanterm_write(ft_ctx, s, strlen(s));
}