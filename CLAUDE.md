# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A polyphonic wavetable synthesizer firmware for the Raspberry Pi Pico (RP2040, overclocked to 250 MHz),
16-channel multi-timbral, 16-bit stereo I2S out at 44.1 kHz, driven by USB MIDI and serial MIDI.
GPLv3. There is no test suite — verification is by flashing hardware and listening.

## Build

Requires the Pico SDK toolchain plus NodeJS (used at build time to generate lookup tables).
Three external repos are expected under `${PICO_HOME}` (default `/opt/pico`):

```
setenv PICO_HOME       "/opt/pico"
setenv PICO_SDK_PATH   "${PICO_HOME}/pico-sdk"
setenv PICO_EXTRAS_PATH "${PICO_HOME}/pico-extras"
# also needs ${PICO_HOME}/pimoroni-pico when CONFIG_LCD_ACTIVE=1
```

```
cmake -B build -DCMAKE_BUILD_TYPE=Release   # once
make -C build -j8                           # picks up CONFIG_* and generator edits by itself
```

Artifacts land in `build/` (`PicoSynth.uf2` to flash, plus `.elf`/`.dis`/`.elf.map` for inspecting
code size and disassembly — useful when tuning the audio inner loop).

The user's shell is tcsh; use `setenv`, not `export`, in any suggested shell snippets.

The build is pinned to RP2040 in the existing cache (`PICO_PLATFORM=rp2040`, `PICO_BOARD=pico`);
the in-flight RP2350 work therefore needs a separate build directory rather than a reconfigure of
`build/`, since the platform is not re-derivable from a cached tree.

## Flashing and printf debugging

The SDK fetches picotool into the tree at `build/_deps/picotool/picotool` (it is not on `PATH`);
otherwise hold BOOTSEL and copy `build/PicoSynth.uf2` to the mounted volume.

USB is dedicated to MIDI, so `pico_enable_stdio_usb` is **0** and `pico_enable_stdio_uart` is **1** —
`printf`/`stdio` goes out UART0 (pins 0/1), which is a different UART from the serial MIDI input on
UART1 (pins 4/5). Note that stdio is blocking: a `printf` from core 1 inside `audio_loop` will miss
the buffer deadline and produce audible glitches. Instrument core 1 by pushing to `bench_queue` and
printing from core 0 instead.

## Generated sources — do not edit

Nothing generated lives in `src/`. Everything below is written into `build/generated/tables/` by
`add_custom_command` rules at **build time**, so `make` alone is enough after editing a generator:

- `utils/data.js <outdir> <sample_rate> <wave_shift> <buffer_size>` → `data.c` (`note_table`,
  `pan_table`, `power_table`, `svf_table`), `data.h` (declarations for those four), and
  `settings.h` (`SAMPLE_RATE`, `BUFFER_SIZE`, `WAVE_SHIFT`, `WAVE_LEN`, `WAVE_MAX`, `SVF_LEN`)
- `utils/waves.js <outdir>` → `waves.c` (2048-sample × 16-bit sine/square/saw/triangle tables and
  the `waves[]` index, declared by the hand-written `src/waves.h`)

**Never hand-write an `extern` for a generated table — include `data.h`.** Those declarations used
to be scattered through the consumers, and nothing checked them against the definitions: `filter.cxx`
once declared `extern uint16_t* svf_table;` against an `int16_t[]`, which linked silently and
dereferenced the table's first four bytes as an address. `data.c` includes `data.h` itself, so both
a bad definition and a bad consumer declaration are now compile errors.

Two build-system details worth preserving if you touch `CMakeLists.txt`:

- the rules `DEPENDS` on `CMakeLists.txt` as well as the generator, because the `CONFIG_*` values
  are baked into the command line where `make` can't see them from timestamps alone
- the generated **headers** are listed as target sources next to the `.c` files; that is what orders
  generation ahead of every compile instead of racing with it on a parallel build from clean

`build/generated/` also holds the SDK's own `pico_base` (written at configure time, so `make`
cannot recreate it) — delete `build/generated/tables`, never the parent.

## Compile-time configuration

All knobs live at the top of `CMakeLists.txt` and are pushed into the code as either generated
macros (audio params) or `target_compile_definitions` (hardware selection):

