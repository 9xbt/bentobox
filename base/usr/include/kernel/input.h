#pragma once
#include <stdint.h>
#include <kernel/time.h>

#define EV_SYN     0x00
#define EV_KEY     0x01
#define EV_REL     0x02
#define EV_ABS     0x03
#define REL_X      0x00
#define REL_Y      0x01
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

#define SYN_REPORT		0

#define KEY_A      0x1e
#define KEY_B      0x30
#define KEY_C      0x2e
#define KEY_D      0x20
#define KEY_E      0x12
#define KEY_F      0x21
#define KEY_G      0x22
#define KEY_H      0x23
#define KEY_I      0x17
#define KEY_J      0x24
#define KEY_K      0x25
#define KEY_L      0x26
#define KEY_M      0x32
#define KEY_N      0x31
#define KEY_O      0x18
#define KEY_P      0x19
#define KEY_Q      0x10
#define KEY_R      0x13
#define KEY_S      0x1f
#define KEY_T      0x14
#define KEY_U      0x16
#define KEY_V      0x2f
#define KEY_W      0x11
#define KEY_X      0x2d
#define KEY_Y      0x15
#define KEY_Z      0x2c
#define KEY_1      0x02
#define KEY_2      0x03
#define KEY_3      0x04
#define KEY_4      0x05
#define KEY_5      0x06
#define KEY_6      0x07
#define KEY_7      0x08
#define KEY_8      0x09
#define KEY_9      0x0a
#define KEY_0      0x0b
#define KEY_SPACE      0x39
#define KEY_TAB        0x0f
#define KEY_ENTER      0x1c
#define KEY_BACKSPACE  0x0e
#define KEY_ESC        0x01
#define KEY_MINUS      0x0c
#define KEY_EQUAL      0x0d
#define KEY_LEFTBRACE  0x1a
#define KEY_RIGHTBRACE 0x1b
#define KEY_BACKSLASH  0x2b
#define KEY_SEMICOLON  0x27
#define KEY_APOSTROPHE 0x28
#define KEY_GRAVE      0x29
#define KEY_COMMA      0x33
#define KEY_DOT        0x34
#define KEY_SLASH      0x35
#define KEY_LEFTCTRL   0x1d
#define KEY_LEFTSHIFT  0x2a
#define KEY_RIGHTSHIFT 0x36
#define KEY_LEFTALT    0x38
#define KEY_CAPSLOCK   0x3a
#define KEY_UP         0x67
#define KEY_DOWN       0x6c
#define KEY_LEFT       0x69
#define KEY_RIGHT      0x6a
#define KEY_MAX        0x2ff

struct input_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};