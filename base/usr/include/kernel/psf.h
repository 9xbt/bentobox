#pragma once
#include <stdint.h>

struct psf1_header {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t glyph_size;
};

struct psf2_header {
    uint8_t magic[4];
    uint32_t version;
    uint32_t header_size;
    uint32_t flags;
    uint32_t length;
    uint32_t glyph_size;
    uint32_t height;
    uint32_t width;
};