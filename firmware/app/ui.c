/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois and Brad Garton */
/* The panel (docs/SPEC.md "Controls", "Screens"): the controls, the LEDs, and three pages in a NeXTSTEP homage: MAIN
 * (the original's Looching window: the dog, RELAX / ENOUGH!, the tone rows as radio buttons, three sliders, four
 * voice meters), GLO (a preferences panel), INFO (the original's Info panel). NeXT's four greys, fixed: the look is
 * the point, so no theme palette. Every area is drawn lazily: a signature of what it shows, redrawn on a change or
 * when the platform drew over the screen (app_redraw). */
#define NX_BLACK RGB(0, 0, 0)
#define NX_DARK RGB(85, 85, 85)
#define NX_LIGHT RGB(170, 170, 170)
#define NX_WHITE RGB(255, 255, 255)

enum { PG_MAIN, PG_GLO, PG_INFO, NPAGES };
enum { A_TITLE, A_TOP, A_MID, A_METERS, A_STATUS, NAREAS };   /* MAIN's areas (GLO, INFO: A_TITLE, A_TOP, A_MID) */

#define HOLD_MS 500u             /* a hold: HOME (INFO), SAVE (the defaults) */
#define MSG_MS 1500u             /* how long a note stays in the status strip */
#define METER_FRAMES 6u          /* the meters redraw at most every 6 frames (~10 a second) */
#define KEY_FLASH_MS 150u

static struct {
    uint8_t page;
    uint32_t last_buttons;
    uint32_t down_ms[NB];        /* when a button went down */
    uint8_t hold_done[NB];       /* its hold already acted (the release does nothing then) */
    const char *msg;             /* the status strip's note, until msg_until */
    uint32_t msg_until;
    int8_t saved;                /* the last SAVE: 1 saved, -1 failed (for the tests) */
    uint32_t frames;
    int32_t master_q8;           /* the MASTER pot, 0..256 */
    uint32_t key_flash_until;    /* a key pressed: its LED flashes */
    uint32_t key_flash_mask;
    uint32_t sig[NAREAS];        /* what each area shows now */
    uint8_t drawn_page;
} ui = {.master_q8 = 256};

/* the planned extras (docs/SPEC.md "Out of scope for v1") behind their buttons */
static const char *soon_of(uint32_t b)
{
    switch (b) {
    case B_FX: return "SOON: DELAY MIX";
    case B_SCL: return "SOON: NOTE LENGTH";
    case B_ENV: return "SOON: ATTACK / RELEASE";
    case B_LFO: return "SOON: WAKE-UP TIME";
    case B_SEQ: return "SOON: DENSITY";
    case B_ARP: return "SOON: NEW TONE ROWS";
    case B_REC: return "SOON: NEW TIMBRES";
    case B_OCTDN: case B_OCTUP: return "SOON: TRANSPOSE";
    default: return 0;
    }
}

static void msg(const char *s)
{
    ui.msg = s;
    ui.msg_until = fm1_ms + MSG_MS;
}

static uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* ------------------------------------------------------------- drawing --- */
static void nx_bevel(int32_t x, int32_t y, int32_t w, int32_t h, int in)   /* a NeXT button (in: pressed / sunken) */
{
    uint16_t tl = in ? NX_BLACK : NX_WHITE, br = in ? NX_WHITE : NX_BLACK;
    cv_rect(x, y, w, h, in ? NX_DARK : NX_LIGHT);
    cv_rect(x, y, w, 1, tl);
    cv_rect(x, y, 1, h, tl);
    cv_rect(x, y + h - 1, w, 1, br);
    cv_rect(x + w - 1, y, 1, h, br);
    if (!in) {                                   /* the inner shadow of a raised button */
        cv_rect(x + 1, y + h - 2, w - 2, 1, NX_DARK);
        cv_rect(x + w - 2, y + 1, 1, h - 2, NX_DARK);
    }
}

static void nx_dog(int32_t x, int32_t y)         /* the LoochityDog icon (tools/gen_looch.py), 48 x 48 */
{
    uint16_t g[4];
    uint32_t i, j;
    g[0] = NX_WHITE;
    g[1] = NX_LIGHT;
    g[2] = NX_DARK;
    g[3] = NX_BLACK;
    for (j = 0; j < DOG_H; j++)
        for (i = 0; i < DOG_W; i++) {
            uint8_t p = DOG_PX[j * DOG_W + i];
            if (p < 4u)
                cv_rect(x + (int32_t)i, y + (int32_t)j, 1, 1, g[p]);
        }
}

