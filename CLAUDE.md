# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A polyphonic wavetable synthesizer firmware for the Raspberry Pi Pico 2 (RP2350, overclocked to 250 MHz),
16-channel multi-timbral, 16-bit stereo I2S out at 44.1 kHz, driven by USB MIDI and serial MIDI.
GPLv3. There is no test suite — verification is by flashing hardware and listening.

## Build

Requires only the Pico SDK toolchain. NodeJS used to be needed to generate the lookup tables at
build time; they are computed at startup now.
Three external repos are expected under `${PICO_HOME}` (default `/opt/pico`):

```
setenv PICO_HOME       "/opt/pico"
setenv PICO_SDK_PATH   "${PICO_HOME}/pico-sdk"
setenv PICO_EXTRAS_PATH "${PICO_HOME}/pico-extras"
# also needs ${PICO_HOME}/pimoroni-pico when CONFIG_LCD_ACTIVE=1
```

```
cmake -B build -DCMAKE_BUILD_TYPE=Release   # once
make -C build -j8                           # picks up CONFIG_* edits by itself
```

Artifacts land in `build/` (`PicoSynth.uf2` to flash, plus `.elf`/`.dis`/`.elf.map` for inspecting
code size and disassembly — useful when tuning the audio inner loop).

The user's shell is tcsh; use `setenv`, not `export`, in any suggested shell snippets.

The target device is `CONFIG_PICO_PLATFORM` / `CONFIG_PICO_BOARD` at the top of `CMakeLists.txt`,
presently `rp2350` / `pico2`, and pushed into `PICO_PLATFORM` / `PICO_BOARD` before the SDK import
because that is what settles the compiler flags. A build directory bakes the platform into its
cache and cannot be retargeted in place, so changing those means deleting `build/` and configuring
it again — `make` alone will not notice, and neither will `cmake -B build` over the old tree.

**Those knobs no longer reach back to RP2040, and setting them to it will not build.** The audio
path now depends on things ARMv6-M does not have: the FPU and `vcvtr` in `SVF::apply`, `vmrs`/`vmsr`
to set flush-to-zero, `ssat` for the output clamp, and `umull` in `frequency_modulate` — where the
old hand-rolled 16-bit decomposition was not merely slower but *wrong*, overflowing on any upward
modulation. Going back would mean reinstating a fixed-point filter, and the measurements below say
that costs about 22 points of the audio deadline at 64 voices. The knobs are kept because the SDK
wants them, not because RP2040 is still a supported target.

## Flashing and printf debugging

The SDK fetches picotool into the tree at `build/_deps/picotool/picotool` (it is not on `PATH`);
otherwise hold BOOTSEL and copy `build/PicoSynth.uf2` to the mounted volume.

USB is dedicated to MIDI, so `pico_enable_stdio_usb` is **0** and `pico_enable_stdio_uart` is **1** —
`printf`/`stdio` goes out UART0 (pins 0/1), which is a different UART from the serial MIDI input on
UART1 (pins 4/5). Note that stdio is blocking: a `printf` from core 1 inside `audio_loop` will miss
the buffer deadline and produce audible glitches. Instrument core 1 by pushing to `bench_queue` and
printing from core 0 instead.

## Lookup tables — computed at startup

Every table is built at boot rather than baked into the image, by `tables_init()` in `src/tables.c`
and `waves_init()` in `src/waves.c`, both called from `main`. They live in `.bss`, so they cost RAM
and nothing in flash.

**No global's constructor may read a table.** `main` calls the initialisers before anything *it*
does reads one, but C++ static constructors run earlier still, and this document used to claim the
tables were up "before anything reads one" — which was never true of them. `Channel` derived its
pan values from `pan_table` in its constructor and, being reached through the global `SynthEngine`,
got a table of zeros; every channel came up with both sides muted and stayed that way, because
nothing recomputes them but an incoming CC 10. The result was silence for any MIDI file that never
sends a pan controller, and correct sound for any that does — which reads as a property of the file.
The fix is the pattern to copy: table-dependent setup goes in an `init()` that `main` calls after
`tables_init()`, never in a constructor. Worth remembering that this class of bug did not exist
while the tables were generated into `.rodata`, so it arrived with the change above and lay latent.

This replaced a pair of NodeJS generators that emitted C at build time. Moving the formulas into C
costs about **22 ms at boot** and 2 KB of code, and buys 28 KB of flash — but the reason to do it
was to retire the NodeJS dependency and the `add_custom_command` machinery that went with it. Two
build subtleties this document used to insist on preserving (the rules' `DEPENDS` on
`CMakeLists.txt`, and listing generated headers as target sources to order generation) simply no
longer exist.

