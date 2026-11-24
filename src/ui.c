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

    The interface, drawn with CUI (https://github.com/yappologistic/CUI).

    Same two panels the raw Win32 dialog always had, one for each hand space,
    each with the six axes and their four buttons. Whatever the game mapped
    at /user/hand/left or /user/hand/right (an Index controller, a tracker,
    tracked hands) receives the offsets of its panel.

    Every button writes the configuration file right away, as before.
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "devices.h"

#include "cui/cui.h"
#include "cui/cui_app.h"

#define XRUI_WINDOW_TITLE "OpenXR controller offsets"
#define XRUI_LINE 256
#define XRUI_GROUPS 6

/* Step button width. The longest label ("+0.10") measures 32 px at the 12 px
 * small-control font, and small controls keep 11 px of inner padding on each
 * side, so 60 px is the smallest width in which the label fits and centers. */
#define XRUI_BUTTON_W 60
/* "+0.001" is wider than "+0.10", the smoothing tab has room for more. */
#define XRUI_SMOOTH_BUTTON_W 72

    /* The six axes of a panel, in the order the original dialog showed them. */
    typedef struct xrui_group
{
    const char *title; /* "Pitch" */
    bool rotation;     /* true for the three rotations, false for positions */
    int axis;          /* XRDEV_AXIS_X/Y/Z */
} xrui_group;

static const xrui_group groups[XRUI_GROUPS] = {
    {"Pitch", true, XRDEV_AXIS_X},
    {"Yaw", true, XRDEV_AXIS_Y},
    {"Roll", true, XRDEV_AXIS_Z},
    {"X pos", false, XRDEV_AXIS_X},
    {"Y pos", false, XRDEV_AXIS_Y},
    {"Z pos", false, XRDEV_AXIS_Z}};

/* The two spaces the offsets are injected into, always in this order. */
static const char *const hand_paths[2] = {XRDEV_PATH_HAND_LEFT, XRDEV_PATH_HAND_RIGHT};
static const char *const hand_titles[2] = {"left controller", "right controller"};

/* The tabs, in the order they appear. */
enum
{
    XRUI_TAB_OFFSETS = 0,
    XRUI_TAB_SMOOTHING = 1,
    XRUI_TAB_DEADZONES = 2,
    XRUI_TAB_COUNT = 3
};

    typedef struct xrui_view
{
    xrui_options options;
    int tab; /* one of XRUI_TAB_* */
} xrui_view;

static xrui_view view;
static volatile long xrui_running = 0;

    /* ----------------------------------------------------------- helpers */

/* The file we read and write, or NULL while the model has none. */
static const char *save_path(void)
{
    const char *path = xrdev_config_path();

    if (path != NULL)
        return path;

    return view.options.config_path;
}

/* The original dialog wrote the file after every single click. */
static void commit(void)
{
    if (save_path() != NULL)
        xrdev_save(save_path());
}

static double value_of(const xrdev_device *d, const xrui_group *g)
{
    if (g->rotation)
        return xrdev_rad2deg(d->rotation[g->axis]);

    return d->position[g->axis];
}

static void set_value(int device, const xrui_group *g, double value)
{
    if (g->rotation)
        xrdev_set_rotation(device, g->axis, value);
    else
        xrdev_set_position(device, g->axis, value);
}

/* ---------------------------------------------------------------- panel */

/* One axis: "Pitch: 0.00" over the four buttons, coarse column on the left
 * and fine column on the right, exactly as the Win32 dialog had them. */
static void draw_group(cui_ctx *ui, int device, const xrdev_device *d, const xrui_group *g)
{
    const xrdev_axis_info *info = g->rotation ? xrdev_axis_rotation(g->axis)
                                              : xrdev_axis_position(g->axis);
    char header[XRUI_LINE];
    char coarse_up[24];
    char coarse_down[24];
    char fine_up[24];
    char fine_down[24];
    char id[48];

    /* CUI identifies a widget by its label, and every group has a "+1.0":
     * without a scope per device and axis one click lands on all of them. */
    snprintf(id, sizeof(id), "%d:%s", device, g->title);
    cui_push_id(ui, id);

    snprintf(header, sizeof(header), "%s: %.2f", g->title, value_of(d, g));
    snprintf(coarse_up, sizeof(coarse_up), "+%.1f", info->coarse);
    snprintf(coarse_down, sizeof(coarse_down), "-%.1f", info->coarse);
    snprintf(fine_up, sizeof(fine_up), "+%.2f", info->fine);
    snprintf(fine_down, sizeof(fine_down), "-%.2f", info->fine);

    cui_column(ui, .gap = 6, .align = CUI_ALIGN_CENTER)
    {
        cui_text(ui, header, .size = 12, .tabular = true, .align = CUI_TEXT_CENTER);

        cui_box(ui, .dir = CUI_GRID, .columns = 2, .gap = 6, .align = CUI_ALIGN_CENTER)
        {
            int row;

            for (row = 0; row < 2; row++)
            {
                double coarse = row == 0 ? info->coarse : -info->coarse;
                double fine = row == 0 ? info->fine : -info->fine;

                if (cui_button(ui, row == 0 ? coarse_up : coarse_down,
                               .width = CUI_PX(XRUI_BUTTON_W), .size = CUI_SIZE_SM,
                               .tooltip = "Add or take away one whole step"))
                {
                    set_value(device, g, value_of(d, g) + coarse);
                    commit();
                }

                if (cui_button(ui, row == 0 ? fine_up : fine_down,
                               .width = CUI_PX(XRUI_BUTTON_W), .size = CUI_SIZE_SM,
                               .tooltip = "Add or take away a tenth of a step"))
                {
                    set_value(device, g, value_of(d, g) + fine);
                    commit();
                }
            }
        }
    }

    cui_pop_id(ui);
}

static void draw_panel(cui_ctx *ui, int side)
{
    xrdev_device d;
    int device;

    device = xrdev_find(hand_paths[side]);

    /* The hand spaces are always there, but be kind if they are not. */
    if (device < 0 || !xrdev_get(device, &d))
        return;

    cui_push_id(ui, hand_paths[side]);
    cui_card(ui, .id = hand_paths[side], .width = CUI_FILL, .pad = 12, .gap = 10,
             .dir = CUI_COLUMN)
    {
        int i;

        cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER)
        {
            cui_text(ui, hand_titles[side], .font = CUI_FONT_MEDIUM, .size = 14,
                     .align = CUI_TEXT_CENTER);
        }

        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_START,
                .justify = CUI_JUSTIFY_BETWEEN)
        {
            for (i = 0; i < XRUI_GROUPS; i++)
                draw_group(ui, device, &d, &groups[i]);
        }
    }
    cui_pop_id(ui);
}

