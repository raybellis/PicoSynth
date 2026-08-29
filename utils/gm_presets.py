#!/usr/bin/env python3
#
# Emits src/presets.c: a rough General MIDI set for this synth.
#
# NOT part of the build.  The lookup-table generators were deliberately
# retired so that nothing but the Pico SDK is needed to compile; this is
# an authoring aid, run by hand, and its output is committed.  Run it
# only when regenerating the patch set:
#
#     python3 utils/gm_presets.py > src/presets.c
#
# Patches are specified musically - cutoff as a MIDI note, LFO rate in
# Hz - and converted here, so that the C says what was meant rather than
# what the tables happen to index.

import math, sys, itertools

Q_LEN, STEPS, BUF, FS = 128, 16, 288, 48000
NOTE_MAX = 69 + 12 * math.log2((FS / 6.0) / 440.0)
N_EXP = math.log(90.0 / NOTE_MAX) / math.log(64.0 / (Q_LEN - 1))

SINE, SQUARE, SAW, TRI, NOISE = 0, 1, 2, 3, 4
WAVE_NAME = {SINE: "SINE", SQUARE: "SQUARE", SAW: "SAW", TRI: "TRI",
             NOISE: "NOISE"}


def cut(note):
    """dcf_freq index that lands on a given MIDI note of cutoff."""
    return max(0, min(127, round(127 * ((note / NOTE_MAX) ** (1 / N_EXP)))))


def lfo(hz):
    """lfo_freq note number for a rate in Hz."""
    return max(0, min(127, round(69 + 12 * math.log2(hz * BUF / 440.0))))


# ---------------------------------------------------------------------
# the instrument table
#
# Each entry is (name, overrides).  Anything not overridden comes from
# the family base, and anything not in that comes out zero - which is
# neutral for every field except the levels, which every base sets.
# ---------------------------------------------------------------------

def auxenv(a, d, s, r):
    """dco[1]'s own amplitude contour, as four ADSR rates.

    All four zero - the default - means the auxiliary follows the DCA,
    which is what most patches want: a detune or a fifth has to track
    the note or the patch comes apart mid-way through it.  Set them
    where the second oscillator is a *transient* rather than part of
    the steady tone: the breath in a pipe, the tine in an electric
    piano, the crack over a drum's body.  Nothing else can do this -
    the filter envelope darkens a partial as a note decays, but noise
    is broadband, so a lowpass ducks it and the tone together.
    """
    return dict(aux_env_a=a, aux_env_d=d, aux_env_s=s, aux_env_r=r)


def osc(wave, level, coarse=0, fine=0):
    return dict(wave=wave, level=level, coarse=coarse, fine=fine)


def arrange(dco):
    """Split three oscillators into (primary, auxiliary, sub).

    The sub has no tuning of its own - it is the primary read an octave
    or more down - so the pair that becomes primary+sub has to share a
    waveform and a detune and sit a whole number of octaves apart.  Any
    of the three can be the auxiliary, so try each assignment rather
    than assuming the order they happen to be written in.
    """
    live = [d for d in dco if d["level"]]

    # noise has no pitch, so it can be neither the primary nor the sub -
    # it is always the auxiliary, which is why only dco[1] may carry it
    noise = [d for d in live if d["wave"] == NOISE]
    if noise:
        assert len(noise) == 1, "only one noise oscillator per patch"
        rest = [d for d in live if d["wave"] != NOISE]
        assert rest, "a noise patch still needs a pitched primary"
        if len(rest) == 1:
            return rest[0], noise[0], None, 0
        drop = rest[0]["coarse"] - rest[1]["coarse"]
        oct_ = max(1, round(drop / 12)) if drop > 0 else 1
        return rest[0], noise[0], rest[1], oct_

    if len(live) < 3:
        return live[0], (live[1] if len(live) > 1 else None), None, 0

    for i, j in itertools.permutations(range(3), 2):
        drop = live[i]["coarse"] - live[j]["coarse"]
        if (live[i]["wave"] == live[j]["wave"]
                and live[i]["fine"] == live[j]["fine"]
                and drop > 0 and drop % 12 == 0):
            aux = live[3 - i - j]
            return live[i], aux, live[j], drop // 12

    # No natural pair, so make one: keep the first two as written and
    # turn the third into a genuine sub of the primary.  It loses any
    # wave or detune of its own, which it was never entitled to - the
    # sub *is* the primary, read lower - and in every case here that
    # was an arbitrary choice rather than a musical requirement.
    pri, aux, third = live[0], live[1], live[2]
    drop = pri["coarse"] - third["coarse"]
    oct_ = max(1, round(drop / 12)) if drop > 0 else 1
    return pri, aux, third, oct_


# struck: fast attack, decays away, filter closing behind it
PIANO = dict(
    _n=2, dco=[osc(SAW, 62), osc(TRI, 48), osc(SAW, 17, coarse=-12)],
    dca_env_level=127, dca_env_a=127, dca_env_d=22, dca_env_s=16, dca_env_r=38,
    dcf_freq=cut(96), dcf_reso=50, dcf_vel=30, dcf_track=70,
    dcf_env_level=-18, dcf_env_a=127, dcf_env_d=12, dcf_env_s=0, dcf_env_r=40,
)

# struck metal and wood: no sustain at all, bright, rings out
BELL = dict(
    _n=2, dco=[osc(SINE, 74), osc(SINE, 34, coarse=19), osc(TRI, 19, coarse=12)],
    **auxenv(127, 64, 24, 60),
    dca_env_level=127, dca_env_a=127, dca_env_d=34, dca_env_s=0, dca_env_r=44,
    dcf_freq=cut(112), dcf_reso=40, dcf_vel=22, dcf_track=90,
)

