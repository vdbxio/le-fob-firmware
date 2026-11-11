#include "ch32fun.h"
#include "board.h"
#include "bthome.h"
#include "ble_adv.h"
#include "keystore.h"
#include <stdio.h>
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
static int8_t   tx_button = -1;
static uint32_t t_last_round;

/*
 * What is queued to go out. A burst is one or two frames sent one after
 * the other, each for its own number of rounds: a button press is one
 * event frame; an encrypted status chirp is the power frame then the text
 * frame.
 *
 * Plaintext builds put finished bytes straight into adv_buf and leave
 * frames[] unused. Encrypted builds keep the plaintext frame here and
 * seal a fresh copy into adv_buf for every round, so each repeat carries
 * its own counter value.
 */
#define MAX_FRAMES 2

static __attribute__((unused)) bthome_frame_t frames[MAX_FRAMES];
static uint8_t        frame_rounds[MAX_FRAMES];
static uint8_t        n_frames;
static uint8_t        cur_frame;
static uint8_t        rounds_left; /* across every queued frame */
static uint8_t        frame_left;  /* of the current frame */
static int            adv_len;

/* Encrypted builds only; unused in a plaintext one. */
static __attribute__((unused)) const uint8_t *key; /* 0 = send nothing */
static __attribute__((unused)) uint8_t flash_ok = 1; /* top-up may write DataFlash */
static __attribute__((unused)) uint8_t refused; /* refusal logged this burst */

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

void ble_adv_set_key(const uint8_t *k)
{
	key = k;
}

void ble_adv_allow_flash_writes(int ok)
{
	flash_ok = (uint8_t)(ok != 0);
}

/* Start a fresh burst of whatever is in frames[]. Anything still queued
 * from a previous one is abandoned: the newer information matters more. */
static void start_queue(void)
{
	cur_frame    = 0;
	frame_left   = frame_rounds[0];
	rounds_left  = 0;
	refused      = 0;
	for (int i = 0; i < n_frames; i++)
		rounds_left += frame_rounds[i];
	t_last_round = 0; /* forces the first round on the next tick */
}

void ble_adv_send_heartbeat(const bthome_status_t *status)
{
	tx_button = -1; /* no pad to light: nothing was pressed */

#if FOB_ENCRYPT
	/*
	 * Two frames, two packet ids, the chirp's usual number of rounds split
	 * between them so a chirp costs no more air or awake time than the
	 * single plaintext frame did.
	 */
	uint8_t half = ADV_HEARTBEAT_ROUNDS / 2 ? ADV_HEARTBEAT_ROUNDS / 2 : 1;

	bthome_build_power_frame(&frames[0], pdu_header, mac, ++packet_id, status);
	bthome_build_text_frame(&frames[1], pdu_header, mac, ++packet_id,
	                        status->text);
	frame_rounds[0] = half;
	frame_rounds[1] = ADV_HEARTBEAT_ROUNDS - half ? ADV_HEARTBEAT_ROUNDS - half : 1;
	n_frames        = 2;
#else
	packet_id++;
#if FOB_CHIRP_TEXT
	/*
	 * Chirp carries supply voltage, battery low, charging and a text
	 * object. The button-none frame below is the fallback, behind
	 * CHIRP_TEXT=0.
	 */
	adv_len = bthome_build_status_adv(adv_buf, pdu_header, mac, packet_id,
	                                  status);
#else
	/*
	 * Proven chirp: the same button frame a press sends, but with every
	 * button at "none". Home Assistant received this reliably; it keeps
	 * "last seen" fresh without introducing an untested object type.
	 */
	uint8_t events[NUM_BUTTONS];

	for (int i = 0; i < NUM_BUTTONS; i++)
		events[i] = BTHOME_EVT_NONE;

	(void)status;
	adv_len = bthome_build_adv(adv_buf, pdu_header, mac, events, packet_id);
#endif
	frame_rounds[0] = ADV_HEARTBEAT_ROUNDS;
	n_frames        = 1;
#endif

	start_queue();
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
	packet_id++;

#if FOB_ENCRYPT
	bthome_build_event_frame(&frames[0], pdu_header, mac, events, packet_id);
#else
	adv_len = bthome_build_adv(adv_buf, pdu_header, mac, events, packet_id);
#endif
	frame_rounds[0] = ADV_BURST_ROUNDS;
	n_frames        = 1;
	start_queue();
}

#if FOB_ENCRYPT
/*
 * Seal the current frame under the next counter value. Returns 0 if no
 * counter may be used -- no key, or the reserved window is spent and could
 * not be topped up -- in which case nothing goes out this round.
 */
static int seal_round(void)
{
	uint32_t ctr;

	if (!key || !keystore_next_counter(flash_ok, &ctr)) {
		if (!refused) {
			refused = 1;
			printf("radio  refused: %s\n",
			       key ? "no counter (window spent, flash write held off)"
			           : "no key");
		}
		return 0;
	}
	adv_len = bthome_seal(&frames[cur_frame], adv_buf, mac, key, ctr);
	return 1;
}
#endif

void ble_adv_tick(uint32_t now)
{
	if (!rounds_left)
		return;

	if (t_last_round &&
	    (uint32_t)(now - t_last_round) < ADV_ROUND_INTERVAL_MS)
		return;

	t_last_round = now ? now : 1;

	/* Move on to the next frame once this one has had its rounds. */
	if (!frame_left && cur_frame + 1 < n_frames)
		frame_left = frame_rounds[++cur_frame];

	rounds_left--;
	if (frame_left)
		frame_left--;

#if FOB_ENCRYPT
	if (!seal_round())
		return;
#endif

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
