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

**The compiler is the authority on warnings, not the language server.** The build runs `-Wall
-Werror` and is clean. A language server in this tree is not, unless configured: clangd parses with
the *host* toolchain by default, cannot then find `<cstring>`, and everything downstream of that
failure is noise that looks exactly like a real defect — `SVF` reported abstract, `size_t` unknown,
hardware headers missing, and includes reported unused that are demonstrably used (`dsp.h` provides
`clamp16`, `pico.h` provides `__not_in_flash_func`). Do not act on any of that without checking it
against a build.

`utils/clangd_config.py` writes a `.clangd` that fixes it, by asking the compiler for its own
implicit include directories — which are implicit precisely because they never appear in
`compile_commands.json`. `.clangd` is **gitignored**: it names one toolchain at one version, so it
is only right for the machine that generated it. Run the script after installing or upgrading
arm-none-eabi-gcc.

Three separate blocks, and each exists because a simpler arrangement broke a different case:

| block | why not merged |
|---|---|
| `.c` | naming the C++ driver globally makes every C file fail `-std=gnu11 not allowed with 'C++'` |
| `.cxx` | needs `-std=gnu++17` stated, since CMake omits `-std` when GCC's default already matches and clangd's default is not GCC's |
| `.h` | forced to C++; left to itself clangd takes a lone `.h` for C, and libstdc++ then rejects the `<cstdint>` in `dsp.h` and `midi.h`. Naming a standard without forcing the language gets `-std=gnu++17 not allowed with 'C'` on `data.h` |

Verify with `clangd --check=<file>`, but filter it: real diagnostics are the lines matching
`^E\[...\] \[diag_name\] `. The `tweak: ExtractFunction ==> FAIL` lines are refactoring-availability
probes, and "Failed to convert location" is internal chatter — neither is a diagnostic, and the
`All checks completed, N errors` summary counts both. Prove the filter still catches something by
injecting a deliberate error before trusting a clean sweep.

`audio_usb.c` is absent from `compile_commands.json` whenever `CONFIG_AUDIO_USB` is 0, so it reports
clean only because clangd's fallback happens to work. Switch the knob before believing it.

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
7-bit parameters; the 128 presets in `src/presets.c` are hard-coded and program change selects
`presets[program % NPRESETS]` — in **two** places, the engine and `Channel::lfo_tick`, so both move
together.

**`presets` is `const`, and that is not cosmetic.** 128 patches at 41 bytes is 5 KB, and a
non-`const` array lands in `.data`, which costs that much RAM *and* the same again in flash for the
initialiser. It belongs in `.rodata`; `Voice::patch` is a `const Patch*` to keep it there. This is
the same rule as the lookup tables, pointing the other way — see the table above.

**Zero is not neutral for several `Patch` fields, and that is the most reliable way to break a new
preset.** `Patch` is a POD filled with designated initialisers, so anything omitted is zero — and
for these, zero is not "off":

| field | zero means | neutral is |
|---|---|---|
| `Osc::level` | that oscillator silent (both, plus no sub: no sound at all) | whatever the patch wants |
| `dca_env_level` | the voice silent | whatever the patch wants |

Both of those are *levels*, where zero-means-silent is unavoidable rather than a design slip. Every
other field is signed or zero-neutral, deliberately: `Osc::coarse` and `Osc::fine`, `dcf_vel`,
`dcf_press`, `dcf_track`, `dcf_env_level`, `dco_env_level`, `sub_level`, the four `aux_env_*` rates,
and all of the LFO depths. Prefer that when adding more.

`sub_octaves` is the one field where zero means something other than zero: it reads as 1, because a
sub at the same pitch as its parent is not a sub. It is only consulted when `sub_level` is non-zero,
so a patch with no sub never reaches it.

**The centred-on-64 form is only for values arriving over the wire**, where the MIDI convention
demands it — `dcf_freq` and `dcf_reso`, which `cc_offset` shifts by CC 74 and CC 71. `dcf_env_level`
used to follow it too, out of symmetry with those two, and that was a mistake: no controller touches
it, so all the convention bought was a preset that shut the filter if it forgot the field. It is
signed now, and reads better for it — the preset that wants 32 semitones says 32 rather than 96.