- `CONFIG_SAMPLE_RATE` / `CONFIG_WAVE_SHIFT` / `CONFIG_BUFFER_SIZE` — feed the JS generators
- `CONFIG_LCD_ACTIVE` — pulls in the Pimoroni Pico Display 2 stack for the benchmark readout
- `CONFIG_HW_PIMORONI_AUDIO` / `CONFIG_HW_PICOADK` — select I2S pin assignments (and, for PicoADK,
  the GPIO 25 DAC soft-unmute in `audio.c`)

## Architecture

**Dual-core split.** Core 0 (`main`) runs the TinyUSB device task, LED blink, and the LCD/benchmark
readout. Core 1 runs `audio_loop()` — the entire synthesis path. The two communicate only through
two SDK `queue_t`s: `midi_queue` (4-byte USB-MIDI-style packets, core 0 → core 1) and `bench_queue`
(timing samples, core 1 → core 0). Keep this boundary: anything touching `SynthEngine` state must
happen on core 1, and anything blocking must stay off it.

**MIDI ingress converges on one format.** Both USB (`tud_midi_rx_cb`) and serial MIDI
(`midi_serial_irq` on UART1, pins 4/5, 31250 baud, with its own running-status/SysEx-skipping state
machine) build a 4-byte packet and call `process_packet()`, which drops non-zero cable numbers and
enqueues. Core 1 dequeues and calls `engine.midi_in(status, d1, d2)`.

**Object model.** `SynthEngine` owns a `VoicePool` (`src/voice.{h,cxx}`, a fixed `Voice voice[nv]`)
and `Channel channel[16]`.
A `Voice` points at (never owns) a `Channel` and a `Patch`, and owns its `Envelope`s and `Filter`.
`Channel` holds MIDI-visible state (CC array, program, bend) plus derived values (`bend_f`,
`pan_l/pan_r`) recomputed on change so the audio loop only does lookups. `Patch` is a flat POD of
7-bit parameters; the four presets in `src/presets.c` are hard-coded and program change selects
`presets[program % 4]`.

**Voice allocation** belongs entirely to `VoicePool`: first-free, then steal-any-released (`steal` is
set on note-off, so a voice in its release tail can be taken). Both paths return through the private
`claim()`, which is the invariant that matters — an earlier version returned a stolen voice still
flagged `free`, so the note was silently dropped and its envelopes and filter leaked. `release()`
deletes them and re-`init()`s. Iterating a pool visits only voices in use, so engine code never
tests `free` itself; releasing mid-iteration is safe. Note `new`/`delete` happen on core 1 per
note-on/off — a known wart in a real-time path.

**Audio path per buffer** (`SynthEngine::update`): tick all envelopes and reap voices whose DCA
envelope went inactive → configure `interp0` once → per active voice, compute the DCA level
(chaining 15-bit envelope × patch level × velocity × channel volume × pan), compute `dco_step` from
`note_table[note]` modulated in turn by pitch bend, the DCO envelope, and the LFO → render mono
samples → filter → accumulate into the stereo `int32_t` buffer. `main.cxx`'s `audio_task()` then
shifts the accumulator down by 6 into the `int16_t` I2S buffer.

All three functions in that path — `SynthEngine::update`, `Voice::update`, `SVF::apply` — are marked
`__not_in_flash_func` so the per-sample loops don't fetch instructions over XIP. Keep it that way
when adding to it. The tables stay in flash deliberately: they are read once per buffer, not per
sample. Note also that the filter runs *before* the `if (!dca) continue;` early-out — a filter that
skips buffers comes back with state hundreds of samples stale, which is an audible click.

**Fixed-point conventions.** Phase is 16.16 within a `WAVE_MAX`-sized space (`WAVE_LEN << 16`);
`note_table` entries are ready-made phase increments for the configured sample rate.
`frequency_modulate()` applies a pitch offset by 32-bit-multiplying the step by
`power_table[x + 8192]`, a 1:15 fixed-point 2^(x/8192) multiplier — so all pitch modulation sources
(bend, DCO envelope, LFO) must be reduced to a 14-bit signed value in that domain before use.
The inline `// N bits` comments in `SynthEngine::update` track accumulator width; preserve and
update them when changing any scaling step, since overflow here is silent and audible.

The filter uses two different scales in the same expressions, which is easy to get wrong: the
damping factor `q` is 1:15 (`fmul_su`), while the cutoff coefficient is 2:14 (`fmul_f`) because the
Chamberlin SVF needs `f = 2·sin(π·Fc/Fs)` and 2.0 will not fit in 1:15. Both round to nearest rather
than shifting — a plain `>>` floors, and inside an integrator that accumulates into audible DC.
`svf_table` is clamped at `Fs/6`, the stability limit of this filter topology, which also caps every
entry at 16384 and so keeps the table inside an `int16_t` at any sample rate.