/* ------------------------------------------------------------- smoothing */

/* A number with its own buttons: "Samples: 3" over "-1" and "+1", or the scale
 * over the coarse and fine steps, in the same style as the offset axes. */
static void draw_stepper(cui_ctx *ui, const char *id, const char *header,
                         double coarse, double fine, int digits,
                         double *delta_out)
{
    char down_big[24], up_big[24], down_small[24], up_small[24];

    snprintf(up_big, sizeof(up_big), "+%.*f", digits, coarse);
    snprintf(down_big, sizeof(down_big), "-%.*f", digits, coarse);
    snprintf(up_small, sizeof(up_small), "+%.*f", digits + 1, fine);
    snprintf(down_small, sizeof(down_small), "-%.*f", digits + 1, fine);
    *delta_out = 0.0;

    cui_push_id(ui, id);
    cui_column(ui, .gap = 6, .align = CUI_ALIGN_CENTER)
    {
        cui_text(ui, header, .size = 12, .tabular = true, .align = CUI_TEXT_CENTER);

        cui_box(ui, .dir = CUI_GRID, .columns = fine > 0.0 ? 2 : 1, .gap = 6,
                .align = CUI_ALIGN_CENTER)
        {
            if (cui_button(ui, up_big, .width = CUI_PX(XRUI_SMOOTH_BUTTON_W), .size = CUI_SIZE_SM))
                *delta_out += coarse;
            if (fine > 0.0 &&
                cui_button(ui, up_small, .width = CUI_PX(XRUI_SMOOTH_BUTTON_W), .size = CUI_SIZE_SM))
                *delta_out += fine;
            if (cui_button(ui, down_big, .width = CUI_PX(XRUI_SMOOTH_BUTTON_W), .size = CUI_SIZE_SM))
                *delta_out -= coarse;
            if (fine > 0.0 &&
                cui_button(ui, down_small, .width = CUI_PX(XRUI_SMOOTH_BUTTON_W), .size = CUI_SIZE_SM))
                *delta_out -= fine;
        }
    }
    cui_pop_id(ui);
}

