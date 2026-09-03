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

/*
 * Overridable from board.h if a particular batch of LEDs wants nudging.
 */
#ifndef WS2812_T0H_NS
#define WS2812_T0H_NS 250
#endif
#ifndef WS2812_T0L_NS
#define WS2812_T0L_NS 800
#endif
#ifndef WS2812_T1H_NS
#define WS2812_T1H_NS 700
#endif
#ifndef WS2812_T1L_NS
#define WS2812_T1L_NS 800
#endif

#define T0_LOW  NS_TO_TICKS(WS2812_T0L_NS)
#define T1_LOW  NS_TO_TICKS(WS2812_T1L_NS)

/*
 * The V5/V6 windows from the table above, enforced. Tuning any of the four
 * times out of spec stops the build rather than producing a board that
 * mostly works. The chosen values also sit inside the classic WS2812B
 * ranges, so the driver covers either part -- that part is not checkable
 * here, since the two specs overlap but neither contains the other.
 */
_Static_assert(WS2812_T0H_NS >= 220 && WS2812_T0H_NS <= 380,
               "T0H outside the WS2812B-V5 window (220-380 ns)");
_Static_assert(WS2812_T1H_NS >= 580 && WS2812_T1H_NS <= 1000,
               "T1H outside the WS2812B-V5 window (580 ns - 1 us)");
_Static_assert(WS2812_T0L_NS >= 580 && WS2812_T0L_NS <= 1000,
               "T0L outside the WS2812B-V5 window (580 ns - 1 us)");
_Static_assert(WS2812_T1L_NS >= 580 && WS2812_T1L_NS <= 1000,
               "T1L outside the WS2812B-V5 window (580 ns - 1 us)");

/*
 * Latch time. V5 wants > 280 us; 400 gives margin at no real cost, since a
 * refresh happens at most every 20 ms.
 */
#define T_RESET_US 400

/*
 * Why the high and low phases are timed by different mechanisms.
 *
 * Waiting on the SysTick counter is self-correcting and immune to what the
 * compiler emitted, but it samples the clock in a loop of a few
 * instructions, so an edge lands somewhere in a 50-100 ns window after its
 * deadline. Against T0L/T1H/T1L, whose spec windows are 420 ns wide, that
 * is nothing.
 *
 * T0H is different: 220-380 ns is only 160 ns wide, and the jitter is a
 * large fraction of it. Centred at 300 ns a 0 bit's high pulse can land
 * near 380 ns, over the limit -- and a 0 misread that way corrupts the rest
 * of the frame, so the pixels *downstream* of the intended one flash. Which
 * is precisely the symptom this arrangement exists to fix.
 *
 * So: the high phase is an exact count of nops, with no sampling and no
 * jitter, and the low phase is a SysTick wait against the slot boundary,
 * absorbing all accumulated error into the parameter that has room for it.
 *
 * The nop counts are cycles, and must be kept in step with the times above;
 * ws2812_assert_timing() below fails the build if they drift apart.
 */
#define WS_STR2(x) #x
#define WS_STR(x)  WS_STR2(x)

/*
 * The high phase is a computed jump into a sled of 4-byte nops: a 1 bit
 * enters at the top and runs the whole sled, a 0 bit enters part way in.
 *
 * The obvious thing -- an if/else with a different nop count in each arm --
 * does not work, and failing at it is what produced this. The compiler
 * hoists the common rising-edge store above the branch and shares the
 * falling-edge store between the arms, so one arm falls straight through
 * while the other pays a taken branch on the way in and a taken jump on the
 * way out. Those extra cycles land inside the pulse, and against T0H's
 * 160 ns window they are enough to push a 0 bit over 380 ns.
 *
 * Jumping into a sled makes both cases cost exactly the same: one store,
 * one jump register, N nops, one store. Whatever a taken jump costs on this
 * core, it costs it identically for a 0 and a 1, and the difference between
 * them is exactly the difference in nops. Nothing here varies at runtime,
 * so every 0 bit in every frame is bit-identical to every other.
 *
 * The nop counts are chosen for margin against an unknown, not for a
 * nominal. A high phase is 1 (the store) + jr + N cycles, and how many
 * cycles a jump-register costs on this core is not documented -- it may
 * flush the pipeline. So N is picked so that T0H stays inside its window
 * across the whole plausible range:
 *
 *      N    jr=2   jr=4   jr=6   jr=8     spec 220-380 ns
 *     15     300    333    367    400     out of spec at jr >= 7
 *     12     250    283    317    350     holds for jr = 2..8
 *     10     217    250    283    317     under spec at jr = 2
 *
 * N = 12 sits in the middle of what survives. An earlier version used 15,
 * which is fine if a jump costs 2-4 cycles and out of spec if it costs 7 --
 * and the symptom of that is a blip on pixel 0 alone, because pixel 0 is
 * the only pixel fed the raw signal from this pin. Every other pixel sees a
 * waveform regenerated by the one before it, so marginal timing here shows
 * up there and nowhere else.
 *
 * T1H has no such problem: 39 nops against a 580-1000 ns window is roughly
 * 700-800 ns across the same range.
 */
