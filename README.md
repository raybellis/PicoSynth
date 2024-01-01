# PicoSynth

A polyphonic wavetable synthesizer for the Raspberry Pi Pico.

(C) Ray Bellis 2023-2024

NB: this is very much a work in progress!

## Current Features

- 128 voices
- 16 channel multi-timbral
- 16-bit stereo I2S audio at 44.1kHz 
- wavetable DCOs (2048 x 16-bit samples) using the RP2040 interpolator
- DCO modulation:
  - LFO (per voice)
  - ADSR pitch envelope
  - pitch bend
- DCA
  - ADSR enveloper
  - stereo pan
- USB MIDI device
- Serial MIDI (UART1, pins 4/5)

The I2S interface is configured for use with the Pimoroni Audio Pack.  A
PCB with MIDI DIN ports and I2S DAC is under development.

The RP2040 is overclocked to 250 MHz.

## License

This source code is released under the GPLv3.0 License
