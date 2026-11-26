/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois and Brad Garton */
/* Looching's settings (docs/SPEC.md "What is stored"): the original window's controls (tone row, the three slider
 * limits) plus the FM-1's own (level, ORIGINAL / INTENDED, MIDI out, key LEDs), one record in store slot 0. The
 * generated data (tools/gen_looch.py: the waves, the pitch rows, the icon) comes in here too, for everything below. */
#include "looch_data.h"

enum { MODE_ORIGINAL, MODE_INTENDED, NMODES };

/* the stored record: append-only (new fields at the end; a shorter record leaves them at their defaults) */
typedef struct {
    uint8_t ver;                 /* SET_VER: the layout this record was written with */
    uint8_t row;                 /* tone row 0..4 (Looching.m trOriginal .. trBach) */
    uint8_t mode;                /* MODE_ORIGINAL / MODE_INTENDED */
    uint8_t midi_out;            /* 0 / 1: the drone's notes out over USB */
    uint8_t leds;                /* 0 / 1: the key LEDs follow the voices */
    uint8_t level;               /* 0..100 % */
    uint16_t vib_rate;           /* vibrato rate limit, 0.05 Hz steps: 0..200 = 0.00..10.00 Hz (the original: 0.5) */
    uint16_t vib_width;          /* vibrato width limit, 0.1 % steps: 0..100 = 0.0..10.0 % (the original: 1.0) */
    uint16_t spread;             /* frequency spread, 0.1 % steps: 0..100 = 0.0..10.0 % (the original: 1.0) */
} settings_t;

#define SET_VER 1u
#define SET_SLOT 35u             /* the last slot: clear of Solinaesque (0 settings, 1..32 presets) on a shared unit */
#define VIB_RATE_MAX 200
#define PCT_MAX 100

static settings_t set;

static const char *const ROW_NAME[LW_ROWS] = {"the Original", "morning stuff", "the Big Drone", "Schoenberg", "Bach"};

static void set_defaults(void)                   /* the original's start values (Looching.m setIn...: 0.5, 1.0, 1.0) */
{
    set.ver = SET_VER;
    set.row = 0;
    set.mode = MODE_ORIGINAL;
    set.midi_out = 1;
    set.leds = 1;
    set.level = 80;
    set.vib_rate = 10;
    set.vib_width = 10;
    set.spread = 10;
}

static void set_clamp(void)                      /* never trust what was stored (another version, a damaged copy) */
{
    set.ver = SET_VER;
    if (set.row >= LW_ROWS)
        set.row = 0;
    if (set.mode >= NMODES)
        set.mode = MODE_ORIGINAL;
    set.midi_out = set.midi_out ? 1 : 0;
    set.leds = set.leds ? 1 : 0;
    if (set.level > 100)
        set.level = 100;
    if (set.vib_rate > VIB_RATE_MAX)
        set.vib_rate = VIB_RATE_MAX;
    if (set.vib_width > PCT_MAX)
        set.vib_width = PCT_MAX;
    if (set.spread > PCT_MAX)
        set.spread = PCT_MAX;
}

static void data_load(void)                      /* defaults, then the record over them */
{
    settings_t s;
    int32_t n;
    set_defaults();
    n = store_load(SET_SLOT, APP_ID, &s, sizeof s);
    if (n >= 2) {                                /* at least ver and row: what is there replaces the defaults */
        memcpy(&set, &s, (uint32_t)n < sizeof set ? (uint32_t)n : sizeof set);
        set_clamp();
    }
}

static int data_save(void) { return store_save(SET_SLOT, APP_ID, &set, sizeof set); }   /* 0 = saved */

/* a fixed-point number with its leading zero ("0.50", "0.5": the platform's fmt_fix writes ".50") */
static void fmt_dec(char *b, int32_t v, int decimals)
{
    int32_t one = decimals == 1 ? 10 : decimals == 2 ? 100 : 1;
    if (decimals && v >= 0 && v < one) {
        b[0] = '0';
        fmt_fix(b + 1, v, decimals);
    } else
        fmt_fix(b, v, decimals);
}

/* the three slider values as text ("0.50 Hz", "1.0 %") */
static void fmt_rate(char *b, uint32_t v)        /* 0.05 Hz steps -> "x.xx Hz" */
{
    fmt_dec(b, (int32_t)v * 5, 2);
    str_cpy(b + str_len(b), " Hz", 8);
}

static void fmt_pct(char *b, uint32_t v)         /* 0.1 % steps -> "x.x %" */
{
    fmt_dec(b, (int32_t)v, 1);
    str_cpy(b + str_len(b), " %", 8);
}
