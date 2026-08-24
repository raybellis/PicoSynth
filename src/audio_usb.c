// USB audio output, as a UAC2 input device - the host sees the synth
// the way it would see a microphone.
//
// The shape of the problem: core 1 produces one BUFFER_SIZE block
// every 6 ms, and TinyUSB asks for one USB frame's worth every 1 ms,
// on core 0.  A ring of blocks sits between them.  BUFFER_SIZE is 288
// at 48 kHz, exactly six frames, so a block is consumed in a whole
// number of callbacks and the fill level is periodic rather than
// wandering - see the note in CMakeLists.txt about why 288.
//
// Nothing here may block: audio_take() is called from core 1, and the
// one time something on that core blocked it cost a boot hang.  The
// ring is single-producer single-consumer with plain volatile indices,
// which is safe on two cores without a lock because each index has
// exactly one writer.

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/structs/usb.h"		// EP abort, see set_itf_close_EP_cb
#include "tusb.h"

#include "audio.h"

// hardware endpoint number of EPNUM_AUDIO_IN (0x82) in usb_descriptors.c.
// Only the abort/teardown below needs it, and only because that has to
// reach the controller directly
#define AUDIO_EP_NUM	2

// frames per block, and samples per USB frame.  static_asserts below
// keep the assumption honest if the config moves
#define FRAME_SAMPLES	(SAMPLE_RATE / 1000)
#define BLOCK_FRAMES	(BUFFER_SIZE / FRAME_SAMPLES)

_Static_assert(SAMPLE_RATE % 1000 == 0,
	"USB frames are 1 ms, so the sample rate must divide by 1000");
_Static_assert(BUFFER_SIZE % FRAME_SAMPLES == 0,
	"the block size must be a whole number of USB frames");

// three blocks: one being filled, one being drained, one in hand.
// mirrors what the I2S producer pool holds
#define NBLOCKS			3

static int16_t			ring[NBLOCKS][2 * BUFFER_SIZE];

// written only by core 1, read by both
static volatile uint32_t	head = 0;

// written only by core 0, read by both
static volatile uint32_t	tail = 0;

// how far into the block at `tail` the consumer has got, in frames.
// core 0 only
static uint32_t			frame = 0;

// when the next block is due.  the clock is the only thing pacing the
// producer, or core 1 would render flat out and run every envelope and
// LFO at some absurd multiple of real time
static absolute_time_t	next_due;

#define BLOCK_US		(BUFFER_SIZE * 1000000 / SAMPLE_RATE)

static inline uint32_t depth(void)
{
	return head - tail;					// wraps harmlessly, both are counters
}

void audio_init(void)
{
	// nothing to bring up: TinyUSB is already running for MIDI, and
	// the host starts the stream when it selects alternate setting 1
	memset((void*)ring, 0, sizeof(ring));
	next_due = get_absolute_time();
}

// Core 1 paces against the clock, never against the consumer.
//
// The obvious design - block until the ring has room - is wrong here
// twice over.  With no host listening there is no consumer at all;
// and with one listening there is still no guarantee it pulls at the
// rate we expect.  Either way core 1 would spin forever, which stops
// the synthesis and the MIDI dequeue with it.  A DAC always consumes,
// so the I2S backend can pace against it; a USB host cannot be relied
// on to.
//
// So the producer free-runs at exactly one block per BLOCK_US and the
// consumer takes whatever is there, resynchronising if it falls
// behind.  Drift between the synth's clock and the host's is what the
// asynchronous endpoint in the descriptor already promises the host it
// will have to absorb.
int16_t* audio_take(void)
{
	if (absolute_time_diff_us(get_absolute_time(), next_due) > 0) {
		return NULL;					// not due yet
	}

	return ring[head % NBLOCKS];
}

void audio_give(void)
{
	next_due = delayed_by_us(next_due, BLOCK_US);

	// the block is complete before it becomes visible to core 0
	__dmb();
	head++;
}

//--------------------------------------------------------------------+
// TinyUSB side - all of this runs on core 0
//--------------------------------------------------------------------+

// master, left, right.  set by the host through the feature unit
static bool				muted[3] = { false, false, false };

// called once per frame while the host is streaming.
//
// Deliberately no usbd_edpt_busy() check here.  It was tried, on the
// theory that a double-arm of the endpoint could be refused from this
// side, and it is wrong twice over: usbd clears its own busy flag in
// usbd_edpt_iso_activate() while the hardware stays armed, so it does
// not even detect the case; and returning false aborts
// audiod_tx_done_cb() *before* it re-arms the endpoint, so a single
// spurious hit stalls the stream for good.  The teardown in
// tud_audio_set_itf_close_EP_cb() is what actually fixes it.
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf,
		uint8_t ep_in, uint8_t cur_alt_setting)
{
	(void)rhport; (void)itf; (void)ep_in; (void)cur_alt_setting;

	// if the producer has run away from us - which it will if this
	// callback stops being called for a while - drop the backlog
	// rather than play it late
	if (depth() > NBLOCKS) {
		tail = head - NBLOCKS;
		frame = 0;
	}

	if (depth() == 0) {
		// underrun: the synth has not produced a block yet.  send
		// silence rather than nothing, so the host's stream does not
		// stall waiting for us
		static const int16_t quiet[2 * FRAME_SAMPLES] = { 0 };
		tud_audio_write(quiet, sizeof(quiet));
		return true;
	}

	if (muted[0]) {
		static const int16_t quiet[2 * FRAME_SAMPLES] = { 0 };
		tud_audio_write(quiet, sizeof(quiet));
	} else {
		const int16_t* block = ring[tail % NBLOCKS];

		tud_audio_write(block + 2 * FRAME_SAMPLES * frame,
				2 * FRAME_SAMPLES * sizeof(int16_t));
	}

	if (++frame == BLOCK_FRAMES) {
		frame = 0;
		__dmb();
		tail++;
	}

	return true;
}

