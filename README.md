# PicoSynth

A polyphonic wavetable synthesizer for the Raspberry Pi Pico.

(C) Ray Bellis 2023-2024

NB: this is very much a work in progress!

## Current Features

- 128 voices
- 16 channel multi-timbral
- four hard-coded presets
- 16-bit stereo I2S audio at 44.1kHz 
- wavetable DCOs (2048 x 16-bit samples) using the RP2040 interpolator
- DCO modulation:
  - LFO (per voice)
  - ADSR pitch envelope
  - pitch bend
- DCA
  - ADSR envelope
  - stereo pan
- USB MIDI device
- Serial MIDI (UART1, pins 4/5)

The RP2040 is overclocked to 250 MHz.

The I2S interface is configured for use with the Pimoroni Audio Pack.  A
PCB with MIDI DIN ports and I2S DAC is under development.

The DatanoiseTV [PicoADK](https://github.com/DatanoiseTV/PicoADK-Hardware)
 board is also supported via `CONFIG_HW_PICOADK` in the `CMakeLists.txt` file.

## Building

Familiarity with using the RP2040 Pico SDK is assumed.

My own development system is macOS and I use the arm-none-eabi-gcc
compiler v13.2.0 from MacPorts.

Building the code requires the following repositories, as well as an
installation of NodeJS which is used to construct the various lookup
tables used by the code:

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
