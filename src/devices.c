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

    Implementation of the device model, see devices.h.

    Two things happen here:

      * discovery: the OpenXR hooks in main.c report what the game declares
        and this file turns that into a list of devices, each owning the
        action spaces that must be rewritten.

      * injection: xrdev_apply_offset() adds a device's rotation and position
        offset to a pose, which is what xrLocateSpace hands back to the game.
*/

#include "devices.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <io.h> /* _access: a missing config file is not an unreadable one */

#include "Quaternion.h"
#include "cJSON.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define XRDEV_MAX_PATH_NAMES 2048
#define XRDEV_LINE_MAX 256

    /* ------------------------------------------------------------- state */

    typedef struct xrdev_path_name
{
    uint64_t path;
    char str[XRDEV_PATH_MAX];
} xrdev_path_name;

    typedef struct xrdev_action_info
{
    uint64_t action;
    char name[XRDEV_NAME_MAX];
    uint64_t subactions[XRDEV_AXIS_COUNT * 4];
    int subaction_count;
} xrdev_action_info;

    typedef struct xrdev_state
{
    SRWLOCK lock;

    xrdev_device devices[XRDEV_MAX_DEVICES];
    int device_count;
    uint64_t generation;

    xrdev_path_name paths[XRDEV_MAX_PATH_NAMES];
    int path_count;

    xrdev_action_info actions[XRDEV_MAX_ACTIONS];
    int action_count;

    char application[XRDEV_NAME_MAX];
    char runtime[XRDEV_NAME_MAX];

    char config_path[XRDEV_PATH_FILE_MAX];
    int dirty;
    uint64_t last_save_ms;
} xrdev_state;

/* Static storage is zeroed, and a zeroed SRWLOCK is what SRWLOCK_INIT makes
 * one, so the lock is usable from the very first hook call with no start up
 * code at all. That matters: the proxy hooks can run before anything else. */
static xrdev_state state;

static xrdev_log_fn logger = 0;

/* Only read by the UI. */
static const xrdev_axis_info rotation_axes[XRDEV_AXIS_COUNT] = {
    {"Pitch", "X", "\xc2\xb0", -180.0, 180.0, 1.0, 0.1},
    {"Yaw", "Y", "\xc2\xb0", -180.0, 180.0, 1.0, 0.1},
    {"Roll", "Z", "\xc2\xb0", -180.0, 180.0, 1.0, 0.1}};

static const xrdev_axis_info position_axes[XRDEV_AXIS_COUNT] = {
    {"Left / right", "X", "m", -0.5, 0.5, 0.1, 0.01},
    {"Up / down", "Y", "m", -0.5, 0.5, 0.1, 0.01},
    {"Forward / back", "Z", "m", -0.5, 0.5, 0.1, 0.01}};

/* Thumb stick deadzones, in percent of full travel from none to all of it:
 * one whole percent per click, no fine step. The unit of the value is what
 * the interface shows. */
static const xrdev_axis_info deadzone_axes[XRDEV_STICK_AXIS_COUNT] = {
    {"Stick left / right", "X", "%", XRDEV_DEADZONE_MIN_PERCENT, XRDEV_DEADZONE_MAX_PERCENT, 1.0, 0.0},
    {"Stick forward / back", "Y", "%", XRDEV_DEADZONE_MIN_PERCENT, XRDEV_DEADZONE_MAX_PERCENT, 1.0, 0.0}};

static void smooth_store_scale(xrdev_smooth *sm, int which, double value);
static void smooth_store_samples(xrdev_smooth *sm, int samples);
static void smooth_store_strength(xrdev_smooth *sm, double strength);
static void smooth_forget(uint64_t space);

/* Only read by the UI. Position scale in metres, rotation scale in degrees. */
static const xrdev_axis_info smooth_scale_axes[XRDEV_SMOOTH_COUNT] = {
    {"Distance", "d", "m", 0.001, 0.1, 0.01, 0.001},
    {"Angle", "a", "\xc2\xb0", 0.1, 20.0, 1.0, 0.1}};

    /* --------------------------------------------------------- utilities */

    static void xrdev_log(const char *fmt, ...)
{
    char buffer[XRDEV_LINE_MAX];
    va_list args;

    if (!logger)
        return;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    logger(buffer);
}

static void trim(char *s)
{
    size_t n;
    char *p = s;

    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);

    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
}

static int ci_equal(const char *a, const char *b)
{
    return _stricmp(a, b) == 0;
}

/* Case insensitive substring test. */
static int contains_ci(const char *haystack, const char *needle)
{
    char low[XRDEV_PATH_MAX];
    char find[XRDEV_PATH_MAX];
    char *hit;

    if (!haystack || !needle || !needle[0])
        return 0;

    /* _strlwr is not portable, so lower case by hand. */
    {
        size_t i;
        for (i = 0; i < sizeof(low) - 1 && haystack[i]; i++)
            low[i] = (char)tolower((unsigned char)haystack[i]);
        low[i] = 0;
        for (i = 0; i < sizeof(find) - 1 && needle[i]; i++)
            find[i] = (char)tolower((unsigned char)needle[i]);
        find[i] = 0;
    }

    hit = strstr(low, find);
    return hit != NULL;
}

/* Copies a string into a fixed buffer, always terminating it and never
 * writing past cap. Long input is cut short without ceremony: every caller
 * here is storing a name, a path or a config key, where a shorter answer is
 * better than no answer, and none of them can do anything useful with a
 * truncation beyond ignoring it.
 *
 * Deliberately not strncpy(): it never NUL terminates when the source exactly
 * fills the space (hence the manual byte above), it zero fills the rest of the
 * destination for no reason, and -Wstringop-truncation reports the length
 * clamping it is being asked to do as if it were a bug. */
static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t len;

    if (!dst || cap == 0)
        return;
    if (!src)
    {
        dst[0] = 0;
        return;
    }

    len = strlen(src);
    if (len > cap - 1)
        len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

double xrdev_deg2rad(double degrees)
{
    return degrees * M_PI / 180.0;
}

double xrdev_rad2deg(double radians)
{
    return radians * 180.0 / M_PI;
}

