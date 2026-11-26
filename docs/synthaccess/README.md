# Looching 2026 for MIDItoSpeech

`Looching2026.json` is a device file for **MIDItoSpeech**, part of [synthaccess](https://github.com/IDMNYU/synthaccess)
(IDM NYU): software on a computer that speaks what a synthesizer sends over MIDI. With it, MIDItoSpeech says what
you change on the FM-1, e.g. "vibrato rate limit 0.55 hertz", "tone row Schoenberg", "looching on".

## Using it

1. Connect the FM-1 by USB. On the GLO page keep **MIDI OUT** on: that is what sends your changes.
2. Copy `Looching2026.json` into MIDItoSpeech's `devices` folder.
   - Web version: also add the file name to `WebMIDI/devlist.txt` (or run `makedevlist.sh`), or load it with the
     page's file control.
   - Max version: open it from the patch.
3. Choose the FM-1's MIDI port, "Looching FM-1", as MIDItoSpeech's input and **channel 1** (MIDItoSpeech's
   default). macOS may keep showing a name it saw before for the same USB device.

## What it covers

Everything the panel sends, on channel 1 (`firmware/app/ctl.c`):

| Message | Control | Spoken |
| --- | --- | --- |
| CC 7 | LEVEL | "level 80 percent" |
| NRPN 0:76 (14-bit) | VIB RATE limit, 0.00..10.00 Hz | "vibrato rate limit 0.55 hertz" |
| CC 77 | VIB WIDTH limit, 0.0..10.0 % | "vibrato width limit 1.5 percent" |
| CC 78 | SPREAD, 0.0..10.0 % | "frequency spread 1.0 percent" |
| CC 80 | MODE | "mode intended" |
| CC 81 | KEY LEDS | "key lights. off" |
| CC 109 | RELAX / ENOUGH! | "looching. on" |
| Program Change 0..4 | the tone row | "tone row Schoenberg" |

The drone's own notes (voices 1..4 on channels 1..4, with pitch bend) are not spoken: the FM-1's screen reader
(fm1-reader) says them instead ("voice 3, 266.6 hertz").

## Keeping it current

The file is generated from the firmware's own tables, so it matches the FM-1's screen and its screen reader:

```sh
cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/gen_synthaccess tools/gen_synthaccess.c -lm
build/host/gen_synthaccess > docs/synthaccess/Looching2026.json
```

`tests/run_tests.sh` fails if the committed file is not what the firmware generates now, and checks every value the
FM-1 sends (515: every panel step of every control) against MIDItoSpeech's reading of it
(`tests/synthaccess_check.py`): each must read exactly as the screen shows it.
