/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * Copyright (C) 2026 R. Luke DuBois and Brad Garton */
/* Looching 2026: Brad Garton's NeXT drone generator Looching (1990-91) on the FM-1 (docs/SPEC.md). The app contract's
 * five hooks (app.h) and the parts, in dependency order:
 *   data.c    the settings and their record; the generated data (waves, pitch rows, the icon: tools/gen_looch.py)
 *   sound.c   the audio ISR: the four Bark voices at 22 050 Hz, the delay, the RELAX / ENOUGH! transport, MIDI out
 *   ui.c      the panel: controls, LEDs, the MAIN (Looching window), GLO and INFO pages */
#ifndef APP_NAME
#define APP_NAME "LOOCHING 2026"
#endif
#ifndef APP_VERSION
#define APP_VERSION "v0.1"       /* build.py --release X.Y */
#endif
#define APP_ID 0xBD58C3F7u       /* this app's records in the store (store.c): every app its own (setup/new_project.py
                                  * draws one), so another firmware's data in the same flash reads as empty */

#include "data.c"
#include "sound.c"
#include "ui.c"

static void app_init(void)
{
    data_load();
    msg(APP_NAME " " APP_VERSION);               /* which firmware this is, in the status strip at power-on */
    ui.msg_until = fm1_ms + 3000u;
    ui_draw();
}

static void app_frame(void) { ui_frame(); }

static const char *app_fault_name(void) { return APP_NAME " CRASH"; }
