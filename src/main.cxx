#include <string>
#include <cstdio>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "tusb.h"
#if __has_include("bsp/board_api.h")
#include "bsp/board_api.h"
#else
#include "bsp/board.h"
#endif

#if CONFIG_LCD_ACTIVE
#include "pico_display_2.hpp"
#include "drivers/st7789/st7789.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#endif

#include "bench.h"
#include "audio.h"
#include "data.h"
#include "waves.h"
#include "engine.h"
#include "version.h"


SynthEngine engine;

static queue_t midi_queue;

static queue_t bench_queue;

struct bench_entry {
	uint32_t	delta;			// nanoseconds, at 250 MHz
	uint32_t	voices;			// how many were rendered for it
};

//--------------------------------------------------------------------+
// LED state
//--------------------------------------------------------------------+

enum {
	BLINK_NOT_MOUNTED = 250,
	BLINK_MOUNTED = 1000,
	BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
static bool led_state = false;

static inline void led_toggle()
{
	board_led_write(led_state);
	led_state = 1 - led_state;
}

void led_blinking_task(void)
{
	static uint32_t start_ms = 0;

	// Blink every interval ms
	if (board_millis() - start_ms < blink_interval_ms) return; // not enough time
	start_ms += blink_interval_ms;

	led_toggle();
}

//--------------------------------------------------------------------+
// Display Pack RGB LED
//--------------------------------------------------------------------+

// the Pimoroni Display Pack's RGB LED, wired common anode - driving a
// pin low lights it, high turns it off.  Pimoroni's own driver PWMs
// these through hardware_pwm; plain GPIO is enough for an indicator,
// and avoids claiming a PWM slice for it
enum {
	RGB_R = 6,
	RGB_G = 7,
	RGB_B = 8,
};

static const uint rgb_pins[] = { RGB_R, RGB_G, RGB_B };

static void rgb_init()
{
	for (uint pin : rgb_pins) {
		gpio_init(pin);
		gpio_set_dir(pin, GPIO_OUT);
		gpio_put(pin, 1);				// off
	}
}

static inline void rgb_set(uint pin, bool on)
{
	gpio_put(pin, !on);
}

//--------------------------------------------------------------------+
// Hard fault reporting
//--------------------------------------------------------------------+

// A fault on this board is otherwise invisible.  stdio is UART-only and
// generally unwired, the LEDs can say *that* something died but not
// where, and a fault on either core takes the other down with it - core
// 0 wedges the moment tud_task() touches a spinlock core 1 died holding,
// which looks like a hang in TinyUSB rather than a fault anywhere near
// the real culprit.
//
// So: stash the exception frame somewhere the runtime will not clear,
// reboot, and put it on the LCD once that is back up.  The section is
// the whole mechanism - .uninitialized_data survives a warm reset, and a
// watchdog reboot is one.
//
// This catches panic() and hard_assert() as well as genuine memory
// faults: both end in a breakpoint, and a breakpoint with no debugger
// attached escalates to HardFault (hfsr bit 31, cfsr zero).  That is
// exactly how the USB isochronous endpoint bug was finally identified,
// after several rounds of guessing from LED states.
#define FAULT_TRACE		6

struct fault_info {
	uint32_t	magic;
	uint32_t	core;
	uint32_t	pc, lr, psr;
	uint32_t	cfsr, hfsr;
	uint32_t	trace[FAULT_TRACE];
};

#define FAULT_MAGIC		0xfa17ed00

static struct fault_info __uninitialized_ram(fault_info);

extern "C" void hardfault_report(uint32_t* frame)
{
	fault_info.core = get_core_num();

	// the frame the hardware pushed: r0 r1 r2 r3 r12 lr pc xpsr
	fault_info.pc   = frame[6];
	fault_info.lr   = frame[5];
	fault_info.psr  = frame[7];

	fault_info.cfsr = *(volatile uint32_t*)0xe000ed28;	// SCB->CFSR
	fault_info.hfsr = *(volatile uint32_t*)0xe000ed2c;	// SCB->HFSR

	// A poor man's backtrace.  panic() is several frames below the
	// breakpoint by the time it fires, and the exception frame's lr only
	// points back into panic itself - so whoever actually asserted is
	// not in the frame at all.  Scanning the stack for anything that
	// looks like a return address into flash finds it, along with some
	// false positives that addr2line makes obvious.
	extern char __flash_binary_start, __flash_binary_end;

	const uint32_t lo  = (uint32_t)&__flash_binary_start;
	const uint32_t hi  = (uint32_t)&__flash_binary_end;
	const uint32_t top = 0x20082000;					// end of SRAM

	uint32_t n = 0;
	for (uint32_t i = 0; i < 512 && n < FAULT_TRACE; ++i) {
		uint32_t* p = frame + i;

		if ((uint32_t)p >= top) break;

		uint32_t w = *p;
		if ((w & 1) && (w & ~1u) >= lo && (w & ~1u) < hi) {
			fault_info.trace[n++] = w & ~1u;
		}
	}
	while (n < FAULT_TRACE) {
		fault_info.trace[n++] = 0;
	}

	fault_info.magic = FAULT_MAGIC;

	watchdog_reboot(0, 0, 0);

	while (true) {
		tight_loop_contents();
	}
}

// naked, so the frame is still exactly where the exception left it and
// nothing of ours has run to disturb r0
extern "C" __attribute__((naked)) void isr_hardfault(void)
{
	__asm volatile (
		"tst	lr, #4				\n"
		"ite	eq					\n"
		"mrseq	r0, msp				\n"
		"mrsne	r0, psp				\n"
		"b		hardfault_report	\n"
	);
}

//--------------------------------------------------------------------+
// USB service
//--------------------------------------------------------------------+

// tud_task() runs from a timer, not from the main loop.
//
// The main loop cannot be trusted to be prompt: benchmark_task() calls
// lcd->update(), which ends in dma_channel_wait_for_finish_blocking()
// and holds core 0 for about 62 ms pushing 150 KB over SPI.  The host
// polls the isochronous IN endpoint every 1 ms, and an endpoint that is
// not re-armed in time simply misses that frame - which measured as
// 550 of every 1000 frames served, and sounded like it.
//
// Servicing USB from a timer decouples the two: the display can block
// for as long as it likes and the endpoint is still re-armed within
// USB_PUMP_US.  Nothing in the path needs thread context - the audio
// callback only copies from a ring buffer, and process_packet() already
// runs from the UART interrupt.
//
// Priority sits *below* USBCTRL_IRQ's 0x80, so the USB interrupt can
// still preempt this and post the completions it is draining.
#define USB_PUMP_US		250

static void usb_pump_cb(uint alarm)
{
	tud_task();

	// re-armed from the end, so a long tud_task() cannot re-enter itself
	hardware_alarm_set_target(alarm, delayed_by_us(get_absolute_time(), USB_PUMP_US));
}

static void usb_pump_init(void)
{
	int alarm = hardware_alarm_claim_unused(true);

	hardware_alarm_set_callback(alarm, usb_pump_cb);
	irq_set_priority(timer_hardware_alarm_get_irq_num(timer_hw, alarm), 0xc0);
	hardware_alarm_set_target(alarm, delayed_by_us(get_absolute_time(), USB_PUMP_US));
}

//--------------------------------------------------------------------+
// MIDI packet dispatch
//--------------------------------------------------------------------+

static void process_packet(uint8_t *packet)
{
	led_toggle();

	uint8_t cable = packet[0] & 0xf0;
	if (cable != 0) return;

	// try_add, not add_blocking.  this runs inside tud_task() and also
	// inside the UART interrupt handler, so blocking here stalls core
	// 0 - and it blocks against core 1 draining the queue, which is
	// exactly the coupling that has bitten twice already.  dropping a
	// MIDI message beats hanging the device
	queue_try_add(&midi_queue, packet);
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
// USB MIDI RX task
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
// Serial MIDI
//--------------------------------------------------------------------+

const auto MIDI = uart1;
const auto MIDI_IRQ = UART1_IRQ;

void midi_serial_irq()
{
	static uint8_t buf[4] = { 0, };
	static uint8_t pos = 1;
	static uint8_t len = 0;
	static bool sysex = false;

	while (uart_is_readable(MIDI)) {
		uint8_t in = uart_getc(MIDI);

		// ignore MIDI realtime messages
		if (in >= 0xf8) continue;

		// (mostly) ignore SysEx messages
		if (sysex) {
			if (in & 0x80) {
				sysex = false;
			} else {
				continue;
			}
		}

		// process command bytes
		if (in & 0x80) {
			if (in == 0xf0) {
				sysex = true;
				continue;
			} else if (in >= 0x80) {
				pos = 1;
				buf[pos++] = in;
				len = ((in & 0xe0) == 0xc0) ? 2 : 3;
			}
			continue;
		}

		// process data bytes
		buf[pos] = in;
		if (pos++ == len) {
			process_packet(buf);
			pos = 2;
		}
	}
}

void midi_init()
{
	uart_init(MIDI, 31250);
	gpio_set_function(4, GPIO_FUNC_UART);
	gpio_set_function(5, GPIO_FUNC_UART);
	uart_set_hw_flow(MIDI, false, false);
	uart_set_format(MIDI, 8, 1, UART_PARITY_NONE);

	irq_set_exclusive_handler(MIDI_IRQ, midi_serial_irq);
	irq_set_enabled(MIDI_IRQ, true);

	uart_set_irq_enables(MIDI, true, false);
}

//--------------------------------------------------------------------+
// Audio Task
//--------------------------------------------------------------------+

int32_t samples[2 * BUFFER_SIZE];


void audio_task(void)
{
	uint32_t t0 = bench_time();

	// clear accumulation buffer
	memset(samples, 0, sizeof(samples));

	// get samples from the synth engine
	uint32_t voices = engine.update(samples, BUFFER_SIZE);

	uint32_t t1 = bench_time();
	bench_entry entry = {
		4 * bench_delta(t0, t1),
		voices
	};

	// dropping a benchmark sample is always better than stalling the
	// audio for one.  the queue holds 64 entries, which is only 371 ms
	// at 44.1 kHz, so blocking here hands core 0 the power to silence
	// core 1 just by being busy for a third of a second
	queue_try_add(&bench_queue, &entry);

	// audio_take never blocks - on core 1 it must not, and asking the
	// I2S backend to block once cost a whole boot hang.  polling sees
	// exactly the same buffers come back, and this core has nothing
	// else it should be doing while it waits
	int16_t *out;
	while (!(out = audio_take())) {
		tight_loop_contents();
	}

	for (auto i = 0U; i < 2 * BUFFER_SIZE; ++i) {
		out[i] = samples[i] >> 6;
	}

	audio_give();
}

// FPSCR is per core, and the filter runs entirely on this one.
// flush-to-zero costs nothing to set and takes denormals off the
// table: they are slower than normal operands on this FPU, and the
// filter's state is what would reach them.  in practice the
// oscillator feeding it never goes silent while a voice exists, so
// this is insurance rather than a fix for anything observed
static void fpu_flush_to_zero(void)
{
	uint32_t fpscr;

	__asm volatile ("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr |= (1u << 24);				// FZ
	__asm volatile ("vmsr fpscr, %0" :: "r" (fpscr));
}

void audio_loop(void)
{
	fpu_flush_to_zero();
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

#if CONFIG_LCD_ACTIVE

static pimoroni::DisplayDriver* lcd = nullptr;
static pimoroni::PicoGraphics* graphics = nullptr;

void lcd_init()
{
	using namespace pimoroni;

	lcd = new ST7789(PicoDisplay2::WIDTH, PicoDisplay2::HEIGHT, ROTATE_0, false, get_spi_pins(BG_SPI_FRONT));
	graphics = new PicoGraphics_PenRGB332(lcd->width, lcd->height, nullptr);

	lcd->set_backlight(192);

	graphics->set_font("bitmap8");
	graphics->set_pen(0, 0, 0);
	graphics->clear();
	lcd->update(graphics);
}

#else
void lcd_init()
{
}
#endif

// Show a fault captured before the last reboot, and stop.
// Stopping is deliberate - if it carried on, the host would re-attach,
// it would fault again, and the report would flicker past on a reboot
// loop instead of sitting still long enough to read
static void show_fault(const struct fault_info* f)
{
#if CONFIG_LCD_ACTIVE
	using namespace pimoroni;

	char buf[64];

	graphics->set_pen(0, 0, 0);
	graphics->clear();

	graphics->set_pen(255, 96, 96);
	snprintf(buf, sizeof(buf), "HARD FAULT core %u", (unsigned)f->core);
	graphics->text(buf, Point(4, 4), 310);

	graphics->set_pen(255, 255, 255);
	int y = 26;

	snprintf(buf, sizeof(buf), "pc %08lx  cfsr %08lx",
		(unsigned long)f->pc, (unsigned long)f->cfsr);
	graphics->text(buf, Point(4, y), 310);
	y += 18;

	snprintf(buf, sizeof(buf), "lr %08lx  hfsr %08lx",
		(unsigned long)f->lr, (unsigned long)f->hfsr);
	graphics->text(buf, Point(4, y), 310);
	y += 22;

	// the stack scan - this is the part that names the caller
	graphics->set_pen(160, 200, 255);
	for (uint32_t i = 0; i < FAULT_TRACE; ++i) {
		if (!f->trace[i]) break;

		snprintf(buf, sizeof(buf), "t%lu %08lx",
			(unsigned long)i, (unsigned long)f->trace[i]);
		graphics->text(buf, Point(4, y), 310);
		y += 18;
	}

	graphics->set_pen(128, 128, 128);
	graphics->text(GIT_VERSION, Point(4, y + 6), 310);

	lcd->update(graphics);
#else
	(void)f;
#endif
}

//--------------------------------------------------------------------+
// Benchmarking
//--------------------------------------------------------------------+

void benchmark_task()
{
	static uint32_t start_ms = 0;
	static uint32_t bench_min = 0xffffffff, bench_max = 0;
	static uint32_t voices = 0, voices_max = 0;

	bench_entry entry;
	while (queue_try_remove(&bench_queue, &entry)) {
		uint32_t& delta = entry.delta;
		if (delta < bench_min) {
			bench_min = delta;
		} else if (delta > bench_max) {
			bench_max = delta;
		}

		// the latest count, and the high water mark since boot
		voices = entry.voices;
		if (voices > voices_max) {
			voices_max = voices;
		}
	}

	// refresh every 250 ms.  This blocks for ~62 ms - lcd->update() ends
	// in dma_channel_wait_for_finish_blocking() - which is survivable
	// only because USB is no longer serviced from this loop; see
	// usb_pump_cb()
	if (board_millis() - start_ms < 250) return;
	start_ms += 250;

#if CONFIG_LCD_ACTIVE
	using namespace pimoroni;
	graphics->set_pen(0, 0, 0);
	graphics->clear();
	graphics->set_pen(255, 255, 255);
	graphics->text(std::to_string(bench_min), Point(4,  4), 120);
	graphics->text(std::to_string(bench_max), Point(4, 20), 120);
	graphics->text(std::to_string(voices) + "/" + std::to_string(voices_max),
		Point(4, 36), 120);

	// which build this is.  dimmer than the live figures, since it
	// never changes - a trailing + means the tree had uncommitted
	// changes when it was built, so the revision alone does not
	// identify what is running
	graphics->set_pen(128, 128, 128);
	graphics->text(GIT_VERSION, Point(4, 52), 120);
	lcd->update(graphics);
#endif
}

//--------------------------------------------------------------------+
// Program startup
//--------------------------------------------------------------------+

int main() {

	stdio_init_all();

	// the lookup tables are computed rather than baked in, so this
	// has to happen before anything reads one
	tables_init();
	waves_init();

	// and only now can the engine set its channel defaults: Channel's
	// pan values come out of pan_table, and this object is a global, so
	// its constructor ran long before the tables existed
	engine.init();

	// so the build is identifiable over the UART too, not only on a
	// display that may not be fitted
	printf("PicoSynth %s\n", GIT_VERSION);

	// these float at reset, so drive them off before anything else -
	// and before the one case below that wants to light one
	rgb_init();

	vreg_set_voltage(VREG_VOLTAGE_1_30);
	sleep_ms(1);

	// this is asked for, not required, so it returns false rather than
	// panicking if it can't be had.  that matters more than it looks:
	// the bench figures scale cycles to nanoseconds assuming 250 MHz,
	// so a silent failure would misreport the audio budget by 1.67x
	if (!set_sys_clock_khz(250000, false)) {
		rgb_set(RGB_R, true);
		printf("clock: set_sys_clock_khz(250000) failed, still at %lu Hz\n",
			(unsigned long)clock_get_hz(clk_sys));
	}

	board_init();
	audio_init();
	lcd_init();

	// a fault captured before the last reboot outranks
	// everything else.  Report it before USB comes up, so the host
	// cannot provoke the same fault again while it is being read
	if (fault_info.magic == FAULT_MAGIC) {
		fault_info.magic = 0;
		show_fault(&fault_info);

		while (true) {
			tight_loop_contents();
		}
	}

	tusb_init();
	midi_init();

	queue_init(&midi_queue, 4, 64);
	queue_init(&bench_queue, sizeof(bench_entry), 64);

	// after tusb_init(), and after the queues it feeds
	usb_pump_init();

	multicore_launch_core1(audio_loop);

	while (1)
	{
		// no tud_task() here - it runs from a timer, see usb_pump_cb().
		// benchmark_task() blocks for ~62 ms in the LCD update, which is
		// 62 missed USB frames if the two share this loop
		led_blinking_task();
		benchmark_task();
	}
}
