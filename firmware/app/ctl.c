/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois and Brad Garton */
/* The panel's MIDI out (docs/SPEC.md "MIDI"): every control the player changes goes out over USB on channel 1 (with
 * MIDI OUT on), so a DAW can record it and synthaccess's MIDItoSpeech can say it (docs/synthaccess/, generated from
 * the table below by tools/gen_synthaccess.c):
 *   CC 7      LEVEL 0..100 %             (v = level x 127 / 100)
 *   NRPN 0:76 VIB RATE 0.00..10.00 Hz    (14-bit: v = step x 16383 / 200; 201 steps do not fit in 7 bits)
 *   CC 77     VIB WIDTH 0.0..10.0 %      (v = step x 127 / 100)
 *   CC 78     SPREAD 0.0..10.0 %         (the same)
 *   CC 80     MODE                       (0 ORIGINAL, 127 INTENDED)
 *   CC 81     KEY LEDS                   (0 off, 127 on)
 *   CC 109    RELAX / ENOUGH!            (127 looching, 0 stopped)
 *   PC 0..4   the tone row
 * The main loop (ctl_frame) compares what the panel shows with what it last sent and queues the difference; the
 * audio ISR sends the queue after its notes (ctl_drain: MIDI out from one context). Nothing is sent at power-on, and
 * changes made while MIDI OUT is off are not sent later. The drone's notes are the ISR's own (sound.c). */
#define CTL_CH 0u                /* channel 1 */
#define CTLQ 32u                 /* queued messages (an NRPN is four) */

enum { C_LEVEL, C_RATE, C_WIDTH, C_SPREAD, C_MODE, C_LEDS, C_RUN, C_ROW, NCTL };
enum { CK_CC, CK_NRPN, CK_PC };
static const struct { uint8_t kind, num; } CMAP[NCTL] = {
    {CK_CC, 7}, {CK_NRPN, 76}, {CK_CC, 77}, {CK_CC, 78}, {CK_CC, 80}, {CK_CC, 81}, {CK_CC, 109}, {CK_PC, 0},
};

static struct {
    uint32_t q[CTLQ];            /* status | d1 << 8 | d2 << 16 */
    volatile uint32_t w, r;      /* main loop writes w, the ISR r */
    int32_t sent[NCTL];          /* the panel value last sent (or passed over) */
    uint8_t valid;
} ctl;

static int32_t ctl_panel(uint32_t k)             /* what the panel shows for control k */
{
    switch (k) {
    case C_LEVEL: return set.level;
    case C_RATE: return set.vib_rate;
    case C_WIDTH: return set.vib_width;
    case C_SPREAD: return set.spread;
    case C_MODE: return set.mode;
    case C_LEDS: return set.leds;
    case C_RUN: return snd.pub_running;
    default: return set.row;
    }
}

static uint32_t ctl_value(uint32_t k, int32_t p)   /* a panel value -> the value sent (7 bits; NRPN: 14) */
{
    switch (k) {
    case C_LEVEL: return ((uint32_t)p * 127u + 50u) / 100u;
    case C_RATE: return ((uint32_t)p * 16383u + 100u) / 200u;
    case C_WIDTH: case C_SPREAD: return ((uint32_t)p * 127u + 50u) / 100u;
    case C_MODE: case C_LEDS: case C_RUN: return p ? 127u : 0u;
    default: return (uint32_t)p;
    }
}

static void ctl_push(uint32_t status, uint32_t d1, uint32_t d2)
{
    ctl.q[ctl.w % CTLQ] = status | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu) << 16;
    ctl.w++;
}

static void ctl_frame(void)                      /* main loop: queue what changed */
{
    uint32_t k;
    for (k = 0; k < NCTL; k++) {
        int32_t p = ctl_panel(k);
        uint32_t need = CMAP[k].kind == CK_NRPN ? 4u : 1u, v;
        if (ctl.valid && p == ctl.sent[k])
            continue;
        if (!ctl.valid || !set.midi_out) {      /* power-on, or MIDI OUT off: nothing sent, now or later */
            ctl.sent[k] = p;
            continue;
        }
        if (CTLQ - (ctl.w - ctl.r) < need)       /* full: next frame */
            continue;
        v = ctl_value(k, p);
        if (CMAP[k].kind == CK_CC)
            ctl_push(0xB0u | CTL_CH, CMAP[k].num, v);
        else if (CMAP[k].kind == CK_PC)
            ctl_push(0xC0u | CTL_CH, v, 0);
        else {                                   /* NRPN 0:num, data entry MSB, LSB: all four in one go */
            ctl_push(0xB0u | CTL_CH, 99, 0);
            ctl_push(0xB0u | CTL_CH, 98, CMAP[k].num);
            ctl_push(0xB0u | CTL_CH, 6, v >> 7);
            ctl_push(0xB0u | CTL_CH, 38, v & 0x7Fu);
        }
        ctl.sent[k] = p;
    }
    ctl.valid = 1;
}

static void ctl_drain(void)                      /* audio ISR, after the notes: the queue out */
{
    while (ctl.r != ctl.w) {
        uint32_t m = ctl.q[ctl.r % CTLQ];
        midi_out(m & 0xFFu, (m >> 8) & 0x7Fu, (m >> 16) & 0x7Fu);
        ctl.r++;
    }
}
