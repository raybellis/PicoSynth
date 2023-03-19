#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"
#include "pico_rgb_keypad.hpp"
#include "bsp/board.h"
#include "tusb.h"
#include "voice.h"

#define USE_MIDI_CALLBACK 0

enum	{
	BLINK_NOT_MOUNTED = 250,
	BLINK_MOUNTED = 1000,
	BLINK_SUSPENDED = 2500,
};

pimoroni::PicoRGBKeypad keypad;

Voice voices[32];

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
static bool led_state = false;

void led_blinking_task();
void keypad_task();
void audio_task(audio_buffer_pool *);
void midi_task(uint8_t);

int main() {
	board_init();
	tusb_init();
	midid_init();
	auto *pool = audio_init();

	keypad.init();
	keypad.set_brightness(0.2f);

	while (1)
	{
		tud_task();
		led_blinking_task();
		keypad_task();
#if USE_MIDI_CALLBACK == 0
		midi_task(0);
#endif
		audio_task(pool);
	}
}

//--------------------------------------------------------------------+
// Voice allocator
//--------------------------------------------------------------------+

static struct {
	uint8_t	chan;
	uint8_t	note;
} valloc[32];

static uint32_t in_use = 0;
static uint8_t next = 0;

static void note_on(uint8_t chan, uint8_t note, uint8_t vel)
{
	uint32_t mask = 1;
	for (uint8_t i = 0; i < 32; ++i, mask <<= 1) {
		if ((in_use & mask) == 0) {
			in_use |= mask;
			valloc[i] = { chan, note };
			voices[i].note_on(note, vel);
			return;
		}
	}
}

static void note_off(uint8_t chan, uint8_t note)
{
	uint32_t mask = 1;
	for (uint8_t i = 0; i < 32; ++i, mask <<= 1) {
		if (valloc[i].chan == chan && valloc[i].note == note) {
			in_use &= ~(1 << i);
			valloc[i] = { 255, };
			voices[i].note_off();
			break;
		}
	}
}

static void process_packet(uint8_t *packet)
{
	board_led_write(led_state);
	led_state = 1 - led_state; // toggle

	uint8_t cable = packet[0] & 0xf0;
	if (cable != 0) return;

	uint8_t cin = packet[0] & 0x0f;
	if (cin != 0x08 && cin != 0x09) return;

	uint8_t cmd = packet[1] & 0xf0;
	uint8_t chan = packet[1] & 0x0f;

	uint8_t note = packet[2];
	uint8_t vel = packet[3];

	if (cmd == 0x90 && vel > 0) {
		note_on(chan, note, vel);
	} else {
		note_off(chan, note);
	}
}

//--------------------------------------------------------------------+
// USB device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
	blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
	blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than
// 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
	(void) remote_wakeup_en;
	blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
	blink_interval_ms = BLINK_MOUNTED;
}

//--------------------------------------------------------------------+
// MIDI RX task
//--------------------------------------------------------------------+

#if USE_MIDI_CALLBACK == 1
void tud_midi_rx_cb(uint8_t itf)
#else
void midi_task(uint8_t itf)
#endif
{
	uint8_t packet[4];

	while (tud_midi_n_available(itf, 0) >= 4) {
		if (tud_midi_n_packet_read(itf, packet)) {
			process_packet(packet);
		}
	}
}

//--------------------------------------------------------------------+
// Keyboard Task
//--------------------------------------------------------------------+

uint16_t prev = 0;
uint16_t pressed = 0;
uint8_t packet[4];

void keypad_task(void)
{
	pressed = keypad.get_button_states();
	auto changed = pressed ^ prev;
	if (!changed) {
		return;
	}

	uint16_t mask = 1;
	for (auto i = 0U; i < 16; ++i, mask <<= 1) {
		if (changed & mask) {
			if (pressed & mask) {
				packet[0] = 0x09;
				packet[1] = 0x90;
				packet[2] = 60 + i;
				packet[3] = 80;
				tud_midi_n_packet_write(0, packet);
				process_packet(packet);
				keypad.illuminate(i, 0xff - (i << 4), i << 4, 0);
			} else if (prev & mask) {
				packet[0] = 0x08;
				packet[1] = 0x80;
				packet[2] = 60 + i;
				packet[3] = 80;
				tud_midi_n_packet_write(0, packet);
				process_packet(packet);
				keypad.illuminate(i, 0, 0, 0);
			}
		}
	}

	keypad.update();

	prev = pressed;
}

//--------------------------------------------------------------------+
// AUDIO TASK
//--------------------------------------------------------------------+
void audio_task(audio_buffer_pool* ap)
{
	static const int n = SAMPLES_PER_BUFFER;
	static int32_t *samples = (int32_t*)calloc(n, sizeof(int32_t));

	for (int i = 0; i < n; ++i) {
		samples[i] = 0;
	}

	for (auto& voice : voices) {
		voice.update(samples);
	}

	struct audio_buffer *buffer = take_audio_buffer(ap, true);
	int16_t *out = (int16_t *) buffer->buffer->bytes;
	for (int i = 0; i < buffer->max_sample_count; ++i) {
		out[i] = samples[i] >> 5;
	}
	buffer->sample_count = buffer->max_sample_count;
	give_audio_buffer(ap, buffer);
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+

void led_blinking_task(void)
{
	static uint32_t start_ms = 0;

	// Blink every interval ms
	if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
	start_ms += blink_interval_ms;

	board_led_write(led_state);
	led_state = 1 - led_state; // toggle
}
