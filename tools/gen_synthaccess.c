/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois and Brad Garton */
/* The MIDItoSpeech sidecar (IDMNYU/synthaccess, MIDItoSpeech/devices, a JSON file) for what Looching 2026 SENDS over
 * USB-MIDI on channel 1 (firmware/app/ctl.c), generated from the firmware's own map and texts so it never drifts:
 *   percent      LEVEL (CC 7)
 *   floatmap     the three limits, linear, spoken with the screen's decimals ("precision"): VIB RATE as 14-bit NRPN
 *                0:76 ("hires"), VIB WIDTH and SPREAD as CC 77 / 78
 *   enumsplit    MODE (CC 80)
 *   offon        KEY LEDS (CC 81), RELAX / ENOUGH! (CC 109: "looching on / off")
 *   presets      the tone rows, Program Change 0..4
 *   cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/gen_synthaccess tools/gen_synthaccess.c -lm
 *   build/host/gen_synthaccess > docs/synthaccess/Looching2026.json   (tests/synthaccess_check.py checks it)
 *   build/host/gen_synthaccess --pairs   every control at every panel step: what the FM-1 sends, what it means */
#include "host_platform.h"

static const struct { uint32_t k; const char *label; } LABEL[] = {
    {C_LEVEL, "level"},        {C_RATE, "vibrato rate limit"}, {C_WIDTH, "vibrato width limit"},
    {C_SPREAD, "frequency spread"}, {C_MODE, "mode"},       {C_LEDS, "key lights"},
    {C_RUN, "looching"},
};

static const char *label_of(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < sizeof LABEL / sizeof LABEL[0]; i++)
        if (LABEL[i].k == k)
            return LABEL[i].label;
    return "";
}

static int32_t panel_max(uint32_t k)             /* each control's last panel step */
{
    switch (k) {
    case C_LEVEL: return 100;
    case C_RATE: return VIB_RATE_MAX;
    case C_WIDTH: case C_SPREAD: return PCT_MAX;
    case C_ROW: return LW_ROWS - 1;
    default: return 1;
    }
}

static void meaning(char *b, uint32_t n, uint32_t k, int32_t p)   /* what panel step p means, as the screen says it */
{
    char t[16];
    switch (k) {
    case C_LEVEL: fmt_int(b, p); break;
    case C_RATE:
        fmt_rate(t, (uint32_t)p);
        t[str_len(t) - 3u] = 0;                  /* (" Hz" off) */
        str_cpy(b, t, n);
        break;
    case C_WIDTH: case C_SPREAD: fmt_dec(b, p, 1); break;
    case C_MODE: str_cpy(b, p ? "intended" : "original", n); break;
    case C_ROW: str_cpy(b, ROW_NAME[p], n); break;
    default: str_cpy(b, p ? "on" : "off", n); break;
    }
}

static void entry(uint32_t k, int last)
{
    printf("      \"%u\": {\n        \"label\": \"%s\"", CMAP[k].num, label_of(k));
    switch (k) {
    case C_LEVEL: printf(",\n        \"data\": \"percent\""); break;
    case C_RATE:
        printf(",\n        \"hires\": \"true\",\n        \"data\": \"floatmap\",\n        \"map\": [0, 16383, 0, 10],\n"
               "        \"clamp\": \"true\",\n        \"precision\": 2,\n        \"suffix\": \"hertz\"");
        break;
    case C_WIDTH: case C_SPREAD:
        printf(",\n        \"data\": \"floatmap\",\n        \"map\": [0, 127, 0, 10],\n        \"clamp\": \"true\",\n"
               "        \"precision\": 1,\n        \"suffix\": \"percent\"");
        break;
    case C_MODE:
        printf(",\n        \"data\": \"enumsplit\",\n        \"split\": [0, %u],\n        \"enum\": [\"original\", \"intended\"]",
               (ctl_value(C_MODE, 0) + ctl_value(C_MODE, 1) + 1u) / 2u);
        break;
    default: printf(",\n        \"data\": \"offon\""); break;
    }
    printf("\n      }%s\n", last ? "" : ",");
}

static void pairs(void)                          /* "CC|NRPN|PC <key> <sent> <mode> <meaning>" */
{
    static const char *const KIND[3] = {"CC", "NRPN", "PC"};
    uint32_t k;
    char b[32];
    for (k = 0; k < NCTL; k++) {
        int32_t p;
        for (p = 0; p <= panel_max(k); p++) {
            const char *mode = k == C_LEVEL ? "percent" : k == C_RATE || k == C_WIDTH || k == C_SPREAD ? "floatmap"
                             : k == C_MODE || k == C_ROW ? "enum" : "offon";
            meaning(b, sizeof b, k, p);
            printf("%s %u %u %s %s\n", KIND[CMAP[k].kind], CMAP[k].num, ctl_value(k, p), mode, b);
        }
    }
}

int main(int argc, char **argv)
{
    uint32_t k, n_cc = 0, i, r;
    host_boot();
    if (argc > 1 && !strcmp(argv[1], "--pairs")) {
        pairs();
        return 0;
    }
    printf("{\n  \"device\": {\n");
    printf("    \"name\": \"Looching 2026\",\n");
    printf("    \"manufacturer\": \"R. Luke DuBois and Brad Garton (custom firmware for the M-VAVE FM-1)\",\n");
    printf("    \"language\": \"en-US\",\n    \"version\": 1,\n");
    printf("    \"program_change\": {\n      \"0\": {\n        \"label\": \"tone row\",\n        \"data\": \"enum\",\n"
           "        \"enum\": \"presets\"\n      }\n    },\n");
    printf("    \"CC\": {\n");
    for (k = 0; k < NCTL; k++)
        n_cc += CMAP[k].kind == CK_CC;
    for (k = 0, i = 0; k < NCTL; k++)
        if (CMAP[k].kind == CK_CC)
            entry(k, ++i == n_cc);
    printf("    },\n    \"NRPN\": {\n");
    entry(C_RATE, 1);                            /* NRPN 0:76: its key is MSB x 128 + LSB = 76 */
    printf("    },\n    \"presets\": [");
    for (r = 0; r < LW_ROWS; r++)
        printf("%s\"%s\"", r ? ", " : "", ROW_NAME[r]);
    printf("]\n  }\n}\n");
    return 0;
}
