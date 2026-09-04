#include "ch32fun.h"
#include "board.h"
#include "bthome.h"
#include "ble_adv.h"
#include <string.h>

#include "iSLER.h"

/* The fixed access address every BLE advertising channel uses. */
#define ACCESS_ADDRESS 0x8E89BED6

static const uint8_t adv_channels[] = { 37, 38, 39 };

/*
 * iSLER transmits straight out of this buffer, so it has to satisfy the
 * alignment (and, on some parts, the DMA-reachable placement) that
 * ISLER_BUF_ATTR encodes.
 */
ISLER_BUF_ATTR static uint8_t adv_buf[BTHOME_ADV_MAX];

static uint8_t  mac[6];
static uint8_t  pdu_header;
static uint8_t  tx_power = BLE_TX_POWER;
static uint8_t  packet_id;
static uint8_t  rounds_left;
static int8_t   tx_button = -1;
static uint32_t t_last_round;
static int      adv_len;

const uint8_t *ble_adv_mac(void) { return mac; }
int ble_adv_mac_is_random(void)  { return pdu_header == 0x42; }

void ble_adv_init(void)
{
	pdu_header = bthome_resolve_address(mac);
	iSLERInit(tx_power);
}

void ble_adv_resume(void)
{
	/* Documented iSLER behaviour: the radio comes out of sleep in an
	 * undefined state and must be re-initialised before the next TX. */
	iSLERInit(tx_power);
}

static void start_burst(const uint8_t events[NUM_BUTTONS], uint8_t rounds)
{
	packet_id++;
	adv_len = bthome_build_adv(adv_buf, pdu_header, mac, events, packet_id);

	/* Start a fresh burst. Anything still queued from a previous frame
	 * is abandoned: the newer information is the one that matters. */
	rounds_left  = rounds;
	t_last_round = 0; /* forces the first round on the next tick */
}

void ble_adv_send_heartbeat(const char *status_text)
{
	tx_button = -1; /* no pad to light: nothing was pressed */

	packet_id++;

#if FOB_CHIRP_TEXT
	/*
	 * Chirp carries a BTHome text object. This frame shape has not yet
	 * been confirmed received, so it is behind a knob; the button-none
	 * frame below is the one Home Assistant has actually accepted.
	 */
	adv_len = bthome_build_status_adv(adv_buf, pdu_header, mac, packet_id,
	                                  status_text);
#else
	/*
	 * Proven chirp: the same button frame a press sends, but with every
	 * button at "none". Home Assistant received this reliably; it keeps
	 * "last seen" fresh without introducing an untested object type.
	 */
	uint8_t events[NUM_BUTTONS];

	for (int i = 0; i < NUM_BUTTONS; i++)
		events[i] = BTHOME_EVT_NONE;

	(void)status_text;
	adv_len = bthome_build_adv(adv_buf, pdu_header, mac, events, packet_id);
#endif

	rounds_left  = ADV_HEARTBEAT_ROUNDS;
	t_last_round = 0;
}

void ble_adv_send_event(uint8_t button, uint8_t event)
{
	uint8_t events[NUM_BUTTONS];

	/*
	 * Exactly one button reports an event per advertisement; the others
	 * carry "none". Sending two gestures in one packet would be legal
	 * BTHome, but a receiver has no way to order them, so each gesture
	 * gets its own packet ID and its own burst.
	 */
	for (int i = 0; i < NUM_BUTTONS; i++)
		events[i] = BTHOME_EVT_NONE;

	if (button < NUM_BUTTONS)
		events[button] = event;

	tx_button = (int8_t)button;
	start_burst(events, ADV_BURST_ROUNDS);
}

void ble_adv_tick(uint32_t now)
{
	if (!rounds_left)
		return;

	if (t_last_round &&
	    (uint32_t)(now - t_last_round) < ADV_ROUND_INTERVAL_MS)
		return;

	t_last_round = now ? now : 1;
	rounds_left--;

	for (unsigned c = 0; c < sizeof(adv_channels); c++)
		iSLERTX(ACCESS_ADDRESS, adv_buf, adv_len, adv_channels[c], PHY_1M);
}

int ble_adv_busy(void)
{
	return rounds_left != 0;
}

int ble_adv_tx_button(void)
{
	return rounds_left ? tx_button : -1;
}
