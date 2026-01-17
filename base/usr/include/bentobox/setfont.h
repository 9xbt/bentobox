#pragma once
#include <stddef.h>

#define BBLOADFONT  0xF001

struct bb_font_op {
    size_t fontlen;
    void  *fontdata;
};