**Two controllers must be defaulted or they silence the synth**, and both are defaulted in
`Channel::init()`. `control[]` is zero-initialised; `volume` and `expression` both multiply the DCA
chain, so either left at zero mutes the channel outright, and `pan` left at zero mutes both sides
through `pan_table`. This is not hypothetical — pan was exactly this bug, and it hid for a long time
because most MIDI files send CC 10 and mask it. Any new controller that multiplies the chain needs
the same treatment.

**Sustain (CC 64) defers the release rather than suppressing it.** A note-off arriving with the
pedal down marks the voice `sustained` and leaves it sounding; `SynthEngine::sustain_off()` settles
the debt when the pedal lifts. The channel holds the pedal state but only the engine can see the
voices it is holding, so `SynthEngine::midi_in` intercepts the controller after forwarding it. On a
pedal-heavy piece the peak voice count went from 11 to 23. Two limits: a sustained voice is not
stealable, since `steal` is only set by a real note-off, so a heavily pedalled passage can exhaust
the pool and drop notes rather than take the oldest; and a file that sends pedal-down without ever
lifting it leaves those voices ringing, there being no CC 123 all-notes-off yet.

**Expression (CC 11) is the other half of MIDI volume.** CC 7 is where a part sits in the mix, CC 11
is how it is played, and a score's dynamics live in the second one. It multiplies the DCA chain
alongside volume.

**CC 123 releases every voice on a channel and CC 120 cuts them dead**, both ignoring the sustain
pedal — which is the point of them, since a file that sends a pedal down and then stops has nothing
left to lift it. Releasing mid-iteration is safe: the pool iterator only visits voices in use, and
`release()` marks them free behind it.

**There are two LFOs, and they are kept separate because they sound different.** Per voice, the
phase is whatever that voice was last doing, so notes wobble independently and the result reads as
an ensemble or a chorus; per channel, all its notes share one phase and it reads as vibrato.
`lfo_global` picks. The channel form is also the cheaper of the two — sixteen adds a buffer in
`SynthEngine::update`, against one per *voice*.

**It is amount-and-routing, not a depth per destination.** `lfo_amount` says how much LFO is
running, summed from the patch's own base plus what the mod wheel adds through `lfo_wheel` and what
aftertouch adds through `lfo_press`, capped at 127. That scales the oscillator *itself*, and only
then do `lfo_pitch`, `lfo_dcf` and `lfo_dca` route it to vibrato, the cutoff and a tremolo.

The ordering is the whole point. Depths used to be per-destination with the wheel added to the
*pitch* one, which meant a controller could only ever reach the vibrato — a filter sweep or a
tremolo sat at whatever the patch said with no way to bring it in. Scaling the source instead lets
the wheel reach all three, which is what the pads do: their sweep opens under the wheel and was
previously inexpressible. `lfo_delay` already worked this way, fading the LFO itself rather than
each depth so that every destination arrives together, and the amount now joins it there.

Two consequences worth knowing. `lfo_amount` of zero means no LFO at all regardless of routing, so
a patch that wants movement must set it — the migration gives it 127 where nothing else says
otherwise. And because the amount and the routing multiply, the same vibrato can be written many
ways; prefer a full routing depth with the amount carrying the expression, so the wheel has
somewhere to travel.

**The tremolo ducks from unity rather than modulating around it**, so the gain can never exceed 1
and nothing downstream has to keep room for it. It also has to be applied to `level_l`/`level_r`
rather than folded into the DCA chain: that chain is already 50 bits, and one more 15-bit stage
would put it past what a `uint64_t` holds.

**The cutoff has five sources and is clamped once, after summing all of them** — the patch value
through `cutoff_table`, key tracking, velocity, aftertouch, the LFO, and the envelope. The clamp
used to sit inside the envelope branch, which was safe only while the envelope was the sole thing
moving it; any of the others could drive it negative, and `set_cutoff` takes a `uint16_t`, so it
would wrap to wide open rather than closing. Key tracking is measured from note 60, so `dcf_freq`
stays the cutoff at middle C whatever it is set to, and 127 means the cutoff follows the keyboard
one for one.