static double clampd(double v, double lo, double hi)
{
    if (!(v == v)) /* NaN */
        return 0.0;
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

    /* ------------------------------------------------------------ locking */

    static void rd_begin(void)
{
    AcquireSRWLockShared(&state.lock);
}

static void rd_end(void)
{
    ReleaseSRWLockShared(&state.lock);
}

static void wr_begin(void)
{
    AcquireSRWLockExclusive(&state.lock);
}

static void wr_end(void)
{
    ReleaseSRWLockExclusive(&state.lock);
}

    /* ------------------------------------------------- kind and name rules */

    typedef struct xrdev_profile_info
{
    const char *path;      /* as it appears in the interaction profile */
    const char *label;     /* what the interface shows */
    xrdev_kind kind;
} xrdev_profile_info;

static const xrdev_profile_info profile_table[] = {
    {"/khr/simple_controller", "KHR simple controller", XRDEV_KIND_CONTROLLER},
    {"/valve/index_controller", "Valve Index controllers", XRDEV_KIND_CONTROLLER},
    {"/oculus/touch_controller", "Oculus Touch", XRDEV_KIND_CONTROLLER},
    {"/oculus/rift_s_controller", "Oculus Rift S", XRDEV_KIND_CONTROLLER},
    {"/oculus/pro_controller", "Oculus Pro controller", XRDEV_KIND_CONTROLLER},
    {"/htc/vive_controller", "HTC Vive wands", XRDEV_KIND_CONTROLLER},
    {"/htc/vive_pro", "HTC Vive Pro controller", XRDEV_KIND_CONTROLLER},
    {"/htc/wand_htcx", "HTC Vive wand", XRDEV_KIND_CONTROLLER},
    {"/microsoft/motion_controller", "Windows Motion controller", XRDEV_KIND_CONTROLLER},
    {"/microsoft/holo_lens", "HoloLens", XRDEV_KIND_CONTROLLER},
    {"/mndx/magic_window", "MMagic Leap", XRDEV_KIND_CONTROLLER},
    {"/pico/ne4_controller", "Pico Neo 4", XRDEV_KIND_CONTROLLER},
    {"/pico/unity_controller", "Pico controller", XRDEV_KIND_CONTROLLER},
    {"/byted/flame3_pro", "Pico 4", XRDEV_KIND_CONTROLLER},
    {"/ml2/ml2_controller", "Magic Leap 2", XRDEV_KIND_CONTROLLER},
    {"/utah_v2/basic_controller", "Utah RDK controller", XRDEV_KIND_CONTROLLER},
    {"/utah_v2/with_index_controllers", "Utah RDK with Index", XRDEV_KIND_CONTROLLER},
    {"/google/daydream_1", "Daydream", XRDEV_KIND_CONTROLLER},
    {"/ext/hand_qt", "Hand tracking (QT)", XRDEV_KIND_HAND},
    {"/ext/hand_ovr", "Hand tracking (Oculus)", XRDEV_KIND_HAND},
    {"/ext/hand_msft", "Hand tracking (Microsoft)", XRDEV_KIND_HAND},
    {"/ext/hand_ml2", "Hand tracking (Magic Leap)", XRDEV_KIND_HAND},
    {"/ext/hand_htc", "Hand tracking (HTC)", XRDEV_KIND_HAND},
    {"/ext/hand_tracker_htcx", "HTC tracker", XRDEV_KIND_TRACKER},
    {"/htc/vive_tracker_htcx", "Vive tracker", XRDEV_KIND_TRACKER},
    {"/user/vive_tracker_htcx", "Vive tracker", XRDEV_KIND_TRACKER},
};

const char *xrdev_profile_label(const char *profile)
{
    size_t i;
    if (!profile || !profile[0])
        return 0;
    for (i = 0; i < sizeof(profile_table) / sizeof(profile_table[0]); i++)
    {
        if (strstr(profile, profile_table[i].path))
            return profile_table[i].label;
    }
    return 0;
}

xrdev_kind xrdev_kind_of(const char *path, const char *profile)
{
    size_t i;

    if (profile && profile[0])
    {
        for (i = 0; i < sizeof(profile_table) / sizeof(profile_table[0]); i++)
        {
            if (strstr(profile, profile_table[i].path) && profile_table[i].kind != XRDEV_KIND_TRACKER)
                return profile_table[i].kind;
        }
    }

    if (path)
    {
        if (strstr(path, "/role/") || contains_ci(path, "tracker"))
            return XRDEV_KIND_TRACKER;
        if (contains_ci(path, "hand") || contains_ci(path, "finger"))
            return XRDEV_KIND_HAND;
    }

    if (profile)
    {
        if (contains_ci(profile, "hand") || contains_ci(profile, "express"))
            return XRDEV_KIND_HAND;
        if (contains_ci(profile, "tracker"))
            return XRDEV_KIND_TRACKER;
    }

    return XRDEV_KIND_CONTROLLER;
}

const char *xrdev_kind_label(xrdev_kind kind)
{
    switch (kind)
    {
    case XRDEV_KIND_CONTROLLER:
        return "Controller";
    case XRDEV_KIND_HAND:
        return "Hand tracking";
    case XRDEV_KIND_TRACKER:
        return "Tracker";
    default:
        return "Device";
    }
}

const char *xrdev_kind_icon(xrdev_kind kind)
{
    switch (kind)
    {
    case XRDEV_KIND_HAND:
        return "\xee\x91\xaf"; /* lucide hand */
    case XRDEV_KIND_TRACKER:
        return "\xee\x94\xbb"; /* lucidelocate */
    case XRDEV_KIND_GENERIC:
        return "\xee\x94\x97"; /* lucide box */
    default:
        return "\xee\x82\xa9"; /* lucide cpu, stands in for a controller */
    }
}

/* Copies the last meaningful segment of a /user/... path, skipping the
 * "role" and "input" words the runtimes like to insert. */
static void last_segment(const char *path, char *out, size_t cap)
{
    const char *best = 0;
    const char *p;

    out[0] = 0;
    if (!path)
        return;

    p = path;
    while (*p)
    {
        const char *slash = strchr(p + 1, '/');
        size_t len;
        if (!slash)
            slash = p + strlen(p);
        len = (size_t)(slash - p);
        if (len > 1)
        {
            char word[64];
            size_t n = len - 1 < sizeof(word) - 1 ? len - 1 : sizeof(word) - 1;
            memcpy(word, p + 1, n);
            word[n] = 0;
            if (!ci_equal(word, "user") && !ci_equal(word, "role") && !ci_equal(word, "input"))
            {
                copy_text(out, cap, word);
                best = out;
            }
        }
        if (!*slash)
            break;
        p = slash;
    }
    (void)best;
}

void xrdev_default_name(const char *path, char *out, size_t cap)
{
    char segment[XRDEV_PATH_MAX];
    char *c;

    if (!out || cap == 0)
        return;

    if (path && ci_equal(path, XRDEV_PATH_HAND_LEFT))
    {
        copy_text(out, cap, "Left hand");
        return;
    }
    if (path && ci_equal(path, XRDEV_PATH_HAND_RIGHT))
    {
        copy_text(out, cap, "Right hand");
        return;
    }

    last_segment(path, segment, sizeof(segment));
    if (!segment[0])
    {
        copy_text(out, cap, "Device");
        return;
    }

    for (c = segment; *c; c++)
        if (*c == '_' || *c == '-')
            *c = ' ';
    segment[0] = (char)toupper((unsigned char)segment[0]);

    snprintf(out, cap, "%s", segment);
}

    /* --------------------------------------------------------- path table */

    /* Caller holds the write lock. */
    static void path_note(uint64_t path, const char *str)
{
    int i;
    if (!str || !str[0] || str[0] != '/')
        return;

    for (i = 0; i < state.path_count; i++)
    {
        if (state.paths[i].path == path)
        {
            if (strcmp(state.paths[i].str, str) != 0)
                copy_text(state.paths[i].str, XRDEV_PATH_MAX, str);
            return;
        }
    }

    if (state.path_count >= XRDEV_MAX_PATH_NAMES)
        return;

    state.paths[state.path_count].path = path;
    copy_text(state.paths[state.path_count].str, XRDEV_PATH_MAX, str);
    state.path_count++;
}

/* Caller holds either lock. */
static const char *path_lookup(uint64_t path)
{
    int i;
    for (i = 0; i < state.path_count; i++)
    {
        if (state.paths[i].path == path)
            return state.paths[i].str;
    }
    return 0;
}

void xrdev_note_path(uint64_t path, const char *path_string)
{
    wr_begin();
    path_note(path, path_string);
    wr_end();
}

const char *xrdev_path_string(uint64_t path)
{
    const char *found;
    rd_begin();
    found = path_lookup(path);
    /* Hand back a copy the caller can keep. */
    if (found)
    {
        static char buffer[XRDEV_MAX_DEVICES][XRDEV_PATH_MAX];
        static int slot = 0;
        char *out = buffer[slot];
        slot = (slot + 1) % XRDEV_MAX_DEVICES;
        copy_text(out, XRDEV_PATH_MAX, found);
        rd_end();
        return out;
    }
    rd_end();
    return 0;
}

/* Caller holds the write lock. */
static int device_lookup_by_path_nolock(const char *path)
{
    int i;
    if (!path)
        return -1;
    for (i = 0; i < state.device_count; i++)
    {
        if (state.devices[i].in_use && ci_equal(state.devices[i].path, path))
            return i;
    }
    return -1;
}

/* Smoothing starts off, with settings that only touch tremor sized motion. */
static void device_smooth_defaults(xrdev_device *d)
{
    d->smooth[XRDEV_SMOOTH_POSITION].enabled = 0;
    d->smooth[XRDEV_SMOOTH_POSITION].samples = 3;
    d->smooth[XRDEV_SMOOTH_POSITION].strength = 1.0;
    d->smooth[XRDEV_SMOOTH_POSITION].scale = 0.005;
    d->smooth[XRDEV_SMOOTH_ROTATION].enabled = 0;
    d->smooth[XRDEV_SMOOTH_ROTATION].samples = 3;
    d->smooth[XRDEV_SMOOTH_ROTATION].strength = 1.0;
    d->smooth[XRDEV_SMOOTH_ROTATION].scale = xrdev_deg2rad(1.0);
}

/* Smoothing is for the controllers only, the headset is never touched. */
static int device_smoothable(const xrdev_device *d)
{
    return ci_equal(d->path, XRDEV_PATH_HAND_LEFT) || ci_equal(d->path, XRDEV_PATH_HAND_RIGHT);
}

/* Caller holds the write lock. Creates the device when it is new. */
static int device_observe(const char *path)
{
    int index = device_lookup_by_path_nolock(path);
    xrdev_device *d;

    if (index >= 0)
        return index;

    if (state.device_count >= XRDEV_MAX_DEVICES)
    {
        xrdev_log("NOTICE: too many devices, ignoring %s", path ? path : "(null)");
        return -1;
    }

    index = state.device_count;
    d = &state.devices[index];
    memset(d, 0, sizeof(*d));

    d->in_use = 1;
    d->index = index;
    d->enabled = 1;
    copy_text(d->path, sizeof(d->path), path ? path : XRDEV_PATH_NONE);
    xrdev_default_name(d->path, d->name, sizeof(d->name));
    d->kind = xrdev_kind_of(d->path, 0);
    d->seen = 1;
    device_smooth_defaults(d);
    state.device_count++;
    state.generation++;

    xrdev_log("NOTICE: discovered device #%d \"%s\" (%s)", index, d->name, d->path);
    return index;
}

    /* ---------------------------------------------------------- start up */

    void xrdev_set_logger(xrdev_log_fn fn)
{
    logger = fn;
}

void xrdev_init(void)
{
    wr_begin();
    if (state.device_count == 0)
    {
        device_observe(XRDEV_PATH_HAND_LEFT);
        device_observe(XRDEV_PATH_HAND_RIGHT);
    }
    wr_end();
}

    /* ----------------------------------------------------------- config */

    const char *xrdev_config_path(void)
{
    return state.config_path[0] ? state.config_path : 0;
}

int xrdev_is_dirty(void)
{
    return state.dirty;
}

void xrdev_mark_dirty(void)
{
    state.dirty = 1;
}

/* Caller holds the write lock. */
static void device_set_rotation_deg_nolock(xrdev_device *d, int axis, double degrees)
{
    const xrdev_axis_info *info = xrdev_axis_rotation(axis);
    d->rotation[axis] = xrdev_deg2rad(clampd(degrees, info->min, info->max));
}

/* Caller holds the write lock. */
static void device_set_position_nolock(xrdev_device *d, int axis, double metres)
{
    const xrdev_axis_info *info = xrdev_axis_position(axis);
    d->position[axis] = clampd(metres, info->min, info->max);
}

/* Caller holds the write lock. The interface and the config file state a
 * deadzone in percent, the filter wants the fraction of full travel. */
static void device_set_deadzone_pct_nolock(xrdev_device *d, int axis, double percent)
{
    const xrdev_axis_info *info = xrdev_axis_deadzone(axis);
    d->deadzone[axis] = clampd(percent, info->min, info->max) / 100.0;
}

/* Legacy keys, so the file written by the previous version of this tool still
 * loads. Returns 1 when the key was one of these. */
static int apply_legacy_key(xrdev_device *d, const char *key, double value)
{
    static const char *const rot_keys[XRDEV_AXIS_COUNT] = {
        "rotation_offset_x",
        "rotation_offset_y",
        "rotation_offset_z"};
    static const char *const pos_keys[XRDEV_AXIS_COUNT] = {
        "position_offset_x",
        "position_offset_y",
        "position_offset_z"};
    int axis;

    for (axis = 0; axis < XRDEV_AXIS_COUNT; axis++)
    {
        if (ci_equal(key, rot_keys[axis]))
        {
            /* the old file stores degrees */
            device_set_rotation_deg_nolock(d, axis, value);
            return 1;
        }
    }
    for (axis = 0; axis < XRDEV_AXIS_COUNT; axis++)
    {
        if (ci_equal(key, pos_keys[axis]))
        {
            device_set_position_nolock(d, axis, value);
            return 1;
        }
    }
    return 0;
}

/* Turns "left_hand_rotation_offset_x" into a device path plus a legacy key. */
static int split_legacy_key(const char *key, const char **path, const char **legacy_key)
{
    static const char *prefixes[] = {
        "left_hand_",
        "right_hand_",
        "left_controller_",
        "right_controller_",
    };
    static const char *paths[] = {
        XRDEV_PATH_HAND_LEFT,
        XRDEV_PATH_HAND_RIGHT,
        XRDEV_PATH_HAND_LEFT,
        XRDEV_PATH_HAND_RIGHT,
    };
    size_t i;

    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
    {
        size_t len = strlen(prefixes[i]);
        if (_strnicmp(key, prefixes[i], len) == 0)
        {
            *path = paths[i];
            *legacy_key = key + len;
            return 1;
        }
    }
    return 0;
}

/* Applies "key=value" from inside a [device:...] section. */
static void apply_section_key(xrdev_device *d, const char *key, const char *value)
{
    if (ci_equal(key, "enabled"))
    {
        d->enabled = (int)strtol(value, 0, 10);
        if (d->enabled != 0)
            d->enabled = 1;
    }
    else if (ci_equal(key, "rotation_x"))
        device_set_rotation_deg_nolock(d, XRDEV_AXIS_X, strtod(value, 0));
    else if (ci_equal(key, "rotation_y"))
        device_set_rotation_deg_nolock(d, XRDEV_AXIS_Y, strtod(value, 0));
    else if (ci_equal(key, "rotation_z"))
        device_set_rotation_deg_nolock(d, XRDEV_AXIS_Z, strtod(value, 0));
    else if (ci_equal(key, "position_x"))
        device_set_position_nolock(d, XRDEV_AXIS_X, strtod(value, 0));
    else if (ci_equal(key, "position_y"))
        device_set_position_nolock(d, XRDEV_AXIS_Y, strtod(value, 0));
    else if (ci_equal(key, "position_z"))
        device_set_position_nolock(d, XRDEV_AXIS_Z, strtod(value, 0));
    else if (ci_equal(key, "deadzone_x"))
        device_set_deadzone_pct_nolock(d, XRDEV_STICK_X, strtod(value, 0));
    else if (ci_equal(key, "deadzone_y"))
        device_set_deadzone_pct_nolock(d, XRDEV_STICK_Y, strtod(value, 0));
    else if (ci_equal(key, "smooth_position"))
        d->smooth[XRDEV_SMOOTH_POSITION].enabled = strtol(value, 0, 10) != 0;
    else if (ci_equal(key, "smooth_position_samples"))
        smooth_store_samples(&d->smooth[XRDEV_SMOOTH_POSITION], (int)strtol(value, 0, 10));
    else if (ci_equal(key, "smooth_position_scale"))
        smooth_store_scale(&d->smooth[XRDEV_SMOOTH_POSITION], XRDEV_SMOOTH_POSITION, strtod(value, 0));
    else if (ci_equal(key, "smooth_position_strength"))
        smooth_store_strength(&d->smooth[XRDEV_SMOOTH_POSITION], strtod(value, 0));
    else if (ci_equal(key, "smooth_rotation_strength"))
        smooth_store_strength(&d->smooth[XRDEV_SMOOTH_ROTATION], strtod(value, 0));
    else if (ci_equal(key, "smooth_rotation"))
        d->smooth[XRDEV_SMOOTH_ROTATION].enabled = strtol(value, 0, 10) != 0;
    else if (ci_equal(key, "smooth_rotation_samples"))
        smooth_store_samples(&d->smooth[XRDEV_SMOOTH_ROTATION], (int)strtol(value, 0, 10));
    else if (ci_equal(key, "smooth_rotation_scale"))
        smooth_store_scale(&d->smooth[XRDEV_SMOOTH_ROTATION], XRDEV_SMOOTH_ROTATION, strtod(value, 0));
    else if (ci_equal(key, "name") && value[0])
        copy_text(d->name, sizeof(d->name), value);
    else if (ci_equal(key, "profile") && value[0])
    {
        copy_text(d->profile, sizeof(d->profile), value);
        d->kind = xrdev_kind_of(d->path, d->profile);
    }
}

/* The text format of the previous versions: "key=value" lines and
 * [device:...] sections. Read only, new files are written as JSON. */
static int load_ini(FILE *arq)
{
    char line[1024];
    xrdev_device *current = 0;
    int loaded = 0;

    while (fgets(line, sizeof(line), arq) != NULL)
    {
        char *eq;
        char key[128];
        char *value;
        char *comment;

        comment = strpbrk(line, "#;");
        if (comment)
            *comment = 0;
        trim(line);
        if (!line[0])
            continue;

        /* [device:/user/hand/left] */
        if (line[0] == '[')
        {
            char *close = strchr(line, ']');
            current = 0;
            if (close)
            {
                char *name = line + 1;
                *close = 0;
                if (_strnicmp(name, "device:", 7) == 0)
                {
                    int index;
                    wr_begin();
                    index = device_observe(name + 7);
                    if (index >= 0)
                        current = &state.devices[index];
                    wr_end();
                }
            }
            continue;
        }

        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        value = eq + 1;
        copy_text(key, sizeof(key), line);
        trim(key);
        trim(value);
        if (!key[0])
            continue;

        wr_begin();

        if (current)
        {
            apply_section_key(current, key, value);
            loaded++;
        }
        else
        {
            const char *legacy_path = 0;
            const char *legacy_key = 0;
            if (split_legacy_key(key, &legacy_path, &legacy_key))
            {
                int index = device_observe(legacy_path);
                if (index >= 0)
                {
                    /* rotation is stored in degrees, position in metres */
                    apply_legacy_key(&state.devices[index], legacy_key, strtod(value, 0));
                    loaded++;
                }
            }
            else if (ci_equal(key, "application") || ci_equal(key, "runtime"))
            {
                copy_text(key[0] == 'a' || key[0] == 'A' ? state.application : state.runtime,
                          XRDEV_NAME_MAX, value);
            }
        }

        wr_end();
    }

    return loaded;
}

/* Reads a number out of a JSON object, returns 0 when it is missing. */
static int json_number(const cJSON *object, const char *name, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsNumber(item))
        return 0;
    *out = item->valuedouble;
    return 1;
}

