#pragma once
#include <limine.h>

struct tar {
    union {
        struct {
            char name[100];
            char mode[8];
            char uid[8];
            char gid[8];
            char size[12];
            char mtime[12];
            char checksum[8];
            char type;
            char link_name[100];
            char ustar[8];
            char owner[32];
            char group[32];
            char major[8];
            char minor[8];
            char prefix[155];
        };
        char block[512];
    };
};

void tar_mount_root(struct tar *tar);
void tar_module(struct limine_file *mod);