/* One smoothing kind of one hand: the switch, the sample count and the scale. */
static void draw_smooth_section(cui_ctx *ui, int device, const xrdev_device *d, int which)
{
    const xrdev_smooth *sm = &d->smooth[which];
    const xrdev_axis_info *info = xrdev_smooth_scale_info(which);
    bool on = sm->enabled != 0;
    double shown_scale = which == XRDEV_SMOOTH_ROTATION ? xrdev_rad2deg(sm->scale) : sm->scale;
    char header[XRUI_LINE];
    double delta;

    cui_push_id(ui, which == XRDEV_SMOOTH_POSITION ? "pos" : "rot");
    cui_column(ui, .gap = 10, .align = CUI_ALIGN_START)
    {
        if (cui_checkbox(ui, which == XRDEV_SMOOTH_POSITION ? "Motion smoothing"
                                                            : "Rotation smoothing",
                         &on))
        {
            xrdev_set_smooth_enabled(device, which, on ? 1 : 0);
            commit();
        }

        cui_row(ui, .gap = 24, .align = CUI_ALIGN_START)
        {
            snprintf(header, sizeof(header), "Samples: %d", sm->samples);
            draw_stepper(ui, "samples", header, 1.0, 0.0, 0, &delta);
            if (delta != 0.0)
            {
                xrdev_set_smooth_samples(device, which, sm->samples + (int)delta);
                commit();
            }

            snprintf(header, sizeof(header), which == XRDEV_SMOOTH_ROTATION ? "Scale: %.2f %s" : "Scale: %.3f %s",
                     shown_scale, which == XRDEV_SMOOTH_ROTATION ? "deg" : "m");
            draw_stepper(ui, "scale", header, info->coarse, info->fine,
                         which == XRDEV_SMOOTH_ROTATION ? 1 : 2, &delta);
            if (delta != 0.0)
            {
                xrdev_set_smooth_scale(device, which, shown_scale + delta);
                commit();
            }

            snprintf(header, sizeof(header), "Strength: %.2f", sm->strength);
            draw_stepper(ui, "strength", header, 0.1, 0.01, 1, &delta);
            if (delta != 0.0)
            {
                xrdev_set_smooth_strength(device, which, sm->strength + delta);
                commit();
            }
        }
    }
    cui_pop_id(ui);
}

static void draw_smooth_panel(cui_ctx *ui, int side)
{
    xrdev_device d;
    int device = xrdev_find(hand_paths[side]);

    if (device < 0 || !xrdev_get(device, &d))
        return;

    cui_push_id(ui, hand_paths[side]);
    cui_card(ui, .id = hand_paths[side], .width = CUI_FILL, .pad = 12, .gap = 10,
             .dir = CUI_COLUMN)
    {
        cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER)
        {
            cui_text(ui, hand_titles[side], .font = CUI_FONT_MEDIUM, .size = 14,
                     .align = CUI_TEXT_CENTER);
        }

        cui_row(ui, .width = CUI_FILL, .gap = 24, .align = CUI_ALIGN_START,
                .justify = CUI_JUSTIFY_BETWEEN)
        {
            draw_smooth_section(ui, device, &d, XRDEV_SMOOTH_POSITION);
            draw_smooth_section(ui, device, &d, XRDEV_SMOOTH_ROTATION);
        }
    }
    cui_pop_id(ui);
}

/* -------------------------------------------------------------- deadzones */

/* One axis of one thumb stick: "Stick left / right: 8%" over a "-1" / "+1"
 * pair. A whole percent per click: stick drift is never worth tenths, and the
 * two axes are on their own because a worn stick usually drifts along one. */
