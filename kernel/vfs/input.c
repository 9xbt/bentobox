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

static long copy_empty_bitmap(void *dest, int size) {
    void *bitmap = kmalloc(size);
    memset(bitmap, 0, size);
    long ret = copy_to_user(dest, bitmap, size);
    kfree(bitmap);
    return ret;
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
        return copy_empty_bitmap(arg, size);
	} else if (number == EVIOCGBIT) {
        return copy_to_user(arg, input_dev->bitmap[0], size);
	} else if (number > EVIOCGBIT && number < EVIOCGBIT + EV_CNT) {
		int ev_type = number - EVIOCGBIT;
        if (ev_type > EV_SYN && ev_type <= EV_ABS) {
            return copy_to_user(arg, input_dev->bitmap[ev_type], size);
        } else {
		    return copy_empty_bitmap(arg, size);
        }
    } else if (number == EVIOCSREP || number == EVIOCSKEYCODE || number == EVIOCSFF || number == EVIOCRMFF || number == EVIOCGRAB || number == EVIOCREVOKE) {
        return 0;
	} else if (number >= EVIOCGABS && number < EVIOCGABS + ABS_CNT && bitmap_get(input_dev->bitmap[0], EV_ABS)) {
        int axis = number - EVIOCGABS;
        struct input_absinfo info = {0};
        if (axis == ABS_X) {
            info = input_dev->abs[0];
        } else if (axis == ABS_Y) {
            info = input_dev->abs[1];
        } else if (axis == ABS_PRESSURE) {
            info = input_dev->abs[2];
        } else {
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m axis %d reporting not implemented\n", __func__, axis);
            return -EINVAL;
        }
        return copy_to_user(arg, &info, MIN((size_t)size, sizeof info));
    } else {
        dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%x not implemented for input type %s\n", __func__, op, input_get_human_name(input_dev->type));
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
    for (int i = 0; i < 3; i++) {
        memset(&dev->abs[i], 0, sizeof(struct input_absinfo));
    }
    for (int i = 0; i < EV_ABS + 1; i++) {
        dev->bitmap[i] = kmalloc(128);
        memset(dev->bitmap[i], 0, 128);
    }
    switch (type) {
        case INPUT_KEYBOARD:
            bitmap_set(dev->bitmap[0], EV_SYN);
            bitmap_set(dev->bitmap[0], EV_KEY);
            for (int i = KEY_ESC; i <= 0xff; i++)
                bitmap_set(dev->bitmap[EV_KEY], i);
            break;
        case INPUT_MOUSE:
            bitmap_set(dev->bitmap[0], EV_SYN);
            bitmap_set(dev->bitmap[0], EV_KEY);
            bitmap_set(dev->bitmap[0], EV_REL);
            bitmap_set(dev->bitmap[EV_REL], REL_X);
            bitmap_set(dev->bitmap[EV_REL], REL_Y);
            bitmap_set(dev->bitmap[EV_REL], REL_WHEEL);
            bitmap_set(dev->bitmap[EV_KEY], BTN_LEFT);
            bitmap_set(dev->bitmap[EV_KEY], BTN_RIGHT);
            bitmap_set(dev->bitmap[EV_KEY], BTN_MIDDLE);
            break;
        case INPUT_TABLET:
            bitmap_set(dev->bitmap[0], EV_SYN);
            bitmap_set(dev->bitmap[0], EV_KEY);
            bitmap_set(dev->bitmap[0], EV_ABS);
            bitmap_set(dev->bitmap[EV_ABS], ABS_X);
            bitmap_set(dev->bitmap[EV_ABS], ABS_Y);
            bitmap_set(dev->bitmap[EV_ABS], ABS_PRESSURE);
            bitmap_set(dev->bitmap[EV_KEY], BTN_TOUCH);
            bitmap_set(dev->bitmap[EV_KEY], BTN_STYLUS);
            bitmap_set(dev->bitmap[EV_KEY], BTN_STYLUS2);
            break;
        default:
            break;
    }
    return dev;
}