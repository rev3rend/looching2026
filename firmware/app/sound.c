/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois and Brad Garton */
/* The audio ISR: four "Bark" voices (Bark.m, the original's MusicKit SynthPatch) rendered at the NeXT's 22 050 Hz and
 * upsampled to 44 100 (docs/SPEC.md "The sound"). Per voice:
 *   two interpolating 64-sample wavetable oscillators (OscgafiUG), both on the note's table; osc 0 at f0, osc 1 at
 *   f1 = f0 + d (the spread); each with its own sine vibrato (OscgUG -> Mul2UG -> Add2UG into the increment):
 *   rate r, depth a Hz; one asymptotic envelope for both (AsympUG: x += (target - x) k, k from a T60);
 *   left = the voice, right = the voice through a delay line (DelayUG; 500 samples in ORIGINAL, see data.c MODE_).
 * Fixed point: phases Q32 (2^32 = one table cycle), the envelope Q24 (1.0 = 2^24, the MusicKit amplitude), samples
 * Q15. Control (vibrato, envelope) runs once per block: 16 samples at 22 050 Hz = 1378.125 blocks a second.
 *
 * The scheduler (Looching.m StartAndStop, the child's loop) wakes every ranInt(2..10) seconds, counted in samples:
 * each voice's remaining time drops by the sleep; a playing voice whose time is up releases with a T60 fade of 10..20
 * s and waits that long; an idle voice whose time is up starts a note half the time (snd_note: the original's random
 * choices, in its order). ORIGINAL and INTENDED differ in the pitch choice, the detune and the timbre set (data.c
 * MODE_, docs/SPEC.md "The quirks") and the delays. */
#define LFS 22050u               /* the NeXT's rate: every internal sample is two output frames */
#define LCTL (CTL / 2u)          /* internal samples per block */
#define NV 4u
#define DLY_N 512u               /* delay ring per voice, internal samples (the longest delay: 500) */
#define ENV_ONE (1 << 24)
#define ENV_FLOOR 4096           /* below this (-72 dB of an amp 1.0 note) a release snaps to 0: exact silence */
#define FADE_STEP 10             /* the stop fade, Q8 per block: 256 / 10 = 26 blocks = ~19 ms */
#define XF_STEP 16               /* a delay length change crossfades, Q8 per block: 16 blocks = ~12 ms; = LCTL, so
                                  * the crossfade moves by 1 per internal sample within a block (xf + i) */
_Static_assert(XF_STEP == LCTL, "the crossfade steps 1 per sample");

typedef struct {
    uint32_t ph[2], inc0[2];     /* oscillator phase, base increment (Q32 per internal sample) */
    int32_t dev[2];              /* vibrato depth as an increment (a Hz) */
    uint32_t vph[2], vinc[2];    /* vibrato phase, its increment per block */
    int32_t env, target, k;      /* the envelope, Q24; k: the per-block coefficient, Q24 */
    const int16_t *tab;          /* the note's wave (LW_WAVE[set][n]) */
    uint32_t f0c;                /* the base frequency, centi-Hz (0: a silent note) */
    int32_t remain;              /* the scheduler's durations[j]: internal samples until the next decision */
    uint8_t playing;             /* between start and release (the original's isPlaying[j]) */
    uint8_t note;                /* MIDI note sent on its channel (0: none) */
} bark_t;

/* a note's values, as LoochPerformer sets them: frequencies and rates in centi-Hz, times in ms, amp Q24 */
typedef struct {
    uint32_t f0c, f1c, r0c, r1c, a0c, a1c, amp, attack_ms;
    uint8_t set, wave;
} note_t;

static struct {
    bark_t v[NV];
    uint16_t dlen[NV];           /* delay per voice, internal samples */
    uint16_t dold[NV];           /* the length before a change, crossfaded out */
    int32_t xf[NV];              /* the crossfade, Q8: 256 = done */
    int32_t sleep_left;          /* internal samples until the scheduler wakes */
    int32_t last_sleep;          /* the sleep it chose: what each voice's time drops by at the wake */
    note_t last;                 /* the last note started (for the tests) */
    uint32_t notes;              /* notes started since boot (for the tests) */
    uint16_t dpos;
    int32_t prev_l, prev_r;      /* the last internal sample (upsampling) */
    int32_t g_last;              /* the gain of the last block, Q8 (interpolated across the next) */
    int32_t fade;                /* the stop fade's attenuation, Q8: 0 = full (all-zero start), 256 = silent */
    uint32_t rng;                /* the RandomIzer: seeded at each RELAX */
    uint8_t running, stopping, set_now;   /* set_now: the timbre set loaded at RELAX (ORIGINAL keeps it) */
    volatile uint8_t run_req;    /* main loop -> ISR: 1 RELAX, 2 ENOUGH! */
    volatile uint8_t stop_req;   /* main loop -> ISR: silence at once, every note forgotten (app_audio_stop) */
    volatile int32_t gain_q8;    /* main loop -> ISR: level x MASTER, 0..256 */
    volatile uint8_t pub_running;         /* ISR -> main loop: for the screen and the LEDs */
    volatile uint8_t pub_meter[NV];       /* envelope 0..255 (255 = amp 0.1) */
    volatile uint8_t pub_note[NV];        /* the nearest MIDI note of a sounding voice, 0 = idle or silent */
    volatile uint32_t pub_f0c[NV];        /* its base frequency, centi-Hz */
} snd;

