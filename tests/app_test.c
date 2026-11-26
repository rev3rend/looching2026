/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * Copyright (C) 2026 R. Luke DuBois and Brad Garton */
/* Host test of Looching 2026 (firmware/app/) on the host platform (tests/host_platform.h): the three pages (drawn,
 * lazy, redrawn after the platform drew over them, the layout lint at the widest values), the controls of
 * docs/SPEC.md's table, the LEDs, the settings record (SAVE, power-on, the defaults hold, another app's data,
 * damaged and short records, no flash) and the sound (the Bark voice's pitch and level, the right channel's delay,
 * no clicks, ENOUGH! and app_audio_stop to silence, MIDI out). Screens in build/shots/. Prove each new check can
 * fail (break the code once, watch it go red).
 *   cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/app_test tests/app_test.c -lm
 *   build/host/app_test */
#include "host_platform.h"
#include <math.h>

static int failed;
static void check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    failed += !ok;
}

static void press(uint32_t b)                    /* a tap: down one frame, up the next */
{
    host_press(b, 1);
    host_frame();
    host_press(b, 0);
    host_frame();
}

static void frames(uint32_t n)
{
    while (n--)
        host_frame();
}

/* ---------------------------------------------------------------- screen --- */
static uint32_t count(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint16_t c)   /* pixels of colour c */
{
    uint32_t x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += host_fb[y * 240u + x] == c;
    return n;
}

static uint32_t bright(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)   /* text ink on a dark ground */
{
    uint32_t x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += ((host_fb[y * 240u + x] >> 5) & 63u) > 40u;   /* green above ~65 % */
    return n;
}

static void lint(const char *what)
{
    check(what, host_clipped == 0u);
    if (host_clipped)
        printf("  (%u, the last: %s)\n", host_clipped, host_clip_what);
    host_clipped = 0;
}

