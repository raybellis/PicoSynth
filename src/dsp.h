#pragma once

#include <cstdint>

//
// Small per-sample helpers that more than one stage of the audio path
// needs.  Both are written as inline assembly on purpose, and both are
// on loops that run per sample per voice - the reasons are in the
// comments, and neither is a micro-optimisation for its own sake.
//

// saturate to full scale instead of wrapping, so that anything
// overdriven distorts at the rails rather than inverting the signal.
// ARMv8-M does this in one instruction; the compare-and-select the
// compiler generates from the obvious C costs four
static inline int32_t clamp16(int32_t x)
{
	int32_t r;
	__asm volatile ("ssat %0, #16, %1" : "=r" (r) : "r" (x));
	return r;
}

// round to nearest on the way out.  a C cast truncates towards zero,
// biasing every sample; vcvtr honours the FPSCR rounding mode, which
// is round-to-nearest-even, and costs exactly the same two
// instructions as the cast.
//
// written out because lrintf() does not inline - it has to set errno,
// so the compiler emits a call, and in these loops that is a call per
// sample
static inline int32_t to_int(float x)
{
	int32_t r;
	float t;

	// t is early-clobbered so it gets a scratch register rather than
	// aliasing x, which would otherwise cost a copy to preserve it
	__asm volatile ("vcvtr.s32.f32 %1, %2\n\tvmov %0, %1"
			: "=r" (r), "=&t" (t) : "t" (x));

	return r;
}