**Voice allocation** belongs entirely to `VoicePool`: first-free, then steal-any-released (`steal` is
set on note-off, so a voice in its release tail can be taken). Both paths return through the private
`claim()`, which is the invariant that matters — an earlier version returned a stolen voice still
flagged `free`, so the note was silently dropped and its envelopes and filter leaked. `release()`
deletes them and re-`init()`s. Iterating a pool visits only voices in use, so engine code never
tests `free` itself; releasing mid-iteration is safe. Note `new`/`delete` happen on core 1 per
note-on/off — a known wart in a real-time path.

**Audio path per buffer.** `SynthEngine::update` ticks all envelopes and reaps voices whose DCA
envelope went inactive, advances every channel's LFO, and then calls `Voice::render` per voice.
**The engine manages voices and MIDI; it does not synthesise** — everything per-voice is in
`voice.cxx`, which is why that file is more than twice the size of `engine.cxx`. Keep it that way.

`Voice::render` computes the DCA level (chaining 15-bit envelope × patch level × velocity × channel
volume × expression × pan), works out the pitch modulation common to all the oscillators, renders
and mixes them, sets and applies the filter, and accumulates into the stereo `int32_t` buffer.
`main.cxx`'s `audio_task()` then scales the accumulator into the `int16_t` output buffer — see the
output stage below, which is not a plain shift any more.

**Three oscillators per voice, but only two `Osc` structures.** `Patch` holds `dco[NDCO]` with
`NDCO` of 2 — the primary `dco[0]` and the auxiliary `dco[1]`, each with a waveform, a level, and
coarse and fine tuning. The third is a **sub-oscillator**, and it is not an `Osc` at all: it *is*
`dco[0]`, read a whole number of octaves lower, so it has no wave, no coarse and no fine of its own.
`sub_level` sets it and `sub_octaves` says how far down, with 0 reading as 1.

That sharing is the design rather than a simplification. The sub inherits `dco[0]`'s waveform and
any detune, which is what stops a sub beating against its own parent, and it costs one phase
accumulator, one step, one table pointer and one round of pitch modulation between the two of them.
Registers are the resource that has decided every oscillator measurement here — see below.

The tuning reaches the two tunable oscillators by two different routes. Coarse shifts the note
before the table lookup — exact, since a semitone is a table entry, and unbounded within the
keyboard. Fine goes through `frequency_modulate`, which reaches only an octave either way because
that is all `power_table` covers, and so *could not carry coarse* even if it were convenient. Both
are constant for the life of a note, so `Voice::note_on` folds them into `dco_step_base[]` and they
cost nothing per buffer.

Both are also **signed, with zero meaning no offset**, breaking the centred-on-64 convention the
rest of `Patch` follows. Those are 7-bit values arriving over the wire; these are preset-only
fields, and the centred form has a nasty property in a POD full of designated initialisers — the
neutral value is not zero, so an omitted field is a large detune rather than none. See the list of
fields further down that share that hazard.

**Noise is a fourth waveform, and only `dco[1]` may use it.** `NOISE` is not a table: the phase
accumulator is run as an LCG (`p = p * 1664525 + 1013904223`) and the sample is taken from its high
bits, so extracting one costs the same shift a table index would. Restricting it to the auxiliary
keeps the generator off the primary pitch path — the primary carries the sub, which needs a real
phase — and turns "which oscillator is noise" into a single test rather than a case analysis. Noise
advances by a different rule from the other two, so it cannot share a loop with them.

This is the one thing subtractive synthesis needed and did not have. Breath, fret noise, seashore,
applause and gunshot *are* noise, and a drum map on channel 10 is now reachable in principle, though
none exists yet.

**No interpolator, and that was measured rather than assumed.** `interp0` and `interp1` in
blend/wave-lookup mode used to be the oscillators. They are gone, `hardware_interp` is off the link
line, and `Voice::oscillators` indexes the tables directly. Three findings, in the order they
arrived:

- **Saw needs no table.** `saw_table[i]` is `32767 - i*32` and nothing else, so a saw is an exact
  function of the phase. That saves the read and — the part that actually mattered — the *pointer*.
