#pragma once

#define _NSIG  65
#define _NSIG_WORDS  ((_NSIG + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long)))

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;