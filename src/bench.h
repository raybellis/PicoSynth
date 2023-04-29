#pragma once

#include "hardware/structs/systick.h"

static inline void bench_init()
{
	systick_hw->csr = 0x05;
	systick_hw->rvr = 0xffffff;
}

static inline uint32_t bench_time()
{
	return systick_hw->cvr;
}

static inline uint32_t bench_delta(uint32_t t0, uint32_t t1)
{
	if (t1 < t0) {
		return t0 - t1;
	} else {
		return t0 + 0x1000000 - t1;
	}
}
