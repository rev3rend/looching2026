// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 M-Vave-OP1 contributors
// Copyright (C) 2026 R. Luke DuBois and Brad Garton
// The browser emulator's WebAssembly without a browser: boot, the screen drawn, a key flashes its LED, RELAX sounds
// and lights PLAY, ENOUGH! silences, MIDI IN is ignored, the render keeps up with real time by a wide margin.
//   node web/emu/emu_test.mjs build/emu/emu.wasm
import { readFileSync } from "node:fs";

let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(60)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };

const { instance } = await WebAssembly.instantiate(readFileSync(process.argv[2] || "build/emu/emu.wasm"), {});
const ex = instance.exports;
ex._initialize();
ex.web_boot();
const N = 128;
const render = (quanta) => {
  let peak = 0;
  for (let q = 0; q < quanta; q++) {
    ex.web_render(N);
    const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), N);
    for (const v of l) peak = Math.max(peak, Math.abs(v));
  }
  return peak;
};
const screen = () => new Uint16Array(ex.memory.buffer, ex.web_screen(), 240 * 240);
const leds = () => new Uint8Array(ex.memory.buffer, ex.web_leds(), 41);

render(20);
const fb = screen(), bg = fb[0];
ok(fb.some((c) => c !== bg), "boot: the screen drawn");
ok(ex.web_sample_rate() === 44100, "sample rate 44100");
ok(render(50) === 0, "silent with nothing held");

// Looching 2026 (docs/SPEC.md): the keys play nothing (their LED flashes), PLAY is RELAX / ENOUGH!, no MIDI in
ex.web_keys(1);
ok(render(10) === 0, "a key plays nothing");
ok(leds()[14] === 1, "a key flashes its LED");
ex.web_keys(0);
render(80);
ok(leds()[14] === 0, "released: LED dark");

ex.web_buttons(1 << 10);                     // PLAY: RELAX
render(20);
ex.web_buttons(0);
let p = 0, secs = 0;                         // the scheduler's coins: a voice within the first wakes (2..10 s each)
while (p <= 0.01 && secs < 30) { p = Math.max(p, render(Math.round(44100 / N))); secs++; }
ok(p > 0.01 && p <= 0.5, `RELAX: the drone sounds (peak ${p.toFixed(3)} after ${secs} s)`);
ok(leds()[ex.web_button_id(10)] === 1, "RELAX: PLAY's LED lit");

const draws = ex.web_screen_draws();
ex.web_enc(3, 2);                            // KNOB 1
render(20);
ok(ex.web_screen_draws() > draws, "a knob turned redraws");

const t0 = performance.now();
render(44100 / N * 5);                       // 5 s of audio with the main loop, the drone sounding
const rt = 5000 / (performance.now() - t0);
ok(rt > 20, `5 s rendered at ${rt.toFixed(0)}x real time (> 20x)`);

ex.web_buttons(1 << 10);                     // PLAY: ENOUGH!
render(20);
ex.web_buttons(0);
render(40);
ok(render(20) === 0 && leds()[ex.web_button_id(10)] === 0, "ENOUGH!: silent, PLAY's LED dark");
ex.web_midi(0x90, 69, 100);
ok(render(50) === 0, "MIDI in: ignored (no MIDI in)");

console.log(failed ? `EMU TEST FAILED (${failed})` : "emu test passed");
process.exit(failed ? 1 : 0);
