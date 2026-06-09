#pragma once
#include <stdint.h>
#include <kernel/time.h>

#define EV_SYN       0x00
#define EV_KEY       0x01
#define EV_REL       0x02
#define EV_ABS       0x03
#define EV_MAX 		 0x1f
#define EV_CNT 	     (EV_MAX + 1)
#define REL_X        0x00
#define REL_Y        0x01
#define REL_WHEEL	 0x08
#define ABS_X        0x00
#define ABS_Y        0x01
#define ABS_PRESSURE 0x18
#define ABS_MAX 	 0x3f
#define ABS_CNT 	 (ABS_MAX + 1)
#define BTN_LEFT     0x110
#define BTN_RIGHT    0x111
#define BTN_MIDDLE   0x112
#define BTN_TOUCH    0x14a
#define BTN_STYLUS   0x14b
#define BTN_STYLUS2  0x14c

#define SYN_REPORT   0

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

struct input_absinfo {
	int32_t value;
	int32_t minimum;
	int32_t maximum;
	int32_t fuzz;
	int32_t flat;
	int32_t resolution;
};

#define EVIOCGVERSION 0x1
#define EVIOCGID 	  0x2
#define EVIOCGNAME 	  0x6
#define EVIOCGPHYS 	  0x7
#define EVIOCGUNIQ 	  0x8
#define EVIOCGPROP 	  0x9
#define EVIOCGKEY 	  0x18
#define EVIOCGLED 	  0x19
#define EVIOCGSND 	  0x1a
#define EVIOCGSW 	  0x1b
#define EVIOCGBIT 	  0x20
#define EVIOCGABS 	  0x40
#define EVIOVSABS 	  0xc0
#define EVIOCSREP     0x03
#define EVIOCSKEYCODE 0x04
#define EVIOCSFF      0x80
#define EVIOCRMFF     0x81
#define EVIOCGRAB     0x90
#define EVIOCREVOKE   0x91

#define ID_BUS 	   	0
#define ID_VENDOR  	1
#define ID_PRODUCT 	2
#define ID_VERSION 	3

#define BUS_I8042 	0x11
#define BUS_RS232 	0x13

enum input_device_type {
	INPUT_KEYBOARD,
	INPUT_MOUSE,
	INPUT_TABLET,
	INPUT_JOYSTICK,
	INPUT_MAX
};

struct input_device {
	enum input_device_type type;
	uint16_t bus;
	uint16_t vendor;
	uint16_t product;
	uint16_t version;
	int max_x;
	int max_y;
};

long input_generic_ioctl(struct input_device *input_dev, int op, void *arg);
struct input_device *input_create(enum input_device_type type, uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version);