- **The interpolator's one advantage is holding phase and step off the register file**, and that is
  worth less than not needing a table pointer at all. It also capped at two units, which forced a
  second pass over the buffer for a third oscillator.
- **Register pressure, not instruction count, governs this loop.** Two of the measurements below go
  the opposite way to their instruction counts.

`Voice::oscillators` is consequently a `MIX` macro instantiated **twelve** times: `dco[0]` computed
or read (2) × sub present or not (2) × an auxiliary that is a table, a computed saw, or noise (3).
Each variant is a single pass with everything resolved at compile time; the shape is chosen once per
buffer. It is about 100 lines against the 209 the three experimental paths took.

Measured at 64 voices against the 6,000,000 ns deadline, on the two patches that bracket the work —
an all-saw test and a three-oscillator organ:

| | saw test | organ test |
|---|---|---|
| two interpolators, one pass per pair | 5.03M | 5.03M |
| direct indexing, table pointers not deduplicated | 5.19M | — |
| direct indexing, deduplicated | 4.99M | — |
| **direct indexing + computed saw** | **4.57M** | **5.02M** |

So the interpolator cost 9.2% on saw-heavy material and nothing on an organ, which is the expected
shape: the organ is all sines and gains only the deduplication. Note the third row — deduplicating
the pointers when a patch's oscillators share a table took 4% off, which is why the second row is
*worse* than the interpolator it replaced and the fourth is better.

One oscillator at full level comes out at full scale to within 0.07 dB, and three at full level
reach three times that and saturate. That is the conventional arrangement — levels are independent,
and a patch wanting all three loud turns them down.

**`saw_table`'s contents are now dead.** A saw is computed for either oscillator, so nothing
dereferences it. It is still built because `waves[]` is indexed by wave number and the entry has to
be a valid pointer — 4 KB of RAM reclaimable by pointing `waves[SAW]` at another table, which has
not been done.

**`dco[1]` has an amplitude envelope of its own**, the fourth per-voice ADSR, set by the four
`aux_env_*` rates and applied in `Voice::oscillators` as `l1 = (l1 * aux_env->level()) >> 15`.
Unlike the other three there is no depth field to test, so **all four rates zero is the sentinel for
"follow the DCA"** — safe because it otherwise describes the slowest possible attack into silence,
which no patch wants. The envelope is only allocated when one of them is set, so `v.aux_env` being
null is the normal case and every use is guarded, exactly as `dcf_env` is.

Note it cannot end a note the way the DCA envelope can: it silences one oscillator and the voice
plays on. Nothing checks `aux_env->active()`.

**This is the one modulation the filter cannot stand in for**, which is the whole reason it exists.
A downward filter envelope darkens a partial as a note decays, and that covers the ordinary case.
It cannot duck *noise*, which is broadband — a lowpass takes the tone with it in the same
proportion — and it cannot fade an oscillator tuned *below* the primary at all. 20 of the 128
presets use one: the pipes, whose breath is a chiff at the onset; the electric pianos and
struck bars, whose partial is a tine or a mallet click; and Synth Drum and Gunshot, where a noise
crack sits over a pitched body that outlasts it.

**Zero sustain is usually what a transient wants, and getting that wrong is audible.** The pipes
first shipped with sustains of 35–55%, which is not a chiff — it is a hiss under the whole note, on
a family whose DCA sustains at 112. At `s = 0` the decay ends in the `off` phase and the noise is
genuinely gone. Remember `d` is a *rate*: a larger number is a shorter transient.

The cost is close to nothing, and that is why it was worth adding. Every level here is already
block-rate — `dca_env->level()` is read once per buffer and `l1` is a constant across the sample
loop — so this is one `update()` and one multiply per voice per buffer, both outside the inner
loops, where all non-sample-loop work has been measured at about 1% of the voice budget. Plus about
24 bytes of heap per sounding voice that has one. The limitation that comes with block rate is that
a transient shorter than about 20 ms steps in 6 ms increments and could zipper; that is a property
the DCA envelope has always had, but noise transients are where it would first be heard.