static int json_flag(const cJSON *object, const char *name, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (cJSON_IsBool(item))
        *out = cJSON_IsTrue(item) ? 1 : 0;
    else if (cJSON_IsNumber(item))
        *out = item->valuedouble != 0.0;
    else
        return 0;
    return 1;
}

/* "rotation": {"x":..,"y":..,"z":..}, degrees; "position": metres. */
static int json_apply_axes(xrdev_device *d, const cJSON *object, int rotation)
{
    static const char *const names[XRDEV_AXIS_COUNT] = {"x", "y", "z"};
    int axis;
    int count = 0;

    if (!cJSON_IsObject(object))
        return 0;
    for (axis = 0; axis < XRDEV_AXIS_COUNT; axis++)
    {
        double value;
        if (!json_number(object, names[axis], &value))
            continue;
        if (rotation)
            device_set_rotation_deg_nolock(d, axis, value);
        else
            device_set_position_nolock(d, axis, value);
        count++;
    }
    return count;
}

/* "deadzone": {"x":..,"y":..}, percent of full travel like the interface. */
static int json_apply_deadzone(xrdev_device *d, const cJSON *object)
{
    static const char *const names[XRDEV_STICK_AXIS_COUNT] = {"x", "y"};
    int axis;
    int count = 0;

    if (!cJSON_IsObject(object))
        return 0;
    for (axis = 0; axis < XRDEV_STICK_AXIS_COUNT; axis++)
    {
        double value;
        if (!json_number(object, names[axis], &value))
            continue;
        device_set_deadzone_pct_nolock(d, axis, value);
        count++;
    }
    return count;
}

/* One of the two smoothing blocks. `scale` is metres or degrees. */
static int json_apply_smooth(xrdev_device *d, const cJSON *object, int which)
{
    xrdev_smooth *sm = &d->smooth[which];
    double value;
    int flag;
    int count = 0;

    if (!cJSON_IsObject(object))
        return 0;
    if (json_flag(object, "enabled", &flag))
    {
        sm->enabled = flag;
        count++;
    }
    if (json_number(object, "samples", &value))
    {
        smooth_store_samples(sm, (int)value);
        count++;
    }
    if (json_number(object, "strength", &value))
    {
        smooth_store_strength(sm, value);
        count++;
    }
    if (json_number(object, "scale", &value))
    {
        smooth_store_scale(sm, which, value);
        count++;
    }
    return count;
}

