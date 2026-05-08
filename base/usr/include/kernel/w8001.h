#pragma once
#include <stdint.h>

#define W8001_PORT  0x220

#define W8001_MAX_PHYS		42

#define W8001_MAX_LENGTH	13
#define W8001_LEAD_MASK		0x80
#define W8001_LEAD_BYTE		0x80
#define W8001_TAB_MASK		0x40
#define W8001_TAB_BYTE		0x40
/* set in first byte of touch data packets */
#define W8001_TOUCH_MASK	(0x10 | W8001_LEAD_MASK)
#define W8001_TOUCH_BYTE	(0x10 | W8001_LEAD_BYTE)

/* length of data packets in bytes, depends on device. */
#define W8001_PKTLEN_TOUCH93	5
#define W8001_PKTLEN_TOUCH9A	7
#define W8001_PKTLEN_TPCPEN	9
#define W8001_PKTLEN_TPCCTL	11	/* control packet */
#define W8001_PKTLEN_TOUCH2FG	13

struct w8001_coord {
	uint8_t rdy;
	uint8_t tsw;
	uint8_t f1;
	uint8_t f2;
	uint16_t x;
	uint16_t y;
	uint16_t pen_pressure;
	uint8_t tilt_x;
	uint8_t tilt_y;
};

/* touch query reply packet */
struct w8001_touch_query {
	uint16_t x;
	uint16_t y;
	uint8_t panel_res;
	uint8_t capacity_res;
	uint8_t sensor_id;
};