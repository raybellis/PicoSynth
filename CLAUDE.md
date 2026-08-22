# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A polyphonic wavetable synthesizer firmware for the Raspberry Pi Pico 2 (RP2350, overclocked to 250 MHz),
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

The target device is `CONFIG_PICO_PLATFORM` / `CONFIG_PICO_BOARD` at the top of `CMakeLists.txt`,
presently `rp2350` / `pico2`, and pushed into `PICO_PLATFORM` / `PICO_BOARD` before the SDK import
because that is what settles the compiler flags. A build directory bakes the platform into its
cache and cannot be retargeted in place, so changing those means deleting `build/` and configuring
it again — `make` alone will not notice, and neither will `cmake -B build` over the old tree.

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
  `pan_table`, `power_table`, `svf_table`, `q_table`, `scale_table`, `cutoff_table`), `data.h`
  (declarations for all seven), and `settings.h` (`SAMPLE_RATE`, `BUFFER_SIZE`, `WAVE_SHIFT`,
  `WAVE_LEN`, `WAVE_MAX`, `SVF_LEN`, `SVF_Q_LEN`, `SVF_Q_MAX`)
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

That last rule is not stylistic — it was the cause of a hard boot hang on RP2350, and it cost a long
bisect to find. `audio_task` used to call `take_audio_buffer(ap, true)`, whose blocking path waits
on `__wfe()`. The wake never comes on core 1, because the DMA interrupt that frees audio buffers is
installed on core 0, where `audio_init()` runs. The pool holds three buffers, so the hang only
happens once core 1 arrives while all three are in flight — which is why it looked like "enabling
the filter crashes it": the filter added enough per-voice work to change the timing, not to break
anything. Both calls in `audio_task` are now non-blocking (`take_audio_buffer(ap, false)` in a poll
loop, `queue_try_add` for the bench queue). If you add anything to core 1, check it the same way.

The symptom is worth recognising because it does not look like a core 1 problem: core 0 goes dark
too, USB never enumerates, and the LED never blinks. Core 1 stalling is enough to do that, either
through a lock it holds or, as here, by core 0 waiting on it. Bisecting it needs the LED, since
`printf` takes the stdio lock and a wedged core 1 can hold that too — see `git log` around the
RP2350 port for the beacon technique.

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
overflow, since no index escapes the table. `q_table` runs Q = 1..16 on a curve bent so its
*midpoint* lands on `svf_q_mid` — see below for why the midpoint is the interesting part. The exact
stability bound for this update order is
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

**Hardware used directly:** `interp0` in blend/wave-lookup mode is the wavetable oscillator
(shift 15, mask `wave_shift`, raw add) — `Voice::update` loads the step/base/accum and pops samples;
`hw_divider` for bend scaling; SysTick as a cycle counter via `bench.h` (`bench_delta` handles the
24-bit wrap).

The LCD shows three lines: min and max **nanoseconds**, then the voice count as `now/peak`.
`SynthEngine::update` returns how many voices it rendered — the pool iterator only visits voices in
use, and each costs a full render and filter pass whether or not its DCA has anything left — so the
third line is what the first two should be read against. Note that formatting it used to go through
`std::stringstream`, which cost **264 KB** of flash for one hex number; `std::to_string` does the
job, so don't reintroduce `<sstream>` here.

The timing figures: `bench_delta` counts `clk_sys` cycles and
`audio_task` multiplies by 4, one cycle being 4 ns at 250 MHz. The deadline is 5,805,000, one
256-sample buffer at 44.1 kHz. Three things to know before trusting the number: it spans only the
`memset` and `SynthEngine::update`, not the output copy or the audio buffer calls; `bench_min` and
`bench_max` are lifetime extremes that nothing resets, so a slow first buffer pegs the maximum
forever; and the `4 *` assumes the 250 MHz overclock actually took, which
`set_sys_clock_khz(250000, false)` does not guarantee — at 150 MHz a cycle is 6.67 ns and the
figure under-reports by 1.67×.

That last one is now checked rather than assumed: `main` tests the return and lights the Display
Pack's **red LED** if the clock could not be set, so the bench figures are only to be trusted while
it is dark. That LED is GPIO 6/7/8 (`RGB_R`/`RGB_G`/`RGB_B`), wired common anode — low lights it,
high turns it off — and `rgb_init()` drives all three off at startup because they float at reset.
Plain GPIO rather than Pimoroni's `RGBLED`, which would claim PWM slices for an indicator. Note
these are GPIO numbers, not header pin numbers; the pins carrying the I2S signals are 9/10/11.

**The overclock is confirmed working on RP2350** — the LED stays dark on a Pico 2 at
`VREG_VOLTAGE_1_30`, so the part really is at 250 MHz and the full CPU budget is available. Worth
recording because the datasheet only rates RP2350 to 150 MHz, and because a silent fallback was one
of the early suspects when the port would not boot. It wasn't that.

All three survive the move to RP2350: the interpolators exist on both, and `hardware_divider` is
real silicon on RP2040 but a software emulation (`divider.c`, selected by the `else()` in its
`CMakeLists.txt`) on RP2350, with the same API — so nothing had to change. The one place the two
parts genuinely differ is the multiplier: ARMv6-M has only `MULS`, 32×32→32, while the M33 has a
single-cycle `UMULL`/`SMULL`. Three places now depend on the wide one, and none of them would be
safe to move back to RP2040 unchanged:

