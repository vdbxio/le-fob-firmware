#include "ch32fun.h"
#include "board.h"
#include "ws2812.h"

/*
 * WS2812 timing, in SysTick ticks.
 *
 * funconfig.h runs SysTick straight off HCLK, so one tick is one CPU cycle
 * (16.7 ns at 60 MHz) and these convert cleanly from the datasheet's
 * nanoseconds. Waiting on the free-running counter rather than counting
 * nops means the timing does not depend on what the compiler emitted, and
 * a bit that overruns slightly does not accumulate error into the next one.
 */
#define TICKS_PER_US   (FUNCONF_SYSTEM_CORE_CLOCK / 1000000)
#define NS_TO_TICKS(ns) (((ns) * TICKS_PER_US + 500) / 1000)

#define T_PERIOD  NS_TO_TICKS(1250)  /* one bit, T0H+T0L == T1H+T1L */
#define T1_HIGH   NS_TO_TICKS(700)   /* "1" bit high time  */
#define T0_HIGH   NS_TO_TICKS(350)   /* "0" bit high time  */

/*
 * Latch time. WS2812B latches after 50 us, but the WS2812E on this board
 * (and most recent clones) need >280 us or the first pixel eats the frame.
 */
#define T_RESET_US 300

/* Physical bit within the port register. PB pins carry 0x80000000 as a
 * port marker in ch32fun's pin encoding; mask it off. */
#define WS_BIT (PIN_WS2812 & 0x7FFFFFFF)

/*
 * Edges are driven through the write-1-to-act SET and CLR registers rather
 * than read-modify-writing R32_PB_OUT. Two reasons: it is a single store, so
 * the edge lands within a cycle or two of its deadline instead of four; and
 * it leaves the rest of the port alone. That second point is not cosmetic
 * here -- on CH59x the output latch of an *input* pin selects its interrupt
 * edge polarity, so bits 4, 22 and 23 of this register belong to the four
 * buttons. Reading and rewriting them would work today but is one refactor
 * away from silently changing which edge wakes the chip.
 */

/*
 * Lives in RAM (.highcode). Running this from flash would let flash wait
 * states stretch individual instructions, which is exactly the kind of
 * jitter a 350 ns pulse cannot absorb.
 */
__HIGH_CODE
void ws2812_show(const uint8_t *grb, int count)
{
	if (count <= 0)
		return;

	const uint8_t *p   = grb;
	int            len = count * 3;

	/*
	 * One frame of 4 pixels is 96 bits * 1.25 us = 120 us with interrupts
	 * off. That delays the 1 ms SysTick by at most ~12% of one period, and
	 * because the tick handler advances CMP by a fixed interval rather than
	 * reloading from "now", the delay does not accumulate into clock drift.
	 */
	__disable_irq();

	/*
	 * `next` is the ideal start of the current bit slot. Both edges are
	 * derived from it, never from "now" -- so the few cycles of loop
	 * overhead shift an edge slightly but cannot accumulate across the
	 * 96 bits of a frame.
	 *
	 * The order here matters. Everything for this bit is computed *before*
	 * the alignment wait, so that once the wait falls through, the only
	 * thing between it and the rising edge is the store itself. Computing
	 * the fall time after raising the line would push the rising edge late
	 * while leaving the fall time fixed, silently shortening every high
	 * pulse by the length of that arithmetic -- which for a 350 ns "0" bit
	 * is most of the pulse.
	 */
	uint32_t next = funSysTick32() + T_PERIOD;

	while (len--) {
		uint8_t byte = *p++;

		for (int bit = 0; bit < 8; bit++) {
			uint32_t t_fall = next + ((byte & 0x80) ? T1_HIGH : T0_HIGH);

			byte <<= 1;

			/* Keep the arithmetic above from sinking past the wait. */
			asm volatile("" ::: "memory");

			while ((int32_t)(funSysTick32() - next) < 0)
				;
			R32_PB_SET = WS_BIT;

			while ((int32_t)(funSysTick32() - t_fall) < 0)
				;
			R32_PB_CLR = WS_BIT;

			next += T_PERIOD;
		}
	}

	__enable_irq();

	/* Hold the line low long enough for the chain to latch. */
	Delay_Us(T_RESET_US);
}

void ws2812_init(void)
{
	/* 20 mA drive: the data line runs the length of the board to W1 and
	 * wants fast edges to keep the 350 ns field unambiguous. */
	funPinMode(PIN_WS2812, GPIO_CFGLR_OUT_10Mhz_PP);
	funDigitalWrite(PIN_WS2812, FUN_LOW);
	Delay_Us(T_RESET_US);
}