static int load_json(const char *text, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(text, length);
    const cJSON *devices;
    const cJSON *entry;
    int loaded = 0;

    if (!root)
    {
        const char *where = cJSON_GetErrorPtr();
        xrdev_log("Config file is not valid JSON (near byte %d), keeping the defaults.",
                  where ? (int)(where - text) : 0);
        return 0;
    }

    devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    cJSON_ArrayForEach(entry, devices)
    {
        const cJSON *path = cJSON_GetObjectItemCaseSensitive(entry, "path");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        const cJSON *profile = cJSON_GetObjectItemCaseSensitive(entry, "profile");
        const cJSON *smoothing = cJSON_GetObjectItemCaseSensitive(entry, "smoothing");
        xrdev_device *d;
        int index;
        int flag;

        if (!cJSON_IsString(path) || !path->valuestring[0])
            continue;

        wr_begin();
        index = device_observe(path->valuestring);
        if (index < 0)
        {
            wr_end();
            continue;
        }
        d = &state.devices[index];

        if (cJSON_IsString(name) && name->valuestring[0])
            copy_text(d->name, sizeof(d->name), name->valuestring);
        if (cJSON_IsString(profile) && profile->valuestring[0])
        {
            copy_text(d->profile, sizeof(d->profile), profile->valuestring);
            d->kind = xrdev_kind_of(d->path, d->profile);
        }
        if (json_flag(entry, "enabled", &flag))
            d->enabled = flag;
        loaded += json_apply_axes(d, cJSON_GetObjectItemCaseSensitive(entry, "rotation"), 1);
        loaded += json_apply_axes(d, cJSON_GetObjectItemCaseSensitive(entry, "position"), 0);
        loaded += json_apply_deadzone(d, cJSON_GetObjectItemCaseSensitive(entry, "deadzone"));
        if (cJSON_IsObject(smoothing))
        {
            loaded += json_apply_smooth(d, cJSON_GetObjectItemCaseSensitive(smoothing, "position"),
                                        XRDEV_SMOOTH_POSITION);
            loaded += json_apply_smooth(d, cJSON_GetObjectItemCaseSensitive(smoothing, "rotation"),
                                        XRDEV_SMOOTH_ROTATION);
        }
        wr_end();
    }

    cJSON_Delete(root);
    return loaded;
}

/* "x.json" -> "x.txt", the name the previous versions used. */
static int legacy_twin_path(const char *path, char *out, size_t cap)
{
    size_t len = strlen(path);

    if (len < 6 || len + 1 > cap || _stricmp(path + len - 5, ".json") != 0)
        return 0;
    memcpy(out, path, len - 5);
    memcpy(out + len - 5, ".txt", 5);
    return 1;
}

int xrdev_load(const char *path)
{
    FILE *arq;
    char twin[XRDEV_PATH_FILE_MAX];
    int migrate = 0;
    int loaded;
    char *text;
    long size;

    if (!path || !path[0])
        return 0;

    arq = fopen(path, "rb");
    if (!arq && legacy_twin_path(path, twin, sizeof(twin)))
    {
        /* No JSON yet, but the old .txt is there: read it, write JSON. */
        arq = fopen(twin, "rb");
        migrate = arq != NULL;
    }
    if (!arq)
    {
        /*
            "Could not open" is not the same as "does not exist", and the two
            must not be treated the same way: what follows writes the file,
            which would replace the saved offsets of anyone whose config
            merely could not be read this once (an antivirus holding it open,
            a second game instance, a read only share, a permissions problem).
            Defaults are only ever written when the file provably is absent.
        */
        if (_access(path, 0) == 0)
        {
            xrdev_log("The config file at %s exists but could not be opened (locked, or no permission). It is being left alone and this session runs with zero offsets.\n", path);
            return 0;
        }

        xrdev_log("Could not find my config file at:%s!\n We are trying to run with default offsets now(all zeroes)\n", path);
        wr_begin();
        copy_text(state.config_path, sizeof(state.config_path), path);
        wr_end();
        xrdev_save(path);
        return 0;
    }

    wr_begin();
    copy_text(state.config_path, sizeof(state.config_path), path);
    wr_end();

    fseek(arq, 0, SEEK_END);
    size = ftell(arq);
    fseek(arq, 0, SEEK_SET);
    text = size >= 0 ? (char *)malloc((size_t)size + 1) : NULL;
    if (!text)
    {
        fclose(arq);
        xrdev_log("Could not read my config file at:%s!", path);
        return 0;
    }
    size = (long)fread(text, 1, (size_t)size, arq);
    text[size] = 0;

    {
        /* JSON starts with a brace (after an optional byte order mark),
         * the old format never does: it begins with a comment or a key. */
        const char *first = text;
        if (size >= 3 && (unsigned char)first[0] == 0xEF && (unsigned char)first[1] == 0xBB && (unsigned char)first[2] == 0xBF)
            first += 3;
        while (*first && isspace((unsigned char)*first))
            first++;

        if (*first == '{')
            loaded = load_json(first, (size_t)(size - (first - text)));
        else
        {
            rewind(arq);
            loaded = load_ini(arq);
        }
    }

    free(text);
    fclose(arq);

    wr_begin();
    state.dirty = 0;
    wr_end();

    xrdev_log("read configuration file from: %s (%d values, %d devices)", migrate ? twin : path, loaded, xrdev_device_count());

    if (migrate)
    {
        xrdev_log("converted %s to JSON at %s", twin, path);
        xrdev_save(path);
    }
    return loaded;
}

/* Seven decimals is plenty for degrees and metres, and keeps the file free of
 * 5.1566200000000002 noise from the radian round trip. */
static double round7(double v)
{
    return floor(v * 1e7 + 0.5) / 1e7;
}

static cJSON *json_axes(const double values[XRDEV_AXIS_COUNT], int degrees)
{
    static const char *const names[XRDEV_AXIS_COUNT] = {"x", "y", "z"};
    cJSON *object = cJSON_CreateObject();
    int axis;

    if (!object)
        return NULL;
    for (axis = 0; axis < XRDEV_AXIS_COUNT; axis++)
        cJSON_AddNumberToObject(object, names[axis],
                                round7(degrees ? xrdev_rad2deg(values[axis]) : values[axis]));
    return object;
}

/* "deadzone": {"x":..,"y":..}, percent like the interface shows it. */
static cJSON *json_deadzone(const double deadzone[XRDEV_STICK_AXIS_COUNT])
{
    static const char *const names[XRDEV_STICK_AXIS_COUNT] = {"x", "y"};
    cJSON *object = cJSON_CreateObject();
    int axis;

    if (!object)
        return NULL;
    for (axis = 0; axis < XRDEV_STICK_AXIS_COUNT; axis++)
        cJSON_AddNumberToObject(object, names[axis], round7(deadzone[axis] * 100.0));
    return object;
}

static cJSON *json_smooth(const xrdev_smooth *sm, int which)
{
    cJSON *object = cJSON_CreateObject();

    if (!object)
        return NULL;
    cJSON_AddBoolToObject(object, "enabled", sm->enabled != 0);
    cJSON_AddNumberToObject(object, "samples", sm->samples);
    cJSON_AddNumberToObject(object, "strength", round7(sm->strength));
    /* metres for position, degrees for rotation, like the interface shows */
    cJSON_AddNumberToObject(object, "scale",
                            round7(which == XRDEV_SMOOTH_ROTATION ? xrdev_rad2deg(sm->scale) : sm->scale));
    return object;
}

/* Caller holds the read lock. */
static cJSON *json_device(const xrdev_device *d)
{
    cJSON *object = cJSON_CreateObject();

    if (!object)
        return NULL;
    cJSON_AddStringToObject(object, "path", d->path);
    cJSON_AddStringToObject(object, "name", d->name);
    if (d->profile[0])
        cJSON_AddStringToObject(object, "profile", d->profile);
    cJSON_AddBoolToObject(object, "enabled", d->enabled != 0);
    cJSON_AddItemToObject(object, "rotation", json_axes(d->rotation, 1));
    cJSON_AddItemToObject(object, "position", json_axes(d->position, 0));
    cJSON_AddItemToObject(object, "deadzone", json_deadzone(d->deadzone));
    if (device_smoothable(d))
    {
        cJSON *smoothing = cJSON_AddObjectToObject(object, "smoothing");
        if (smoothing)
        {
            cJSON_AddItemToObject(smoothing, "position", json_smooth(&d->smooth[XRDEV_SMOOTH_POSITION], XRDEV_SMOOTH_POSITION));
            cJSON_AddItemToObject(smoothing, "rotation", json_smooth(&d->smooth[XRDEV_SMOOTH_ROTATION], XRDEV_SMOOTH_ROTATION));
        }
    }
    return object;
}