# drawbars: three octaves of sine, on and off with the key
ORGAN = dict(
    dco=[osc(SINE, 58), osc(SINE, 40, coarse=12), osc(SINE, 29, coarse=19)],
    dca_env_level=120, dca_env_a=127, dca_env_d=1, dca_env_s=127, dca_env_r=112,
    dcf_freq=cut(108), dcf_reso=30, dcf_track=40,
)

# plucked: immediate, decays, but with a body under it
GUITAR = dict(
    _n=2, dco=[osc(SAW, 66), osc(TRI, 40), osc(SAW, 21, fine=6)],
    dca_env_level=124, dca_env_a=127, dca_env_d=26, dca_env_s=14, dca_env_r=40,
    dcf_freq=cut(90), dcf_reso=56, dcf_vel=32, dcf_track=60,
    dcf_env_level=-14, dcf_env_a=127, dcf_env_d=14, dcf_env_s=0, dcf_env_r=40,
)

# low and round, with the filter opening on the attack
BASS = dict(
    _n=2, dco=[osc(SAW, 74, coarse=-12), osc(SQUARE, 38, coarse=-12), osc(SINE, 22, coarse=-24)],
    dca_env_level=127, dca_env_a=120, dca_env_d=30, dca_env_s=52, dca_env_r=46,
    dcf_freq=cut(58), dcf_reso=64, dcf_vel=34, dcf_track=45,
    dcf_env_level=24, dcf_env_a=110, dcf_env_d=20, dcf_env_s=10, dcf_env_r=40,
)

# bowed: slow on, slow off, vibrato that arrives after the note
STRING = dict(
    dco=[osc(SAW, 60), osc(SAW, 46, fine=5), osc(SAW, 20, coarse=-12)],
    dca_env_level=118, dca_env_a=13, dca_env_d=18, dca_env_s=104, dca_env_r=22,
    dcf_freq=cut(88), dcf_reso=44, dcf_vel=18, dcf_track=55,
    lfo_global=1,
    lfo_wave=SINE, lfo_freq=lfo(5.5), lfo_delay=96, lfo_pitch=3, lfo_wheel=7,
)

# many of them at once: wider detune, slower still
ENSEMBLE = dict(
    dco=[osc(SAW, 50), osc(SAW, 47, fine=9), osc(SAW, 30, coarse=-12, fine=-6)],
    dca_env_level=112, dca_env_a=7, dca_env_d=16, dca_env_s=116, dca_env_r=15,
    dcf_freq=cut(86), dcf_reso=38, dcf_vel=14, dcf_track=50,
    lfo_wave=SINE, lfo_freq=lfo(4.5), lfo_delay=104, lfo_pitch=2, lfo_wheel=6,
)

# blown: the filter opens as it speaks, which is most of the character
BRASS = dict(
    dco=[osc(SAW, 70), osc(SAW, 44, fine=4), osc(SQUARE, 18, coarse=-12)],
    dca_env_level=124, dca_env_a=30, dca_env_d=24, dca_env_s=96, dca_env_r=32,
    dcf_freq=cut(72), dcf_reso=58, dcf_vel=40, dcf_track=55,
    dcf_env_level=26, dcf_env_a=34, dcf_env_d=16, dcf_env_s=54, dcf_env_r=34,
    lfo_global=1,
    lfo_wave=SINE, lfo_freq=lfo(5.0), lfo_delay=104, lfo_pitch=2, lfo_wheel=7,
)

# reeds: hollow, square-ish, with a player's vibrato
REED = dict(
    _n=2, dco=[osc(SQUARE, 58), osc(SAW, 44), osc(SQUARE, 20, coarse=-12)],
    dca_env_level=120, dca_env_a=38, dca_env_d=22, dca_env_s=106, dca_env_r=30,
    dcf_freq=cut(80), dcf_reso=54, dcf_vel=30, dcf_track=55,
    dcf_env_level=16, dcf_env_a=40, dcf_env_d=18, dcf_env_s=60, dcf_env_r=32,
    lfo_global=1,
    lfo_wave=SINE, lfo_freq=lfo(5.5), lfo_delay=92, lfo_pitch=4, lfo_wheel=8,
)

# pipes: nearly a sine, and without a noise source that is all the
# breath we can manage
# a flute is a sine with air behind it, and the air is the half of that
# which could not be reached before
PIPE = dict(
    _n=2, dco=[osc(SINE, 96), osc(NOISE, 7), osc(SINE, 16, coarse=12)],
    **auxenv(127, 127, 0, 60),
    dca_env_level=116, dca_env_a=26, dca_env_d=20, dca_env_s=112, dca_env_r=28,
    dcf_freq=cut(98), dcf_reso=34, dcf_vel=20, dcf_track=60,
    lfo_global=1,
    lfo_wave=SINE, lfo_freq=lfo(5.0), lfo_delay=88, lfo_pitch=4, lfo_wheel=8,
)

# what the synth is actually for
LEAD = dict(
    _n=2, dco=[osc(SAW, 66), osc(SQUARE, 44, fine=7), osc(SAW, 22, coarse=-12)],
    dca_env_level=124, dca_env_a=100, dca_env_d=24, dca_env_s=100, dca_env_r=30,
    dcf_freq=cut(78), dcf_reso=88, dcf_vel=36, dcf_track=60,
    dcf_env_level=22, dcf_env_a=90, dcf_env_d=16, dcf_env_s=54, dcf_env_r=32,
    lfo_global=1,
    lfo_wave=SINE, lfo_freq=lfo(5.5), lfo_delay=100, lfo_pitch=2, lfo_wheel=9,
)

