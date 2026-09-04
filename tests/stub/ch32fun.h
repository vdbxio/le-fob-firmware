/* Minimal stand-in for ch32fun.h, so board.h and bthome.c can be compiled
 * and exercised on the host. Only the few things board.h touches. */
#ifndef LEFOB_TEST_CH32FUN_H
#define LEFOB_TEST_CH32FUN_H

#include <stdint.h>

#define PA4  (0x00000010)
#define PA5  (0x00000020)
#define PA8  (0x00000100)
#define PA9  (0x00000200)
#define PA12 (0x00001000)
#define PA13 (0x00002000)
#define PA14 (0x00004000)
#define PA15 (0x00008000)
#define PB0  (0x80000001)
#define PB4  (0x80000010)
#define PB12 (0x80001000)
#define PB13 (0x80002000)
#define PB14 (0x80004000)
#define PB15 (0x80008000)
#define PB6  (0x80000040)
#define PB7  (0x80000080)
#define PB14 (0x80004000)
#define PB15 (0x80008000)
#define PB22 (0x80400000)
#define PB23 (0x80800000)

#define LL_TX_POWER_0_DBM 0

/* The tests redirect LEFOB_ROM_MAC_ADDR at this buffer so the factory-address
 * handling can be driven through each of its cases. */
extern uint8_t g_fake_rom[6];

/* ------------------------------------------------------------------ */
/* GPIO registers, as plain variables the gesture test can drive.      */
/* Active low, so a bit that is CLEAR means that button is pressed.    */
/* ------------------------------------------------------------------ */
extern uint32_t R32_PA_PIN, R32_PB_PIN;
extern uint32_t R32_PA_CLR, R32_PB_CLR;
extern uint16_t R16_PA_INT_EN, R16_PB_INT_EN;
extern uint16_t R16_PA_INT_MODE, R16_PB_INT_MODE;
extern uint16_t R16_PA_INT_IF, R16_PB_INT_IF;
extern uint16_t R16_PIN_ALTERNATE;
extern uint8_t  R8_SLP_WAKE_CTRL;

#define RB_PIN_INTX      0x2000
#define RB_SLP_GPIO_WAKE 0x10

#define SYS_SAFE_ACCESS(a) do { a } while (0)

typedef enum { GPIO_CFGLR_IN_PUPD, GPIO_CFGLR_IN_FLOAT } GPIOModeTypeDef;
#define funPinMode(pin, mode) ((void)(pin), (void)(mode))

typedef enum { GPIOA_IRQn, GPIOB_IRQn } IRQn_Type;
#define NVIC_EnableIRQ(n) ((void)(n))

/* --- sleep / wake registers, as variables ------------------------------ */
extern unsigned char  R8_SLP_WAKE_CTRL;
extern unsigned char  R8_SLP_POWER_CTRL;
extern unsigned char  R8_RTC_FLAG_CTRL;
extern unsigned short R16_RTC_CNT_LSI;
extern unsigned short R16_RTC_CNT_DIV1;

#define RB_SLP_GPIO_WAKE  0x10
#define RB_WAKE_EV_MODE   0x40
#define RB_WAKE_DLY_MOD   0x03
#define RB_RTC_TRIG_CLR   0x20

#define RB_PWR_RAM2K      0x02
#define RB_PWR_CORE       0x04
#define RB_PWR_EXTEND     0x08
#define RB_PWR_RAMX       0x10
#define RB_PWR_MUST_0010  0x1000
#define RB_PWR_PLAN_EN    0x8000

#define RTC_MAX_COUNT     0xA8C00000u

#define FUN_LOW  0
#define FUN_HIGH 1
#define GPIO_CFGLR_OUT_2Mhz_PP 3
#define funDigitalWrite(pin, v) ((void)(pin), (void)(v))

#define SysTick_IRQn 12
#define NVIC_ClearPendingIRQ(n) ((void)(n))
#define SYSTICK_CTLR_STIE (1 << 1)
#define SYSTICK_CTLR_STE  (1 << 0)
#define SYSTICK_CTLR_STCLK (1 << 2)
#define FUNCONF_SYSTEM_CORE_CLOCK 60000000
#define DELAY_US_TIME (FUNCONF_SYSTEM_CORE_CLOCK/1000000)
#define Delay_Us(n) DelaySysTick((n) * DELAY_US_TIME)
void DelaySysTick(unsigned n);

typedef struct { unsigned CTLR, SR; unsigned long long CNT, CMP; } SysTick_Type;
extern SysTick_Type *SysTick;

/* Only SCTLR matters here: bit 2 is SLEEPDEEP, and it is sticky. */
typedef struct { unsigned SCTLR; } NVIC_Type;
extern NVIC_Type *NVIC;
#define __WFI() ((void)0)

#endif