int xrdev_save(const char *path)
{
    FILE *arq;
    cJSON *root;
    cJSON *devices;
    char *text;
    int i;

    if (!path || !path[0])
        path = state.config_path;
    if (!path || !path[0])
        return 0;

    root = cJSON_CreateObject();
    if (root)
        cJSON_AddNumberToObject(root, "version", 2);
    devices = root ? cJSON_AddArrayToObject(root, "devices") : NULL;
    if (!devices)
    {
        cJSON_Delete(root);
        xrdev_log("Out of memory while writing my config file to:%s!", path);
        return 0;
    }

    /* Rotation is written in degrees, position in metres; x is the
     * "up/down" angle, y "left/right", z the "twist". Deadzones are written
     * in percent of the stick's full travel. Smoothing exists for
     * the two hands only: strength 0.0 is raw, 1.0 fully smoothed, samples
     * 1 to 5 (the newest counts), scale is the distance at which an older
     * sample weighs half (metres for position, degrees for rotation). */
    rd_begin();
    for (i = 0; i < state.device_count; i++)
    {
        const xrdev_device *d = &state.devices[i];
        cJSON *entry;
        if (!d->in_use)
            continue;
        entry = json_device(d);
        if (entry)
            cJSON_AddItemToArray(devices, entry);
    }
    rd_end();

    text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text)
    {
        xrdev_log("Out of memory while writing my config file to:%s!", path);
        return 0;
    }

    arq = fopen(path, "wb");
    if (!arq)
    {
        cJSON_free(text);
        xrdev_log("Could not write my config file to:%s!", path);
        return 0;
    }
    fputs(text, arq);
    fputc('\n', arq);
    fclose(arq);
    cJSON_free(text);

    wr_begin();
    state.dirty = 0;
    state.last_save_ms = GetTickCount64();
    wr_end();

    return 1;
}

int xrdev_save_throttled(int period_ms)
{
    uint64_t now = GetTickCount64();

    if (!state.dirty)
        return 0;

    wr_begin();
    if (state.last_save_ms != 0 && (now - state.last_save_ms) < (uint64_t)period_ms)
    {
        wr_end();
        return 0;
    }
    state.last_save_ms = now;
    state.dirty = 0;
    wr_end();

    return xrdev_save(0);
}

    /* ------------------------------------------------------- enumeration */

    int xrdev_device_count(void)
{
    return state.device_count;
}

int xrdev_get(int index, xrdev_device *out)
{
    int ok = 0;
    if (!out || index < 0)
        return 0;
    rd_begin();
    if (index < state.device_count && state.devices[index].in_use)
    {
        *out = state.devices[index];
        ok = 1;
    }
    rd_end();
    return ok;
}

int xrdev_find(const char *path)
{
    int index;
    rd_begin();
    index = device_lookup_by_path_nolock(path);
    rd_end();
    return index;
}

int xrdev_is_active(int index)
{
    int active = 0;
    rd_begin();
    if (index >= 0 && index < state.device_count)
    {
        const xrdev_device *d = &state.devices[index];
        active = d->in_use && d->enabled && d->space_count > 0;
    }
    rd_end();
    return active;
}

int xrdev_is_injecting(void)
{
    int i, injecting = 0;
    rd_begin();
    for (i = 0; i < state.device_count; i++)
    {
        const xrdev_device *d = &state.devices[i];
        if (d->in_use && d->enabled && d->space_count > 0)
        {
            injecting = 1;
            break;
        }
    }
    rd_end();
    return injecting;
}

uint64_t xrdev_generation(void)
{
    return state.generation;
}

const xrdev_axis_info *xrdev_axis_rotation(int axis)
{
    if (axis < 0 || axis >= XRDEV_AXIS_COUNT)
        axis = 0;
    return &rotation_axes[axis];
}

const xrdev_axis_info *xrdev_axis_position(int axis)
{
    if (axis < 0 || axis >= XRDEV_AXIS_COUNT)
        axis = 0;
    return &position_axes[axis];
}

const xrdev_axis_info *xrdev_axis_deadzone(int axis)
{
    if (axis < 0 || axis >= XRDEV_STICK_AXIS_COUNT)
        axis = 0;
    return &deadzone_axes[axis];
}

    /* ------------------------------------------------------------ editing */

    static int edit_device(int index, int *changed_out)
{
    if (index < 0 || index >= state.device_count)
        return 0;
    if (!state.devices[index].in_use)
        return 0;
    if (changed_out)
        *changed_out = 1;
    return 1;
}

int xrdev_set_rotation(int index, int axis, double degrees)
{
    int changed = 0;
    if (axis < 0 || axis >= XRDEV_AXIS_COUNT)
        return 0;
    wr_begin();
    if (edit_device(index, &changed))
    {
        device_set_rotation_deg_nolock(&state.devices[index], axis, degrees);
        state.dirty = 1;
    }
    wr_end();
    return changed;
}

int xrdev_set_position(int index, int axis, double metres)
{
    int changed = 0;
    if (axis < 0 || axis >= XRDEV_AXIS_COUNT)
        return 0;
    wr_begin();
    if (edit_device(index, &changed))
    {
        device_set_position_nolock(&state.devices[index], axis, metres);
        state.dirty = 1;
    }
    wr_end();
    return changed;
}

int xrdev_nudge_rotation(int index, int axis, double degrees)
{
    xrdev_device d;
    if (!xrdev_get(index, &d))
        return 0;
    return xrdev_set_rotation(index, axis, xrdev_rad2deg(d.rotation[axis]) + degrees);
}

int xrdev_nudge_position(int index, int axis, double metres)
{
    xrdev_device d;
    if (!xrdev_get(index, &d))
        return 0;
    return xrdev_set_position(index, axis, d.position[axis] + metres);
}

int xrdev_set_deadzone(int index, int axis, double percent)
{
    int changed = 0;
    if (axis < 0 || axis >= XRDEV_STICK_AXIS_COUNT)
        return 0;
    wr_begin();
    if (edit_device(index, &changed))
    {
        device_set_deadzone_pct_nolock(&state.devices[index], axis, percent);
        state.dirty = 1;
    }
    wr_end();
    return changed;
}

int xrdev_nudge_deadzone(int index, int axis, double percent)
{
    xrdev_device d;
    if (axis < 0 || axis >= XRDEV_STICK_AXIS_COUNT)
        return 0;
    if (!xrdev_get(index, &d))
        return 0;
    return xrdev_set_deadzone(index, axis, d.deadzone[axis] * 100.0 + percent);
}

int xrdev_set_enabled(int index, int enabled)
{
    int changed = 0;
    wr_begin();
    if (edit_device(index, &changed))
    {
        state.devices[index].enabled = enabled ? 1 : 0;
        state.dirty = 1;
    }
    wr_end();
    return changed;
}

int xrdev_reset(int index)
{
    int axis, changed = 0;
    wr_begin();
    if (edit_device(index, &changed))
    {
        for (axis = 0; axis < XRDEV_AXIS_COUNT; axis++)
        {
            state.devices[index].rotation[axis] = 0.0;
            state.devices[index].position[axis] = 0.0;
        }
        state.dirty = 1;
    }
    wr_end();
    return changed;
}

int xrdev_reset_all(void)
{
    int i, axis;
    wr_begin();
    for (i = 0; i < state.device_count; i++)
    {
        if (!state.devices[i].in_use)
            continue;
        for (axis = 0; axis < XRDEV_AXIS_COUNT; axis++)
        {
            state.devices[i].rotation[axis] = 0.0;
            state.devices[i].position[axis] = 0.0;
        }
    }
    state.dirty = 1;
    wr_end();
    return 1;
}

int xrdev_mirror(int src_index, int dst_index, int mirror_yaw)
{
    int axis, changed = 0;
    xrdev_device src;

    if (!xrdev_get(src_index, &src))
        return 0;
    if (src_index == dst_index)
        return 0;

    wr_begin();
    if (edit_device(dst_index, &changed))
    {
        xrdev_device *dst = &state.devices[dst_index];
        for (axis = 0; axis < XRDEV_AXIS_COUNT; axis++)
        {
            /* Mirror across the plane x -> -x (left/right). Position x flips.
             * Rotation is an axial vector: the part along x is kept, the
             * parts about y and z flip. */
            double rsign = (mirror_yaw && axis != XRDEV_AXIS_X) ? -1.0 : 1.0;
            double psign = (mirror_yaw && axis == XRDEV_AXIS_X) ? -1.0 : 1.0;
            dst->rotation[axis] = src.rotation[axis] * rsign;
            dst->position[axis] = src.position[axis] * psign;
        }
        state.dirty = 1;
    }
    wr_end();
    return changed;
}

const xrdev_axis_info *xrdev_smooth_scale_info(int which)
{
    if (which < 0 || which >= XRDEV_SMOOTH_COUNT)
        which = 0;
    return &smooth_scale_axes[which];
}

/* Caller holds the write lock. NULL unless the device may be smoothed. */
static xrdev_smooth *smooth_target(int index, int which)
{
    if (which < 0 || which >= XRDEV_SMOOTH_COUNT)
        return 0;
    if (index < 0 || index >= state.device_count || !state.devices[index].in_use)
        return 0;
    if (!device_smoothable(&state.devices[index]))
        return 0;
    return &state.devices[index].smooth[which];
}

static void smooth_store_scale(xrdev_smooth *sm, int which, double value)
{
    const xrdev_axis_info *info = &smooth_scale_axes[which];
    value = clampd(value, info->min, info->max);
    sm->scale = which == XRDEV_SMOOTH_ROTATION ? xrdev_deg2rad(value) : value;
}

static void smooth_store_strength(xrdev_smooth *sm, double strength)
{
    sm->strength = clampd(strength, 0.0, 1.0);
}

static void smooth_store_samples(xrdev_smooth *sm, int samples)
{
    if (samples < XRDEV_SMOOTH_MIN_SAMPLES)
        samples = XRDEV_SMOOTH_MIN_SAMPLES;
    if (samples > XRDEV_SMOOTH_MAX_SAMPLES)
        samples = XRDEV_SMOOTH_MAX_SAMPLES;
    sm->samples = samples;
}

int xrdev_set_smooth_enabled(int index, int which, int enabled)
{
    xrdev_smooth *sm;
    int ok = 0;
    wr_begin();
    sm = smooth_target(index, which);
    if (sm)
    {
        sm->enabled = enabled ? 1 : 0;
        state.dirty = 1;
        ok = 1;
    }
    wr_end();
    return ok;
}