static void screen(void)
{
    uint32_t n0;
    host_boot();
    host_frame();
    check("boot: MAIN page", ui.page == PG_MAIN);
    check("boot: the black title bar with the name in white", count(0, 0, 240, 36, NX_BLACK) > 4000u &&
                                                                  bright(0, 0, 240, 36) > 800u);
    {   /* the large face is sparse (tools/gen_aa_font.py L_CHARS): a character it lacks draws as a space */
        const char *c = APP_NAME;
        int all = 1;
        for (; *c; c++)
            all &= glyph_at(&AF_L, (uint8_t)*c) >= 0;
        check("boot: every character of APP_NAME is in the large font", all);
    }
    check("boot: the dog icon drawn (black outline, white face)", count(8, 40, 56, 88, NX_BLACK) > 150u &&
                                                                  count(8, 40, 56, 88, NX_WHITE) > 300u);
    check("boot: RELAX raised (light face), stopped", !snd.pub_running && count(70, 46, 230, 82, NX_DARK) < 400u);
    check("boot: the name and version in the status strip", ui.msg && bright(0, 222, 240, 240) > 100u);
    host_png("build/shots/main.png");
    host_frame();
    check("lazy: nothing changed, nothing sent to the panel", host_blits == 0u);
    frames(200);                                 /* the power-on note ends (3 s) */
    check("the power-on note ends: mode and level shown", !(ui.msg && (int32_t)(ui.msg_until - fm1_ms) > 0));

    press(B_PLAY);
    check("PLAY: RELAX -> running (the ISR takes the request)", snd.run_req == 1u);
    {
        int32_t b[2 * 64];
        host_render(b, 64);
    }
    frames(1);
    check("PLAY: ENOUGH! drawn pressed (dark face)", snd.pub_running && count(70, 46, 230, 82, NX_DARK) > 3000u);
    check("PLAY: its LED lit", host_led[panel.btn[B_PLAY]]);
    host_png("build/shots/main_running.png");
    press(B_PLAY);
    check("PLAY again: ENOUGH! -> stop requested", snd.run_req == 2u);

    n0 = count(4, 92 + 2 + 16 + 4, 16, 92 + 2 + 16 + 12, NX_BLACK);
    host_turn(EN_PRESET, 1);
    host_frame();
    check("PRESETS: the next tone row (morning stuff)", set.row == 1u);
    check("PRESETS: its radio button filled", count(4, 92 + 2 + 16 + 4, 16, 92 + 2 + 16 + 12, NX_BLACK) > n0);
    host_turn(EN_PRESET, 99);
    host_frame();
    check("PRESETS: clamped at Bach", set.row == 4u);
    host_turn(EN_PRESET, -99);
    host_frame();
    check("PRESETS: clamped at the Original", set.row == 0u);

    host_turn(EN_K1, 5);
    host_turn(EN_K2, -3);
    host_turn(EN_K3, 2);
    host_turn(EN_K4, -10);
    host_frame();
    check("KNOB 1..4: rate +5, width -3, spread +2, level -10", set.vib_rate == 15u && set.vib_width == 7u &&
                                                                    set.spread == 12u && set.level == 70u);
    check("KNOB 1..4: the screen follows", host_blits > 0u);
    host_turn(EN_K1, 999);
    host_turn(EN_K2, -999);
    host_frame();
    check("KNOB 1 clamped at 10.00 Hz, KNOB 2 at 0.0 %", set.vib_rate == VIB_RATE_MAX && set.vib_width == 0u);

    host_turn(EN_ALGO, 1);
    host_frame();
    check("ALGORITHM clockwise: INTENDED", set.mode == MODE_INTENDED);
    host_turn(EN_ALGO, -1);
    host_frame();
    check("ALGORITHM counter-clockwise: ORIGINAL", set.mode == MODE_ORIGINAL);

    host_turn(EN_SELECT, 1);
    host_frame();
    check("SELECT: MAIN -> GLO", ui.page == PG_GLO);
    host_turn(EN_SELECT, 1);
    host_frame();
    check("SELECT: GLO -> INFO", ui.page == PG_INFO);
    host_turn(EN_SELECT, 1);
    host_frame();
    check("SELECT: INFO -> MAIN (wraps)", ui.page == PG_MAIN);
    host_turn(EN_SELECT, -1);
    host_frame();
    check("SELECT counter-clockwise: MAIN -> INFO", ui.page == PG_INFO);
    press(B_HOME);
    check("HOME: MAIN", ui.page == PG_MAIN && host_led[panel.btn[B_HOME]]);
    press(B_GLO);
    check("GLO: the preferences page, its LED", ui.page == PG_GLO && host_led[panel.btn[B_GLO]]);
    host_turn(EN_K1, 1);
    host_turn(EN_K2, -1);
    host_turn(EN_K3, -1);
    host_turn(EN_K4, 5);
    host_frame();
    check("GLO KNOB 1..4: mode, MIDI out, key LEDs, level", set.mode == MODE_INTENDED && !set.midi_out &&
                                                                !set.leds && set.level == 75u);
    host_png("build/shots/glo.png");
    host_turn(EN_K1, -1);
    host_turn(EN_K2, 1);
    host_turn(EN_K3, 1);
    host_frame();
    press(B_EDIT);
    check("EDIT: INFO", ui.page == PG_INFO && host_led[panel.btn[B_EDIT]]);
    check("INFO: the dog drawn", count(12, 44, 60, 92, NX_BLACK) > 150u);
    host_png("build/shots/info.png");
    press(B_HOME);
    host_press(B_HOME, 1);                       /* HOME held: INFO */
    frames(40);
    check("HOME held 0.5 s: INFO", ui.page == PG_INFO);
    host_press(B_HOME, 0);
    host_frame();
    press(B_HOME);

    press(B_SEQ);
    check("SEQ: SOON with its planned extra", ui.msg && str_eq(ui.msg, "SOON: DENSITY"));
    press(B_FX);
    check("FX: SOON: DELAY MIX", str_eq(ui.msg, "SOON: DELAY MIX"));

    memset(host_fb, 0, sizeof host_fb);          /* the platform drew over the screen (UBOOT countdown) */
    app_redraw = 1;
    host_frame();
    check("app_redraw: the whole screen drawn again", count(0, 0, 240, 36, NX_BLACK) > 4000u && !app_redraw);

    /* everything at its widest: the longest note, the biggest values, four voices with five-digit frequencies */
    set.vib_rate = VIB_RATE_MAX;
    set.vib_width = set.spread = PCT_MAX;
    set.level = 100;
    set.row = 2;
    msg("DEFAULTS (NOT SAVED)");
    {
        uint32_t j;
        for (j = 0; j < NV; j++) {
            snd.pub_meter[j] = 255;
            snd.pub_f0c[j] = 9999999u;
        }
    }
    host_clipped = 0;
    app_redraw = 1;
    host_frame();
    host_png("build/shots/widest.png");
    lint("layout MAIN: nothing cut or outside its canvas");
    msg("SOON: ATTACK / RELEASE");
    host_frame();
    lint("layout MAIN: the longest SOON note fits");
    set.mode = MODE_INTENDED;
    ui.page = PG_GLO;
    host_frame();
    lint("layout GLO (INTENDED)");
    set.mode = MODE_ORIGINAL;
    host_frame();
    lint("layout GLO (ORIGINAL)");
    ui.page = PG_INFO;
    host_frame();
    lint("layout INFO");
    ui.page = PG_MAIN;
    {
        uint32_t j;
        for (j = 0; j < NV; j++) {
            snd.pub_meter[j] = 0;
            snd.pub_f0c[j] = 0;
        }
    }
    host_frame();

    fm1_in.notes = 1u << 5;                      /* a key: plays nothing, its LED flashes */
    host_frame();
    check("a key pressed: its LED flashes", host_led[14 + 5]);
    frames(12);
    check("a key held: the flash ends", !host_led[14 + 5]);
    fm1_in.notes = 0;
    host_frame();
}

