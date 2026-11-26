# Looching 2026: specification

Version 0.1 (skeleton) · R. Luke DuBois and Brad Garton

## What it is

Looching 2026 is a revival of *Looching*, Brad Garton's 1990–91 NeXT application (Columbia University Music
Department) that "generates pseudo-NewAge (or perhaps it's the Real McCoy!) music using the Motorola DSP chip": long,
slowly fading drones played by four voices that a lazy random scheduler wakes every few seconds. The FM-1 version
emulates the original as closely as possible, from its source code (MusicKit, `Looching.m`, `Bark.m`,
`LoochPerformer.m`, `RandomIzer.m`, `LoochWaves.h`): the same tone rows and wavetables, the same voice ("Bark": two
interpolating 64-sample wavetable oscillators with their own vibratos, an asymptotic envelope, a dry left and a
delayed right channel), the same random choices and ranges, computed at the original 22 050 Hz. It is a generative
instrument: start it, pick a tone row, turn three knobs, listen. The keys play nothing; their LEDs show where the
drone is.

The original's code has quirks that shaped its sound. Looching 2026 has two modes:

- **ORIGINAL** (default): what the NeXT actually did, quirks included (see *The quirks*).
- **INTENDED**: what the code meant to do.

## Controls

| Control | Action | What it does |
| --- | --- | --- |
| PLAY | press | RELAX / ENOUGH!: start / stop looching. Start seeds the random generator and (ORIGINAL) loads the tone row's timbre set. Stop silences everything at once, as the original's `kill` did (with a 20 ms fade against a click) |
| PRESETS | turn | Tone row (the original's radio buttons): the Original, morning stuff, the Big Drone, Schoenberg, Bach. Takes effect for the next notes; the timbre set follows at the next start (ORIGINAL) or with the next notes (INTENDED) |
| KNOB 1 | turn | Vibrato rate limit, 0.00..10.00 Hz (steps of 0.05; default 0.50) |
| KNOB 2 | turn | Vibrato width limit, 0.0..10.0 % (steps of 0.1; default 1.0) |
| KNOB 3 | turn | Frequency spread, 0.0..10.0 % (steps of 0.1; default 1.0) |
| KNOB 4 | turn | Output level, 0..100 % (default 80) |
| ALGORITHM | turn | Mode: ORIGINAL / INTENDED (also on GLO) |
| SELECT | turn | Next / previous page: MAIN, GLO, INFO |
| HOME | press | MAIN page |
| HOME | hold (≥ 0.5 s) | INFO page |
| EDIT | press | INFO page |
| GLO | press | GLO page |
| SAVE | tap | Store the settings (a "SAVED" note on screen) |
| SAVE | hold (≥ 0.5 s) | The original's defaults again (row the Original, 0.5 Hz, 1 %, 1 %, ORIGINAL); not stored until SAVE |
| FX, SCL, ENV, LFO, ARP, SEQ, REC, OCT−, OCT+ | press | "SOON" with the planned extra (see *Out of scope for v1*) |
| OCT− + OCT+ | hold 5 s | Reserved by the platform (recovery) |
| 27 keys | press | Nothing (a brief LED flash) |
| Key LEDs | – | Each sounding voice lights the key nearest its pitch (folded by octaves into F3..G5), brightness following its envelope where the LEDs allow; off on GLO |
| MASTER pot | turn | Hardware output level, as the platform handles it |

Knob changes apply to the next notes at once: there is no "Set Them Values" button (the original's sliders only
reached the sound process when it was pressed). Sounding notes keep their values, as in the original.

## Screens

240 × 240, a NeXTSTEP homage: mid grey (#AAAAAA) window background, a small dark grey title band with white text, light grey
and dark grey bevels for buttons and slider wells, black text, white for highlights. Every screen is drawn lazily (a
signature per screen, redrawn only on change); the voice meters update at most a few times a second.

### MAIN (the Looching window)

- Top band: `LOOCHING 2026` in the small font, left-justified, white on dark grey, 18 px: the same band as the status
  strip (every page's title is in this band: `Preferences`, `Info`).
- Top left: the LoochityDog icon (48 × 48, 2-bit grey, converted from the original's `LoochityDog.tiff`).
- Top right: the RELAX / ENOUGH! button, bevelled, drawn pressed while running.
- Left: the five tone rows as radio buttons, the current one filled.
- Right: three sliders with their labels and values (`VIB RATE 0.50 Hz`, `VIB WIDTH 1.0 %`, `SPREAD 1.0 %`).
- Bottom: four voice meters (envelope level as a bar, the base frequency in Hz below, `--` when idle or silent).
- The status strip (dark grey, the last 18 rows): the mode (`ORIGINAL` / `INTENDED`) and the level; short notes
  ("SAVED", "SOON: DENSITY", the name and version at power-on) replace it for 1.5 s (3 s at power-on).

### GLO

A NeXT preferences panel, KNOB 1..4: MODE (ORIGINAL / INTENDED), MIDI OUT (on / off), KEY LEDS (on / off),
LEVEL. Below, a one-line explanation of the current mode.

### INFO

The original's Info panel: the dog icon, `Looching`, `from Brad Garton`, `(relaxing sounds)`, `(this is not the
Looch)`, a bevelled button reading `Living or dead, that's the big question`, and below: `FM-1 version 0.1`,
`R. Luke DuBois and Brad Garton`, `after Looching 2.0 for NeXT, 1991`. (The original's help text quoted John
Ashbery's *Three Poems*; it stays out.)

## The sound

All synthesis runs at 22 050 Hz (one sample per two output samples) and is upsampled to 44 100 Hz with a short
interpolation, keeping the 64-sample tables' character and halving the cost.

### Voices

Four voices ("Bark" patches), each:

- Two wavetable oscillators, interpolating, a 64-sample table each (both the same table, chosen per note).
- Oscillator 0 at the base frequency f0, oscillator 1 at f1 = f0 + d (the spread, below).
- A sine vibrato per oscillator: rate r0, r1 ∈ [0, rate limit] Hz; depth a0, a1 ∈ [0, width limit / 100 · f0] Hz
  (both depths relative to f0, as in the original).
- One amplitude envelope for both oscillators, MusicKit `AsympUG`-style: x ← x + (target − x) · k, k set from a T60
  (time to −60 dB). Note-on: x = 0, target = amp, T60 = attack; release: target = 0, T60 = release.
- amp ∈ [0.05, 0.10] per oscillator; the two oscillators summed.
- Output: left = the summed oscillators, right = the same through a delay line (see the quirks). All four voices
  summed per channel, times the level.

### Wavetables

Three timbre sets of seven waves each, from `LoochWaves.h`: seven partials per wave (frequency ratios and amplitudes
as listed there, missing seventh amplitudes = 0), phases 0, normalised to a peak of 1 (as MusicKit's `Partials` does
by default; to be checked by ear), generated at build time into 64-sample Q15 tables.

Tone rows and their timbre sets: the Original (set 0), morning stuff (set 1), the Big Drone (set 1), Schoenberg
(set 2), Bach (set 0), with the pitch tables of `LoochWaves.h`.

### The scheduler (both modes)

On RELAX, all four voices are idle with a remaining time of 0. Then, in a loop driven by the sample clock:

1. For each voice j: remaining_j −= elapsed.
   - Playing and remaining ≤ 0: release with a fade ∈ [10, 20] s (T60), remaining += fade, now idle.
   - Idle and remaining ≤ 0: with probability 0.5, start a note: a wave n = index(6) of the timbre set; a pitch from
     the row (below); spread s = U(0, spread) / 100 · f0, d = plusminus(s); amp ∈ [0.05, 0.10]; r0, a0, r1, a1;
     attack T60 ∈ [10, 20] s; remaining = U(5, 20) s; playing.
2. Wait elapsed = index(2..10) whole seconds, then repeat.

U(a, b) is uniform; index(n) and index(lo..hi) are the original's `ranInt`: round(U · (hi − lo) + lo), so the two
end values come up half as often. The generator is a 31-bit `random()`-like PRNG, seeded at each RELAX.

### The quirks (ORIGINAL vs INTENDED)

| What | ORIGINAL (what the NeXT did) | INTENDED |
| --- | --- | --- |
| Right-channel delay | 500 samples (22.7 ms) for every voice (the patch's counter never advanced) | 500, 85, 40, 5 samples for voices 1..4 |
| Pitch choice | index(toneindex[row]): one past the row's end can come up. The Original: 116.5 Hz (from the next row); Big Drone: 261.6 Hz (likewise); Bach: past the table, a silent note. Rows with 0.0 entries (morning stuff, Schoenberg) give silent notes (0 Hz) | uniform over the row's non-zero entries |
| Detune d | plusminus(s) = U · 2s − s/2, from −s/2 to +3s/2 | U · 2s − s, from −s to +s |
| Timbre set | loaded at RELAX from the row then chosen; changing the row while running keeps the old set | follows the row with the next notes |

A silent note still occupies its voice for its duration and fade, as in the original. Switching the mode applies to
the next notes (the delay lengths at once, with a short crossfade).

## Timing and sequencing

No sequencer, tempo or clock: the scheduler above is the whole of the time. Note lengths 5..20 s, fades 10..20 s,
wake-ups every 2..10 s. It counts samples in the audio path (no wall clock); `app_frame` only reads its requests and
results.

## What is stored

One settings record (store.c slot 35, the last: clear of Solinaesque's 0..32 on a shared unit; the app id): tone row, the three slider values, level, mode, MIDI out on / off, key
LEDs on / off, with a version and a check value. Loaded in `app_init` (the defaults when it is missing, from another
app or damaged, values clamped); written by SAVE. It does not autostart: it always powers up stopped.

## MIDI

- Out over USB (MIDI OUT on): each voice on its own channel (voice 1..4: channels 1..4). Note start: pitch bend to the
  exact frequency (±2 semitones), then note-on at the nearest note, velocity from amp (0.05..0.10 → 64..127). Release
  start: note-off. Silent (0 Hz) notes send nothing. ENOUGH! sends note-offs for sounding voices and All Notes Off.
- **The panel out** (USB, MIDI OUT on; `firmware/app/ctl.c`): every control the player changes, on channel 1, so a
  DAW can record it and MIDItoSpeech can say it. Nothing at power-on; changes made while MIDI OUT is off are not sent
  later. The main loop queues, the audio ISR sends after its notes (MIDI out from one context).

  | Message | Control | Value |
  | --- | --- | --- |
  | CC 7 | LEVEL | level x 127 / 100 |
  | NRPN 0:76 (CC 99 0, 98 76, 6 MSB, 38 LSB) | VIB RATE | step x 16383 / 200 (201 steps: 14-bit) |
  | CC 77 | VIB WIDTH | step x 127 / 100 |
  | CC 78 | SPREAD | step x 127 / 100 |
  | CC 80 | MODE | 0 ORIGINAL, 127 INTENDED |
  | CC 81 | KEY LEDS | 0 off, 127 on |
  | CC 109 | RELAX / ENOUGH! | 127 looching, 0 stopped |
  | Program Change 0..4 | tone row | the row |

- **MIDItoSpeech sidecar** (`docs/synthaccess/Looching2026.json`, for IDM NYU's synthaccess): generated from the
  firmware's map (`tools/gen_synthaccess.c`) and checked by the suite: every value sent (515) reads exactly as the
  screen shows it (`tests/synthaccess_check.py`, MIDItoSpeech's parser modes: percent, floatmap with the screen's
  decimals, enumsplit, offon, the rows as presets).
- In: none (TRS MIDI IN off by default; incoming messages are ignored).
- **USB name:** the FM-1 shows up as "Looching FM-1" (USB product string, `firmware/src/usb.c` STR2). It keeps "FM-1"
  so the installers find it; during an install the loader is "Felucca Update".

## Accessibility: the screen reader (fm1-reader)

The FM-1 speaks through the fm1-reader screen reader (https://github.com/charlesvestal/fm1-reader, protocol
version 1), as Solinaesque does (`sr.c` is Solinaesque's, unchanged): USB-MIDI SysEx `F0 7D 53 52 <cmd> ... F7`;
silent until a reader's HELLO (every 2 s; silent again 5 s after the last, or on BYE); the HELLO answer names the
firmware ("LOOCHING 2026 v0.1", an empty words link: the device speaks in full words); SAY messages (ASCII, at most
160 characters) with NOW for what the player caused and KEEP for warnings; WHERE (and the first HELLO) reads the
whole page. Never blocks: a message the USB queue has no room for is dropped. The platform's main loop calls the
app's `app_sysex()` before the update entry; it takes only frames starting 7D 53 52.

What is said (`speak.c`, fm1-reader's RULES.md: full words, no symbols, the important thing first):

- A page, in full: MAIN "looching, stopped. tone row, the Original. vibrato rate 0.50 hertz, vibrato width 1.0
  percent, spread 1.0 percent. original mode, level 80 percent. no voices sounding." (or "voices sounding: 114.0
  hertz, 266.6 hertz."); GLO "preferences. mode original, MIDI out on, key lights on, level 80 percent. original:
  what the NeXT did. ..."; INFO the Info panel in words.
- RELAX / ENOUGH! at once: "relax, looching", "enough, stopped".
- A control when it settles (120 ms still, at least every ~half second while turning): "vibrato rate 0.55 hertz",
  "tone row, Schoenberg", "intended mode", "level 75 percent", "MIDI out off", "key lights on".
- On MAIN, each note a voice starts, queued so it never cuts the player off: "voice 3, 266.6 hertz" (silent notes
  are not said).
- The status strip's notes: "saved", "coming soon, density", "defaults, not saved"; "save failed" as a warning.

## Open questions

- The original slider ranges (in the nib's binary data) are not recovered: 0..10 Hz and 0..10 % are a choice.
- MusicKit's `Partials` normalisation, and the DSP's `OscgafiUG` interpolation and amplitude scaling: to compare by
  ear with recordings of the original, if any exist.
- What the NeXT read past the end of the Bach row (assumed silent: the bytes after the table are small integers).
- The upsampling filter: linear interpolation first; a short half-band filter if the images are audible.


## Out of scope for v1 (planned extras)

- The original's hard-coded values on knobs: note density (the 0.5 coin, SEQ), attack / release range (ENV), note
  length range (SCL), wake-up interval (LFO), right-channel delay mix / length (FX). The buttons say "SOON" with the
  name.
- Transposing the rows (OCT− / OCT+), more voices, new tone rows or timbre sets (ARP, REC).
- MIDI in (CCs, Start / Stop), MIDI clock.
- The original's help pages.

## Notes from the build

- The original's data and icon (`assets/looching/`) are GPL-3.0-only with the rest: Brad Garton agreed (2026-11-26).

- The skeleton (milestone 1) is built: `firmware/app/{data,sound,ui}.c`, the data generated from the original files
  in `assets/looching/` by `tools/gen_looch.py` (the TIFF is a NeXT 2-bit grey + alpha, planar: read by hand).
- NeXT's four greys are fixed colours (no theme palette); text ink tops out just below pure white (the fonts' ramp).
- The audio ISR's static cost went from the start app's 867 to 2249 (four voices, two oscillators each, at 22 050 Hz),
  then 2600 with the scheduler and the delay crossfade; the emulator renders at ~1300x real time with the drone
  sounding.
- Milestones 2, 3 and 4 are built together (the pitch, detune and timbre quirks are part of choosing a note):
  `sound.c snd_wake` (the loop), `snd_note` (the draws, in the original's order), `row_pitch` (both modes). The host
  test checks the quirks statistically over 40 000 wakes (the stray 116.5 Hz and Bach's silent note ~1/28 each, the
  detune -s/2 .. +3s/2 vs -s .. +s, ranInt's half-weight ends) and a minute through the ISR.
- The delay crossfade on a mode change first stepped once per block (a zipper up to ~800): it now moves per sample.
- A minute of ORIGINAL starts ~4..8 notes; three minutes ~21, peaks around -7 dBFS (Q15) as notes overlap.

## Milestones

1. **Skeleton**: the three pages drawn (NeXT look, the dog icon), every control wired (values on screen, "SOON"),
   the settings record, one Bark voice playing a fixed note while running (keys do nothing), MIDI out of that note,
   tests and screenshots.
Milestones 1..4 are done (2026-11-25); 5 (MIDI out per voice, the LEDs) works and is tested for one voice.

2. **The Bark voice**: the generated wavetables, two oscillators with vibratos, the asymptotic envelope, at 22 050 Hz
   upsampled; tests for pitch, level, T60, no clicks.
3. **The scheduler**: four voices, the PRNG and `ranInt`, the original's timings; the right-channel delay; test
   with a fixed seed and the emulator at speed.
4. **ORIGINAL / INTENDED**: every quirk in the table, switchable; tests per quirk.
5. **MIDI out and LEDs**: the drone notes on channels 1..4, the key LEDs.
6. **Polish and the first install**: budgets, a long listen in the emulator, then the unit.
