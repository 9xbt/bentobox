#pragma once
#include <stdint.h>
#include <sys/time.h>

#define S_IFMT   0xF000
#define S_IFIFO  0x1000
#define S_IFCHR  0x2000
#define S_IFDIR  0x4000
#define S_IFBLK  0x6000
#define S_IFREG  0x8000
#define S_IFLNK  0xA000
#define S_IFSOCK 0xC000
#define S_IFWHT  0xE000

#define S_IRUSR 0x100
#define S_IWUSR 0x80
#define S_IXUSR 0x40

#define S_IRGRP 0x20
#define S_IWGRP 0x10
#define S_IXGRP 0x8

#define S_IROTH 0x4
#define S_IWOTH 0x2
#define S_IXOTH 0x1

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

struct stat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;

	uint32_t st_mode;
	uint32_t st_uid;
	uint32_t st_gid;
	unsigned int __pad0;
	uint64_t st_rdev;
	int64_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;

	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	long __unused[3];
};