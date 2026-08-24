# PicoSynth

A polyphonic wavetable synthesizer for the Raspberry Pi Pico 2.

(C) Ray Bellis 2023-2024

NB: this is very much a work in progress!

## Current Features

- 64 voices
- 16 channel multi-timbral
- four hard-coded presets
- 16-bit stereo audio at 48kHz, over either I2S or USB
- wavetable DCOs (2048 x 16-bit samples) using the RP2350 interpolator
- DCO modulation:
  - LFO (per voice)
  - ADSR pitch envelope
  - pitch bend
- resonant state-variable filter, per voice, offset by CC 74 and CC 71
- DCA
  - ADSR envelope
  - stereo pan
- USB MIDI device
- Serial MIDI (UART1, pins 4/5)

## Audio output

`CONFIG_AUDIO_USB` in `CMakeLists.txt` selects the transport:

- **0** (default) — 16-bit stereo I2S, as configured for the Pimoroni
  Audio Pack
- **1** — USB audio.  The synth appears to the host as a stereo 48kHz
  recording device, alongside its MIDI interface, so it can be recorded
  or monitored over the same cable that carries the notes.  No DAC
  needed

Only one is compiled in.

## Hardware

A **Raspberry Pi Pico 2** (RP2350) is required — the RP2040 is no longer
supported.  The audio path uses the M33's FPU and saturating and
64-bit-multiply instructions, none of which exist on the RP2040's M0+.

The RP2350 is overclocked to 250 MHz.

The I2S interface is configured for use with the Pimoroni Audio Pack.  A
PCB with MIDI DIN ports and I2S DAC is under development.

`CONFIG_HW_PICOADK` in `CMakeLists.txt` still selects the pin assignments
for the [Datanoise PicoADK](https://github.com/DatanoiseTV/PicoADK-Hardware),
but that board is RP2040-based and so will not currently build.

## Building

Familiarity with using the Pico SDK is assumed.

My own development system is macOS and I use the arm-none-eabi-gcc
compiler v13.2.0 from MacPorts.

Building the code requires only the Pico SDK toolchain and the following
repositories:

- Pico SDK (https://github.com/raspberrypi/pico-sdk.git)
- Pico Extras (https://github.com/raspberrypi/pico-extras.git)
- Pimoroni Pico Lib (https://github.com/pimoroni/pimoroni-pico.git)

I use the following in my `.cshrc` with the above three repositories all
checked out into `${PICO_HOME}`:

```
setenv PICO_HOME "/opt/pico"
setenv PICO_SDK_PATH "${PICO_HOME}/pico-sdk"
setenv PICO_EXTRAS_PATH "${PICO_HOME}/pico-extras"
```

## License

This source code is released under the GPLv3.0 License
