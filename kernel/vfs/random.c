#include <stddef.h>
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

typedef struct {
    uint64_t x[2];
} XorShift128pState;

uint64_t xorshift128p(XorShift128pState* state) {
	uint64_t t = state->x[0];
	const uint64_t s = state->x[1];
	state->x[0] = s;
	t ^= t << 24;
	t ^= t >> 17;
	t ^= s ^ (s >> 5);
	state->x[1] = t;
	return t + s;
}

static XorShift128pState urandom_state;

long urandom_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;
    (void)offset;

    uint8_t *buf = (uint8_t *)buffer;
    size_t i = 0;

    while (i + 8 <= len) {
        uint64_t rnd = xorshift128p(&urandom_state);
        memcpy(buf + i, &rnd, 8);
        i += 8;
    }
    if (i < len) {
        uint64_t rnd = xorshift128p(&urandom_state);
        memcpy(buf + i, &rnd, len - i);
    }

    return (long)len;
}

vfs_ops_t urandom_ops = {
    .read = urandom_read
};

void random_initialize(void) {
    uptime(&urandom_state.x[0], &urandom_state.x[1]);
    
    vfs_node_t *urandom = devfs_create_node("urandom", VFS_CHARDEVICE);
    urandom->perms = 0666;
    urandom->ops = &urandom_ops;
}