int xrdev_set_smooth_samples(int index, int which, int samples)
{
    xrdev_smooth *sm;
    int ok = 0;
    wr_begin();
    sm = smooth_target(index, which);
    if (sm)
    {
        smooth_store_samples(sm, samples);
        state.dirty = 1;
        ok = 1;
    }
    wr_end();
    return ok;
}

int xrdev_set_smooth_strength(int index, int which, double strength)
{
    xrdev_smooth *sm;
    int ok = 0;
    wr_begin();
    sm = smooth_target(index, which);
    if (sm)
    {
        smooth_store_strength(sm, strength);
        state.dirty = 1;
        ok = 1;
    }
    wr_end();
    return ok;
}

int xrdev_set_smooth_scale(int index, int which, double scale)
{
    xrdev_smooth *sm;
    int ok = 0;
    wr_begin();
    sm = smooth_target(index, which);
    if (sm)
    {
        smooth_store_scale(sm, which, scale);
        state.dirty = 1;
        ok = 1;
    }
    wr_end();
    return ok;
}

int xrdev_set_name(int index, const char *name)
{
    int changed = 0;
    if (!name || !name[0])
        return 0;
    wr_begin();
    if (edit_device(index, &changed))
    {
        copy_text(state.devices[index].name, XRDEV_NAME_MAX, name);
        state.dirty = 1;
    }
    wr_end();
    return changed;
}

    /* --------------------------------------------------------- discovery */

    void xrdev_note_application(const char *application_name, const char *engine_name)
{
    wr_begin();
    if (application_name && application_name[0])
        copy_text(state.application, XRDEV_NAME_MAX, application_name);
    if (!state.runtime[0] && engine_name && engine_name[0])
        copy_text(state.runtime, XRDEV_NAME_MAX, engine_name);
    wr_end();
}

void xrdev_note_runtime(const char *manufacturer, const char *product)
{
    char combined[XRDEV_NAME_MAX];
    if (manufacturer && manufacturer[0] && product && product[0])
        snprintf(combined, sizeof(combined), "%s %s", manufacturer, product);
    else if (product && product[0])
        copy_text(combined, sizeof(combined), product);
    else if (manufacturer && manufacturer[0])
        copy_text(combined, sizeof(combined), manufacturer);
    else
        return;

    wr_begin();
    copy_text(state.runtime, XRDEV_NAME_MAX, combined);
    wr_end();
}

void xrdev_get_application(char *out, size_t cap)
{
    rd_begin();
    copy_text(out, cap, state.application);
    rd_end();
}

void xrdev_get_runtime(char *out, size_t cap)
{
    rd_begin();
    copy_text(out, cap, state.runtime);
    rd_end();
}

int xrdev_note_subaction(uint64_t path, const char *path_string)
{
    const char *str = path_string;
    int index;

    if (!str)
    {
        rd_begin();
        str = path_lookup(path);
        rd_end();
    }
    if (!str)
        return -1;

    wr_begin();
    path_note(path, str);
    index = device_observe(str);
    wr_end();
    return index;
}

/* Caller holds the write lock. */
static void action_note(uint64_t action, const char *name)
{
    int i;
    if (action == 0)
        return;

    for (i = 0; i < state.action_count; i++)
    {
        if (state.actions[i].action == action)
        {
            copy_text(state.actions[i].name, XRDEV_NAME_MAX, name);
            return;
        }
    }
    if (state.action_count >= XRDEV_MAX_ACTIONS)
        return;

    state.actions[state.action_count].action = action;
    copy_text(state.actions[state.action_count].name, XRDEV_NAME_MAX, name);
    state.actions[state.action_count].subaction_count = 0;
    state.action_count++;
}

/* Caller holds the write lock. */
static void action_attach(uint64_t action, uint64_t subaction)
{
    int i;
    int j;

    for (i = 0; i < state.action_count; i++)
    {
        if (state.actions[i].action != action)
            continue;
        for (j = 0; j < state.actions[i].subaction_count; j++)
            if (state.actions[i].subactions[j] == subaction)
                return;
        if (state.actions[i].subaction_count >= (int)(sizeof(state.actions[i].subactions) / sizeof(state.actions[i].subactions[0])))
            return;
        state.actions[i].subactions[state.actions[i].subaction_count++] = subaction;
        return;
    }
}

/* Caller holds the write lock. Returns -1 when the action is unknown. */
static int action_device_count_nolock(uint64_t action, uint64_t *subactions, int cap)
{
    int i;
    for (i = 0; i < state.action_count; i++)
    {
        if (state.actions[i].action == action)
        {
            int n = state.actions[i].subaction_count;
            if (n > cap)
                n = cap;
            memcpy(subactions, state.actions[i].subactions, (size_t)n * sizeof(subactions[0]));
            return n;
        }
    }
    return -1;
}

/* Caller holds the write lock. */
static void device_add_action(xrdev_device *d, uint64_t action)
{
    int i;
    if (action == 0)
        return;
    for (i = 0; i < d->action_count; i++)
        if (d->actions[i] == action)
            return;
    if (d->action_count >= XRDEV_MAX_ACTIONS)
        return;
    d->actions[d->action_count++] = action;
}

/* Caller holds the write lock. */
static void device_add_space(xrdev_device *d, uint64_t space)
{
    int i;
    if (space == 0)
        return;
    for (i = 0; i < d->space_count; i++)
        if (d->spaces[i] == space)
            return;
    if (d->space_count >= XRDEV_MAX_SPACES)
        return;
    d->spaces[d->space_count++] = space;
}

/* Caller holds the write lock. */
static void device_add_source(xrdev_device *d, const char *source)
{
    int i;
    if (!source || !source[0])
        return;
    for (i = 0; i < d->source_count; i++)
        if (ci_equal(d->sources[i], source))
            return;
    if (d->source_count >= XRDEV_MAX_SOURCES)
        return;
    copy_text(d->sources[d->source_count++], XRDEV_PATH_MAX, source);
}

void xrdev_note_action(uint64_t action, const char *action_name,
                       const uint64_t *subaction_paths, int subaction_count)
{
    int i;

    wr_begin();
    action_note(action, action_name);
    for (i = 0; i < subaction_count; i++)
        action_attach(action, subaction_paths[i]);
    wr_end();
}

/* Caller holds the write lock. Finds the device whose subaction path is a
 * prefix of a binding path such as /user/hand/left/input/grip. */
static int device_for_binding_path(const char *binding)
{
    int best = -1;
    size_t best_len = 0;
    int i;

    if (!binding)
        return -1;

    for (i = 0; i < state.device_count; i++)
    {
        const char *path = state.devices[i].path;
        size_t len;
        if (!state.devices[i].in_use || !path[0])
            continue;
        len = strlen(path);
        if (strncmp(binding, path, len) != 0)
            continue;
        if (binding[len] != '/' && binding[len] != 0)
            continue;
        if (len > best_len)
        {
            best = i;
            best_len = len;
        }
    }
    return best;
}

void xrdev_note_profile(uint64_t profile_path, const uint64_t *binding_paths,
                        const uint64_t *actions, int count)
{
    const char *profile;
    int i;

    rd_begin();
    profile = path_lookup(profile_path);
    rd_end();
    if (!profile)
        return;

    wr_begin();
    for (i = 0; i < count; i++)
    {
        const char *binding = binding_paths ? path_lookup(binding_paths[i]) : 0;
        int index = device_for_binding_path(binding);
        if (index < 0)
            continue;

        if (strcmp(state.devices[index].profile, profile) != 0)
        {
            copy_text(state.devices[index].profile, XRDEV_PATH_MAX, profile);
            state.devices[index].kind = xrdev_kind_of(state.devices[index].path, profile);
            xrdev_log("NOTICE: device #%d \"%s\" uses the interaction profile %s",
                      index, state.devices[index].name, profile);
            if (actions && actions[i])
                action_attach(actions[i], 0);
        }
        state.devices[index].seen = 1;
        if (actions && actions[i])
            device_add_action(&state.devices[index], actions[i]);
    }
    wr_end();
}

int xrdev_note_current_profile(uint64_t top_level_user_path, uint64_t profile_path)
{
    char path_str[XRDEV_PATH_MAX];
    char profile[XRDEV_PATH_MAX];
    int index = -1;

    path_str[0] = 0;
    profile[0] = 0;

    rd_begin();
    copy_text(path_str, sizeof(path_str), path_lookup(top_level_user_path));
    copy_text(profile, sizeof(profile), path_lookup(profile_path));
    rd_end();

    if (!path_str[0] || !profile[0])
        return -1;

    wr_begin();
    index = device_lookup_by_path_nolock(path_str);
    if (index >= 0)
    {
        xrdev_device *d = &state.devices[index];
        d->seen = 1;
        if (strcmp(d->profile, profile) != 0)
        {
            copy_text(d->profile, XRDEV_PATH_MAX, profile);
            d->kind = xrdev_kind_of(d->path, profile);
            xrdev_log("NOTICE: the runtime mapped \"%s\" onto %s", d->name, profile);
        }
    }
    wr_end();

    return index;
}

void xrdev_note_action_sources(uint64_t action, const uint64_t *sources, int count)
{
    int i;

    wr_begin();
    for (i = 0; i < count; i++)
    {
        const char *source = sources ? path_lookup(sources[i]) : 0;
        int index = device_for_binding_path(source);
        if (index < 0 && action != 0)
        {
            /* The source path is for the action's device, take that one. */
            uint64_t subs[8];
            int n = action_device_count_nolock(action, subs, 8);
            int j;
            for (j = 0; j < n; j++)
            {
                const char *sp = path_lookup(subs[j]);
                int k = device_lookup_by_path_nolock(sp);
                if (k >= 0)
                    device_add_source(&state.devices[k], source);
            }
            continue;
        }
        if (index >= 0)
            device_add_source(&state.devices[index], source);
    }
    wr_end();
}

