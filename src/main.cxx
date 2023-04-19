#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/structs/systick.h"
#include "pico_rgb_keypad.hpp"
#include "bsp/board.h"
#include "tusb.h"
#include "audio.h"
#include "engine.h"

#include <stdfix.h>

#define USE_MIDI_CALLBACK 0

enum	{
	BLINK_NOT_MOUNTED = 250,
	BLINK_MOUNTED = 1000,
	BLINK_SUSPENDED = 2500,
};

pimoroni::PicoRGBKeypad keypad;
SynthEngine engine;

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
static bool led_state = false;
static audio_buffer_pool *ap = nullptr;
static queue_t midi_queue;

void led_blinking_task();
void keypad_task();
void audio_task();
void audio_loop();
void midi_task(uint8_t);

int main() {

    systick_hw->rvr = 0xffffff;
    systick_hw->csr = 0x5;

	stdio_init_all();
	board_init();
	tusb_init();
	ap = audio_init();

	keypad.init();
	keypad.set_brightness(0.2f);

	queue_init(&midi_queue, 4, 64);
	multicore_launch_core1(audio_loop);

	while (1)
	{
		tud_task();
		led_blinking_task();
		keypad_task();
#if !USE_MIDI_CALLBACK
		midi_task(0);
#endif
	}
}

static void process_packet(uint8_t *packet)
{
	board_led_write(led_state);
	led_state = 1 - led_state; // toggle

	uint8_t cable = packet[0] & 0xf0;
	if (cable != 0) return;

	queue_add_blocking(&midi_queue, packet);
	// engine.midi_in(packet[1], packet[2], packet[3]);
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
				packet[3] = 100;
				tud_midi_n_packet_write(0, packet);
				process_packet(packet);
				keypad.illuminate(i, 0xff - (i << 4), i << 4, 0);
			} else if (prev & mask) {
				packet[0] = 0x08;
				packet[1] = 0x80;
				packet[2] = 60 + i;
				packet[3] = 100;
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

int32_t samples[2 * SAMPLES_PER_BUFFER];

void audio_task(void)
{
	for (int i = 0; i < 2 * SAMPLES_PER_BUFFER ; ++i) {
		samples[i] = 0;
	}

	// get samples from the synth engine
	engine.update(samples, SAMPLES_PER_BUFFER);

	struct audio_buffer *buffer = take_audio_buffer(ap, true);
	int16_t *out = (int16_t *) buffer->buffer->bytes;

	for (auto i = 0U; i < 2 * buffer->max_sample_count; ++i) {
		out[i] = samples[i] >> 6;
	}

	buffer->sample_count = buffer->max_sample_count;
	give_audio_buffer(ap, buffer);
}

void audio_loop(void)
{
	while (true) {
		uint8_t msg[4];
		while (queue_try_remove(&midi_queue, msg)) {
			engine.midi_in(msg[1], msg[2], msg[3]);
		}
		audio_task();
	}
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+

void led_blinking_task(void)
{
	static uint32_t start_ms = 0;

	// Blink every interval ms
	if (board_millis() - start_ms < blink_interval_ms) return; // not enough time
	start_ms += blink_interval_ms;

	board_led_write(led_state);
	led_state = 1 - led_state; // toggle
}