- `frequency_modulate` was a hand-rolled 16-bit decomposition, and it **overflowed** — `lsb * mul`
  reaches 8589017100 against a `uint32_t` ceiling, costing exactly 65536 off the phase increment
  whenever modulation went *upward*. One `umull` replaces it. See the commit for the measured
  damage; it was thousands of cents on low notes.
- the DCA chain in `SynthEngine::update` carries all 43 bits instead of shifting twice partway
  through to stay in range, which recovers 11 bits and retires a multiply that finished within 1%
  of overflowing `uint32_t`.
- three of the four multiplies in `SVF::apply`, which is what lets the integrators run at their
  true width instead of being clamped to `int16_t` — see below.

## Current state (branch `filter`)

Work in progress on a state-variable filter (`src/filter.{h,cxx}`, per-voice), now driven by the
`vcf_freq` / `vcf_reso` fields of `Patch`, offset by CC 74 and CC 71, and both curves have now been
tuned by ear on hardware — the presets sit at 64 for both, which lands on Q = 1.28 and a cutoff of
1480 Hz. There is still no filter envelope.

The two filter controllers follow the MIDI sound-controller convention of offsetting the patch
around a centre of 64 rather than replacing it (`cc_offset` in `engine.cxx`), which is why
`Channel()` has to centre them at construction — left at zero they would close every filter to MIDI
note 0.

**Both presets stay at 64 and both curves are tuned around that**, which is the thing to understand
before touching either table. Because the controllers offset rather than replace, the *middle* of a
table is what every patch sounds like before anyone moves a knob — so the midpoint is the number
that gets tuned, and it was tuned by ear:

| | midpoint | reached via | curve |
|---|---|---|---|
| `q_table` | Q = 1.28, ≈2 dB emphasis | `svf_q_mid` | Q = 1..16, power-bent |
| `cutoff_table` | MIDI note 90, 1480 Hz | `svf_note_mid` | note 0..118, power-bent |

Both are one constant each in `data.js`. Neither is a plain sweep, because a plain sweep cannot
have both a sane midpoint and a wide range — the two constraints fight and the midpoint wins, so
each curve is raised to a power that pins its endpoints while placing the middle.

What that costs, in each case, is resolution at the end furthest from the midpoint. For resonance
the bottom half is compressed to nothing, but only because the chosen default sits close to the
Q = 1 floor and there is genuinely nothing between them; the useful travel is upward, 1.28 → 2.8 →
5.9 → 16 across the top half. For cutoff the lowest steps move in ~3-semitone jumps, across
territory that is muffled anyway, in exchange for sub-semitone steps up where sweeping happens.

Two bounds worth keeping if you retune them. `svf_q_min` is 2048 (Q = 16) rather than 512 (Q = 64)
because the extreme top was never wanted and giving it up made the curve far more even. And
`svf_note_max` is 118, not 127, because `svf_table` saturates at `Fs/6` — note 117.8 — so mapping
above that is the same filter setting and would only waste control travel. Exactly one of the 128
cutoff steps is redundantly wide open; a naive offset-based mapping wasted about 35.

Unlike the rest of the per-voice parameters, the filter is configured in `SynthEngine::update`
rather than latched in `Voice::note_on`, so that moving a controller affects notes already
sounding. That makes the coefficients step once per buffer (172 Hz at the default settings), which
is normal for block-based synthesis but can zipper on a fast sweep at high resonance.

The firmware runs on RP2350 hardware and the filter work has been heard. The numbers behind it —
the response curves, the Q range, the passband/peak split, the overflow margin — are still from
simulation against the generated tables rather than from measuring the board's output, so treat
them as designed-for rather than measured-on-hardware values.

**Only the filter's output is clamped, never its state.** This matters more than it sounds. The
lowpass peak legitimately reaches `sqrt(Q)` times the input, so with a full-scale oscillator and
Q=64 the integrators need to swing to about 2^18. Clamping them at `int16_t`, as this used to,
saturates *inside* the feedback loop — a nonlinearity, not a clip — and it destroys the response at
any useful resonance. Measured against the ideal filter with the same output clip, a full-scale
input gave 9 dB SNR at Q=8 and 6.9 dB at Q=64; letting the state run and clamping only `buf[i]`
gives 86 dB and 81 dB. Below about Q=4, or at low input levels, the two are identical, so the old
scheme looked fine right up until resonance was actually used.

That is what the wide multiplies buy: `band` no longer fits the 32-bit products, so `fmul_su_wide`
and `fmul_f_wide` take 64-bit intermediates. Only `fmul_su(input, scale)` stays narrow, because its
operand really is an `int16_t`. Three `smlal` and one `mul.w`, 61 instructions against the old 52 —
about 5% of a core at 32 voices, and it also retires the `fmul_f(high, f)` overflow that used to be
documented here as a known defect.

There is deliberately **no** clamp on the state. Three things bound it: the input is an `int16_t`,
every table entry keeps the filter stable, and a stable filter's state cannot exceed input × peak
gain. Measured over 9 cutoffs × all 128 resonances × square/saw/sine/impulse/DC/noise at full
scale, the state peaks at 178575 — 2^17.4, or 12000× inside `int32_t` — and a 60-second run shows no
drift, with silence settling to exactly zero and DC settling to the filter's DC gain. A clamp cost
19 instructions a sample and could only ever fire if one of those three premises broke.

`README.md` advertises 128 voices; `VoicePool::nv` is presently 32. Trust the code.