static void draw_deadzone_axis(cui_ctx *ui, int device, const xrdev_device *d, int axis)
{
    const xrdev_axis_info *info = xrdev_axis_deadzone(axis);
    double shown = d->deadzone[axis] * 100.0;
    char header[XRUI_LINE];
    char id[24];
    double delta;

    snprintf(id, sizeof(id), "axis%d", axis);
    if (shown >= XRDEV_DEADZONE_MAX_PERCENT)
        /* the whole travel is dead: the axis stops reaching the game at all */
        snprintf(header, sizeof(header), "%s: %.0f%% ignored", info->name, shown);
    else
        snprintf(header, sizeof(header), "%s: %.0f%%", info->name, shown);

    draw_stepper(ui, id, header, info->coarse, info->fine, 0, &delta);
    if (delta != 0.0)
    {
        xrdev_set_deadzone(device, axis, shown + delta);
        commit();
    }
}

static void draw_deadzone_panel(cui_ctx *ui, int side)
{
    xrdev_device d;
    int device = xrdev_find(hand_paths[side]);

    if (device < 0 || !xrdev_get(device, &d))
        return;

    cui_push_id(ui, hand_paths[side]);
    cui_card(ui, .id = hand_paths[side], .width = CUI_FILL, .pad = 12, .gap = 10,
             .dir = CUI_COLUMN)
    {
        cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER)
        {
            cui_text(ui, hand_titles[side], .font = CUI_FONT_MEDIUM, .size = 14,
                     .align = CUI_TEXT_CENTER);
            cui_text(ui, "inside the deadzone the stick reads as centred, at 100% it is ignored",
                     .size = 12, .align = CUI_TEXT_CENTER);
        }

        cui_row(ui, .width = CUI_FILL, .gap = 24, .align = CUI_ALIGN_START,
                .justify = CUI_JUSTIFY_BETWEEN)
        {
            draw_deadzone_axis(ui, device, &d, XRDEV_STICK_X);
            draw_deadzone_axis(ui, device, &d, XRDEV_STICK_Y);
        }
    }
    cui_pop_id(ui);
}

/* ------------------------------------------------------------------ frame */

static void xrui_frame(cui_ctx *ui, void *user)
{
    static const char *const tabs[XRUI_TAB_COUNT] = {"Offsets", "Smoothing",
                                                     "Deadzones"};
    xrui_view *v = (xrui_view *)user;

    cui_box(ui, .width = CUI_FILL, .height = CUI_FILL, .dir = CUI_COLUMN, .gap = 12,
            .pad = 12)
    {
        cui_tabs(ui, "tabs", tabs, XRUI_TAB_COUNT, &v->tab);

        if (v->tab == XRUI_TAB_SMOOTHING)
        {
            draw_smooth_panel(ui, 0);
            draw_smooth_panel(ui, 1);
        }
        else if (v->tab == XRUI_TAB_DEADZONES)
        {
            draw_deadzone_panel(ui, 0);
            draw_deadzone_panel(ui, 1);
        }
        else
        {
            draw_panel(ui, 0);
            draw_panel(ui, 1);
        }
    }
}

/* ------------------------------------------------------------------- run */

#if defined(_WIN32)
#include <windows.h>

static DWORD CALLBACK xrui_watchdog(void *parameter)
{
    DWORD ms = (DWORD)(uintptr_t)parameter;

    Sleep(ms);
    xrui_close();
    return 0;
}
#endif

