#pragma once
#include <stdint.h>

struct sysinfo {
	int64_t uptime;
	uint64_t loads[3];
	uint64_t totalram;
	uint64_t freeram;
	uint64_t sharedram;
	uint64_t bufferram;
	uint64_t totalswap;
	uint64_t freeswap;
	uint16_t procs;
	uint16_t pad;
	uint64_t totalhigh;
	uint64_t freehigh;
	uint32_t mem_unit;
	char _f[0];
};