PAD = dict(
    dco=[osc(SAW, 48), osc(SAW, 44, fine=11), osc(SAW, 32, coarse=-12, fine=-8)],
    dca_env_level=110, dca_env_a=4, dca_env_d=12, dca_env_s=122, dca_env_r=6,
    dcf_freq=cut(74), dcf_reso=54, dcf_vel=12, dcf_track=45,
    dcf_env_level=18, dcf_env_a=5, dcf_env_d=6, dcf_env_s=70, dcf_env_r=6,
    lfo_wave=SINE, lfo_freq=lfo(1.0), lfo_delay=110, lfo_dcf=10,
    lfo_amount=64, lfo_wheel=63,
)

# effects: the LFO doing something obvious, since subtlety is not the point
SYNFX = dict(
    dco=[osc(SAW, 46), osc(SINE, 44, coarse=7), osc(TRI, 30, coarse=-12, fine=13)],
    dca_env_level=110, dca_env_a=6, dca_env_d=14, dca_env_s=110, dca_env_r=7,
    dcf_freq=cut(80), dcf_reso=76, dcf_track=40,
    lfo_wave=SINE, lfo_freq=lfo(0.5), lfo_pitch=0, lfo_dcf=10, lfo_dca=29,
)

ETHNIC = dict(
    _n=2, dco=[osc(SAW, 62), osc(TRI, 42, fine=8), osc(SAW, 22, coarse=12)],
    dca_env_level=122, dca_env_a=127, dca_env_d=26, dca_env_s=20, dca_env_r=38,
    dcf_freq=cut(92), dcf_reso=62, dcf_vel=30, dcf_track=65,
    dcf_env_level=-12, dcf_env_a=127, dcf_env_d=14, dcf_env_s=0, dcf_env_r=38,
)

# struck, short, and pitched - the unpitched ones are out of reach
PERC = dict(
    _n=2, dco=[osc(TRI, 76), osc(SINE, 40, coarse=12), osc(SQUARE, 14, coarse=19)],
    dca_env_level=127, dca_env_a=127, dca_env_d=52, dca_env_s=0, dca_env_r=60,
    dcf_freq=cut(100), dcf_reso=52, dcf_vel=28, dcf_track=70,
    dcf_env_level=-20, dcf_env_a=127, dcf_env_d=40, dcf_env_s=0, dcf_env_r=60,
)

# these wanted noise and there wasn't any; now there is.  A pitched
# primary stays because most of them are noise *plus* something - a
# gunshot has a thump under it, a helicopter a rotor
SFX = dict(
    _n=2, dco=[osc(SAW, 18), osc(NOISE, 52), osc(SAW, 0)],
    dca_env_level=110, dca_env_a=90, dca_env_d=30, dca_env_s=60, dca_env_r=40,
    dcf_freq=cut(84), dcf_reso=70, dcf_track=30,
    lfo_wave=SINE, lfo_freq=lfo(7.0), lfo_dcf=8, lfo_dca=36,
)



def _migrate(p):
    """Split the old intrinsic/wheel pitch depths into a master amount
    and a routing depth, preserving what both ends of the wheel did."""
    if 'lfo_amount' in p:
        # already expressed in the new terms - a patch that routes the
        # wheel somewhere other than pitch says so directly
        return p

    d = p.pop('lfo_pitch', 0)
    w = p.get('lfo_wheel', 0)
    total = d + w
    if total:
        p['lfo_pitch']  = total
        p['lfo_amount'] = round(127 * d / total)
        p['lfo_wheel']  = 127 - p['lfo_amount']
    elif p.get('lfo_dcf') or p.get('lfo_dca'):
        # no vibrato, but something else is routed - run the LFO at full
        # and let a patch add a wheel amount if it wants one
        p['lfo_amount'] = 127
    return p


def P(base, n=None, **over):
    p = {k: (list(v) if k == "dco" else dict(v) if isinstance(v, dict) else v)
         for k, v in base.items()}
    p["dco"] = [dict(d) for d in p["dco"]]
    if "dco" in over:
        for i, d in enumerate(over.pop("dco")):
            if d is not None:
                p["dco"][i] = dict(d)
    p.update(over)

    # how many oscillators this patch actually needs.  Three is not free
    # - one costs 3.92M of the 6M deadline at 64 voices and three cost
    # 5.01M - so a flute that is nearly a sine should not pay for a
    # detuned stack it does not use
    n = n if n is not None else p.pop("_n", 3)
    p.pop("_n", None)
    if n < 3:
        before = sum(d["level"] for d in p["dco"])
        after = sum(d["level"] for d in p["dco"][:n])
        if after:
            k = before / after
            for d in p["dco"][:n]:
                d["level"] = min(127, round(d["level"] * k))
        for d in p["dco"][n:]:
            d["level"] = 0

    return _migrate(p)