static int xrui_frame_loop(const xrui_options *options)
{
    cui_app_desc desc;
    int result;

    memset(&view, 0, sizeof(view));
    if (options != NULL)
        view.options = *options;
    view.tab = (view.options.tab >= XRUI_TAB_OFFSETS &&
                view.options.tab < XRUI_TAB_COUNT)
                   ? view.options.tab
                   : XRUI_TAB_OFFSETS;

#if defined(_WIN32)
    if (options->exit_after_ms > 0)
    {
        HANDLE thread = CreateThread(NULL, 0, xrui_watchdog,
                                     (void *)(uintptr_t)(unsigned)options->exit_after_ms,
                                     0, NULL);
        if (thread != NULL)
            CloseHandle(thread);
    }
#endif

    memset(&desc, 0, sizeof(desc));
    desc.title = options->title != NULL ? options->title : XRUI_WINDOW_TITLE;
    /* 860 = the six groups of two 60 px buttons, their gaps and both paddings. */
    desc.width = options->width > 0 ? options->width : 1000;
    desc.height = options->height > 0 ? options->height : 480;
    desc.min_width = 640;
    desc.min_height = 360;
    desc.fixed_size = true;
    desc.theme = CUI_THEME_LIGHT;
    /* A screenshot has to be taken after a fixed number of frames, which the
     * event driven loop would never reach on its own. */
    desc.continuous = options->screenshot != NULL && options->screenshot[0] != '\0';

    xrui_running = 1;
    result = cui_app_run(&desc, xrui_frame, &view);
    xrui_running = 0;

    if (xrdev_is_dirty() && save_path() != NULL)
        xrdev_save(save_path());

    return result;
}

int xrui_run(const xrui_options *options)
{
    xrui_options local;

    memset(&local, 0, sizeof(local));
    if (options != NULL)
        local = *options;

    if (local.screenshot != NULL && local.screenshot[0] != '\0')
    {
        /* cui_app reads these two to grab a PNG and quit, see cui_app.c. */
        _putenv_s("CUI_SCREENSHOT", local.screenshot);
        {
            char frames[16];
            snprintf(frames, sizeof(frames), "%d",
                     local.screenshot_frames > 0 ? local.screenshot_frames : 60);
            _putenv_s("CUI_SCREENSHOT_FRAMES", frames);
        }
    }

    xrdev_init();

    if (local.config_path != NULL && local.config_path[0] != '\0')
        xrdev_load(local.config_path);

    return xrui_frame_loop(&local);
}

void xrui_close(void)
{
    if (xrui_running)
        cui_app_quit(NULL);
}

int xrui_is_running(void)
{
    return xrui_running != 0;
}

/* ----------------------------------------------------------- launched UI */

#if defined(_WIN32)
static char xrui_config_copy[XRDEV_PATH_FILE_MAX];

/* Claimed by xrui_launch itself, before anything can block. xrui_running only
 * turns true once the thread is inside the frame loop, seconds after the
 * call, so it cannot be what keeps a second interface from being started. */
static LONG xrui_launch_claim = 0;

static DWORD CALLBACK xrui_thread_proc(void *parameter)
{
    xrui_options options;

    (void)parameter;
    memset(&options, 0, sizeof(options));
    options.config_path = xrui_config_copy[0] ? xrui_config_copy : NULL;
    options.title = XRUI_WINDOW_TITLE;

    /* Engines meddle with the windows of their own process while they start
     * up, so the interface waits before making one. That wait belongs here
     * and not in xrui_launch: its caller is DllMain, which holds the loader
     * lock, and sleeping there stalls every thread of the game. */
    Sleep(3000);

    xrui_run(&options);

    InterlockedExchange(&xrui_launch_claim, 0);
    return 0;
}

int xrui_launch(const char *config_path)
{
    HANDLE thread;

    if (InterlockedCompareExchange(&xrui_launch_claim, 1, 0) != 0)
        return 0;

    xrui_config_copy[0] = '\0';
    if (config_path != NULL)
    {
        /* A filesystem path cut short here points at a file nobody ever
         * reads: every save would go to the truncated name, every load to
         * the real one. Refuse rather than lose the adjustments. */
        if (strlen(config_path) + 1 > sizeof(xrui_config_copy))
        {
            InterlockedExchange(&xrui_launch_claim, 0);
            return 0;
        }
        memcpy(xrui_config_copy, config_path, strlen(config_path) + 1);
    }

    thread = CreateThread(NULL, 0, xrui_thread_proc, NULL, 0, NULL);
    if (thread == NULL)
    {
        InterlockedExchange(&xrui_launch_claim, 0);
        return 0;
    }

    CloseHandle(thread);
    return 1;
}
#endif /* _WIN32 */