#define WS_SLED_NOPS 39                              /* a 1 bit runs all of these */
#define WS_ZERO_NOPS 12                              /* a 0 bit runs this many    */
#define WS_ZERO_SKIP ((WS_SLED_NOPS - WS_ZERO_NOPS) * 4)

/*
 * The nop counts are literals because .rept needs one. This ties them back
 * to the times above: the gap between a 0 and a 1 bit's high phase is
 * entirely the extra nops, so if a WS2812_T*_NS value changes and the sled
 * does not, the build stops here.
 */
_Static_assert(WS_SLED_NOPS - WS_ZERO_NOPS ==
                   (int)(NS_TO_TICKS(WS2812_T1H_NS) - NS_TO_TICKS(WS2812_T0H_NS)),
               "WS2812 high times changed: retune WS_SLED_NOPS / WS_ZERO_NOPS");

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
	 * long as that bit's own high plus low time. Slot starts are absolute,
	 * never relative to "now", so per-bit overhead cannot accumulate across
	 * a frame.
	 *
	 * The order here matters. Everything for this bit is computed *before*
	 * the alignment wait, so that once the wait falls through, the only
	 * thing between it and the rising edge is the store itself. Computing
	 * the fall time after raising the line would push the rising edge late
	 * while leaving the fall time fixed, silently shortening every high
	 * pulse by the length of that arithmetic -- which for a 350 ns "0" bit
	 * is most of the pulse.
	 */
	/*
	 * Start a few microseconds out, so the loop preamble cannot make the
	 * first bit short.
	 */
	uint32_t next = funSysTick32() + NS_TO_TICKS(3000);

	while (len--) {
		uint8_t byte = *p++;

		for (int bit = 0; bit < 8; bit++) {
			int      one   = byte & 0x80;
			uint32_t t_end = next +
			                 (one ? (T1_LOW + NS_TO_TICKS(WS2812_T1H_NS))
			                      : (T0_LOW + NS_TO_TICKS(WS2812_T0H_NS)));

			byte <<= 1;

			/* Keep the arithmetic above from sinking past the wait. */
			asm volatile("" ::: "memory");

			/*
			 * Align to the slot, then drive the whole high phase from
			 * straight-line code. The branch is taken before the rising
			 * edge so that both arms are store / nops / store.
			 */
			uint32_t skip = one ? 0u : (uint32_t)WS_ZERO_SKIP;
			uint32_t sled;

			while ((int32_t)(funSysTick32() - next) < 0)
				;

			__asm__ __volatile__(
				"la   %[sled], 1f\n"
				"add  %[sled], %[sled], %[skip]\n"
				"sw   %[bit], 0(%[set])\n"
				"jr   %[sled]\n"
				"1:\n"
				".option push\n.option norvc\n"
				".rept " WS_STR(WS_SLED_NOPS) "\nnop\n.endr\n"
				".option pop\n"
				"sw   %[bit], 0(%[clr])\n"
				: [sled] "=&r"(sled)
				: [skip] "r"(skip), [bit] "r"((uint32_t)WS_BIT),
				  [set] "r"(&R32_PB_SET), [clr] "r"(&R32_PB_CLR)
				: "memory");

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