**The output stage scales for how voices actually sum, and limits what is left.** It used to shift
down by 6, which is what 64 voices at full level need *in phase*. They are never in phase: voices
sit at unrelated phases and sum in power, so 64 of them reach about 8 times one voice, not 64 times.
That reserved 18 dB nothing ever used, and it measured — a 16-channel piece with the voice count
pinned at 64 peaked at 2100 of 32767, which is −24 dB, while a six-voice piano piece was inaudible
without winding the monitors up. `OUTPUT_SHIFT` is 3 now, `sqrt(64)`, and it is a named constant
because its value is a claim about how voices sum.

The rare genuinely coherent peak goes to a stereo-linked peak limiter. Attack is instantaneous, so
the gain exactly meets the threshold and the output cannot exceed it; release is a slow exponential
to unity over 150 ms, which is what stops it pumping. One gain covers both channels — limiting them
separately would pull the image toward whichever side was quieter every time it engaged. Verified on
the host: a 4× overload gives exactly 0.25 gain, recovering 0.46 / 0.80 / 0.99 at 50 / 200 / 600 ms,
and the worst case for ripple, a loud 80 Hz sine, modulates the gain by 0.32 dB.

**It is deliberately not a voice-count normaliser**, and that is worth stating because it is the
obvious thing to reach for and it is wrong. Dividing by the number of sounding voices makes adding a
note quieten every note already playing: audible pumping, and backwards musically, since a chord
should be louder than a single note. Dividing by `sqrt(voices)` is the same mistake more quietly.
The gain here moves only when the output would otherwise clip. Velocity is left alone for the same
reason — a passage marked quiet should sound quiet.

Every function in that path — `SynthEngine::update`, `Voice::render`, `Voice::oscillators`,
`SVF::apply` — is marked
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

**That reduction is where a modulation source quietly loses its range**, so check the arithmetic
rather than the comment beside it. The DCO envelope shifted by 10, which left a full-depth pitch
envelope reaching 4063 of the 8192 that is an octave — half of what was intended — while the
comments alongside claimed 16, 24 and 14 bits for values that were actually 15, 22 and 12. It is
`>> 9` now: `32767 * 127 >> 9` is 8127, an octave to within 0.8%, and still inside the signed 14-bit
domain. Nothing in the tree exercises this path, every preset having `dco_env_level` of 0, so it was
verified by temporarily giving a preset a full-depth envelope gliding down over a second and
checking the interval against the octave below.

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
`int16_t` buffer as before, so `Voice::oscillators`, the DCA chain and the accumulator are untouched.
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
that remains is what the filter envelope sweeps through — which is also why `set_cutoff` takes a
fine index rather than a 7-bit one. 1/16 semitone is 3.1 cents worst case, well below
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

**Hardware used directly:** `hw_divider` for bend scaling, and SysTick as a cycle counter via
`bench.h` (`bench_delta` handles the 24-bit wrap). The interpolators were the third item here and
are no longer used at all — see the oscillator section above for the measurements that retired them,
and note that `hardware_interp` came off the `target_link_libraries` list with them.

The LCD shows three lines: min and max **nanoseconds**, then the voice count as `now/peak`.
`SynthEngine::update` returns how many voices it rendered — the pool iterator only visits voices in
use, and each costs a full render and filter pass whether or not its DCA has anything left — so the
third line is what the first two should be read against. Note that formatting it used to go through
`std::stringstream`, which cost **264 KB** of flash for one hex number; `std::to_string` does the
job, so don't reintroduce `<sstream>` here.

The timing figures: `bench_delta` counts `clk_sys` cycles and
`audio_task` multiplies by 4, one cycle being 4 ns at 250 MHz. The deadline is **6,000,000 ns**, one
288-sample buffer at 48 kHz — it follows `CONFIG_BUFFER_SIZE` and `CONFIG_SAMPLE_RATE`, so it moves
when either does, and the measurements further down were taken at the older 256 / 44.1 kHz where it
was 5,805,000. Three things to know before trusting the number: it spans only the
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