static int16_t snd_dly[NV][DLY_N] __attribute__((section(".pool")));   /* zero at boot: silence */

static const uint16_t DLY_ORIGINAL[NV] = {500, 500, 500, 500};   /* Bark.m's counter i never advanced */
static const uint16_t DLY_INTENDED[NV] = {500, 85, 40, 5};

/* centi-Hz -> increment per internal sample: f * 2^32 / 22050 (= f_c * 1947.8310...) */
static uint32_t inc_of(uint32_t fc) { return fc * 1947u + (uint32_t)(((uint64_t)fc * 54459u) >> 16); }

/* centi-Hz -> vibrato increment per block: f * 2^32 / 1378.125 / 100 */
static uint32_t vinc_of(uint32_t fc) { return fc * 31165u; }

/* T60 in ms -> the per-block coefficient, Q24: k = 1 - 0.001^(1 / blocks) ~ 6.9078 / blocks (blocks >= 1378: k < 0.5 %) */
static int32_t k_of(uint32_t ms) { return (int32_t)(84094620u / (ms ? ms : 1u)); }

/* the RandomIzer (RandomIzer.m): one stream, random() / MAXINT as a Q16 fraction */
static uint32_t rnd_u(void)                      /* 0..65535 */
{
    uint32_t x = snd.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    snd.rng = x;
    return x >> 16;
}

static uint32_t rnd_scale(uint32_t v) { return (uint32_t)(((uint64_t)v * rnd_u()) >> 16); }   /* GetNumber:v */

/* ranInt (RandomIzer.m): (int)(U (hi - lo) + lo + .5): the two end values come up half as often */
static uint32_t rnd_int(uint32_t lo, uint32_t hi) { return (rnd_u() * (hi - lo) + (lo << 16) + 32768u) >> 16; }

static uint32_t rnd_secs(uint32_t lo, uint32_t hi)   /* GetNumberRangeHi:hi Lo:lo seconds, in internal samples */
{
    return lo * LFS + rnd_scale((hi - lo) * LFS);
}

static uint32_t nearest_note(uint32_t fc)        /* MIDI note nearest to fc (centi-Hz), 0 for silence */
{
    uint32_t lo = 0, hi = 127;
    if (fc < NOTE_C[0])
        return 0;
    while (lo < hi) {                            /* the first note >= fc */
        uint32_t m = (lo + hi) >> 1;
        if (NOTE_C[m] < fc)
            lo = m + 1u;
        else
            hi = m;
    }
    if (lo && fc - NOTE_C[lo - 1u] < NOTE_C[lo] - fc)
        lo--;
    return lo;
}

static __attribute__((noinline)) void snd_midi_on(uint32_t j, uint32_t fc, uint32_t amp)
{
    uint32_t n = nearest_note(fc), vel;
    int32_t cents, bend;
    if (!set.midi_out || !n)
        return;
    /* the bend to the exact frequency: cents ~ 1731 (f - fn) / fn within half a semitone (1.5 cents off at the edge),
     * +-2 semitones = +-8192 */
    cents = ((int32_t)fc - (int32_t)NOTE_C[n]) * 1731 / (int32_t)NOTE_C[n];   /* |f - fn| < 3 % of fn: fits */
    bend = 8192 + cents * 8192 / 200;
    bend = bend < 0 ? 0 : bend > 16383 ? 16383 : bend;
    vel = amp <= (uint32_t)(ENV_ONE / 20) ? 64u : 64u + (amp - (uint32_t)(ENV_ONE / 20)) * 63u / (uint32_t)(ENV_ONE / 20);
    vel = vel > 127u ? 127u : vel;
    midi_out(0xE0u | j, (uint32_t)bend & 0x7Fu, (uint32_t)bend >> 7);
    midi_out(0x90u | j, n, vel);
    snd.v[j].note = (uint8_t)n;
}