`version.h` is the one thing still generated — see `utils/version.cmake`. `build/generated/` also
holds the SDK's own `pico_base`, written at configure time, so `make` cannot recreate it: delete
`build/generated/tables`, never the parent.

**The tables are deliberately not `const`.** A `const` table goes to `.rodata`, which is in flash
and cannot be written, so it could not be computed at runtime at all. That is the whole mechanism —
no attributes, no linker script.

**Never hand-write an `extern` for a table — include `data.h`.** Those declarations used to be
scattered through the consumers, and nothing checked them against the definitions: `filter.cxx` once
declared `extern uint16_t* svf_table;` against an `int16_t[]`, which linked silently and
dereferenced the table's first four bytes as an address. `tables.c` includes `data.h` itself, so
both a bad definition and a bad consumer declaration are compile errors.

What was lost is the generator's build-time validation, which used to refuse to emit a `q_table`
outside the range the filter is stable over. The curve now pins its endpoints by construction, and
`tables.c` carries `_Static_assert`s for the constants themselves, so the check is compile-time
again — just of the inputs rather than the output.

Accuracy against the generator it replaced, verified on the host: `pan`, `svf`, `q`, `cutoff`,
`square`, `saw` and `tri` come out bit-identical. `power_table` and `scale_table` differ by 1 LSB on
a handful of entries and `note_table` by 6 parts in 38 million — float against the generator's
double, worth 0.05 cents at worst. `sine_table` differs at exactly one entry, index 1024, where
float π is a hair above true π so `sinf` returns −8.7e-8 and `floorf` takes it to −1 instead of 0.

## Compile-time configuration

All knobs live at the top of `CMakeLists.txt` and reach the code as
`target_compile_definitions`:

- `CONFIG_SAMPLE_RATE` / `CONFIG_WAVE_SHIFT` / `CONFIG_BUFFER_SIZE` — reach the code as compile
  definitions, which `src/settings.h` derives `WAVE_LEN`, `WAVE_MAX` and the table sizes from
- `CONFIG_LCD_ACTIVE` — pulls in the Pimoroni Pico Display 2 stack for the benchmark readout
- `CONFIG_AUDIO_USB` — 0 selects the I2S backend (`src/audio_i2s.c`, the default), 1 the USB audio
  one (`src/audio_usb.c`). Both present the three calls in `src/audio.h` and nothing else; only one
  is compiled in
- `CONFIG_HW_PIMORONI_AUDIO` / `CONFIG_HW_PICOADK` — select I2S pin assignments (and, for PicoADK,
  the GPIO 25 DAC soft-unmute in `audio.c`)

## Architecture

**Dual-core split.** Core 0 (`main`) runs the LED blink and the LCD/benchmark readout, and services
TinyUSB. Core 1 runs `audio_loop()` — the entire synthesis path. The two communicate only through
two SDK `queue_t`s: `midi_queue` (4-byte USB-MIDI-style packets, core 0 → core 1) and `bench_queue`
(timing samples, core 1 → core 0). Keep this boundary: anything touching `SynthEngine` state must
happen on core 1, and anything blocking must stay off it.

**`tud_task()` runs from a timer, not from the main loop** — `usb_pump_cb` in `main.cxx`, every
250 µs, at IRQ priority 0xc0 so `USBCTRL_IRQ` (0x80) can still preempt it to post the completions it
is draining. It re-arms from the *end* of the callback, so a long `tud_task()` cannot re-enter
itself. This is not tidiness: `benchmark_task()` blocks for about **62 ms** in `lcd->update()`,
which ends in `dma_channel_wait_for_finish_blocking()` pushing 150 KB over SPI, and the USB host
polls the isochronous audio endpoint every 1 ms. With both in the same loop the endpoint went
unre-armed through every display refresh and only 550 of each 1000 frames were served — audibly.
The display now blocks as long as it likes without touching USB, and serial and USB MIDI got more
responsive for the same reason. Nothing on that path needs thread context.

Note the measuring trap that hid this for a while: core 0 turns the main loop over **670,000 times
a second**, which says nothing about the worst case. An average loop rate is entirely compatible
with one 62 ms stall buried in it, and only measuring the longest single trip found it.

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
when adding to it. Note also that the filter runs *before* the `if (!dca) continue;` early-out — a
filter that skips buffers comes back with state hundreds of samples stale, which is an audible
click.