//--------------------------------------------------------------------+
// Class-specific control requests
//
// The host queries every control the descriptor declares, and a query
// that goes unanswered stalls - at which point the device enumerates
// perfectly and no audio device ever appears, which is exactly how
// this failed the first time.  So: the clock source has to report its
// frequency and validity, and the feature unit its mute state.
//--------------------------------------------------------------------+

#define ENTITY_FEATURE_UNIT		0x02
#define ENTITY_CLOCK_SOURCE		0x04

// one supported rate, so min and max are the same and the step is 0
static const audio_control_range_4_n_t(1) rate_range = {
	.wNumSubRanges = 1,
	.subrange[0] = { SAMPLE_RATE, SAMPLE_RATE, 0 },
};

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request)
{
	const uint8_t entity = TU_U16_HIGH(p_request->wIndex);
	const uint8_t ctrl   = TU_U16_HIGH(p_request->wValue);
	const uint8_t chan   = TU_U16_LOW(p_request->wValue);

	if (entity == ENTITY_CLOCK_SOURCE) {
		if (ctrl == AUDIO_CS_CTRL_SAM_FREQ) {
			if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
				audio_control_cur_4_t cur = { .bCur = (int32_t)SAMPLE_RATE };
				return tud_control_xfer(rhport, p_request, &cur, sizeof(cur));
			}
			if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
				return tud_control_xfer(rhport, p_request,
						(void*)&rate_range, sizeof(rate_range));
			}
		}

		if (ctrl == AUDIO_CS_CTRL_CLK_VALID && p_request->bRequest == AUDIO_CS_REQ_CUR) {
			audio_control_cur_1_t cur = { .bCur = 1 };
			return tud_control_xfer(rhport, p_request, &cur, sizeof(cur));
		}
	}

	if (entity == ENTITY_FEATURE_UNIT) {
		if (ctrl == AUDIO_FU_CTRL_MUTE && p_request->bRequest == AUDIO_CS_REQ_CUR) {
			audio_control_cur_1_t cur = { .bCur = muted[chan < 3 ? chan : 0] };
			return tud_control_xfer(rhport, p_request, &cur, sizeof(cur));
		}
	}

	return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request,
		uint8_t* buf)
{
	(void)rhport;

	const uint8_t entity = TU_U16_HIGH(p_request->wIndex);
	const uint8_t ctrl   = TU_U16_HIGH(p_request->wValue);
	const uint8_t chan   = TU_U16_LOW(p_request->wValue);

	if (entity == ENTITY_FEATURE_UNIT && ctrl == AUDIO_FU_CTRL_MUTE) {
		if (chan < 3) {
			muted[chan] = ((audio_control_cur_1_t*)buf)->bCur;
		}
		return true;
	}

	return false;
}

// the host selecting an alternate setting starts or stops the stream;
// resync to a block boundary so a restart does not begin mid-block
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const* p_request)
{
	(void)rhport;

	// alternate setting 1 is the streaming one; 0 is the host letting
	// go.  resync to a block boundary either way, so a restart does
	// not begin mid-block
	// nothing here needs to know whether this is a start or a stop:
	// the producer paces off the clock either way, and resyncing tail
	// to head is right for both
	frame = 0;
	tail = head;

	return true;
}

// Tear down a transfer left armed on the ISO IN endpoint.
//
// Nothing else does.  On RP2350 TUP_DCD_EDPT_ISO_ALLOC is defined, which
// compiles the usbd_edpt_close() out of the driver's own close path, and
// the only place that ever writes buffer_control back to zero is
// hw_endpoint_init() - which dcd_edpt_iso_activate() does not call.  So
// an armed-but-never-consumed buffer keeps its AVAIL bit set for good.
//
// The next time the host selects the streaming alternate setting the
// driver primes the endpoint again, the hardware sees AVAIL already set,
// and the dcd panics: "ep 82 was already available".  That is a
// breakpoint, which escalates to HardFault with no debugger attached,
// and it takes core 0 down inside tud_task() - and core 1 with it, once
// core 0 dies holding the MIDI queue's spinlock.
//
// Checking usbd_edpt_busy() first is not enough, and was tried: usbd
// clears its own busy flag when the endpoint is reactivated, so the
// software state says idle while the hardware is still armed.  Only the
// controller knows, so ask the controller.
//
// The driver calls this on every SET_INTERFACE for the streaming
// interface, and always before it primes - which is what makes it the
// right place.
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const* p_request)
{
	(void)rhport; (void)p_request;

	// ep_id numbering follows the dcd: 2*n for IN, 2*n+1 for OUT
	const uint32_t mask = 1u << (2 * AUDIO_EP_NUM);

	hw_set_bits(&usb_hw->abort, mask);

	// bounded, because this runs on core 0 inside tud_task().  An
	// unbounded wait on a hardware bit here is the same mistake that
	// hung the boot once already; if the controller does not answer,
	// clearing the buffer control unilaterally is still better than
	// spinning forever
	for (uint32_t i = 0; i < 1000 && !(usb_hw->abort_done & mask); ++i) {
		tight_loop_contents();
	}

	usb_dpram->ep_buf_ctrl[AUDIO_EP_NUM].in = 0;

	hw_clear_bits(&usb_hw->abort, mask);

	// EP_ABORT_DONE is write-to-clear ("WC" in the register docs), so
	// the bit stays latched otherwise - and a latched abort on this
	// endpoint is not something to leave behind for the controller
	usb_hw->abort_done = mask;

	return true;
}
