#pragma once

#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_ACCMODE       0x0003
#define O_CREAT         0x0100
#define O_EXCL          0x0200
#define O_NOCTTY        0x0400
#define O_TRUNC         0x0800
#define O_APPEND        0x1000
#define O_NONBLOCK      0x2000
#define O_DSYNC         0x4000
#define O_SYNC          0x8000
#define O_RSYNC         0x10000
#define O_DIRECTORY     0x20000
#define O_NOFOLLOW      0x40000
#define O_CLOEXEC       0x80000

#define AT_FDCWD  -100

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_DUPFD       0
#define F_GETFD       1
#define F_SETFD       2
#define F_GETFL       3
#define F_SETFL       4
#define F_GETLK       5
#define F_SETLK       6
#define F_SETLKW      7

#define F_DUPFD_CLOEXEC  1030

#define FD_CLOEXEC        1