`q` holds `1/Q`, not `Q`, so *small* values are the resonant ones — Q=2 is 16384, Q=64 is 512.
`set_q` takes a 7-bit *index*, not a value, and reads the pair `q_table[n]` / `scale_table[n]`; that
is what guarantees the damping stays where the filter is stable and `fmul_f(high, f)` cannot
overflow, since no index escapes the table. `q_table` is geometric over Q = 1..64, so each step is a
constant increment in dB of emphasis. The exact stability bound for this update order is
`q1 < 2/f − f/2`, bottoming out at 1.5 where `f` reaches 1.0, comfortably above the table's ceiling
of 1.0 (`SVF_Q_MAX`).

`scale` is the separate input scaling, and it is the knob that decides where the response sits in
absolute terms: the resonant peak is always a factor of Q above the passband, and all `scale` picks
is which of the two is held at unity. Scaling by `q` itself pins the peak at unity and lets the
passband fall as `1/Q` — 36 dB of bass loss by Q=64. `scale_table` instead uses the geometric mean
of `q` and 1.0, splitting it evenly: 18 dB of droop against an 18 dB peak. Measured passband/peak
at Q=64 is −17.9/+18.0 dB, and at the presets' default Q≈8, −9.0/+9.1 dB. It stays at or below 1.0
for every entry, which is why it costs nothing in the overflow headroom — worst-case `|high|` is
unchanged at 91269 against a limit of 131071. Any table that boosted *above* unity would need that
overflow fixed first.

Do not "optimise" `SVF::apply` by hoisting `q` and `scale` into locals. They are `uint16_t` members
and `buf` is `int16_t*`, so they may alias and the compiler does reload both every iteration — but
hoisting measured two instructions a sample worse, because on M0+ the extra live values land in
high registers and every `muls` then needs a `mov` down to a low one.

**RP2040 hardware used directly:** `interp0` in blend/wave-lookup mode is the wavetable oscillator
(shift 15, mask `wave_shift`, raw add) — `Voice::update` loads the step/base/accum and pops samples;
`hw_divider` for bend scaling; SysTick as a cycle counter via `bench.h` (`bench_delta` handles the
24-bit wrap), reported as min/max microsecond-ish figures on the LCD.

## Current state (branch `filter`)

Work in progress on a state-variable filter (`src/filter.{h,cxx}`, per-voice), now driven by the
`vcf_freq` / `vcf_reso` fields of `Patch`, offset by CC 74 and CC 71. All four presets are set to
the values the code used to hard-code, so they sound as they did — they have not been tuned by ear,
and that is the obvious next thing to do. There is still no filter envelope.

The two filter controllers follow the MIDI sound-controller convention of offsetting the patch
around a centre of 64 rather than replacing it (`cc_offset` in `engine.cxx`), which is why
`Channel()` has to centre them at construction — left at zero they would close every filter to MIDI
note 0. One consequence is that a parameter's full range is only reachable when the patch's own
value is 64, which is why both `vcf_freq` and `vcf_reso` are 64 in every preset — at `vcf_reso` =
21 a full CC 71 would only reach Q ≈ 16. That ties the presets' default resonance to the middle of
`q_table`, which is Q ≈ 8 because the table is geometric; if a milder default is wanted without
giving up the range, it is the table's curve that needs changing, not the preset.

Unlike the rest of the per-voice parameters, the filter is configured in `SynthEngine::update`
rather than latched in `Voice::note_on`, so that moving a controller affects notes already
sounding. That makes the coefficients step once per buffer (172 Hz at the default settings), which
is normal for block-based synthesis but can zipper on a fast sweep at high resonance.

None of the recent filter work has been verified on hardware; it is backed by compilation,
disassembly, symbol placement and numerical simulation only.

`fmul_f(high, f)` can in principle overflow `int32_t` at high `q`, and `clamp16` would then saturate
the wrapped value into a full-scale sign flip. The `SVF_Q_MAX` cap puts this out of reach — worst
case across the whole of `q_table` at the worst-case cutoff is `|high|` = 91269 against a limit of
131071 — but the margin is only 1.44×, so anything that widens the `q` range or the input scaling
needs re-checking. A real fix means clamping `high` before the multiply or using a 64-bit
intermediate, which is expensive on M0+.

`README.md` advertises 128 voices; `VoicePool::nv` is presently 32. Trust the code.
