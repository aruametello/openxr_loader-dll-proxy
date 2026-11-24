/*
    Copyright (C) 2022 Aruã Metello <aruametello@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

    ------------------------------------------------------------------------

    The interface: one row per device the proxy has seen, and the offsets of
    whichever device is selected. Built with CUI
    (https://github.com/yappologistic/CUI), so it needs nothing but a C
    compiler and the windowing system.

    The same code runs inside the proxy (as a thread, see xrui_launch) and on
    its own as a test program (see ui_main.c and the "ui" make target).
*/

#ifndef XRUI_H
#define XRUI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct xrui_options
    {
        const char *config_path;   /* where to load from and save to; may be NULL */
        const char *title;         /* window title, NULL for the default */
        int width, height;         /* window size, 0 for the default */
        const char *screenshot;    /* save a PNG after screenshot_frames and quit */
        int screenshot_frames;     /* frames to wait before the screenshot, 0 for 60 */
        int exit_after_ms;         /* close by itself, 0 to stay open */
        int simulate;              /* stand in for a session, for testing */
        int tab;                   /* tab shown first: 0 offsets, 1 smoothing,
                                    * 2 deadzones */
    } xrui_options;

    /* Blocks until the window is closed. Returns 0 when it ran fine. */
    int xrui_run(const xrui_options *options);

    /* Asks a running interface to close. */
    void xrui_close(void);
    /* True while an interface window is up. */
    int xrui_is_running(void);

    /* Starts xrui_run on its own thread. `config_path` is copied. Returns 0
     * when no thread could be created or one is already running. */
    int xrui_launch(const char *config_path);

#ifdef __cplusplus
}
#endif

#endif /* XRUI_H */
