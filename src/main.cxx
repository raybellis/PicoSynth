#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"
#include "pico_rgb_keypad.hpp"
#include "bsp/board.h"
#include "tusb.h"

enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

pimoroni::PicoRGBKeypad keypad;

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
const uint LED_PIN = PICO_DEFAULT_LED_PIN;

void led_blinking_task(void);
void midi_task(void);

int main() {
  board_init();
  tusb_init();

  keypad.init();
  keypad.set_brightness(0.2f);

  while (1)
  {
    tud_task(); // tinyusb device task
    led_blinking_task();
    midi_task();
  }
}

//--------------------------------------------------------------------+
// Device callbacks
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
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
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
// MIDI Task
//--------------------------------------------------------------------+

uint16_t prev = 0;
uint16_t pressed = 0;
uint8_t msg[3];

void midi_task(void)
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
        msg[0] = 0x90;
        msg[1] = 60 + i;
        msg[2] = 127;
        tud_midi_n_stream_write(0, 0, msg, 3);
        keypad.illuminate(i, 0xff - (i << 4), i << 4, 0);
      } else if (prev & mask) {
        msg[0] = 0x80;
        msg[1] = 60 + i;
        msg[2] = 0;
        tud_midi_n_stream_write(0, 0, msg, 3);
        keypad.illuminate(i, 0, 0, 0);
      }
    }
  }
  keypad.update();

  prev = pressed;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