static void snd_midi_off(uint32_t j)
{
    if (snd.v[j].note)
        midi_out(0x80u | j, snd.v[j].note, 0);
    snd.v[j].note = 0;
}

static __attribute__((noinline)) void bark_start(uint32_t j, const note_t *nt)   /* LoochPerformer startNote */
{
    bark_t *v = &snd.v[j];
    uint32_t o;
    snd_midi_off(j);
    v->tab = LW_WAVE[nt->set % LW_SETS][nt->wave % LW_WAVES];
    v->f0c = nt->f0c;
    v->inc0[0] = inc_of(nt->f0c);
    v->inc0[1] = inc_of(nt->f1c);
    v->dev[0] = (int32_t)inc_of(nt->a0c);       /* both depths in Hz of f0, as Bark.m incAtFreq:(f0 + a) - inc(f0) */
    v->dev[1] = (int32_t)inc_of(nt->a1c);
    v->vinc[0] = vinc_of(nt->r0c);
    v->vinc[1] = vinc_of(nt->r1c);
    for (o = 0; o < 2u; o++)
        v->ph[o] = v->vph[o] = 0;
    v->env = 0;                                  /* Bark.m noteOnSelf: setCurVal:0, setT60:attack */
    v->target = (int32_t)nt->amp;
    v->k = k_of(nt->attack_ms);
    v->playing = 1;
    snd.last = *nt;
    snd.notes++;
    snd.pub_f0c[j] = nt->f0c;
    snd.pub_note[j] = (uint8_t)nearest_note(nt->f0c);
    snd_midi_on(j, nt->f0c, nt->amp);
}

static __attribute__((noinline)) void bark_release(uint32_t j, uint32_t fade_ms)   /* noteUpdate: target 0, T60 */
{
    snd.v[j].target = 0;
    snd.v[j].k = k_of(fade_ms);
    snd.v[j].playing = 0;
    snd_midi_off(j);
}

static __attribute__((noinline)) void snd_silence(void)   /* every voice and the delays to zero */
{
    uint32_t j;
    for (j = 0; j < NV; j++) {
        snd_midi_off(j);
        snd.v[j].env = snd.v[j].target = 0;
        snd.v[j].playing = 0;
        snd.v[j].f0c = 0;
        snd.pub_meter[j] = snd.pub_note[j] = 0;
        snd.pub_f0c[j] = 0;
    }
    memset(snd_dly, 0, sizeof snd_dly);
    snd.prev_l = snd.prev_r = 0;
    snd.running = snd.stopping = 0;
    snd.fade = 0;                                /* (the voices are silent: the next note at full level) */
    snd.pub_running = 0;
}

static uint32_t row_pitch(uint32_t row)         /* the pitch choice (Looching.m "set the frequency") */
{
    const uint32_t *p = LW_PITCH_C[row];
    uint32_t i, n = 0, k;
    if (set.mode != MODE_INTENDED)               /* ranInt(0, toneindex): one past the row's end can come up */
        return p[rnd_int(0, LW_ROW_N[row])];
    for (i = 0; i < 14u; i++)                    /* INTENDED: uniform over the row's own non-zero pitches */
        n += p[i] != 0u;
    k = rnd_u() * n >> 16;
    for (i = 0; i < 14u; i++)
        if (p[i] && !k--)
            return p[i];
    return p[0];
}

static __attribute__((noinline)) void snd_note(uint32_t j)   /* a new note on voice j, the original's draws in order */
{
    note_t nt;
    uint32_t row = set.row % LW_ROWS, f, sv;
    int32_t d;
    nt.set = set.mode == MODE_INTENDED ? LW_ROW_SET[row] : snd.set_now;   /* ORIGINAL: the set loaded at RELAX */
    nt.wave = (uint8_t)rnd_int(0, LW_WAVES - 1u);
    f = row_pitch(row);
    nt.f0c = f;
    sv = rnd_scale(f * set.spread) / 1000u;      /* (U spread / 100) f, spread in 0.1 % */
    /* LoochPerformer setfreq:spread: GetPlusMinus:s = U 2s - s/2 (ORIGINAL), the even U 2s - s (INTENDED) */
    d = (int32_t)rnd_scale(2u * sv) - (int32_t)(set.mode == MODE_INTENDED ? sv : sv / 2u);
    nt.f1c = (uint32_t)((int32_t)f + d < 0 ? 0 : (int32_t)f + d);
    nt.amp = (uint32_t)ENV_ONE / 20u + rnd_scale((uint32_t)ENV_ONE / 20u);   /* 0.05 .. 0.10 */
    nt.r0c = rnd_scale(set.vib_rate * 5u);       /* U rate limit, 0.05 Hz steps -> centi-Hz */
    nt.a0c = rnd_scale(f * set.vib_width) / 1000u;   /* (U width / 100) f0 */
    nt.r1c = rnd_scale(set.vib_rate * 5u);
    nt.a1c = rnd_scale(f * set.vib_width) / 1000u;
    nt.attack_ms = 10000u + rnd_scale(10000u);  /* 10 .. 20 s */
    snd.v[j].remain = (int32_t)rnd_secs(5, 20); /* the note's duration */
    bark_start(j, &nt);
}

