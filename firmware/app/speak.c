/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois and Brad Garton */
/* What the screen reader says (main loop, after ui_frame, only while a reader listens: sr.c). Each frame compares
 * what the screen shows with the last frame and says what changed, in full words (fm1-reader's RULES.md: no
 * abbreviations or symbols, the important thing first, short):
 *   a page          the whole page (MAIN: transport, tone row, the three limits, mode, level, the voices sounding)
 *   RELAX / ENOUGH! "relax, looching" / "enough, stopped" at once
 *   a control       its name and value when it settles ("vibrato rate 0.55 hertz", "tone row, Schoenberg")
 *   a note          on MAIN, a voice starting a note: "voice 2, 266.6 hertz" (queued: it never cuts the player off;
 *                   silent notes are not said)
 *   a message       the status strip's note ("saved", "coming soon, density"); a failure as a warning (KEEP)
 *   WHERE           the whole page */
#define SPK_LEN 420u             /* a whole readout (sr_say_long cuts it into SAYs) */

static void spk_cat(char *d, unsigned max, const char *s) { str_cpy(d + str_len(d), s, max - str_len(d)); }

static const char *spk_ver(void)                 /* "v0.1" -> "0.1" ("dev" stays) */
{
    const char *v = APP_VERSION;
    return v[0] == 'v' ? v + 1 : v;
}

static int spk_starts(const char *s, const char *p)   /* s begins with p */
{
    while (*p)
        if (*s++ != *p++)
            return 0;
    return 1;
}

static void spk_hz(char *d, unsigned max, uint32_t fc)   /* centi-Hz -> "266.6 hertz" */
{
    char b[16];
    fmt_dec(b, (int32_t)(fc / 10u), 1);
    spk_cat(d, max, b);
    spk_cat(d, max, " hertz");
}

static void spk_rate(char *d, unsigned max)      /* "vibrato rate 0.50 hertz" */
{
    char b[16];
    fmt_rate(b, set.vib_rate);
    b[str_len(b) - 3u] = 0;                      /* (" Hz" off) */
    spk_cat(d, max, "vibrato rate ");
    spk_cat(d, max, b);
    spk_cat(d, max, " hertz");
}

static void spk_pct(char *d, unsigned max, const char *name, uint32_t v)   /* 0.1 % steps: "spread 1.0 percent" */
{
    char b[16];
    fmt_dec(b, (int32_t)v, 1);
    spk_cat(d, max, name);
    spk_cat(d, max, " ");
    spk_cat(d, max, b);
    spk_cat(d, max, " percent");
}

static void spk_level(char *d, unsigned max)
{
    char b[8];
    fmt_int(b, set.level);
    spk_cat(d, max, "level ");
    spk_cat(d, max, b);
    spk_cat(d, max, " percent");
}

static void spk_row(char *d, unsigned max)
{
    spk_cat(d, max, "tone row, ");
    spk_cat(d, max, ROW_NAME[set.row % LW_ROWS]);
}

static const char *spk_mode(void) { return set.mode == MODE_INTENDED ? "intended mode" : "original mode"; }

/* the status strip's note, spoken: small letters, "SOON:" as "coming soon,", "/" as "and", no brackets */
static void spk_msg(char *d, unsigned max, const char *m)
{
    char w[64];
    uint32_t i, n = 0;
    if (spk_starts(m, "SOON: ")) {
        spk_cat(d, max, "coming soon, ");
        m += 6;
    }
    if (spk_starts(m, APP_NAME " ")) {   /* the power-on note: "looching 2026, version 0.1" */
        spk_cat(d, max, "looching 2026, version ");
        spk_cat(d, max, spk_ver());
        return;
    }
    for (i = 0; m[i] && n + 5u < sizeof w; i++) {
        char c = m[i];
        if (c == '/') {
            w[n++] = 'a', w[n++] = 'n', w[n++] = 'd';
            continue;
        }
        if (c == '(')
            c = ',';
        else if (c == ')' || c == ':')
            continue;
        else if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c == ',' && n && w[n - 1u] == ' ')   /* "defaults (not" -> "defaults, not" */
            w[n - 1u] = ',', c = ' ';
        w[n++] = c;
    }
    w[n] = 0;
    spk_cat(d, max, w);
}

/* ------------------------------------------------------------ the pages --- */
static void spk_main(char *d, unsigned max)
{
    uint32_t j, any = 0;
    spk_cat(d, max, snd.pub_running ? "looching, playing. " : "looching, stopped. ");
    spk_row(d, max);
    spk_cat(d, max, ". ");
    spk_rate(d, max);
    spk_cat(d, max, ", ");
    spk_pct(d, max, "vibrato width", set.vib_width);
    spk_cat(d, max, ", ");
    spk_pct(d, max, "spread", set.spread);
    spk_cat(d, max, ". ");
    spk_cat(d, max, spk_mode());
    spk_cat(d, max, ", ");
    spk_level(d, max);
    spk_cat(d, max, ".");
    for (j = 0; j < NV; j++)
        if (snd.pub_f0c[j] && snd.pub_meter[j]) {
            spk_cat(d, max, any ? ", " : " voices sounding: ");
            spk_hz(d, max, snd.pub_f0c[j]);
            any = 1;
        }
    spk_cat(d, max, any ? "." : " no voices sounding.");
}