/* ---------------------------------------------------------------- flash --- */
static void power_cycle(void)                    /* the app's RAM gone, the flash kept */
{
    memset(&set, 0, sizeof set);
    memset(&ui, 0, sizeof ui);
    ui.master_q8 = 256;
    memset(&snd, 0, sizeof snd);
    host_boot();
    host_frame();
}

static void persistence(void)
{
    host_nor_erase();
    power_cycle();
    check("a fresh unit: the original's defaults (0.5 Hz, 1 %, 1 %)", set.vib_rate == 10u && set.vib_width == 10u &&
                                                                    set.spread == 10u && set.row == 0u &&
                                                                    set.mode == MODE_ORIGINAL && set.level == 80u);
    host_turn(EN_PRESET, 3);
    host_turn(EN_K1, 4);
    host_frame();
    power_cycle();
    check("not saved: power-on at the defaults", set.row == 0u && set.vib_rate == 10u);
    host_turn(EN_PRESET, 3);
    host_turn(EN_K1, 4);
    host_turn(EN_ALGO, 1);
    host_frame();
    press(B_SAVE);
    check("SAVE: stored, SAVED shown", ui.saved == 1 && host_erases > 0u && str_eq(ui.msg, "SAVED"));
    host_png("build/shots/saved.png");
    power_cycle();
    check("power-on: the saved values back", set.row == 3u && set.vib_rate == 14u && set.mode == MODE_INTENDED);
    check("power-on: stopped (no autostart)", !snd.pub_running && snd.run_req == 0u);

    host_press(B_SAVE, 1);                       /* SAVE held: the defaults, not stored */
    frames(40);
    host_press(B_SAVE, 0);
    host_frame();
    check("SAVE held: the defaults", set.row == 0u && set.vib_rate == 10u && set.mode == MODE_ORIGINAL);
    power_cycle();
    check("SAVE held: not stored (power-on: the saved values)", set.row == 3u && set.vib_rate == 14u);

    {   /* the unit ran another firmware since: its record in the same slot reads as empty */
        uint8_t other[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        host_nor_erase();
        store_save(SET_SLOT, 0x12345678u, other, sizeof other);
        power_cycle();
        check("another app's record: ignored (the defaults)", set.row == 0u && set.vib_rate == 10u);
    }
    {   /* a record of ours with values out of range (a bug, damage): clamped */
        settings_t w;
        memset(&w, 0xEE, sizeof w);
        store_save(SET_SLOT, APP_ID, &w, sizeof w);
        power_cycle();
        check("stored values out of range: clamped", set.row == 0u && set.mode == MODE_ORIGINAL &&
                                                         set.level == 100u && set.vib_rate == VIB_RATE_MAX &&
                                                         set.vib_width == PCT_MAX && set.spread == PCT_MAX &&
                                                         set.midi_out == 1u);
    }
    {   /* an older, shorter record: what it has, the rest at the defaults */
        uint8_t shortrec[3] = {1, 4, MODE_INTENDED};
        store_save(SET_SLOT, APP_ID, shortrec, sizeof shortrec);
        power_cycle();
        check("a short record: its fields loaded, the rest defaults", set.row == 4u && set.mode == MODE_INTENDED &&
                                                                         set.vib_rate == 10u && set.level == 80u);
    }
    flash_ok = 0;
    press(B_SAVE);
    check("no flash: SAVE fails visibly", ui.saved == -1 && str_eq(ui.msg, "SAVE FAILED"));
    flash_ok = 1;
    host_nor_erase();
    power_cycle();
}

/* ---------------------------------------------------------------- sound --- */
#define SECS 4u
static int32_t buf[2u * FS * SECS];

static double pitch(const int32_t *b, uint32_t frames)   /* autocorrelation of the left channel, 30..1000 Hz */
{
    uint32_t lag, i, best = 0;
    double bv = -1e30, e0 = 0;
    for (i = 0; i < frames; i++)
        e0 += (double)b[2u * i] * b[2u * i];
    if (e0 == 0)
        return 0;
    for (lag = FS / 1000u; lag < FS / 30u && lag < frames / 2u; lag++) {
        double s = 0;
        for (i = 0; i + lag < frames; i++)
            s += (double)b[2u * i] * b[2u * (i + lag)];
        s /= (double)(frames - lag);
        if (s > bv * 1.02) {                     /* the first clear maximum: not a multiple of the period */
            bv = s;
            best = lag;
        }
    }
    {   /* refine: the peak between lag - 1 .. lag + 1 (parabolic) */
        double a = 0, c = 0, m = 0;
        for (i = 0; i + best + 1u < frames; i++) {
            a += (double)b[2u * i] * b[2u * (i + best - 1u)];
            m += (double)b[2u * i] * b[2u * (i + best)];
            c += (double)b[2u * i] * b[2u * (i + best + 1u)];
        }
        return FS / (best + 0.5 * (a - c) / (a - 2 * m + c));
    }
}

static int32_t peak(const int32_t *b, uint32_t frames, uint32_t ch)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < frames; i++)
        m = abs(b[2u * i + ch]) > m ? abs(b[2u * i + ch]) : m;
    return m;
}