**Which tables live in RAM is set by `const`, and nothing else.** A `const` table goes to `.rodata`
and is read in place from flash; drop the `const` and it goes to `.data`, which the runtime copies
into RAM at boot — costing the RAM *and* the same again in flash for the initialiser. There is no
attribute involved, and
why `data.h` has to agree or the definition and declaration conflict. The wave tables have always
been in RAM for exactly this reason, though for a long time it looked like an oversight rather than
a decision.

| in RAM | in flash |
|---|---|
| `sine`/`square`/`saw`/`tri_table`, read per *sample* | `note_table`, `pan_table` |
| `power_table`, `svf_table`, the hot ones | `q_table`, `scale_table`, `cutoff_table` |

**Whether table size costs time is unresolved, and the evidence is mixed.** Shrinking them from
66 KB to 13.7 KB measured `bench_max` 3,400,000 ns → 3,300,000 at 64 voices, which looked like XIP
cache misses turning into hits — about 98 cycles a read across the four reads a voice makes per
buffer. But *then* moving the two hot tables into RAM, which should have eliminated any misses that
remained, changed nothing measurable.

Those two results are compatible if 13.7 KB already fits the cache comfortably, so the shrink
removed the misses and the RAM move had none left to remove. They are equally compatible with the
first 100,000 ns having been measurement wander — `bench_max` is a lifetime maximum from a single
run, and 2.9% is well inside what a max statistic can drift. Distinguishing them needs repeated runs
at each configuration, which has not been done. Do not quote the 98 cycles as a measured constant.

What is safe to say: the reads are infrequent — four per voice per buffer — and no arrangement of
these tables has yet been shown to cost more than about 3% of the audio budget.

**Fixed-point conventions.** Phase is 16.16 within a `WAVE_MAX`-sized space (`WAVE_LEN << 16`);
`note_table` entries are ready-made phase increments for the configured sample rate.
`frequency_modulate()` applies a pitch offset by multiplying the step by a 1:16 fixed-point
2^(x/8192) — so all pitch modulation sources (bend, DCO envelope, LFO) must be reduced to a 14-bit
signed value in that domain before use, `x` spanning one octave either side of unity.

`power_table` only stores the *upper* octave, at `POWER_LEN` entries with the index shifted right
by `POWER_SHIFT`. The lower octave is the very same entries read as 1:16 rather than 1:15 — the
shift left is simply omitted — because `table[x + 8192]` is by definition `65536 · 2^(x/8192)` when
`x` is negative. That is exact, not an approximation, so the fold costs one branch and halves the
table. It was 16384 entries and 32 KB; it is now 4096 and 8 KB, worst error 0.173 cents against
0.052, which is some thirty times below the five cents anyone can actually hear. Unity stays exact
at both ends: `x = 0` gives 65536 and `x = -8192` gives 32768. The branch costs 19 instructions
across its three inlined call sites in `SynthEngine::update`, which run once per voice per buffer —
about 0.08% of a core.
The inline `// N bits` comments in `SynthEngine::update` track accumulator width; preserve and
update them when changing any scaling step, since overflow here is silent and audible.

**The filter alone is single-precision float; everything else stays fixed point.** `SVF::apply`
keeps `low` and `band` in `float`, and the boundary sits at its edges: it reads and writes the same
`int16_t` buffer as before, so `Voice::update`, the DCA chain and the accumulator are untouched.
That was measured, not assumed — see the note further down.

The coefficient tables stay integer and are converted **once per buffer**, in the preamble, where
the compiler folds each into a single `vcvt` and keeps all three live in FP registers across the
loop. Float tables would save those three instructions per buffer and cost twice the flash, so
don't. Their fixed-point scales survive only as the divisors in that conversion: `q` and `scale`
are 1:15, while the cutoff coefficient is 2:14, because the Chamberlin SVF needs
`f = 2·sin(π·Fc/Fs)` and 2.0 will not fit in 1:15. `svf_table` is clamped at `Fs/6`, the stability
limit of this topology, which also caps every entry at 16384 and so keeps the table inside an
`int16_t` at any sample rate.