#define BAND_H 18                /* MAIN's name band and the status strip: the same small band, top and bottom */
#define TOP_Y 24                 /* MAIN: the dog and RELAX / ENOUGH! (24..82) */
#define MID_Y 88                 /* MAIN: the tone rows and the sliders (88..172) */

static void draw_band(void)                      /* MAIN's name: small, white on dark grey, as the status strip */
{
    cv_begin(240, BAND_H, NX_DARK);
    cv_text_in(0, 1, 240, &AF_S, APP_NAME, NX_WHITE, NX_DARK);
    cv_blit(0, 0);
}

static void draw_title(const aafont_t *f, const char *s)   /* GLO, INFO: the black title bar, 36 px */
{
    cv_begin(240, 36, NX_BLACK);
    cv_text_in(0, (36 - (int32_t)f->h) / 2 + (f == &AF_L ? 1 : 0), 240, f, s, NX_WHITE, NX_BLACK);
    cv_blit(0, 0);
}

/* MAIN ---------------------------------------------------------------------- */
static void draw_main_top(void)                  /* y 24..82: the dog, RELAX / ENOUGH! */
{
    int on = snd.pub_running;
    cv_begin(240, 56, NX_LIGHT);
    nx_dog(8, 4);
    nx_bevel(68, 8, 164, 40, on);
    cv_text_in(68, 8 + (40 - (int32_t)AF_M.h) / 2, 164, &AF_M, on ? "ENOUGH!" : "RELAX", on ? NX_WHITE : NX_BLACK,
               on ? NX_DARK : NX_LIGHT);
    cv_blit(0, TOP_Y);
}

static void draw_slider(int32_t x, int32_t y, int32_t w, const char *label, const char *val, uint32_t v, uint32_t max)
{
    int32_t kw = 12, kx;
    cv_text_on(x, y, &AF_S, label, NX_BLACK, NX_LIGHT);
    cv_text_r(x + w, y, &AF_S, val, NX_BLACK, NX_LIGHT);
    nx_bevel(x, y + 16, w, 9, 1);                /* the well */
    kx = x + 1 + (int32_t)((uint32_t)(w - 2 - kw) * v / max);
    nx_bevel(kx, y + 15, kw, 11, 0);             /* the knob */
}

static void draw_main_mid(void)                  /* y 88..172: the tone rows, the three sliders */
{
    char b[16];
    uint32_t i;
    cv_begin(240, 84, NX_LIGHT);
    for (i = 0; i < LW_ROWS; i++) {
        int32_t y = 2 + (int32_t)i * 16;
        cv_rrect(4, y + 2, 11, 11, 5, NX_DARK, NX_LIGHT);
        cv_rrect(5, y + 3, 9, 9, 4, NX_WHITE, NX_DARK);
        if (i == set.row)
            cv_rrect(7, y + 5, 5, 5, 2, NX_BLACK, NX_WHITE);
        cv_text_on(19, y, &AF_S, ROW_NAME[i], NX_BLACK, NX_LIGHT);
    }
    fmt_rate(b, set.vib_rate);
    draw_slider(122, 0, 114, "VIB RATE", b, set.vib_rate, VIB_RATE_MAX);
    fmt_pct(b, set.vib_width);
    draw_slider(122, 28, 114, "VIB WIDTH", b, set.vib_width, PCT_MAX);
    fmt_pct(b, set.spread);
    draw_slider(122, 56, 114, "SPREAD", b, set.spread, PCT_MAX);
    cv_blit(0, MID_Y);
}

static void draw_main_meters(void)               /* y 176..222: the four voices */
{
    char b[16];
    uint32_t j;
    cv_begin(240, 46, NX_LIGHT);
    cv_rect(0, 0, 240, 1, NX_DARK);
    cv_rect(0, 1, 240, 1, NX_WHITE);
    for (j = 0; j < NV; j++) {
        int32_t x = 4 + (int32_t)j * 59, w = 54;
        uint32_t m = snd.pub_meter[j], fc = snd.pub_f0c[j];
        nx_bevel(x, 6, w, 12, 1);
        if (m)
            cv_rect(x + 1, 7, 1 + (int32_t)((uint32_t)(w - 3) * m / 255u), 10, NX_WHITE);
        if (fc) {
            fmt_fix(b, (int32_t)(fc / 10u), 1);  /* Hz to one decimal */
        } else
            str_cpy(b, "--", sizeof b);
        cv_text_in(x, 22, w, &AF_S, b, NX_BLACK, NX_LIGHT);
    }
    cv_blit(0, 176);
}

