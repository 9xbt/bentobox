#include <kernel/bitmap.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/input.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

const char *input_human_names[] = {
    "Keyboard",
    "Mouse",
    "Tablet",
    "Joystick"
};

const char *input_get_human_name(enum input_device_type type) {
    if (type > INPUT_MAX - 1)
        return "Invalid";
    return input_human_names[type];
}

long input_generic_ioctl(struct input_device *input_dev, int op, void *arg) {
    int number = op & 0xff;
	int type = (op >> 8) & 0xff;
	int size = (op >> 16) & 0x3fff;

	if (type != 'E')
		return -ENOTTY;

	if (number == EVIOCGVERSION) {
		uint32_t version = 0;
		version |= 1 << 16;
		version |= 0 << 8;
		version |= 0 << 0;

		return copy_to_user(arg, &version, sizeof(version));
	} else if (number == EVIOCGID) {
		uint16_t id[4];
		id[ID_BUS]     = input_dev->bus;
		id[ID_VENDOR]  = input_dev->vendor;
		id[ID_PRODUCT] = input_dev->product;
		id[ID_VERSION] = input_dev->version;

		return copy_to_user(arg, &id, sizeof(id));
	} else if (number == EVIOCGNAME) {
		return copy_to_user(arg, "stub", MIN((size_t)size, strlen("stub")));
	} else if (number == EVIOCGPHYS) {
		return copy_to_user(arg, "stub", MIN((size_t)size, strlen("stub")));
	} else if (number == EVIOCGUNIQ) {
		return copy_to_user(arg, "stub", MIN((size_t)size, strlen("stub")));
	} else if (number == EVIOCGPROP || number == EVIOCGKEY || number == EVIOCGLED || number == EVIOCGSND || number == EVIOCGSW) {
        void *bitmap = kmalloc(size);
        memset(bitmap, 0, size);
        long ret = copy_to_user(arg, bitmap, size);
        kfree(bitmap);
        return ret;
	} else if (number == EVIOCGBIT) {
        uint8_t *bitmap = kmalloc(size);
        memset(bitmap, 0, size);
        switch (input_dev->type) {
            case INPUT_KEYBOARD:
                bitmap_set(bitmap, EV_SYN);
                bitmap_set(bitmap, EV_KEY);
                break;
            case INPUT_MOUSE:
                bitmap_set(bitmap, EV_SYN);
                bitmap_set(bitmap, EV_KEY);
                bitmap_set(bitmap, EV_REL);
                break;
            case INPUT_TABLET:
                bitmap_set(bitmap, EV_SYN);
                bitmap_set(bitmap, EV_KEY);
                bitmap_set(bitmap, EV_ABS);
                break;
            default:
		        dprintf(LOG_DEBUG, "\033[93m%s:\033[0m unsupported input type %d\n", __func__, input_dev->type);
                break;
        }
        long ret = copy_to_user(arg, bitmap, size);
        kfree(bitmap);
        return ret;
	} else if (number > EVIOCGBIT && number < EVIOCGBIT + EV_CNT) {
		int ev_type = number - EVIOCGBIT;
        uint8_t *bitmap = kmalloc(size);
        memset(bitmap, 0, size);
        switch (input_dev->type) {
            case INPUT_KEYBOARD:
                if (ev_type == EV_KEY) {
                    for (int i = KEY_ESC; i <= 0xff; i++)
                        bitmap_set(bitmap, i);
                }
                break;
            case INPUT_MOUSE:
                if (ev_type == EV_REL) {
                    bitmap_set(bitmap, REL_X);
                    bitmap_set(bitmap, REL_Y);
                    bitmap_set(bitmap, REL_WHEEL);
                } else if (ev_type == EV_KEY) {
                    bitmap_set(bitmap, BTN_LEFT);
                    bitmap_set(bitmap, BTN_RIGHT);
                    bitmap_set(bitmap, BTN_MIDDLE);
                }
                break;
            case INPUT_TABLET:
                if (ev_type == EV_ABS) {
                    bitmap_set(bitmap, ABS_X);
                    bitmap_set(bitmap, ABS_Y);
                    bitmap_set(bitmap, ABS_PRESSURE);
                } else if (ev_type == EV_KEY) {
                    bitmap_set(bitmap, BTN_TOUCH);
                    bitmap_set(bitmap, BTN_STYLUS);
                    bitmap_set(bitmap, BTN_STYLUS2);
                }
                break;
            default:
		        dprintf(LOG_DEBUG, "\033[93m%s:\033[0m unsupported input type %d\n", __func__, input_dev->type);
                break;
        }
        long ret = copy_to_user(arg, bitmap, size);
        kfree(bitmap);
        return ret;
    } else if (number == EVIOCSREP) {
        return 0;
    } else if (number == EVIOCSKEYCODE) {
        return 0;
    } else if (number == EVIOCSFF) {
        return 0;
    } else if (number == EVIOCRMFF) {
        return 0;
    } else if (number == EVIOCGRAB) {
        return 0;
    } else if (number == EVIOCREVOKE) {
        return 0;
	} else if (number >= EVIOCGABS && number < EVIOCGABS + ABS_CNT && input_dev->type == INPUT_TABLET) {
        int axis = number - EVIOCGABS;
        struct input_absinfo info = {0};
        if (axis == ABS_X) {
            info.minimum = 0;
            info.maximum = input_dev->max_x;
        } else if (axis == ABS_Y) {
            info.minimum = 0;
            info.maximum = input_dev->max_y;
        } else if (axis == ABS_PRESSURE) {
            info.minimum = 0;
            info.maximum = input_dev->max_pressure;
        } else {
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m axis %d reporting not implemented\n", __func__, axis);
            return -EINVAL;
        }
        return copy_to_user(arg, &info, MIN((size_t)size, sizeof info));
    } else {
        dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%x not implemented for type %s\n", __func__, op, input_get_human_name(input_dev->type));
		return -EINVAL;
	}
}

struct input_device *input_create(enum input_device_type type, uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version) {
    struct input_device *dev = kmalloc(sizeof(struct input_device));
    dev->type = type;
    dev->bus = bus;
    dev->vendor = vendor;
    dev->product = product;
    dev->version = version;
    dev->max_x = 0;
    dev->max_y = 0;
    dev->max_pressure = 0;
    return dev;
}