Both survive the move to RP2350: `hardware_divider` is real silicon on RP2040 but a software
emulation (`divider.c`, selected by the `else()` in its `CMakeLists.txt`) on RP2350, with the same
API — so nothing had to change. The one place the two
parts genuinely differ is the multiplier: ARMv6-M has only `MULS`, 32×32→32, while the M33 has a
single-cycle `UMULL`/`SMULL`. Three places now depend on the wide one, and none of them would be
safe to move back to RP2040 unchanged:

- `frequency_modulate` was a hand-rolled 16-bit decomposition, and it **overflowed** — `lsb * mul`
  reaches 8589017100 against a `uint32_t` ceiling, costing exactly 65536 off the phase increment
  whenever modulation went *upward*. One `umull` replaces it. See the commit for the measured
  damage; it was thousands of cents on low notes.
- the DCA chain in `SynthEngine::update` carries all 50 bits instead of shifting twice partway
  through to stay in range, which recovers 11 bits and retires a multiply that finished within 1%
  of overflowing `uint32_t`. It was 43 before expression joined it; the peak is 63,013, which still
  lands inside the `uint16_t` it is assigned to, and 64 voices still accumulate to 2.0M against
  `int32`'s 2.1 billion.
- `SVF::apply` no longer multiplies in fixed point at all — it is float, and depends on the FPU,
  `vcvtr` and `ssat` besides. See below.

## The patch set

`src/presets.c` is a rough General MIDI set, **generated** by `utils/gm_presets.py` and committed.
That script is deliberately *not* part of the build: the lookup-table generators were retired so
that nothing but the Pico SDK is needed to compile, and this keeps to that — run it by hand and
commit what it emits. It earns its place by letting a patch be written musically, cutoff as a MIDI
note and LFO rate in Hz, and converting on the way out; edit the script rather than the C when a
whole family needs moving.

**How close any of it gets varies by family, and the limit is the instrument rather than the
tuning.** Organs, strings, ensembles, brass, basses, reeds, leads and pads are what subtractive
synthesis is for and come out well. Pianos, guitars and chromatic percussion are recognisable but
plainly synthetic.

**Sixteen patches use the noise generator**, which is what the last family had been waiting for.
The pipes (73–80) carry it as breath, and the effects (121–128) as most of the sound: Guitar Fret
Noise, Breath Noise, Seashore and Applause are noise plus a low-level pitched primary, kept only
because `arrange()` requires one. Gunshot is a −24-semitone thump under noise, Helicopter a −36
square rotor under it. Bird Tweet and Telephone Ring stay tonal, being pitched gestures rather than
noise. In percussion, Synth Drum gets a crack over the body and Reverse Cymbal is near-pure noise
through the upward filter sweep it already had.

The levels were halved once by ear after the first attempt, and note *how*: the generator's
`P()` renormalises the oscillators that survive the two-oscillator truncation, so halving the source
number pushes it straight back up. The values in the script are solved so the *emitted* level lands
where it should, not simply halved.

`arrange()` enforces the invariants the engine assumes: at most one noise oscillator per patch, it
goes in the auxiliary slot, and a pitched primary has to remain. That last one is why Seashore and
Applause carry a token primary at level 8–10 rather than none.

**A drum map on channel 10 is now reachable and does not exist.** It was impossible before — a
snare or a hi-hat is not approachable with tuned oscillators — and is the obvious next thing.

**Two thirds of the patches use two oscillators, not three**, and that is a budget decision as much
as a musical one: one oscillator costs 3.92M of the 6M deadline at 64 voices and three cost about
5.0M, so a flute that is nearly a sine should not pay for a stack it never uses. Dropping one
rebalances the levels that remain, so nothing gets quieter. Three are kept where the detune *is* the
sound — organs, strings, ensembles, brass, pads, effects. Since the third oscillator became a sub
rather than a free `Osc`, a patch that wants a third *independent* pitch has to spend `dco[1]` on
it: the organ fifths do exactly that.

**Strings, brass, reeds, pipes and leads use the channel LFO phase; ensembles deliberately do not.**
One player has one vibrato, so the notes of a chord should move together. A section genuinely has
independent vibrato, and that is what makes it read as a section rather than as one large
instrument.