static void draw_status(void)                    /* y 222..240: the mode and level, or a note */
{
    char b[16];
    cv_begin(240, 18, NX_DARK);
    if (ui.msg && (int32_t)(ui.msg_until - fm1_ms) > 0)
        cv_text_in(0, 1, 240, &AF_S, ui.msg, NX_WHITE, NX_DARK);
    else {
        cv_text_on(6, 1, &AF_S, set.mode == MODE_INTENDED ? "INTENDED" : "ORIGINAL", NX_WHITE, NX_DARK);
        str_cpy(b, "LEVEL ", sizeof b);
        fmt_int(b + 6, set.level);
        str_cpy(b + str_len(b), " %", 4);
        cv_text_r(234, 1, &AF_S, b, NX_WHITE, NX_DARK);
    }
    cv_blit(0, 222);
}

/* GLO ----------------------------------------------------------------------- */
static void glo_row(int32_t y, const char *knob, const char *label, const char *val)
{
    cv_text_on(8, y + 8, &AF_S, knob, NX_DARK, NX_LIGHT);
    cv_text_on(34, y + 6, &AF_M, label, NX_BLACK, NX_LIGHT);
    nx_bevel(140, y + 4, 92, 24, 1);
    cv_text_in(140, y + 6, 92, &AF_M, val, NX_WHITE, NX_DARK);
}

static void draw_glo(void)
{
    char b[16];
    cv_begin(240, 124, NX_LIGHT);
    glo_row(4, "K1", "MODE", set.mode == MODE_INTENDED ? "INTENDED" : "ORIGINAL");
    glo_row(34, "K2", "MIDI OUT", set.midi_out ? "ON" : "OFF");
    glo_row(64, "K3", "KEY LEDS", set.leds ? "ON" : "OFF");
    fmt_int(b, set.level);
    str_cpy(b + str_len(b), " %", 4);
    glo_row(94, "K4", "LEVEL", b);
    cv_blit(0, 36);
    cv_begin(240, 80, NX_LIGHT);
    cv_rect(0, 2, 240, 1, NX_DARK);
    cv_rect(0, 3, 240, 1, NX_WHITE);
    if (set.mode == MODE_INTENDED) {
        cv_text_in(0, 12, 240, &AF_S, "INTENDED: what the code meant:", NX_BLACK, NX_LIGHT);
        cv_text_in(0, 28, 240, &AF_S, "delays 500/85/40/5, even detune,", NX_BLACK, NX_LIGHT);
        cv_text_in(0, 44, 240, &AF_S, "only the row's own pitches.", NX_BLACK, NX_LIGHT);
    } else {
        cv_text_in(0, 12, 240, &AF_S, "ORIGINAL: what the NeXT did:", NX_BLACK, NX_LIGHT);
        cv_text_in(0, 28, 240, &AF_S, "one 500-sample delay, lopsided", NX_BLACK, NX_LIGHT);
        cv_text_in(0, 44, 240, &AF_S, "detune, stray and silent notes.", NX_BLACK, NX_LIGHT);
    }
    cv_blit(0, 160);
}

/* INFO ---------------------------------------------------------------------- */
static void draw_info(void)
{
    char v[32];
    const char *ver = APP_VERSION;
    if (ver[0] == 'v')                           /* "v0.1" -> "0.1" ("dev" stays) */
        ver++;
    str_cpy(v, "FM-1 version ", sizeof v);
    str_cpy(v + str_len(v), ver, sizeof v - str_len(v));
    cv_begin(240, 124, NX_LIGHT);
    nx_dog(12, 8);
    cv_text_on(72, 8, &AF_M, "Looching", NX_BLACK, NX_LIGHT);
    cv_text_on(72, 30, &AF_S, "from Brad Garton", NX_BLACK, NX_LIGHT);
    cv_text_on(72, 46, &AF_S, "(relaxing sounds)", NX_DARK, NX_LIGHT);
    cv_text_in(0, 66, 240, &AF_S, "(this is not the Looch)", NX_DARK, NX_LIGHT);
    nx_bevel(16, 86, 208, 36, 0);
    cv_text_in(16, 89, 208, &AF_S, "Living or dead,", NX_BLACK, NX_LIGHT);
    cv_text_in(16, 104, 208, &AF_S, "that's the big question", NX_BLACK, NX_LIGHT);
    cv_blit(0, 36);
    cv_begin(240, 80, NX_LIGHT);
    cv_rect(0, 4, 240, 1, NX_DARK);
    cv_rect(0, 5, 240, 1, NX_WHITE);
    cv_text_in(0, 12, 240, &AF_S, v, NX_BLACK, NX_LIGHT);
    cv_text_in(0, 30, 240, &AF_S, "R. Luke DuBois and Brad Garton", NX_BLACK, NX_LIGHT);
    cv_text_in(0, 48, 240, &AF_S, "after Looching 2.0 for NeXT, 1991", NX_DARK, NX_LIGHT);
    cv_blit(0, 160);
}

