#include "ch32fun.h"
#include "board.h"
#include "ws2812.h"

/*
 * WS2812 bit timing.
 *
 * These are the WS2812B-V5/V6 numbers, which are NOT the classic WS2812B
 * ones and are the reason an earlier version of this driver produced
 * garbage on V5+ parts:
 *
 *              V5/V6 datasheet      classic WS2812B (0.35/0.8/0.7/0.6 +-150ns)
 *   T0H        220 ns .. 380 ns     200 .. 500
 *   T0L        580 ns .. 1 us       650 .. 950
 *   T1H        580 ns .. 1 us       550 .. 850
 *   T1L        580 ns .. 1 us       450 .. 750
 *   RES        > 280 us             > 50
 *
 * The trap is T1L. Classic WS2812B has a fixed ~1.25 us bit period, so
 * driving it as "period minus high time" is fine. V5 does not: a 1 bit is
 * 580 ns high *and* 580 ns low at minimum, so its period is longer than a
 * 0 bit's. Deriving the low time from a fixed 1.25 us period gives
 * T1L = 1250 - 700 = 550 ns, which is under the V5 minimum -- every 1 bit
 * out of spec, while every 0 bit still looks fine.
 *
 * So the four times are specified independently and a bit's period is
 * whatever they add up to: 1100 ns for a 0, 1400 ns for a 1. The values
 * below sit inside both columns above, so this drives either variant.
 */
#define TICKS_PER_US    (FUNCONF_SYSTEM_CORE_CLOCK / 1000000)
#define NS_TO_TICKS(ns) (((ns) * TICKS_PER_US + 500) / 1000)

#define T0_HIGH  NS_TO_TICKS(300)
#define T0_LOW   NS_TO_TICKS(800)
#define T1_HIGH  NS_TO_TICKS(700)
#define T1_LOW   NS_TO_TICKS(700)

/*
 * Latch time. V5 wants > 280 us; 400 gives margin at no real cost, since a
 * refresh happens at most every 20 ms.
 */
#define T_RESET_US 400

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
	 * One frame of 4 pixels is 96 bits at 1.1-1.4 us, so 105-135 us with
	 * interrupts off depending on the data. That delays the 1 ms SysTick by at most ~12% of one period, and
	 * because the tick handler advances CMP by a fixed interval rather than
	 * reloading from "now", the delay does not accumulate into clock drift.
	 */
	__disable_irq();

	/*
	 * `next` is the ideal start of the current bit slot, and a slot is as
	 * long as that bit's own high plus low time. Both edges are derived
	 * from `next`, never from "now", so the few cycles of loop overhead
	 * shift an edge slightly but cannot accumulate across a frame.
	 *
	 * The order here matters. Everything for this bit is computed *before*
	 * the alignment wait, so that once the wait falls through, the only
	 * thing between it and the rising edge is the store itself. Computing
	 * the fall time after raising the line would push the rising edge late
	 * while leaving the fall time fixed, silently shortening every high
	 * pulse by the length of that arithmetic -- which for a 350 ns "0" bit
	 * is most of the pulse.
	 */
	uint32_t next = funSysTick32() + T1_HIGH;

	while (len--) {
		uint8_t byte = *p++;

		for (int bit = 0; bit < 8; bit++) {
			int      one    = byte & 0x80;
			uint32_t t_fall = next + (one ? T1_HIGH : T0_HIGH);
			uint32_t t_end  = t_fall + (one ? T1_LOW : T0_LOW);

			byte <<= 1;

			/* Keep the arithmetic above from sinking past the wait. */
			asm volatile("" ::: "memory");

			while ((int32_t)(funSysTick32() - next) < 0)
				;
			R32_PB_SET = WS_BIT;

			while ((int32_t)(funSysTick32() - t_fall) < 0)
				;
			R32_PB_CLR = WS_BIT;

			next = t_end;
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