static __attribute__((noinline)) void snd_wake(void)   /* one pass of the original's loop, then its sleep */
{
    uint32_t j;
    for (j = 0; j < NV; j++) {
        bark_t *v = &snd.v[j];
        v->remain -= snd.last_sleep;
        if (v->playing) {
            if (v->remain <= 0) {                /* the fade-out: T60 of 10 .. 20 s, then that long idle */
                uint32_t fade = rnd_secs(10, 20);
                bark_release(j, fade * 2u / 441u);   /* samples -> ms (22.05 a ms) */
                v->remain += (int32_t)fade;
            }
        } else if (v->remain <= 0 && rnd_u() < 32768u)   /* a new note? the coin */
            snd_note(j);
    }
    snd.last_sleep = (int32_t)(rnd_int(2, 10) * LFS);
    if (snd.sleep_left > 0)                      /* woken early (RELAX, the tests): the sleep counts from now */
        snd.sleep_left = 0;
    snd.sleep_left += snd.last_sleep;            /* (else: keep the block's overshoot, the timing stays exact) */
}

static __attribute__((noinline)) void snd_relax(void)   /* RELAX: Looching.m StartAndStop (the child's start) */
{
    uint32_t j;
    snd.rng = (fm1_ms * 2654435761u) ^ 0x5DEECE66u;   /* (gettimeofday's microseconds in RandomIzer.m initRan) */
    if (!snd.rng)
        snd.rng = 1;
    snd.set_now = LW_ROW_SET[set.row % LW_ROWS];  /* the timbre set: "Partials" made once per start */
    snd.running = 1;
    snd.stopping = 0;
    snd.fade = 0;
    snd.pub_running = 1;
    for (j = 0; j < NV; j++) {
        snd.v[j].remain = 0;
        snd.v[j].playing = 0;
    }
    snd.last_sleep = 0;                          /* the first pass at once (sleeptime = 0) */
    snd.sleep_left = 0;
    snd_wake();
}

static void snd_requests(void)                   /* at a block's start: what the main loop asked */
{
    uint32_t r = snd.run_req;
    if (snd.stop_req) {
        snd.stop_req = 0;
        snd.run_req = 0;
        snd_silence();
        return;
    }
    if (r) {
        snd.run_req = 0;
        if (r == 1u && !snd.running)
            snd_relax();
        else if (r == 2u && snd.running)
            snd.stopping = 1;                    /* ENOUGH!: the fade, then silence */
    }
    if (snd.stopping) {
        snd.fade += FADE_STEP;
        if (snd.fade >= 256) {
            snd_silence();
            if (set.midi_out)
                midi_out(0xB0u, 123u, 0u);       /* All Notes Off */
        }
    }
    {
        const uint16_t *d = set.mode == MODE_INTENDED ? DLY_INTENDED : DLY_ORIGINAL;
        uint32_t j;
        for (j = 0; j < NV; j++) {
            if (!snd.dlen[j]) {                  /* boot: the lengths as they are, no crossfade */
                snd.dlen[j] = snd.dold[j] = d[j];
                snd.xf[j] = 256;
            }
            if (snd.xf[j] < 256)
                snd.xf[j] += XF_STEP;
            if (snd.dlen[j] != d[j] && snd.xf[j] >= 256) {   /* a mode change: crossfade old -> new (one at a time) */
                snd.dold[j] = snd.dlen[j] ? snd.dlen[j] : d[j];
                snd.dlen[j] = d[j];
                snd.xf[j] = 0;
            }
        }
    }
    if (snd.running && !snd.stopping) {
        snd.sleep_left -= (int32_t)LCTL;
        if (snd.sleep_left <= 0)
            snd_wake();
    }
}

static __attribute__((noinline)) int32_t snd_xread(const int16_t *dly, uint32_t pos, uint32_t dl, uint32_t dlo, int32_t xf)
{                                                /* a delay tap during a length change: old and new, crossfaded */
    return (dly[(pos - dlo) & (DLY_N - 1u)] * (256 - xf) + dly[(pos - dl) & (DLY_N - 1u)] * xf) >> 8;
}