/* --------------------------------------------------------- signatures --- */
static uint32_t sig_area(uint32_t a)             /* every value an area shows */
{
    uint32_t h = 2166136261u, j;
    h = fnv(h, ui.page);
    switch (a) {
    case A_TOP:
        if (ui.page == PG_MAIN)
            h = fnv(h, snd.pub_running);
        break;
    case A_MID:
        if (ui.page == PG_MAIN) {
            h = fnv(h, set.row);
            h = fnv(h, set.vib_rate);
            h = fnv(h, set.vib_width);
            h = fnv(h, set.spread);
        } else if (ui.page == PG_GLO) {
            h = fnv(h, set.mode);
            h = fnv(h, set.midi_out);
            h = fnv(h, set.leds);
            h = fnv(h, set.level);
        }
        break;
    case A_METERS:
        if (ui.page == PG_MAIN)
            for (j = 0; j < NV; j++) {
                h = fnv(h, snd.pub_meter[j] >> 3);   /* 32 steps across the bar */
                h = fnv(h, snd.pub_f0c[j]);
            }
        break;
    case A_STATUS:
        if (ui.page == PG_MAIN) {
            int on = ui.msg && (int32_t)(ui.msg_until - fm1_ms) > 0;
            h = fnv(h, on ? (uint32_t)(uintptr_t)ui.msg : 0u);
            h = fnv(h, set.mode);
            h = fnv(h, set.level);
        }
        break;
    default:
        break;
    }
    return h;
}

static void ui_draw(void)                        /* every area whose signature changed (all after a page change) */
{
    uint32_t a, s;
    int all = app_redraw || ui.drawn_page != ui.page;
    if (all) {
        app_redraw = 0;
        ui.drawn_page = ui.page;
        for (a = 0; a < NAREAS; a++)
            ui.sig[a] = ~sig_area(a);
        lcd_fill(0, 0, 240, 240, NX_LIGHT);
        if (ui.page == PG_MAIN)
            draw_band();
        else
            draw_title(&AF_M, ui.page == PG_GLO ? "Preferences" : "Info");
    }
    if (ui.page == PG_MAIN) {
        if ((s = sig_area(A_TOP)) != ui.sig[A_TOP]) {
            ui.sig[A_TOP] = s;
            draw_main_top();
        }
        if ((s = sig_area(A_MID)) != ui.sig[A_MID]) {
            ui.sig[A_MID] = s;
            draw_main_mid();
        }
        if ((all || ui.frames % METER_FRAMES == 0u) && (s = sig_area(A_METERS)) != ui.sig[A_METERS]) {
            ui.sig[A_METERS] = s;
            draw_main_meters();
        }
        if ((s = sig_area(A_STATUS)) != ui.sig[A_STATUS]) {
            ui.sig[A_STATUS] = s;
            draw_status();
        }
    } else if ((s = sig_area(A_MID)) != ui.sig[A_MID]) {
        ui.sig[A_MID] = s;
        if (ui.page == PG_GLO)
            draw_glo();
        else
            draw_info();
    }
}

/* ------------------------------------------------------------ controls --- */
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void ui_knobs(void)
{
    int32_t s;
    uint32_t k;
    if ((s = panel_enc(EN_SELECT)) != 0)
        ui.page = (uint8_t)(((int32_t)ui.page + (s > 0 ? 1 : NPAGES - 1)) % NPAGES);
    if ((s = panel_enc(EN_ALGO)) != 0)
        set.mode = s > 0 ? MODE_INTENDED : MODE_ORIGINAL;
    if ((s = panel_enc(EN_PRESET)) != 0)
        set.row = (uint8_t)clampi((int32_t)set.row + s, 0, LW_ROWS - 1);
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.page == PG_GLO) {
            switch (k) {
            case 0: set.mode = s > 0 ? MODE_INTENDED : MODE_ORIGINAL; break;
            case 1: set.midi_out = s > 0; break;
            case 2: set.leds = s > 0; break;
            default: set.level = (uint8_t)clampi(set.level + s, 0, 100); break;
            }
        } else {
            switch (k) {
            case 0: set.vib_rate = (uint16_t)clampi(set.vib_rate + s, 0, VIB_RATE_MAX); break;
            case 1: set.vib_width = (uint16_t)clampi(set.vib_width + s, 0, PCT_MAX); break;
            case 2: set.spread = (uint16_t)clampi(set.spread + s, 0, PCT_MAX); break;
            default: set.level = (uint8_t)clampi(set.level + s, 0, 100); break;
            }
        }
    }
}