`svf_table` is indexed in units of 1/`SVF_STEPS` of a semitone, presently 1/16, giving 2048 entries
and 4 KB. It was 1/128 and 32 KB, which bought a quarter of a cent of precision that nothing could
use: only 128 of those entries were ever reachable, because the only thing that indexes the table is
`cutoff_table`, and that has one entry per value of a 7-bit parameter. The sub-semitone resolution
that remains is there for a filter envelope to sweep through — which is also why `set_cutoff` still
takes a fine index rather than a 7-bit one. 1/16 semitone is 3.1 cents worst case, well below
audibility for a cutoff and finer than a sweep moves between buffers anyway, since the coefficients
only change at 172 Hz. Shrinking it left every selectable cutoff within 3.1 cents and the preset
default bit-identical, note 90 being an exact multiple of the step.

`q` holds `1/Q`, not `Q`, so *small* values are the resonant ones. `set_q` takes a 7-bit *index*,
not a value, and reads the pair `q_table[n]` / `scale_table[n]`; that is what guarantees the damping
stays where the filter is stable, since no index escapes the table. `q_table` runs Q = 1..16 on a
curve bent so its *midpoint* lands on `svf_q_mid` — see below for why the midpoint is the
interesting part. The exact stability bound for this update order is `q1 < 2/f − f/2`, bottoming out
at 1.5 where `f` reaches 1.0, comfortably above the table's ceiling of 1.0 (`SVF_Q_MAX`).

Two things in `SVF::apply` are written as inline assembly on purpose. `clamp16` is `ssat`, one
instruction against the four a compare-and-select costs. And the output conversion is `vcvtr`
written longhand, because `lrintf()` **does not inline** — it has to set `errno`, so the compiler
emits a call, and in this loop that is a call per sample. `vcvtr` also rounds to nearest where a C
cast would truncate toward zero on every sample.

The FPU is left in **flush-to-zero** mode on core 1 (`fpu_flush_to_zero` in `main.cxx`, FPSCR bit
24). With silence at its input the filter's state reaches denormals within 20 ms, and denormals are
slower than normal operands. The oscillator never actually goes quiet while a voice exists, so this
is insurance rather than a fix for anything observed — but it costs one write at startup.

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
- `SVF::apply` no longer multiplies in fixed point at all — it is float, and depends on the FPU,
  `vcvtr` and `ssat` besides. See below.

## USB audio output

`CONFIG_AUDIO_USB 1` swaps the I2S backend for a UAC2 **input** device — the host sees the synth as
it would a microphone — alongside the existing MIDI interface, as one composite device. I2S remains
the default. `src/usb_audio_desc.h` hand-assembles a two-channel descriptor, because TinyUSB ships
one-channel and four-channel macros and nothing between.

Four things that each cost real time to find, all of which look like something else:

**A composite device with an IAD must declare `0xEF / 0x02 / 0x01`.** Leave `bDeviceClass` at 0 and
the host never assembles the audio function: the device enumerates perfectly and no audio device
ever appears.

**Every control the descriptor declares will be queried, and an unanswered query stalls.** Same
symptom again — clean enumeration, no audio device. `tud_audio_get_req_entity_cb` has to answer
sample frequency (CUR and RANGE) and clock validity, and the feature unit's mute.

**The isochronous endpoint has to be torn down by hand.** This one is a genuine gap on RP2350.
`TUP_DCD_EDPT_ISO_ALLOC` is defined there, which compiles the `usbd_edpt_close()` out of the audio
driver's own close path, and the only code that ever writes `buffer_control` back to zero is
`hw_endpoint_init()` — which `dcd_edpt_iso_activate()` does not call. So a buffer left armed and
unconsumed keeps its `AVAIL` bit set for good, and the next time the host selects the streaming
alternate setting the driver primes the endpoint again and the dcd panics outright:
`"ep 82 was already available"`. `tud_audio_set_itf_close_EP_cb` in `audio_usb.c` does the EP abort
the driver does not, and is the fix. Checking `usbd_edpt_busy()` instead does **not** work and was
tried: usbd clears its own busy flag in `usbd_edpt_iso_activate()` while the hardware stays armed,
so the software state says idle; and returning false from the pre-load callback aborts
`audiod_tx_done_cb()` *before* it re-arms, stalling the stream for good.

**Core 1 paces off the clock, never off the consumer.** A DAC always consumes, so the I2S backend
can block on it; a USB host cannot be relied on to — with nothing listening there is no consumer at
all. `audio_take()` returns NULL until the next block is due and never blocks, and the ring
resynchronises if the consumer falls behind. `BUFFER_SIZE` 288 at 48 kHz is exactly six USB frames,
so a block is consumed in a whole number of callbacks.

## Hard fault reporting