Worth knowing before retuning: the patches are systematic rather than heard. They come from a family
base plus per-instrument tweaks, and were corrected twice by measurement — once when the mod wheel
turned out to be reaching four semitones where half a semitone is normal, and once when the LFO to
cutoff was sweeping three and a half octaves. They have not been tuned by ear the way the filter
curves were. The two exceptions are the noise levels and the pipes' breath envelopes, both of which
were corrected on hardware by listening.

`auxenv(a, d, s, r)` in the generator is the helper for the fourth envelope, and the comment on it
is the place to record what a second oscillator is *for* in a given patch — a detune or a fifth has
to track the note, a transient must not.

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

## The filter's two curves

The state-variable filter (`src/filter.{h,cxx}`, per-voice) is driven by the `dcf_freq` /
`dcf_reso` fields of `Patch`, offset by CC 74 and CC 71, and both curves were tuned by ear on
hardware around a midpoint of 64 — which lands on Q = 1.28 and a cutoff of 1480 Hz.

**The presets no longer all sit at that midpoint**, so read the curves below as describing the
mapping rather than what any particular preset does.

The prefix is `dcf`, not `vcf`, throughout `Patch` and the engine: there is no voltage anywhere in
this signal path. `svf_*` is a different thing and stays — that names the state-variable topology
itself, not the synth's filter section.

**There is now a filter envelope**, a third per-voice ADSR beside the DCA and DCO ones. Its depth
`dcf_env_level` is signed semitones: positive sweeps the cutoff up, negative down, zero is no
modulation. Depth maps to semitones exactly rather than approximately, because `cutoff_table[i]` is
`note * SVF_STEPS`, so adding `depth * SVF_STEPS` adds that many semitones. This is what the
sub-semitone resolution of `svf_table` was being kept for — the coarse 7-bit path through
`cutoff_table` cannot reach between its 128 steps, and a sweep has to.

The envelope is created only at non-zero depth, so `v.dcf_env` being null is the normal case and
every use is guarded.

`set_cutoff` clamps its top but takes a `uint16_t`, so the engine has to clamp the bottom itself
before the cast: a negative sweep that went through unclamped would wrap to a huge value and land on
"wide open", which is the exact opposite of what was asked for.

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

Those three are all at 256 samples / 44.1 kHz, against the 5,805,000 ns deadline of the time. The
current configuration is 288 / 48 kHz, so both the buffer and the deadline are 12.5% longer and the
percentages are not directly comparable — divide by the sample count to compare like with like:

| | bench_max | of deadline | ns/sample |
|---|---|---|---|
| float, 13.7 KB tables, 256 @ 44.1 kHz | 3,300,000 ns | 56.8% | 12,891 |
| + filter envelope + output limiter, 288 @ 48 kHz | 3,641,612 ns | 60.7% | 12,644 |

Those are all **one** oscillator. The figures in the oscillator section — 4.57M on saw, 5.02M on a
three-oscillator organ — are the same firmware with a full patch, and are the ones to read against
the 6,000,000 ns deadline when judging how much room is left. They predate the aux envelope, which
has not been measured; it is one `update()` and one multiply per voice per buffer, so it should sit
inside the noise, but that is reasoning rather than a measurement.

Per sample that is unchanged inside measurement noise, which is the expected result and worth
knowing before optimising anything: the filter envelope costs one `update()` and a little
arithmetic *per voice per buffer*, and the limiter about 1,700 operations per buffer against the
~590,000 the voice loops cost at 64 voices. Neither is anywhere near the per-sample inner loops,
which is where essentially the whole budget goes. `audio_task` is consequently still left in flash
rather than `__not_in_flash_func`, despite the output stage now doing a float multiply, a `vcvtr`
and an `ssat` per sample.

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

Raising `nv` costs almost nothing in RAM — 44 bytes of `.bss` per voice, plus roughly 90–140 bytes
of heap per *sounding* voice for its filter and however many of its four envelopes the patch
actually asked for — so the limit is CPU, not memory. Every voice
the pool holds costs a full render and filter pass per buffer, at roughly 65 instructions a sample,
so 64 voices all sounding is on the order of 180M instructions a second against 250 MHz. Read the
LCD before trusting a larger number: the third line gives the peak voice count and the second gives
the worst time it was measured under, against a deadline of 6,000,000 ns.
