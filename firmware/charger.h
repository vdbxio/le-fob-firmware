#ifndef LE_FOB_CHARGER_H
#define LE_FOB_CHARGER_H

#include <stdint.h>

/*
 * AW32001E battery charger, over the shared I2C bus at address 0x49.
 *
 * Two jobs. One is reporting: charge state and VBUS presence, which the
 * status chirp will want. The other is corrective -- the part's reset
 * defaults are wrong for this board in two specific ways, and left alone
 * they produce faults that look like something else entirely. See
 * charger.c.
 */

typedef enum {
	CHARGER_NOT_CHARGING = 0,
	CHARGER_PRECHARGE    = 1,
	CHARGER_CHARGING     = 2,
	CHARGER_DONE         = 3,
} charger_chg_state_t;

/* Fault bits, matching REG09H so the raw value can be logged as-is. */
#define CHARGER_FAULT_VIN      (1u << 5)  /* input OVP or bad source */
#define CHARGER_FAULT_THERMAL  (1u << 4)  /* thermal shutdown        */
#define CHARGER_FAULT_BAT      (1u << 3)  /* battery OVP             */
#define CHARGER_FAULT_TIMER    (1u << 2)  /* safety timer expired    */
#define CHARGER_FAULT_NTC_HOT  (1u << 1)
#define CHARGER_FAULT_NTC_COLD (1u << 0)

typedef struct {
	uint8_t             valid;       /* the read succeeded */
	charger_chg_state_t state;
	uint8_t             power_good;  /* VBUS present and good */
	uint8_t             watchdog_fault;
	uint8_t             in_thermal_regulation;
	uint8_t             faults;      /* CHARGER_FAULT_* bits, raw REG09H */
} charger_status_t;

/*
 * Bring up the bus, identify the part and correct its defaults.
 * Returns 1 if the charger answered, 0 if it did not -- the fob runs either
 * way, so a missing or unreachable charger is not fatal.
 */
int charger_init(void);

/* Did charger_init() find the part? */
int charger_present(void);

/*
 * Why charger_init() did not find it, for the status text. With no console
 * on a board that has had its debug module disabled, this is the only way
 * an I2C problem can be told apart from a missing part.
 */
typedef enum {
	CHARGER_OK = 0,
	CHARGER_NO_ACK,     /* nothing answered at the address: bus or pull-ups */
	CHARGER_BAD_ID,     /* something answered, but not an AW32001 */
	CHARGER_CFG_FAILED, /* found, but configuration did not verify */
} charger_result_t;

charger_result_t charger_result(void);

/* Read status and faults. Returns 1 on success. */
int charger_read(charger_status_t *out);

/* Human-readable charge state, for logging. */
const char *charger_state_name(charger_chg_state_t state);

/*
 * Decode REG08H/REG09H. Separated out so it can be exercised on the host
 * without a bus; charger_read() is this plus the I2C transaction.
 */
void charger_decode(uint8_t reg08, uint8_t reg09, charger_status_t *out);

#endif /* LE_FOB_CHARGER_H */