static int32_t max_step(const int32_t *b, uint32_t frames)   /* the largest jump between two samples: a click */
{
    int32_t m = 0;
    uint32_t i, c;
    for (c = 0; c < 2u; c++)
        for (i = 1; i < frames; i++)
            m = abs(b[2u * i + c] - b[2u * (i - 1u) + c]) > m ? abs(b[2u * i + c] - b[2u * (i - 1u) + c]) : m;
    return m;
}

static void drain_midi(void)
{
    uint32_t pkt;
    while (host_midi_out(&pkt))
        ;
}

static void relax(void)                          /* PLAY, and the ISR takes it */
{
    press(B_PLAY);
    host_render(buf, CTL);
}

static void sound(void)
{
    uint32_t pkt, i, n, ok, got_on = 0, got_bend = 0, bend = 0;
    double f;
    host_render(buf, 4096);
    check("stopped: silence", peak(buf, 4096, 0) == 0 && peak(buf, 4096, 1) == 0);
    host_midi(0x90, 60, 100);                    /* MIDI in is not used */
    host_render(buf, 4096);
    check("stopped: a MIDI note-on in plays nothing", peak(buf, 4096, 0) == 0);

    set.row = 0;                                 /* the Original's first pitch: 50 Hz; no vibrato, no spread */
    set.vib_width = 0;
    set.spread = 0;
    set.level = 100;
    host_adc[FM1_ADC_MASTER] = 1023;
    frames(400);                                 /* the MASTER pot read and smoothed (every 4th frame, ~16 reads) */
    drain_midi();
    relax();
    host_render(buf, FS / CTL * CTL);            /* 1 s of the 2 s attack */
    check("RELAX: the attack starts at 0 (no click)", max_step(buf, 4096) < 200);
    /* a T60 of 2 s: after 23 ms, 1 - 0.001^(0.023 / 2) = 8 % of the amp (~520); after 1 s, 97 % */
    check("RELAX: the asymptotic attack (8 % at 23 ms, > 90 % at 1 s)", peak(buf, 1024, 0) < 900 &&
                                                                     peak(buf + 2u * (FS - 4096u), 4096, 0) > 5600);
    host_render(buf, 3u * FS / CTL * CTL);       /* to ~-60 dB of the target */
    f = pitch(buf + 2u * FS, FS / 2u);
    check("RELAX: the Original's first pitch, 50.00 Hz (+-0.1)", fabs(f - 50.0) < 0.1);
    if (fabs(f - 50.0) >= 0.1)
        printf("  (%.3f Hz)\n", f);
    n = (uint32_t)peak(buf + 2u * FS, FS, 0);
    /* 2 oscillators x 32767 x amp 0.1 x level 1.0 x MASTER 1.0: ~6550 */
    check("level: two oscillators at amp 0.1 (6000..6600)", n > 6000u && n < 6600u);
    if (!(n > 6000u && n < 6600u))
        printf("  (peak %u)\n", n);
    ok = 1;
    for (i = 1000u; i < FS; i++)                 /* ORIGINAL: the right channel is the left 500 NeXT samples later */
        ok &= buf[2u * (FS + i) + 1u] == buf[2u * (FS + i - 1000u)];
    check("ORIGINAL: right = left delayed 500 samples at 22 050 Hz", ok);
    check("meter: voice 1 near full, the others idle", snd.pub_meter[0] > 240u && !snd.pub_meter[1] &&
                                                         snd.pub_f0c[0] == 5000u);
    while (host_midi_out(&pkt)) {
        if ((pkt >> 8 & 0xFFu) == 0x90u) {
            got_on = 1;
            n = pkt >> 16 & 0x7Fu;
        }
        if ((pkt >> 8 & 0xFFu) == 0xE0u) {
            got_bend = 1;
            bend = (pkt >> 16 & 0x7Fu) | (pkt >> 24 & 0x7Fu) << 7;
        }
    }
    check("MIDI out: note-on, channel 1, G1 (49.0 Hz, nearest to 50)", got_on && n == 31u);
    /* 50 / 49.0 = +35 cents: 8192 + 35 x 8192 / 200 ~ 9625 */
    check("MIDI out: the pitch bend +35 cents (9550..9700)", got_bend && bend > 9550u && bend < 9700u);
    frames(1);
    check("key LEDs: the voice's note folded onto the keys (G1 -> G3)", host_led[14 + (55 - 53)]);
    set.leds = 0;
    frames(1);
    check("key LEDs off (GLO): dark", !host_led[14 + (55 - 53)]);
    set.leds = 1;

    set.vib_width = 50;                          /* a new start with vibrato and spread: the pitch moves */
    set.spread = 20;
    press(B_PLAY);
    host_render(buf, 4096);
    check("ENOUGH!: a fade, no click", max_step(buf, 2048) < 600);
    check("ENOUGH!: the fade takes ~20 ms (still sounding at 10 ms)", peak(buf + 2u * 440u, 64, 0) > 0);
    check("ENOUGH!: silent within 30 ms", peak(buf + 2u * 1400u, 4096 - 1400u, 0) == 0 &&
                                              peak(buf + 2u * 1400u, 4096 - 1400u, 1) == 0 && !snd.pub_running);
    got_on = 0;
    while (host_midi_out(&pkt)) {
        if ((pkt >> 8 & 0xFFu) == 0x80u && (pkt >> 16 & 0x7Fu) == 31u)
            got_on |= 1;
        if ((pkt >> 8 & 0xFFu) == 0xB0u && (pkt >> 16 & 0x7Fu) == 123u)
            got_on |= 2;
    }
    check("ENOUGH!: MIDI note-off of G1 and All Notes Off", got_on == 3u);

    set.mode = MODE_INTENDED;                    /* voice 1's delay stays 500 in INTENDED too */
    relax();
    host_render(buf, 2u * FS / CTL * CTL);
    check("vibrato and spread: a moving, wider sound", fabs(pitch(buf + 2u * FS, FS / 4u) - 50.0) < 3.0);
    app_audio_stop();
    host_render(buf, 1024);
    check("app_audio_stop: silent at once, stopped", peak(buf, 1024, 0) == 0 && peak(buf, 1024, 1) == 0 &&
                                                         !snd.pub_running);
    set.mode = MODE_ORIGINAL;

    set.midi_out = 0;                            /* MIDI OUT off: nothing sent */
    drain_midi();
    relax();
    host_render(buf, 1024);
    check("MIDI OUT off: nothing sent", !host_midi_out(&pkt));
    app_audio_stop();
    host_render(buf, 1024);
    set.midi_out = 1;

    set.row = 1;                                 /* every row's first pitch: morning stuff 116.5 Hz */
    set.vib_width = set.spread = 0;
    relax();
    host_render(buf, 3u * FS / CTL * CTL);
    f = pitch(buf + 2u * 2u * FS, FS / 2u);
    check("morning stuff: 116.50 Hz (+-0.2)", fabs(f - 116.5) < 0.2);
    app_audio_stop();
    host_render(buf, 1024);
    set.row = 0;
}

int main(void)
{
    if (system("mkdir -p build/shots"))
        return 1;
    screen();
    persistence();
    sound();
    puts(failed ? "APP TEST FAILED" : "app test passed");
    return failed ? 1 : 0;
}