static void bark_control(bark_t *v, uint32_t j)  /* once per block: the envelope */
{
    int32_t e = v->env;
    e += (int32_t)(((int64_t)(v->target - e) * v->k) >> 24);
    if (v->target == 0 && e < ENV_FLOOR)
        e = 0;
    v->env = e;
    snd.pub_meter[j] = !v->f0c ? 0u   /* a silent note shows as idle */
                     : (uint8_t)((uint32_t)(e >> 8) * 10u >> 8 > 255u ? 255u : (uint32_t)(e >> 8) * 10u >> 8);
    if (!e && !v->playing) {
        snd.pub_note[j] = 0;
        snd.pub_f0c[j] = 0;
    }
}

static void app_audio(int32_t *out, uint32_t n)
{
    int32_t l[LCTL], r[LCTL];
    uint32_t i, j, pkt;
    int32_t g0, g1;
    while (midi_in_take(&pkt, 0))                /* no MIDI in (docs/SPEC.md): drained so the ring never fills */
        ;
    snd_requests();
    for (i = 0; i < LCTL; i++)
        l[i] = r[i] = 0;
    for (j = 0; j < NV; j++) {
        bark_t *v = &snd.v[j];
        uint32_t inc[2], o, pos = snd.dpos, dl = snd.dlen[j], dlo = snd.dold[j];
        int32_t g, xf = snd.xf[j];
        int16_t *dly = snd_dly[j];
        bark_control(v, j);
        if (!v->env || !v->tab) {                /* idle: the delay line still runs out */
            for (i = 0; i < LCTL; i++, pos++) {
                r[i] += xf >= 256 ? dly[(pos - dl) & (DLY_N - 1u)] : snd_xread(dly, pos, dl, dlo, xf + (int32_t)i);
                dly[pos & (DLY_N - 1u)] = 0;
            }
            continue;
        }
        for (o = 0; o < 2u; o++) {               /* the vibratos: the increment of this block */
            inc[o] = v->inc0[o] + (uint32_t)(int32_t)(((int64_t)v->dev[o] * LW_SINE[v->vph[o] >> 24]) >> 15);
            v->vph[o] += v->vinc[o];
        }
        g = v->env >> 8;                         /* Q16 */
        for (i = 0; i < LCTL; i++, pos++) {
            int32_t s = 0, a, b;
            uint32_t p;
            p = v->ph[0] >> 26;
            a = v->tab[p];
            b = v->tab[(p + 1u) & 63u];
            s += a + (((b - a) * (int32_t)((v->ph[0] >> 10) & 0xFFFFu)) >> 16);
            p = v->ph[1] >> 26;
            a = v->tab[p];
            b = v->tab[(p + 1u) & 63u];
            s += a + (((b - a) * (int32_t)((v->ph[1] >> 10) & 0xFFFFu)) >> 16);
            v->ph[0] += inc[0];
            v->ph[1] += inc[1];
            s = (s * g) >> 16;                   /* |s| <= 2 x 32767 x amp */
            l[i] += s;
            r[i] += xf >= 256 ? dly[(pos - dl) & (DLY_N - 1u)] : snd_xread(dly, pos, dl, dlo, xf + (int32_t)i);
            dly[pos & (DLY_N - 1u)] = (int16_t)s;
        }
    }
    snd.dpos = (uint16_t)(snd.dpos + LCTL);
    g0 = snd.g_last;
    g1 = (int32_t)snd.gain_q8 * (256 - snd.fade) >> 8;
    snd.g_last = g1;
    for (i = 0; i < LCTL && 2u * i + 1u < n; i++) {   /* upsampling: the midpoint, then the sample */
        int32_t k, ol[2], orr[2];
        ol[0] = (snd.prev_l + l[i]) >> 1;
        orr[0] = (snd.prev_r + r[i]) >> 1;
        ol[1] = l[i];
        orr[1] = r[i];
        snd.prev_l = l[i];
        snd.prev_r = r[i];
        for (k = 0; k < 2; k++) {
            uint32_t f = 2u * i + (uint32_t)k;
            int32_t g = (g0 * (int32_t)(CTL - f) + g1 * (int32_t)f) >> 5;
            int32_t sl = ol[k] * g >> 8, sr = orr[k] * g >> 8;
            out[2u * f] = sl > 32767 ? 32767 : sl < -32767 ? -32767 : sl;
            out[2u * f + 1u] = sr > 32767 ? 32767 : sr < -32767 ? -32767 : sr;
        }
    }
}

static void app_audio_stop(void) { snd.stop_req = 1; }
