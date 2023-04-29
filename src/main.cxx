#include <string>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "bsp/board.h"
#include "tusb.h"

#include "pico_display.hpp"
#include "drivers/st7789/st7789.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"

#include "bench.h"
#include "audio.h"
#include "engine.h"

enum {
	BLINK_NOT_MOUNTED = 250,
	BLINK_MOUNTED = 1000,
	BLINK_SUSPENDED = 2500,
};

SynthEngine engine;
static audio_buffer_pool *ap = nullptr;
static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
static bool led_state = false;

static queue_t midi_queue;

static queue_t bench_queue;

struct bench_entry {
	uint32_t	delta;
	uint8_t		active;
};

//--------------------------------------------------------------------+
// MIDI packet dispatch
//--------------------------------------------------------------------+

static void process_packet(uint8_t *packet)
{
	board_led_write(led_state);
	led_state = 1 - led_state; // toggle

	uint8_t cable = packet[0] & 0xf0;
	if (cable != 0) return;

	queue_add_blocking(&midi_queue, packet);
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

void tud_midi_rx_cb(uint8_t itf)
{
	uint8_t packet[4];

	while (tud_midi_n_available(itf, 0) >= 4) {
		if (tud_midi_n_packet_read(itf, packet)) {
			process_packet(packet);
		}
	}
}

//--------------------------------------------------------------------+
// Audio Task
//--------------------------------------------------------------------+

int32_t samples[2 * BUFFER_SIZE];

void audio_task(void)
{
	uint32_t t0 = bench_time();

	for (int i = 0; i < 2 * BUFFER_SIZE ; ++i) {
		samples[i] = 0;
	}

	// get samples from the synth engine
	uint8_t active = engine.update(samples, BUFFER_SIZE);

	uint32_t t1 = bench_time();
	bench_entry data = {
		4 * bench_delta(t0, t1),
		active
	};

	queue_add_blocking(&bench_queue, &data);

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
	bench_init();

	while (true) {
		uint8_t msg[4];
		while (queue_try_remove(&midi_queue, msg)) {
			engine.midi_in(msg[1], msg[2], msg[3]);
		}
		audio_task();
	}
}

//--------------------------------------------------------------------+
// LCD handler
//--------------------------------------------------------------------+

static pimoroni::DisplayDriver* lcd = nullptr;
static pimoroni::PicoGraphics* graphics = nullptr;

void lcd_init()
{
	using namespace pimoroni;

	lcd = new ST7789(PicoDisplay::WIDTH, PicoDisplay::HEIGHT, ROTATE_0, false, get_spi_pins(BG_SPI_FRONT));
	graphics = new PicoGraphics_PenRGB332(lcd->width, lcd->height, nullptr);

	lcd->set_backlight(192);

	// graphics->set_font("bitmap8");
	graphics->set_pen(0, 0, 0);
	graphics->clear();
	lcd->update(graphics);
}

//--------------------------------------------------------------------+
// LED handler
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

//--------------------------------------------------------------------+
// Benchmarking
//--------------------------------------------------------------------+

void benchmark_task()
{
	static uint32_t start_ms = 0;
	static uint32_t bench_min = 0xffffffff, bench_max = 0;
	static uint8_t active;

	bench_entry entry;
	while (queue_try_remove(&bench_queue, &entry)) {
		uint32_t& delta = entry.delta;
		if (delta < bench_min) {
			bench_min = delta;
		} else if (delta > bench_max) {
			bench_max = delta;
		}

		if (active != entry.active) {
			active = entry.active;
		}
	}

	// Blink every interval ms
	if (board_millis() - start_ms < 250) return;
	start_ms += 250;

	using namespace pimoroni;
	graphics->set_pen(0, 0, 0);
	graphics->clear();
	graphics->set_pen(255, 255, 255);
	graphics->text(std::to_string(bench_min), Point(4,  4), 120);
	graphics->text(std::to_string(bench_max), Point(4, 20), 120);
	graphics->text(std::to_string(active), Point(4, 36), 120);
	lcd->update(graphics);
}

//--------------------------------------------------------------------+
// Program startup
//--------------------------------------------------------------------+

int main() {

	stdio_init_all();

	vreg_set_voltage(VREG_VOLTAGE_1_30);
	sleep_ms(1);
	set_sys_clock_khz(250000, false);

	board_init();
	ap = audio_init();
	lcd_init();
	tusb_init();

	queue_init(&midi_queue, 4, 64);
	queue_init(&bench_queue, sizeof(bench_entry), 64);

	multicore_launch_core1(audio_loop);

	while (1)
	{
		tud_task();
		led_blinking_task();
		benchmark_task();
	}
}