static void spk_glo(char *d, unsigned max)
{
    spk_cat(d, max, "preferences. mode ");
    spk_cat(d, max, set.mode == MODE_INTENDED ? "intended" : "original");
    spk_cat(d, max, set.midi_out ? ", MIDI out on" : ", MIDI out off");
    spk_cat(d, max, set.leds ? ", key lights on, " : ", key lights off, ");
    spk_level(d, max);
    spk_cat(d, max, set.mode == MODE_INTENDED
                        ? ". intended: what the code meant. delays of 500, 85, 40 and 5 samples, even detune, only "
                          "the row's own pitches."
                        : ". original: what the NeXT did. one 500 sample delay, lopsided detune, stray and silent "
                          "notes.");
}

static void spk_info(char *d, unsigned max)
{
    spk_cat(d, max, "info. Looching, from Brad Garton, relaxing sounds. this is not the Looch. living or dead, that's "
                    "the big question. FM-1 version ");
    spk_cat(d, max, spk_ver());
    spk_cat(d, max, ". R. Luke DuBois and Brad Garton, after Looching 2.0 for NeXT, 1991.");
}

static void spk_screen(char *d, unsigned max)    /* the whole page */
{
    d[0] = 0;
    if (ui.page == PG_GLO)
        spk_glo(d, max);
    else if (ui.page == PG_INFO)
        spk_info(d, max);
    else
        spk_main(d, max);
}

/* ------------------------------------------------------------ what changed --- */
static struct {
    uint8_t page, row, mode, level, midi_out, leds, running;
    uint16_t rate, width, spread;
    uint32_t msg_until;
    uint32_t f0c[NV];
    uint8_t valid;
} sk;

static void speak_frame(void)
{
    char d[SPK_LEN];
    uint32_t now = fm1_ms, j;
    if (!sr_on()) {
        sk.valid = 0;
        sr_tick(now);
        return;
    }
    d[0] = 0;
    if (sr_where() || !sk.valid) {               /* a new reader, or WHERE: the whole page */
        spk_screen(d, sizeof d);
        sr_say_long(d, SR_NOW);
    } else {
        if (ui.msg_until != sk.msg_until && ui.msg && (int32_t)(ui.msg_until - now) > 0) {   /* a new note */
            uint32_t n = str_len(ui.msg);
            spk_msg(d, sizeof d, ui.msg);
            sr_say(d, SR_NOW | (n >= 6u && str_eq(ui.msg + n - 6u, "FAILED") ? SR_KEEP : 0u));
            d[0] = 0;
        }
        if (snd.pub_running != sk.running)       /* RELAX / ENOUGH!: at once */
            sr_say(snd.pub_running ? "relax, looching" : "enough, stopped", SR_NOW);
        if (ui.page != sk.page) {                /* another page */
            spk_screen(d, sizeof d);
            if (str_len(d) <= SR_MAX)
                sr_later(d, SR_NOW, 200u, now);
            else
                sr_say_long(d, SR_NOW);
        } else {                                 /* a control: said when it settles (the newest replaces the last) */
            if (set.row != sk.row)
                spk_row(d, sizeof d);
            else if (set.vib_rate != sk.rate)
                spk_rate(d, sizeof d);
            else if (set.vib_width != sk.width)
                spk_pct(d, sizeof d, "vibrato width", set.vib_width);
            else if (set.spread != sk.spread)
                spk_pct(d, sizeof d, "spread", set.spread);
            else if (set.mode != sk.mode)
                spk_cat(d, sizeof d, spk_mode());
            else if (set.level != sk.level)
                spk_level(d, sizeof d);
            else if (set.midi_out != sk.midi_out)
                spk_cat(d, sizeof d, set.midi_out ? "MIDI out on" : "MIDI out off");
            else if (set.leds != sk.leds)
                spk_cat(d, sizeof d, set.leds ? "key lights on" : "key lights off");
            if (d[0])
                sr_later(d, SR_NOW, 120u, now);
            if (ui.page == PG_MAIN)              /* a voice starts a note: queued, never cutting in */
                for (j = 0; j < NV; j++)
                    if (snd.pub_f0c[j] && snd.pub_f0c[j] != sk.f0c[j]) {
                        char v[40] = "voice ";
                        char n[4];
                        fmt_int(n, (int32_t)j + 1);
                        spk_cat(v, sizeof v, n);
                        spk_cat(v, sizeof v, ", ");
                        spk_hz(v, sizeof v, snd.pub_f0c[j]);
                        sr_say(v, 0);
                    }
        }
    }
    sk.page = ui.page;
    sk.row = set.row;
    sk.rate = set.vib_rate;
    sk.width = set.vib_width;
    sk.spread = set.spread;
    sk.mode = set.mode;
    sk.level = set.level;
    sk.midi_out = set.midi_out;
    sk.leds = set.leds;
    sk.running = snd.pub_running;
    sk.msg_until = ui.msg_until;
    for (j = 0; j < NV; j++)
        sk.f0c[j] = snd.pub_f0c[j];
    sk.valid = 1;
    sr_tick(now);
}