/* Preserves the original tool's fallback for engines that never mention
 * /user/hand/left|right as a subaction path. */
static int device_for_action_name(const char *name)
{
    if (!name)
        return -1;
    if (_strnicmp(name, "left-controller", 15) == 0)
        return xrdev_find(XRDEV_PATH_HAND_LEFT);
    if (_strnicmp(name, "right-controller", 16) == 0)
        return xrdev_find(XRDEV_PATH_HAND_RIGHT);
    return -1;
}

int xrdev_note_action_space(uint64_t space, uint64_t subaction_path, uint64_t action)
{
    int i;
    int bound = -1;
    uint64_t subs[8];
    int n;
    const char *path_str;
    int index = -1;

    wr_begin();

    path_str = path_lookup(subaction_path);
    if (path_str)
    {
        index = device_observe(path_str);
        if (index >= 0)
            state.devices[index].seen = 1;
    }

    if (index < 0)
    {
        n = action_device_count_nolock(action, subs, 8);
        for (i = 0; i < n; i++)
        {
            int k = device_lookup_by_path_nolock(path_lookup(subs[i]));
            if (k >= 0)
            {
                index = k;
                break;
            }
        }
    }

    if (index < 0)
    {
        /* The action-name fallback, then the two default hands, as before. */
        const char *name = 0;
        for (i = 0; i < state.action_count; i++)
        {
            if (state.actions[i].action == action)
            {
                name = state.actions[i].name;
                break;
            }
        }
        index = device_for_action_name(name);
    }

    if (index >= 0)
    {
        device_add_space(&state.devices[index], space);
        bound = index;
    }

    if (bound >= 0)
    {
        xrdev_log("NOTICE: action space %llu now feeds device #%d \"%s\"",
                  (unsigned long long)space, bound, state.devices[bound].name);
    }

    wr_end();
    return bound;
}

void xrdev_forget_space(uint64_t space)
{
    int i, j;
    smooth_forget(space);
    wr_begin();
    for (i = 0; i < state.device_count; i++)
    {
        xrdev_device *d = &state.devices[i];
        for (j = 0; j < d->space_count; j++)
        {
            if (d->spaces[j] == space)
            {
                d->spaces[j] = d->spaces[d->space_count - 1];
                d->space_count--;
                break;
            }
        }
    }
    wr_end();
}

int xrdev_index_of_space(uint64_t space)
{
    int i, j;
    int found = -1;
    if (space == 0)
        return -1;
    rd_begin();
    for (i = 0; i < state.device_count && found < 0; i++)
    {
        const xrdev_device *d = &state.devices[i];
        if (!d->in_use)
            continue;
        for (j = 0; j < d->space_count; j++)
        {
            if (d->spaces[j] == space)
            {
                found = i;
                break;
            }
        }
    }
    rd_end();
    return found;
}

    /* --------------------------------------------------------- injection */

    /* True when every offset is zero, so there is nothing to inject. */
    static int device_idle(const xrdev_device *d)
{
    int a;
    for (a = 0; a < XRDEV_AXIS_COUNT; a++)
    {
        if (d->rotation[a] != 0.0 || d->position[a] != 0.0)
            return 0;
    }
    return 1;
}

/* Back onto the unit sphere. Quaternion_rotate() is a rotation only for unit
 * quaternions: give it one of length 1.3 and it scales the vector it turns by
 * 1.3 squared, so a 0.1 m offset quietly becomes 0.169 m. Runtimes do hand
 * back slightly off quaternions, and wildly off ones for poses they are not
 * sure about, so every orientation taken from a pose is put through this. */
static void quaternion_force_unit(Quaternion *q)
{
    float len = Quaternion_norm(q);

    if (len > 1e-6f && len != 1.0f)
    {
        Quaternion unit;
        Quaternion_normalize(q, &unit);
        *q = unit;
    }
}

/* Caller holds a lock that allows writing to *pose. */
static void inject(const xrdev_device *d, int orientation_valid, xrdev_pose *pose)
{
    Quaternion hand;
    Quaternion rotation;
    Quaternion result;
    float euler[3];
    float offset[3];
    float shifted[3];

    /* Quaternion.h stores v[0..2] as x,y,z, the same order as OpenXR, so the
     * parts are copied straight across. The old code swapped x and z here
     * (and used the Z_AXIS/Y_AXIS/X_AXIS enum, which is 0/1/2 in that order).
     * A swap is a mirror, not a rotation: it left the orientation product
     * correct by accident but made the position offset rotate backwards
     * and around the wrong axes. */
    hand.v[0] = (float)pose->orientation[XRDEV_AXIS_X];
    hand.v[1] = (float)pose->orientation[XRDEV_AXIS_Y];
    hand.v[2] = (float)pose->orientation[XRDEV_AXIS_Z];
    hand.w = (float)pose->orientation[3];

    /* Only a pose the runtime swears is tracked can be trusted to turn
     * anything. For an untracked one the orientation is a guess (often an
     * exact identity, sometimes stale, and not always unit length). */
    if (orientation_valid)
        quaternion_force_unit(&hand);

    /* fromEulerZYX takes [roll about x, pitch about y, yaw about z] and builds
     * Qz(yaw) * Qy(pitch) * Qx(roll). */
    euler[0] = (float)d->rotation[XRDEV_AXIS_X];
    euler[1] = (float)d->rotation[XRDEV_AXIS_Y];
    euler[2] = (float)d->rotation[XRDEV_AXIS_Z];
    Quaternion_fromEulerZYX(euler, &rotation);

    /* The offset lives in the controller's own frame:
     *   new pose = pose * offset
     *   orientation' = hand * rotation
     *   position'    = position + hand * offset_position
     * so the position offset has to be turned by the controller's real
     * orientation, not by the offset rotation. */
    offset[0] = (float)d->position[XRDEV_AXIS_X];
    offset[1] = (float)d->position[XRDEV_AXIS_Y];
    offset[2] = (float)d->position[XRDEV_AXIS_Z];

    if (orientation_valid)
        Quaternion_rotate(&hand, offset, shifted);
    else
    {
        /* Nothing trustworthy to turn the offset by. Adding it along the base
         * space's own axes is the least wrong answer: it keeps the length the
         * user asked for instead of pointing it along a stale or invented
         * orientation. (The pose is flagged untracked anyway, so a well
         * behaved application never looks at the result.) */
        shifted[0] = offset[0];
        shifted[1] = offset[1];
        shifted[2] = offset[2];
    }

    Quaternion_multiply(&hand, &rotation, &result);
    quaternion_force_unit(&result);

    pose->position[XRDEV_AXIS_X] += shifted[0];
    pose->position[XRDEV_AXIS_Y] += shifted[1];
    pose->position[XRDEV_AXIS_Z] += shifted[2];

    pose->orientation[XRDEV_AXIS_X] = result.v[0];
    pose->orientation[XRDEV_AXIS_Y] = result.v[1];
    pose->orientation[XRDEV_AXIS_Z] = result.v[2];
    pose->orientation[3] = result.w;
}

/* ---------------------------------------------------------- smoothing */

/* The last few raw poses of one pose stream, a stream being one action space
 * located against one base space. Raw means before the offsets, so changing an
 * offset while playing never leaves the history pointing somewhere else. */
typedef struct smooth_slot
{
    uint64_t space;
    uint64_t base;
    int64_t last_time;
    int count; /* samples held, newest first */
    double position[XRDEV_SMOOTH_MAX_SAMPLES][3];
    double orientation[XRDEV_SMOOTH_MAX_SAMPLES][4]; /* x y z w */
} smooth_slot;

#define SMOOTH_SLOTS 64
/* A gap this long (ns) means the history is about something else now. */
#define SMOOTH_MAX_GAP_NS 250000000LL

static SRWLOCK smooth_lock; /* zeroed == SRWLOCK_INIT, like state.lock */
static smooth_slot smooth_slots[SMOOTH_SLOTS];
static int smooth_next_victim;

/* Caller holds smooth_lock. */
static smooth_slot *smooth_slot_for(uint64_t space, uint64_t base)
{
    int i;
    smooth_slot *free_slot = 0;

    for (i = 0; i < SMOOTH_SLOTS; i++)
    {
        if (smooth_slots[i].space == space && smooth_slots[i].base == base)
            return &smooth_slots[i];
        if (!free_slot && smooth_slots[i].space == 0)
            free_slot = &smooth_slots[i];
    }

    if (!free_slot)
    {
        free_slot = &smooth_slots[smooth_next_victim];
        smooth_next_victim = (smooth_next_victim + 1) % SMOOTH_SLOTS;
    }

    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->space = space;
    free_slot->base = base;
    return free_slot;
}

static void smooth_forget(uint64_t space)
{
    int i;
    AcquireSRWLockExclusive(&smooth_lock);
    for (i = 0; i < SMOOTH_SLOTS; i++)
    {
        if (smooth_slots[i].space == space)
            memset(&smooth_slots[i], 0, sizeof(smooth_slots[i]));
    }
    ReleaseSRWLockExclusive(&smooth_lock);
}

/* Angle between two orientations, radians, 0..pi. */
static double orientation_distance(const double *a, const double *b)
{
    double dot = fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    if (dot > 1.0)
        dot = 1.0;
    return 2.0 * acos(dot);
}