GM = [
    # --- 1-8  Piano ----------------------------------------------------
    ("Acoustic Grand Piano",   P(PIANO)),
    ("Bright Acoustic Piano",  P(PIANO, dcf_freq=cut(104), dcf_vel=36)),
    ("Electric Grand Piano",   P(PIANO, dco=[osc(SAW, 56), osc(SINE, 54), None])),
    ("Honky-tonk Piano",       P(PIANO, dco=[osc(SAW, 58), osc(SAW, 52, fine=14), None])),
    ("Electric Piano 1",       P(PIANO, dco=[osc(SINE, 70), osc(SINE, 40, coarse=12), None],
                                  **auxenv(127, 70, 20, 60), dcf_freq=cut(100), dca_env_d=18, dca_env_s=24)),
    ("Electric Piano 2",       P(PIANO, dco=[osc(SINE, 66), osc(TRI, 44, coarse=12), None],
                                  **auxenv(127, 60, 24, 60), dcf_freq=cut(104), dcf_reso=64, dca_env_s=28)),
    ("Harpsichord",            P(PIANO, dco=[osc(SAW, 70), osc(SAW, 42, coarse=12), None],
                                  **auxenv(127, 60, 30, 60), dcf_freq=cut(108), dca_env_d=30, dca_env_s=0)),
    ("Clavi",                  P(PIANO, dcf_reso=86, dcf_env_level=-24, dca_env_s=8)),

    # --- 9-16  Chromatic Percussion ------------------------------------
    ("Celesta",                P(BELL)),
    ("Glockenspiel",           P(BELL, dcf_freq=cut(118), dca_env_d=44)),
    ("Music Box",              P(BELL, dca_env_d=40, dca_env_r=52)),
    ("Vibraphone",             P(BELL, dco=[osc(SINE, 78), osc(SINE, 28, coarse=12), None],
                                  **auxenv(127, 50, 26, 60), dca_env_d=22, dca_env_r=54,
                                  lfo_wave=SINE, lfo_freq=lfo(4.5), lfo_dca=34)),
    ("Marimba",                P(BELL, dco=[osc(TRI, 78), osc(SINE, 30, coarse=19), None],
                                  **auxenv(127, 90, 0, 80), dca_env_d=42)),
    ("Xylophone",              P(BELL, dco=[osc(SQUARE, 62), osc(TRI, 40, coarse=12), None],
                                  **auxenv(127, 100, 0, 80), dca_env_d=54)),
    ("Tubular Bells",          P(BELL, dco=[osc(SINE, 62), osc(SINE, 44, coarse=19), None],
                                  **auxenv(127, 30, 40, 40), dca_env_d=14, dca_env_r=30)),
    ("Dulcimer",               P(BELL, dco=[osc(SAW, 60), osc(TRI, 46, fine=9), None],
                                  **auxenv(0, 0, 0, 0), dca_env_d=30)),

    # --- 17-24  Organ --------------------------------------------------
    ("Drawbar Organ",          P(ORGAN)),
    ("Percussive Organ",       P(ORGAN, dcf_env_level=20, dcf_env_a=127, dcf_env_d=48,
                                  dcf_env_s=0, dcf_env_r=60)),
    ("Rock Organ",             P(ORGAN, dco=[osc(SQUARE, 54), osc(SINE, 42, coarse=12), None],
                                  dcf_reso=70, dcf_freq=cut(96))),
    ("Church Organ",           P(ORGAN, dco=[osc(SINE, 52), osc(SINE, 38, coarse=12),
                                             osc(SINE, 34, coarse=24)], dca_env_a=40, dca_env_r=70)),
    ("Reed Organ",             P(ORGAN, dco=[osc(SQUARE, 60), osc(SAW, 36, coarse=12), None])),
    ("Accordion",              P(ORGAN, dco=[osc(SQUARE, 54), osc(SAW, 44, fine=11), None],
                                  lfo_wave=SINE, lfo_freq=lfo(5.5), lfo_pitch=3)),
    ("Harmonica",              P(ORGAN, dco=[osc(SQUARE, 58), osc(SAW, 40, fine=7), None],
                                  dcf_freq=cut(86), dcf_reso=68)),
    ("Tango Accordion",        P(ORGAN, dco=[osc(SQUARE, 56), osc(SAW, 46, fine=14), None],
                                  lfo_wave=SINE, lfo_freq=lfo(6.0), lfo_pitch=4)),

    # --- 25-32  Guitar -------------------------------------------------
    ("Acoustic Guitar (nylon)", P(GUITAR, dco=[osc(TRI, 66), osc(SAW, 38), None])),
    ("Acoustic Guitar (steel)", P(GUITAR, dcf_freq=cut(98))),
    ("Electric Guitar (jazz)",  P(GUITAR, dcf_freq=cut(80), dca_env_s=22)),
    ("Electric Guitar (clean)", P(GUITAR, dcf_freq=cut(94), dcf_reso=66)),
    ("Electric Guitar (muted)", P(GUITAR, dca_env_d=54, dca_env_s=0, dcf_freq=cut(74))),
    ("Overdriven Guitar",       P(GUITAR, dco=[osc(SAW, 96), osc(SQUARE, 74, fine=6), None],
                                   dcf_reso=76, dca_env_s=52, dca_env_d=16)),
    ("Distortion Guitar",       P(GUITAR, dco=[osc(SAW, 120), osc(SQUARE, 104, fine=9), None],
                                   dcf_reso=82, dca_env_s=72, dca_env_d=12)),
    ("Guitar Harmonics",        P(GUITAR, dco=[osc(SINE, 70), osc(SINE, 40, coarse=19), None],
                                   dcf_freq=cut(110))),

    # --- 33-40  Bass ---------------------------------------------------
    ("Acoustic Bass",          P(BASS, dco=[osc(TRI, 76, coarse=-12), osc(SAW, 34, coarse=-12), None])),
    ("Electric Bass (finger)", P(BASS)),
    ("Electric Bass (pick)",   P(BASS, dcf_freq=cut(66), dcf_env_level=30, dca_env_d=36)),
    ("Fretless Bass",          P(BASS, dco=[osc(SAW, 70, coarse=-12), osc(TRI, 42, coarse=-12), None],
                                  dcf_freq=cut(54), dcf_env_level=16)),
    ("Slap Bass 1",            P(BASS, dcf_env_level=44, dcf_env_d=44, dcf_reso=86)),
    ("Slap Bass 2",            P(BASS, dcf_env_level=52, dcf_env_d=52, dcf_reso=92, dca_env_s=34)),
    ("Synth Bass 1",           P(BASS, dco=[osc(SQUARE, 84, coarse=-12), osc(SAW, 32, coarse=-12), None],
                                  dcf_reso=92, dcf_env_level=34)),
    ("Synth Bass 2",           P(BASS, dco=[osc(SAW, 80, coarse=-12), osc(SAW, 40, coarse=-12, fine=9), None],
                                  dcf_reso=96, dcf_env_level=40, dcf_env_d=32)),

    # --- 41-48  Strings ------------------------------------------------
    ("Violin",                 P(STRING)),
    ("Viola",                  P(STRING, dcf_freq=cut(84))),
    ("Cello",                  P(STRING, dco=[osc(SAW, 62, coarse=-12), osc(SAW, 44, coarse=-12, fine=5), None],
                                  dcf_freq=cut(78))),
    ("Contrabass",             P(STRING, dco=[osc(SAW, 66, coarse=-24), osc(SAW, 40, coarse=-24, fine=5), None],
                                  dcf_freq=cut(66))),
    ("Tremolo Strings",        P(STRING, lfo_freq=lfo(7.0), lfo_delay=0, lfo_dca=46, lfo_pitch=2)),
    ("Pizzicato Strings",      P(STRING, dca_env_a=127, dca_env_d=48, dca_env_s=0, dca_env_r=50,
                                  lfo_pitch=0, lfo_wheel=0)),
    ("Orchestral Harp",        P(STRING, dco=[osc(TRI, 68), osc(SAW, 38), None],
                                  dca_env_a=127, dca_env_d=24, dca_env_s=0, dca_env_r=40,
                                  lfo_pitch=0, lfo_wheel=0)),
    ("Timpani",                P(STRING, dco=[osc(SINE, 84, coarse=-24), osc(TRI, 34, coarse=-12), None],
                                  dca_env_a=127, dca_env_d=40, dca_env_s=0, dca_env_r=46,
                                  dcf_freq=cut(58), lfo_pitch=0, lfo_wheel=0)),

    # --- 49-56  Ensemble -----------------------------------------------
    ("String Ensemble 1",      P(ENSEMBLE)),
    ("String Ensemble 2",      P(ENSEMBLE, dca_env_a=5, dcf_freq=cut(82))),
    ("Synth Strings 1",        P(ENSEMBLE, dcf_reso=60, dcf_env_level=16, dcf_env_a=6,
                                  dcf_env_d=8, dcf_env_s=60, dcf_env_r=8)),
    ("Synth Strings 2",        P(ENSEMBLE, dco=[osc(SAW, 48), osc(SQUARE, 44, fine=12), None],
                                  dcf_reso=66, dcf_env_level=20)),
    ("Choir Aahs",             P(ENSEMBLE, dco=[osc(SINE, 56), osc(TRI, 46, fine=7),
                                                osc(SINE, 28, coarse=12)], dcf_freq=cut(80))),
    ("Voice Oohs",             P(ENSEMBLE, dco=[osc(SINE, 68), osc(SINE, 40, coarse=12), None],
                                  dcf_freq=cut(72), dcf_reso=64)),
    ("Synth Voice",            P(ENSEMBLE, dco=[osc(TRI, 58), osc(SAW, 42, fine=9), None],
                                  dcf_freq=cut(78), dcf_reso=70)),
    ("Orchestra Hit",          P(ENSEMBLE, dca_env_a=127, dca_env_d=40, dca_env_s=0, dca_env_r=48,
                                  dcf_env_level=30, dcf_env_a=127, dcf_env_d=34, dcf_env_s=0,
                                  dcf_env_r=48, lfo_pitch=0, lfo_wheel=0)),

    # --- 57-64  Brass --------------------------------------------------
    ("Trumpet",                P(BRASS)),
    ("Trombone",               P(BRASS, dco=[osc(SAW, 70, coarse=-12), osc(SAW, 44, coarse=-12, fine=4), None],
                                  dcf_freq=cut(66))),
    ("Tuba",                   P(BRASS, dco=[osc(SAW, 74, coarse=-24), osc(SQUARE, 34, coarse=-24), None],
                                  dcf_freq=cut(56), dca_env_a=22)),
    ("Muted Trumpet",          P(BRASS, dcf_freq=cut(84), dcf_reso=80, dcf_env_level=14)),
    ("French Horn",            P(BRASS, dca_env_a=20, dcf_freq=cut(68), dcf_env_level=18)),
    ("Brass Section",          P(BRASS, dco=[osc(SAW, 54), osc(SAW, 48, fine=8),
                                             osc(SAW, 26, coarse=-12)], dca_env_a=26)),
    ("Synth Brass 1",          P(BRASS, dcf_reso=76, dcf_env_level=34, dca_env_a=44)),
    ("Synth Brass 2",          P(BRASS, dco=[osc(SAW, 60), osc(SQUARE, 44, fine=6), None],
                                  dcf_reso=84, dcf_env_level=40)),

    # --- 65-72  Reed ---------------------------------------------------
    ("Soprano Sax",            P(REED, dcf_freq=cut(88))),
    ("Alto Sax",               P(REED)),
    ("Tenor Sax",              P(REED, dco=[osc(SQUARE, 60, coarse=-12), osc(SAW, 42, coarse=-12), None],
                                  dcf_freq=cut(74))),
    ("Baritone Sax",           P(REED, dco=[osc(SQUARE, 64, coarse=-24), osc(SAW, 40, coarse=-24), None],
                                  dcf_freq=cut(64))),
    ("Oboe",                   P(REED, dco=[osc(SQUARE, 52), osc(SAW, 50, fine=4), None],
                                  dcf_freq=cut(92), dcf_reso=72)),
    ("English Horn",           P(REED, dco=[osc(SQUARE, 56), osc(SAW, 44), None],
                                  dcf_freq=cut(82), dcf_reso=68)),
    ("Bassoon",                P(REED, dco=[osc(SQUARE, 62, coarse=-12), osc(TRI, 40, coarse=-12), None],
                                  dcf_freq=cut(66))),
    ("Clarinet",               P(REED, dco=[osc(SQUARE, 78), osc(SINE, 30, coarse=19), None],
                                  dcf_freq=cut(84))),

    # --- 73-80  Pipe ---------------------------------------------------
    ("Piccolo",                P(PIPE, dco=[osc(SINE, 76, coarse=12), osc(NOISE, 7), None],
                                  dcf_freq=cut(112))),
    ("Flute",                  P(PIPE)),
    ("Recorder",               P(PIPE, dco=[osc(SINE, 80), osc(NOISE, 8), None])),
    ("Pan Flute",              P(PIPE, dco=[osc(SINE, 70), osc(NOISE, 12), None], **auxenv(127, 110, 0, 60),
                                  lfo_pitch=6)),
    ("Blown Bottle",           P(PIPE, dco=[osc(SINE, 82), osc(NOISE, 10), None], **auxenv(127, 110, 0, 60),
                                  dcf_freq=cut(88))),
    ("Shakuhachi",             P(PIPE, dco=[osc(TRI, 68), osc(NOISE, 15), None],
                                  **auxenv(127, 100, 0, 60), lfo_pitch=7)),
    ("Whistle",                P(PIPE, dco=[osc(SINE, 96), None, None], dcf_freq=cut(114),
                                  **auxenv(127, 127, 0, 60))),
    ("Ocarina",                P(PIPE, dco=[osc(SINE, 88), osc(NOISE, 7), None])),

    # --- 81-88  Synth Lead ---------------------------------------------
    ("Lead 1 (square)",        P(LEAD, dco=[osc(SQUARE, 76), osc(SQUARE, 40, fine=7), None])),
    ("Lead 2 (sawtooth)",      P(LEAD)),
    ("Lead 3 (calliope)",      P(LEAD, dco=[osc(SINE, 76), osc(TRI, 40, coarse=12), None],
                                  dcf_freq=cut(96), dcf_reso=50)),
    ("Lead 4 (chiff)",         P(LEAD, dcf_env_level=40, dcf_env_d=48, dcf_env_s=30)),
    ("Lead 5 (charang)",       P(LEAD, dco=[osc(SAW, 80), osc(SQUARE, 52, fine=11), None],
                                  dcf_reso=96)),
    ("Lead 6 (voice)",         P(LEAD, dco=[osc(TRI, 66), osc(SINE, 46, coarse=12), None],
                                  dcf_freq=cut(74), dcf_reso=72)),
    ("Lead 7 (fifths)",        P(LEAD, dco=[osc(SAW, 60), osc(SAW, 44, coarse=7), None])),
    ("Lead 8 (bass + lead)",   P(LEAD, dco=[osc(SAW, 64), osc(SAW, 46, coarse=-12), None],
                                  dcf_freq=cut(70))),

    # --- 89-96  Synth Pad ----------------------------------------------
    ("Pad 1 (new age)",        P(PAD)),
    ("Pad 2 (warm)",           P(PAD, dcf_freq=cut(68), dca_env_a=3)),
    ("Pad 3 (polysynth)",      P(PAD, dco=[osc(SAW, 50), osc(SQUARE, 44, fine=10), None],
                                  dca_env_a=8, dcf_reso=64)),
    ("Pad 4 (choir)",          P(PAD, dco=[osc(SINE, 54), osc(TRI, 46, fine=8),
                                           osc(SINE, 30, coarse=12)], dcf_freq=cut(78))),
    ("Pad 5 (bowed)",          P(PAD, dcf_env_level=24, dcf_env_a=4, dcf_reso=66)),
    ("Pad 6 (metallic)",       P(PAD, dco=[osc(SQUARE, 46), osc(SAW, 44, coarse=7, fine=6), None],
                                  dcf_reso=78)),
    ("Pad 7 (halo)",           P(PAD, dco=[osc(SINE, 48), osc(SINE, 44, coarse=12, fine=9), None],
                                  dcf_freq=cut(84))),
    ("Pad 8 (sweep)",          P(PAD, lfo_freq=lfo(0.3), lfo_dcf=12, dcf_reso=82)),

    # --- 97-104  Synth Effects -----------------------------------------
    ("FX 1 (rain)",            P(SYNFX)),
    ("FX 2 (soundtrack)",      P(SYNFX, lfo_freq=lfo(0.3), lfo_dcf=14, dcf_reso=84)),
    ("FX 3 (crystal)",         P(SYNFX, dco=[osc(SINE, 62), osc(SINE, 40, coarse=19), None],
                                  dca_env_a=127, dca_env_d=30, dca_env_s=0, dca_env_r=44,
                                  lfo_dca=21)),
    ("FX 4 (atmosphere)",      P(SYNFX, dco=[osc(TRI, 54), osc(SINE, 44, coarse=12, fine=9), None],
                                  lfo_freq=lfo(0.4))),
    ("FX 5 (brightness)",      P(SYNFX, dcf_freq=cut(104), dcf_env_level=28, dcf_env_a=6,
                                  dcf_env_d=8, dcf_env_s=70, dcf_env_r=8)),
    ("FX 6 (goblins)",         P(SYNFX, lfo_freq=lfo(0.8), lfo_pitch=13, lfo_dcf=12)),
    ("FX 7 (echoes)",          P(SYNFX, lfo_freq=lfo(2.5), lfo_dca=50, dca_env_r=10)),
    ("FX 8 (sci-fi)",          P(SYNFX, dco_env_level=-25, dco_env_a=30, dco_env_d=20,
                                  dco_env_s=0, dco_env_r=30, lfo_dcf=14)),

    # --- 105-112  Ethnic -----------------------------------------------
    ("Sitar",                  P(ETHNIC, dcf_reso=88)),
    ("Banjo",                  P(ETHNIC, dco=[osc(SAW, 70), osc(SQUARE, 34, coarse=12), None],
                                  dca_env_d=36, dcf_freq=cut(102))),
    ("Shamisen",               P(ETHNIC, dco=[osc(SAW, 66), osc(TRI, 40), None], dca_env_d=32)),
    ("Koto",                   P(ETHNIC, dco=[osc(TRI, 70), osc(SAW, 36), None], dca_env_d=28)),
    ("Kalimba",                P(ETHNIC, dco=[osc(SINE, 82), osc(TRI, 28, coarse=12), None],
                                  dca_env_d=40, dca_env_s=0)),
    ("Bagpipe",                P(ETHNIC, dco=[osc(SQUARE, 60), osc(SAW, 46, fine=9),
                                              osc(SQUARE, 30, coarse=-12)],
                                  dca_env_a=60, dca_env_s=110, dca_env_d=20,
                                  dcf_env_level=0, lfo_wave=SINE, lfo_freq=lfo(5.0), lfo_pitch=3)),
    ("Fiddle",                 P(ETHNIC, dca_env_a=16, dca_env_s=104, dca_env_d=18,
                                  dcf_env_level=0, lfo_wave=SINE, lfo_freq=lfo(6.0),
                                  lfo_delay=80, lfo_pitch=4)),
    ("Shanai",                 P(ETHNIC, dco=[osc(SQUARE, 62), osc(SAW, 44), None],
                                  dca_env_a=40, dca_env_s=106, dcf_env_level=0,
                                  lfo_wave=SINE, lfo_freq=lfo(5.5), lfo_pitch=5)),

    # --- 113-120  Percussive -------------------------------------------
    ("Tinkle Bell",            P(PERC, dcf_freq=cut(116), dca_env_d=60)),
    ("Agogo",                  P(PERC, dco=[osc(SQUARE, 66), osc(SINE, 40, coarse=12), None],
                                  dca_env_d=58)),
    ("Steel Drums",            P(PERC, dco=[osc(SINE, 74), osc(TRI, 42, coarse=12), None],
                                  dca_env_d=34)),
    ("Woodblock",              P(PERC, dco=[osc(SQUARE, 70), osc(TRI, 40, coarse=19), None],
                                  dca_env_d=90, dca_env_r=90)),
    ("Taiko Drum",             P(PERC, dco=[osc(SINE, 92, coarse=-24), osc(TRI, 30, coarse=-12), None],
                                  dcf_freq=cut(52), dca_env_d=44,
                                  dco_env_level=-10, dco_env_a=127, dco_env_d=90,
                                  dco_env_s=0, dco_env_r=90)),
    ("Melodic Tom",            P(PERC, dco=[osc(SINE, 86, coarse=-12), osc(TRI, 32), None],
                                  dcf_freq=cut(66), dca_env_d=48,
                                  dco_env_level=-8, dco_env_a=127, dco_env_d=80,
                                  dco_env_s=0, dco_env_r=80)),
    ("Synth Drum",             P(PERC, dco=[osc(SINE, 84, coarse=-12), osc(NOISE, 17)], **auxenv(127, 100, 0, 100),
                                  dcf_freq=cut(72), dca_env_d=52,
                                  dco_env_level=-19, dco_env_a=127, dco_env_d=70,
                                  dco_env_s=0, dco_env_r=70)),
    ("Reverse Cymbal",         P(PERC, dco=[osc(SQUARE, 16, coarse=19), osc(NOISE, 50)],
                                  dca_env_a=4, dca_env_d=60, dca_env_s=0, dca_env_r=100,
                                  dcf_freq=cut(70), dcf_env_level=44, dcf_env_a=4,
                                  dcf_env_d=60, dcf_env_s=0, dcf_env_r=100)),

    # --- 121-128  Sound Effects ----------------------------------------
    ("Guitar Fret Noise",      P(SFX, dca_env_a=127, dca_env_d=80, dca_env_s=0, dca_env_r=80,
                                  dcf_freq=cut(100), lfo_dcf=0, lfo_dca=0)),
    ("Breath Noise",           P(SFX, dco=[osc(SINE, 20, coarse=24), osc(NOISE, 58), None],
                                  dca_env_a=30, dca_env_d=30, dca_env_s=40, dca_env_r=30,
                                  dcf_freq=cut(108), dcf_reso=90)),
    ("Seashore",               P(SFX, dco=[osc(SINE, 8, coarse=12), osc(NOISE, 62), None],
                                  dca_env_a=3, dca_env_s=110, dca_env_r=4,
                                  lfo_freq=lfo(0.25), lfo_dcf=16, lfo_dca=43)),
    ("Bird Tweet",             P(SFX, dco=[osc(SINE, 90, coarse=24), osc(SINE, 0)],
                                  dca_env_a=127, dca_env_d=50, dca_env_s=0, dca_env_r=60,
                                  dco_env_level=19, dco_env_a=127, dco_env_d=40,
                                  dco_env_s=0, dco_env_r=60,
                                  lfo_freq=lfo(12.0), lfo_pitch=15, lfo_dcf=0, lfo_dca=0)),
    ("Telephone Ring",         P(SFX, dco=[osc(SQUARE, 78), osc(SQUARE, 50, coarse=7)],
                                  dca_env_a=127, dca_env_s=110, dca_env_r=90,
                                  dcf_freq=cut(96), lfo_freq=lfo(12.0), lfo_dca=79, lfo_dcf=0)),
    ("Helicopter",             P(SFX, dco=[osc(SQUARE, 78, coarse=-36), osc(NOISE, 27)],
                                  dca_env_a=20, dca_env_s=110, dca_env_r=20,
                                  dcf_freq=cut(48), lfo_freq=lfo(11.0), lfo_dca=86, lfo_dcf=4)),
    ("Applause",               P(SFX, dco=[osc(SINE, 10, coarse=19), osc(NOISE, 60), None],
                                  dca_env_a=8, dca_env_s=112, dca_env_r=8,
                                  dcf_freq=cut(104), lfo_freq=lfo(9.0), lfo_dca=50, lfo_dcf=10)),
    ("Gunshot",                P(SFX, dco=[osc(SINE, 70, coarse=-24), osc(NOISE, 46)], **auxenv(127, 110, 0, 110),
                                  dca_env_a=127, dca_env_d=44, dca_env_s=0, dca_env_r=50,
                                  dcf_freq=cut(90),
                                  dco_env_level=-38, dco_env_a=127, dco_env_d=100,
                                  dco_env_s=0, dco_env_r=100,
                                  dcf_env_level=-40, dcf_env_a=127, dcf_env_d=44,
                                  dcf_env_s=0, dcf_env_r=50, lfo_dca=0, lfo_dcf=0)),
]

