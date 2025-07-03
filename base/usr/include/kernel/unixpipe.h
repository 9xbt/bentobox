#pragma once
#include <kernel/vfs.h>
#include <kernel/fifo.h>

/**
 * His ideas were implemented in 1973 when ("in one feverish night", wrote
 * McIlroy) Ken Thompson added the pipe() system call and pipes to the shell
 * and several utilities in Version 3 Unix. "The next day", McIlroy continued,
 * "saw an unforgettable orgy of one-liners as everybody joined in the
 * excitement of plumbing." McIlroy also credits Thompson with the | notation,
 * which greatly simplified the description of pipe syntax in Version 4.[6][2]
 */

struct unix_pipe {
    struct vfs_node *read_end;
    struct vfs_node *write_end;

    volatile bool read_closed;
    volatile bool write_closed;

    struct fifo buffer;
};

int unixpipe_new(int fds[2]);