/* The newest sample counts 1.0. An older one counts 1 / (1 + (d / scale)^2):
 * 1.0 when it sits on the newest, 0.5 at `scale`, 0.1 at three times that.
 * The weight depends on the distance to the newest sample only, so a fast
 * movement (everything old is far away) answers with the raw pose at once.
 *
 * On top of that sample number `i` of `n` is scaled by (n - i) / n, so the
 * older, the less it counts, even with a hand that does not move at all.
 * The weighted mean is then mixed with the newest raw sample by `strength`. */
static double smooth_weight(double distance, double scale)
{
    double r = distance / scale;
    return 1.0 / (1.0 + r * r);
}

/* Caller holds smooth_lock. Updates the history with the raw pose and
 * rewrites *pose with the smoothed one. */
static void smooth_pose(const xrdev_device *d, uint64_t space, uint64_t base,
                        int64_t time, int position_valid, int orientation_valid,
                        xrdev_pose *pose)
{
    const xrdev_smooth *sp = &d->smooth[XRDEV_SMOOTH_POSITION];
    const xrdev_smooth *sr = &d->smooth[XRDEV_SMOOTH_ROTATION];
    smooth_slot *slot = smooth_slot_for(space, base);
    int i, k;

    /* Not tracked: nothing to smooth, and what came before is stale. */
    if (!position_valid || !orientation_valid)
    {
        slot->count = 0;
        return;
    }

    if (slot->count > 0)
    {
        if (time < slot->last_time)
            return; /* a query about the past, leave the stream alone */
        if (time - slot->last_time > SMOOTH_MAX_GAP_NS)
            slot->count = 0;
    }

    /* Same instant asked again: replace the newest and keep the rest, so the
     * answer is the same every time the game asks. */
    if (!(slot->count > 0 && time == slot->last_time))
    {
        for (i = XRDEV_SMOOTH_MAX_SAMPLES - 1; i > 0; i--)
        {
            memcpy(slot->position[i], slot->position[i - 1], sizeof(slot->position[i]));
            memcpy(slot->orientation[i], slot->orientation[i - 1], sizeof(slot->orientation[i]));
        }
        if (slot->count < XRDEV_SMOOTH_MAX_SAMPLES)
            slot->count++;
        slot->last_time = time;
    }

    memcpy(slot->position[0], pose->position, sizeof(slot->position[0]));
    memcpy(slot->orientation[0], pose->orientation, sizeof(slot->orientation[0]));

    if (sp->enabled && sp->samples > 1 && slot->count > 1)
    {
        double sum[3] = {slot->position[0][0], slot->position[0][1], slot->position[0][2]};
        double total = 1.0;
        int n = sp->samples < slot->count ? sp->samples : slot->count;

        for (i = 1; i < n; i++)
        {
            double dx = slot->position[i][0] - slot->position[0][0];
            double dy = slot->position[i][1] - slot->position[0][1];
            double dz = slot->position[i][2] - slot->position[0][2];
            double w = smooth_weight(sqrt(dx * dx + dy * dy + dz * dz), sp->scale) *
                       (double)(n - i) / (double)n;

            for (k = 0; k < 3; k++)
                sum[k] += w * slot->position[i][k];
            total += w;
        }
        for (k = 0; k < 3; k++)
            pose->position[k] = slot->position[0][k] +
                                sp->strength * (sum[k] / total - slot->position[0][k]);
    }

    if (sr->enabled && sr->samples > 1 && slot->count > 1)
    {
        const double *q0 = slot->orientation[0];
        double sum[4] = {q0[0], q0[1], q0[2], q0[3]};
        double len;
        int n = sr->samples < slot->count ? sr->samples : slot->count;

        for (i = 1; i < n; i++)
        {
            const double *q = slot->orientation[i];
            double w = smooth_weight(orientation_distance(q0, q), sr->scale) *
                       (double)(n - i) / (double)n;
            /* q and -q are the same rotation: add the one on q0's side. */
            double side = (q0[0] * q[0] + q0[1] * q[1] + q0[2] * q[2] + q0[3] * q[3]) < 0.0 ? -1.0 : 1.0;

            for (k = 0; k < 4; k++)
                sum[k] += w * side * q[k];
        }

        len = sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2] + sum[3] * sum[3]);
        if (len > 1e-9)
        {
            /* the weighted mean (normalised) mixed with the newest sample */
            double mix[4];
            double mlen;

            for (k = 0; k < 4; k++)
                mix[k] = q0[k] + sr->strength * (sum[k] / len - q0[k]);
            mlen = sqrt(mix[0] * mix[0] + mix[1] * mix[1] + mix[2] * mix[2] + mix[3] * mix[3]);
            if (mlen > 1e-9)
            {
                for (k = 0; k < 4; k++)
                    pose->orientation[k] = mix[k] / mlen;
            }
        }
    }
}

int xrdev_apply_offset(uint64_t space, uint64_t base_space, int64_t time,
                       int position_valid, int orientation_valid, xrdev_pose *pose)
{
    int i, j;
    int applied = 0;

    if (!pose || space == 0)
        return 0;

    /* A space asked about against itself. The honest answer is the identity
     * pose, and an offset is precisely the wrong thing to add to that: it
     * would tell a game that its own controller sits `position` metres away
     * from itself, turned by `rotation`. (An offset here describes where one
     * endpoint sits; a locate between two offset devices would need the base
     * device's offset undone as well, which is not what this injects.) */
    if (space == base_space)
        return 0;

    rd_begin();

    for (i = 0; i < state.device_count; i++)
    {
        xrdev_device *d = &state.devices[i];

        if (!d->in_use || !d->enabled)
            continue;

        for (j = 0; j < d->space_count; j++)
        {
            if (d->spaces[j] != space)
                continue;

            /* locate_count is only an on-screen hint for the interface, so
             * it is counted without asking for the exclusive lock. */
            d->locate_count++;

            /* Smoothing first, on the raw pose. Hands only: the headset is
             * not an action space and never gets here, and device_smoothable
             * keeps trackers and anything else out as well. */
            if (device_smoothable(d) && (d->smooth[XRDEV_SMOOTH_POSITION].enabled ||
                                         d->smooth[XRDEV_SMOOTH_ROTATION].enabled))
            {
                AcquireSRWLockExclusive(&smooth_lock);
                smooth_pose(d, space, base_space, time, position_valid, orientation_valid, pose);
                ReleaseSRWLockExclusive(&smooth_lock);
                applied = 1;
            }

            if (!device_idle(d))
            {
                inject(d, orientation_valid, pose);
                applied = 1;
            }
            break;
        }

        if (applied)
            break;
    }

    rd_end();
    return applied;
}

    /* ---------------------------------------------------- stick deadzones */

/* One axis of one stick. A reading inside the deadzone becomes 0, which is
 * what swallows the drift; a reading outside it is moved back over the rest
 * of the travel, so the stick still reaches -1.0 and 1.0 and nothing jumps
 * where the deadzone ends. When the deadzone is the whole travel there is no
 * "rest of the travel" left to move into, so the axis simply reports 0 and the
 * stick is ignored; that is how one axis of a broken stick is switched off
 * while the other one goes on working. `zone` is the fraction of full travel,
 * already clamped to 0..1. */
static int deadzone_axis(double zone, float *value)
{
    float v = *value;
    float mag = v < 0.0f ? -v : v;
    float limit = (float)zone;
    float out;

    if (zone <= 0.0)
        return 0;

    if (zone >= 1.0)
    {
        if (v == 0.0f)
            return 0;
        *value = 0.0f;
        return 1;
    }

    if (mag <= limit)
    {
        if (v == 0.0f)
            return 0;
        *value = 0.0f;
        return 1;
    }

    out = (mag - limit) / (1.0f - limit);
    if (out >= 1.0f)
        return 0; /* the far end of the travel is already where it belongs */
    *value = v < 0.0f ? -out : out;
    return 1;
}

int xrdev_apply_deadzone(uint64_t subaction_path, float *x, float *y)
{
    const xrdev_device *d;
    const char *path;
    int index;

    if (!x || !y)
        return 0;

    rd_begin();

    /* The hooks are handed the number OpenXR stands for "/user/hand/left",
     * known here because the game made it with xrStringToPath; every other
     * hook in this file finds a device the same way. An unknown number is
     * left completely alone rather than guessed at. */
    path = path_lookup(subaction_path);
    if (!path)
    {
        rd_end();
        return 0;
    }

    index = device_lookup_by_path_nolock(path);
    if (index < 0 || !state.devices[index].in_use || !state.devices[index].enabled)
    {
        rd_end();
        return 0;
    }
    d = &state.devices[index];

    /* One zone per axis, since a worn stick usually drifts along one of them. */
    {
        int hit = deadzone_axis(d->deadzone[XRDEV_STICK_X], x);
        hit |= deadzone_axis(d->deadzone[XRDEV_STICK_Y], y);
        rd_end();
        return hit;
    }
}

    /* ------------------------------------------------- simulated sessions */

    int xrdev_simulate(const char *path, const char *profile, const char *const *sources, int source_count)
{
    int index;
    int i;

    if (!path || !path[0])
        return -1;

    wr_begin();
    index = device_observe(path);
    if (index >= 0)
    {
        xrdev_device *d = &state.devices[index];
        if (profile && profile[0])
        {
            copy_text(d->profile, XRDEV_PATH_MAX, profile);
            d->kind = xrdev_kind_of(d->path, d->profile);
        }
        d->seen = 1;
        for (i = 0; i < source_count && sources; i++)
            device_add_source(d, sources[i]);
        /* Each device owns one action space per role, as a session would. */
        device_add_space(d, (uint64_t)(index + 1) * 0x1000);
    }
    wr_end();

    return index;
}