ORDER = ["dca_env_level", "dca_env_a", "dca_env_d", "dca_env_s", "dca_env_r",
         "dco_env_level", "dco_env_a", "dco_env_d", "dco_env_s", "dco_env_r",
         "dcf_freq", "dcf_reso", "dcf_vel", "dcf_track", "dcf_press",
         "dcf_env_level", "dcf_env_a", "dcf_env_d", "dcf_env_s", "dcf_env_r",
         "lfo_wave", "lfo_freq", "lfo_global", "lfo_delay",
         "lfo_amount", "lfo_wheel", "lfo_press",
         "lfo_pitch", "lfo_dcf", "lfo_dca"]

FAMILY = {0: "Piano", 8: "Chromatic Percussion", 16: "Organ", 24: "Guitar",
          32: "Bass", 40: "Strings", 48: "Ensemble", 56: "Brass", 64: "Reed",
          72: "Pipe", 80: "Synth Lead", 88: "Synth Pad", 96: "Synth Effects",
          104: "Ethnic", 112: "Percussive", 120: "Sound Effects"}

out = sys.stdout
out.write("""// A rough General MIDI set.
//
// GENERATED by utils/gm_presets.py, which is not part of the build - run
// it by hand and commit the result.  Patches are specified musically in
// that script (cutoff as a MIDI note, LFO rate in Hz) and converted on
// the way out, so edit it rather than these numbers if a whole family
// needs moving.
//
// How close these get varies by family, and the limit is the instrument
// rather than the tuning.  Organs, strings, ensembles, brass, basses,
// reeds, leads and pads are what subtractive synthesis is for and come
// out well.  Pianos and guitars are recognisable but plainly synthetic.
//
// The last two families are gestures, because **there is no noise
// source**: fret noise, breath, seashore, applause and gunshot are all
// noise, and three tuned oscillators cannot be it.  Wide detuning and
// extreme registers approximate the shape and nothing more.  The same
// gap is why channel 10 has no drum map - a snare or a hi-hat is not
// reachable from here at all.
//
// waves: 0 sine, 1 square, 2 saw, 3 triangle

#include "patch.h"

const Patch presets[] = {
""")