static void ui_save(void)
{
    ui.saved = data_save() ? -1 : 1;
    msg(ui.saved > 0 ? "SAVED" : "SAVE FAILED");
}

static void ui_buttons(void)
{
    uint32_t now = fm1_in.buttons, b, m;
    uint32_t pressed = now & ~ui.last_buttons, released = ui.last_buttons & ~now;
    ui.last_buttons = now;
    for (b = 0; b < NB; b++) {
        m = panel_mask(b);
        if (pressed & m) {
            ui.down_ms[b] = fm1_ms;
            ui.hold_done[b] = 0;
            switch (b) {
            case B_PLAY: snd.run_req = snd.pub_running ? 2 : 1; break;
            case B_HOME: ui.page = PG_MAIN; break;
            case B_EDIT: ui.page = PG_INFO; break;
            case B_GLO: ui.page = PG_GLO; break;
            case B_SAVE: break;                  /* on release (a tap) or at the hold */
            default:
                if (soon_of(b))
                    msg(soon_of(b));
                break;
            }
        } else if ((now & m) && !ui.hold_done[b] && fm1_ms - ui.down_ms[b] >= HOLD_MS) {
            ui.hold_done[b] = 1;
            if (b == B_HOME)
                ui.page = PG_INFO;
            else if (b == B_SAVE) {
                set_defaults();
                msg("DEFAULTS (NOT SAVED)");
            }
        } else if ((released & m) && b == B_SAVE && !ui.hold_done[b])
            ui_save();
    }
}

static void ui_master(void)                      /* the MASTER pot as an output level, 0..256, with LEVEL */
{
    static int32_t avg = -1;
    int32_t v;
    if ((ui.frames & 3u) == 0u && (v = fm1_adc_read(FM1_ADC_MASTER)) >= 0) {
        int32_t q;
        avg = avg < 0 ? v << 4 : avg + v - (avg >> 4);   /* x16, smoothed over ~16 reads */
        q = (avg >> 4) * 257 >> 10;              /* 0..1023 -> 0..256 */
        if (q - ui.master_q8 > 2 || ui.master_q8 - q > 2 || q == 0 || q >= 256)   /* a dead band */
            ui.master_q8 = q > 256 ? 256 : q;
    }
    snd.gain_q8 = (int32_t)set.level * 256 / 100 * ui.master_q8 >> 8;
}

static uint32_t fold_key(uint32_t n)             /* a MIDI note folded by octaves into the keys F3..G5 (53..79) */
{
    while (n > 79u)
        n -= 12u;
    while (n < 53u)
        n += 12u;
    return n - 53u;
}

static void ui_leds(void)
{
    uint32_t b, k, keys = 0, j, newk = fm1_in.notes & ~ui.key_flash_mask;
    if (newk) {                                  /* a key pressed: it plays nothing, its LED flashes */
        ui.key_flash_until = fm1_ms + KEY_FLASH_MS;
    }
    ui.key_flash_mask = fm1_in.notes;
    for (b = 0; b < NB; b++) {
        int on = (b == B_PLAY && snd.pub_running) || (b == B_HOME && ui.page == PG_MAIN) ||
                 (b == B_GLO && ui.page == PG_GLO) || (b == B_EDIT && ui.page == PG_INFO);
        fm1_led_key(panel.btn[b], on);
    }
    if (set.leds)
        for (j = 0; j < NV; j++)
            if (snd.pub_note[j] && snd.pub_meter[j] > 8u)
                keys |= 1u << fold_key(snd.pub_note[j]);
    if ((int32_t)(ui.key_flash_until - fm1_ms) > 0)
        keys |= fm1_in.notes;
    for (k = 0; k < 27u; k++)
        fm1_led_key(14u + k, (int)((keys >> k) & 1u));
}

static void ui_frame(void)
{
    ui.frames++;
    ui_buttons();
    ui_knobs();
    ui_master();
    ui_leds();
    ui_draw();
}