`isr_hardfault` in `main.cxx` captures the exception frame plus a scan of the stack for return
addresses, stashes it in `.uninitialized_data` (which survives a warm reset), reboots via the
watchdog, and prints it on the LCD before USB comes up — then halts, so the host cannot provoke the
same fault again while it is being read. Resolve the addresses with `arm-none-eabi-addr2line -e
build/PicoSynth.elf -f -C`.

It catches `panic()` and `hard_assert()` as much as bad pointers: both end in a breakpoint, and with
no debugger attached a breakpoint escalates to HardFault — `hfsr` bit 31 set, `cfsr` zero, which is
the signature to recognise. Note that the exception frame's `lr` only points back into `panic`
itself, which is why the stack scan is there; the caller is what identifies the bug.

Keep it. Debugging this board otherwise means inferring from LED states, which took eight rounds of
wrong guesses on the endpoint panic above — several of them confidently wrong, because a fault and a
hang look identical from outside and a beacon placed above USB's interrupt priority cannot tell them
apart. The fault report named the culprit in one reading.

## Current state (branch `filter-float`)

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

Both are one constant each in `src/settings.h`. Neither is a plain sweep, because a plain sweep cannot
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

Letting the state run is also what forced the move to float. In fixed point it needed 64-bit
intermediates on three of the four multiplies, because `band` no longer fitted a 32-bit product.
Float carries the whole thing without that: the state peaks around 2^17.4, against 24 bits of
mantissa and a range that reaches 3.4e38, so neither precision nor overflow is in question.

There is deliberately **no** clamp on the state. Three things bound it: the input is an `int16_t`,
every table entry keeps the filter stable, and a stable filter's state cannot exceed input × peak
gain. Measured over 9 cutoffs × all 128 resonances × square/saw/sine/impulse/DC/noise at full
scale, the state peaks at 178575, and a 60-second run shows no drift.

**Why float, and what it cost to find out.** The fixed-point version ran 34 instructions a sample;
float runs 15. But instruction count is the wrong unit here — `vfma` has roughly three cycles of
latency against one of throughput, and the SVF is a strict serial recurrence, `low` → `high` →
`band` → the next `low`. So it had to be measured on hardware, at 64 voices, on `bench_max`:

| | bench_max | of deadline | cycles/voice-sample | instr | CPI |
|---|---|---|---|---|---|
| fixed point, 66 KB tables | 4,700,000 ns | 81.0% | 71.7 | 51 | 1.41 |
| float, 66 KB tables | 3,400,000 ns | 58.6% | 51.9 | 32 | 1.62 |
| float, 13.7 KB tables | 3,300,000 ns | 56.8% | 50.4 | 32 | 1.57 |

The latency worry was real — CPI got *worse*, 1.41 → 1.62 — but the instruction count fell far
enough to win by 27.7% anyway. The third row is the table shrink, which cost nothing in
instructions and took another 25,000 cycles off the buffer; see the XIP note earlier for why size
rather than read count is what did that. Cumulatively 4,700,000 → 3,300,000, 29.8% faster.

Note also what the table says about everything *outside* the sample loops: they account for roughly
13,100 of the 13,281 cycles a voice costs per buffer, so the DCA chain, the envelope virtuals and
the `set_cutoff`/`set_q` veneers into flash come to about 1% between them. Optimising any of those
is not worth the trouble; the sample loops are the only thing that matters.

Numerically float is also a clear improvement: against a double-precision reference with the same
output clip, SNR went from 89–92 dB to 114–126 dB, bit-exact at low levels, and IEEE rounding makes
the round-to-nearest care that the fixed-point integrators needed automatic.

`VoicePool::nv` is 64, and `README.md` now agrees. It said 128 for a long time because 128 genuinely
ran, *before the filter existed* — the filter is what changed the arithmetic. It is about half the
per-sample work now (15 of the 32 instructions a voice costs per sample), so 128 voices would want
roughly twice the 58.6% of the deadline that 64 currently measures, which does not fit. If the
filter ever became optional per patch, the old number would come back within reach for patches that
switch it off.

Raising `nv` costs almost nothing in RAM — 44 bytes of `.bss` per voice, plus about 90 bytes of heap
per *sounding* voice for its two envelopes and filter — so the limit is CPU, not memory. Every voice
the pool holds costs a full render and filter pass per buffer, at roughly 65 instructions a sample,
so 64 voices all sounding is on the order of 180M instructions a second against 250 MHz. Read the
LCD before trusting a larger number: the third line gives the peak voice count and the second gives
the worst time it was measured under, against a deadline of 5,805,000 ns.