for i, (name, p) in enumerate(GM):
    if i in FAMILY:
        out.write("\n\t//--------------------------------------------------------------\n")
        out.write("\t// %s\n" % FAMILY[i])
        out.write("\t//--------------------------------------------------------------\n")
    out.write("\t{\t// %d  %s\n" % (i + 1, name))

    pri, aux, sub, oct_ = arrange(p["dco"])

    def emit(d):
        bits = [".wave = %s" % WAVE_NAME[d["wave"]], ".level = %d" % d["level"]]
        if d["coarse"]:
            bits.append(".coarse = %d" % d["coarse"])
        if d["fine"]:
            bits.append(".fine = %d" % d["fine"])
        return "{ %s }" % ", ".join(bits)

    out.write("\t\t.dco = {\n")
    out.write("\t\t\t%s,\n" % emit(pri))
    if aux:
        out.write("\t\t\t%s,\n" % emit(aux))
    out.write("\t\t},\n")

    if sub:
        out.write("\t\t.sub_level     = %d,\n" % sub["level"])
        out.write("\t\t.sub_octaves   = %d,\n" % oct_)

    for k in ("aux_env_a", "aux_env_d", "aux_env_s", "aux_env_r"):
        if p.get(k):
            out.write("\t\t.%-14s = %s,\n" % (k, p[k]))

    for k in ORDER:
        v = p.get(k, 0)
        if v:
            if k == "lfo_wave":
                v = WAVE_NAME[v]
            out.write("\t\t.%-14s = %s,\n" % (k, v))
    out.write("\t},\n")

out.write("};\n")
