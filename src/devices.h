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

    The device model behind the proxy.

    Everything the proxy knows about the hardware a game is talking to lives
    here: which subaction paths the game declared (/user/hand/left,
    /user/hand/right, /user/vive_tracker_htcx/role/handheld, ...), which
    interaction profile each of them resolved to, which actions and action
    spaces belong to which device, and the position/rotation offsets that get
    injected into xrLocateSpace for that device.

    The interface is deliberately free of OpenXR types (handles are plain
    uint64_t) so the UI can be built and run without an OpenXR SDK, see the
    "ui" make target.

    All functions are safe to call from more than one thread: the proxy hooks
    run on the game's threads and the UI runs on its own.
*/

#ifndef XRDEVICES_H
#define XRDEVICES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define XRDEV_MAX_DEVICES 32
#define XRDEV_MAX_SPACES 96
#define XRDEV_MAX_ACTIONS 128
#define XRDEV_MAX_SOURCES 6
#define XRDEV_PATH_MAX 160
#define XRDEV_NAME_MAX 64
#define XRDEV_LABEL_MAX 96

/* The two devices the original version of this tool knew about. They are
 * always present so existing configuration files keep working. */
#define XRDEV_PATH_HAND_LEFT "/user/hand/left"
#define XRDEV_PATH_HAND_RIGHT "/user/hand/right"

/* The path used by a device that has no subaction path at all. */
#define XRDEV_PATH_NONE ""

    enum
    {
        XRDEV_AXIS_X = 0,
        XRDEV_AXIS_Y = 1,
        XRDEV_AXIS_Z = 2,
        XRDEV_AXIS_COUNT = 3
    };

    typedef enum xrdev_kind
    {
        XRDEV_KIND_CONTROLLER = 0, /* a hand controller of any vendor */
        XRDEV_KIND_HAND,           /* XR_EXT_hand_tracking / hand poses */
        XRDEV_KIND_TRACKER,        /* a generic tracker role */
        XRDEV_KIND_GENERIC,        /* anything else with a /user/ subaction */
        XRDEV_KIND__COUNT
    } xrdev_kind;

    /* Describes how one axis is shown and stepped in the interface. */
    typedef struct xrdev_axis_info
    {
        const char *name;   /* "Pitch" / "left right" */
        const char *short_name; /* "X" */
        const char *unit;   /* "°" or "m" */
        double min, max;    /* clamp range in the axis' own unit */
        double coarse;      /* big nudge, in the axis' own unit */
        double fine;        /* small nudge, in the axis' own unit */
    } xrdev_axis_info;

    typedef struct xrdev_device
    {
        int in_use;             /* slot holds a device */
        int index;              /* stable index, 0 based */
        char path[XRDEV_PATH_MAX];  /* subaction path, the device's identity */
        char name[XRDEV_NAME_MAX];  /* "Left hand" */
        char profile[XRDEV_PATH_MAX]; /* /interaction_profiles/valve/index_controller */
        xrdev_kind kind;

        int builtin;            /* seeded at start up (the two hands) */
        int enabled;            /* offsets are injected when true */
        double rotation[XRDEV_AXIS_COUNT];  /* radians */
        double position[XRDEV_AXIS_COUNT];  /* metres */

        /* What the session told us about this device. */
        uint64_t spaces[XRDEV_MAX_SPACES];
        int space_count;
        uint64_t actions[XRDEV_MAX_ACTIONS];
        int action_count;
        char sources[XRDEV_MAX_SOURCES][XRDEV_PATH_MAX]; /* /user/hand/left/input/grip */
        int source_count;
        uint64_t locate_count;  /* xrLocateSpace calls offset for this device */

        int seen;               /* the running session mentioned this device */
    } xrdev_device;

/* A pose, in the same units and order OpenXR uses (x,y,z and x,y,z,w). */
typedef struct xrdev_pose
{
    double position[3];
    double orientation[4];
} xrdev_pose;

#if defined(XRDEV_BUILD_DLL)
#define XRDEV_API __declspec(dllexport)
#elif defined(XRDEV_IMPORT_DLL)
#define XRDEV_API __declspec(dllimport)
#else
#define XRDEV_API
#endif

    /* Where "what the proxy saw" messages go. The proxy installs its own
     * logger; without one the model stays quiet. */
    typedef void (*xrdev_log_fn)(const char *message);
    XRDEV_API void xrdev_set_logger(xrdev_log_fn fn);

    /* -------------------------------------------------- start up / config */

    /* Seeds the built in devices. Idempotent. */
    XRDEV_API void xrdev_init(void);

    /* Loads/saves every device. Loading replaces the offsets of devices that
     * are named in the file and leaves the others alone. */
    XRDEV_API int xrdev_load(const char *path);
    XRDEV_API int xrdev_save(const char *path);
    /* The path used by the last xrdev_load/xrdev_save, or NULL. */
    XRDEV_API const char *xrdev_config_path(void);
    /* Unsaved changes, so the interface can grey out its save button. */
    XRDEV_API int xrdev_is_dirty(void);
    XRDEV_API void xrdev_mark_dirty(void);
    /* Writes the file when dirty and at most once every xrdev_save_period_ms. */
    XRDEV_API int xrdev_save_throttled(int period_ms);
    /* Bumps whenever a device appears or disappears, so the UI can animate. */
    XRDEV_API uint64_t xrdev_generation(void);

    /* -------------------------------------------------------- enumeration */

    XRDEV_API int xrdev_device_count(void);
    /* Copies device number `index` into *out. Returns 0 when out of range. */
    XRDEV_API int xrdev_get(int index, xrdev_device *out);
    /* Index of the device with that subaction path, or -1. */
    XRDEV_API int xrdev_find(const char *path);
    /* -1 when no offset would be applied for that index. */
    XRDEV_API int xrdev_is_active(int index);

    XRDEV_API xrdev_kind xrdev_kind_of(const char *path, const char *profile);
    /* "Left hand", "Right hand", "Left foot"... writes into out. */
    XRDEV_API void xrdev_default_name(const char *path, char *out, size_t cap);
    /* Short human readable name of a controller profile, may be NULL. */
    XRDEV_API const char *xrdev_profile_label(const char *profile);
    XRDEV_API const char *xrdev_kind_label(xrdev_kind kind);

    XRDEV_API const xrdev_axis_info *xrdev_axis_rotation(int axis);
    XRDEV_API const xrdev_axis_info *xrdev_axis_position(int axis);

    /* Degrees <-> radians, the config file and the UI speak degrees. */
    XRDEV_API double xrdev_deg2rad(double degrees);
    XRDEV_API double xrdev_rad2deg(double radians);

    /* ------------------------------------------------------- editing (UI) */

    /* All setters take the axis' own unit (degrees / metres) and clamp. */
    XRDEV_API int xrdev_set_rotation(int index, int axis, double degrees);
    XRDEV_API int xrdev_set_position(int index, int axis, double metres);
    XRDEV_API int xrdev_nudge_rotation(int index, int axis, double degrees);
    XRDEV_API int xrdev_nudge_position(int index, int axis, double metres);
    XRDEV_API int xrdev_set_enabled(int index, int enabled);
    XRDEV_API int xrdev_reset(int index);
    XRDEV_API int xrdev_reset_all(void);
    /* Copies src's offsets onto dst. Used by the "mirror to the other hand"
     * button: yaw is negated since the hands face each other. */
    XRDEV_API int xrdev_mirror(int src_index, int dst_index, int mirror_yaw);
    /* Renames the (usually auto-detected) display name of a device. */
    XRDEV_API int xrdev_set_name(int index, const char *name);

    /* -------------------------------------------------- discovery (hooks) */

    /* xrCreateInstance: which game is on the other side. */
    XRDEV_API void xrdev_note_application(const char *application_name, const char *engine_name);
    /* xrGetSystemProperties: which runtime/headset answered. */
    XRDEV_API void xrdev_note_runtime(const char *manufacturer, const char *product);
    XRDEV_API void xrdev_get_application(char *out, size_t cap);
    XRDEV_API void xrdev_get_runtime(char *out, size_t cap);

    /* xrStringToPath: remember what a path number means, so every other hook
     * can name the paths it is handed. Unknown paths stay unknown. */
    XRDEV_API void xrdev_note_path(uint64_t path, const char *path_string);
    XRDEV_API const char *xrdev_path_string(uint64_t path); /* NULL when unknown */

    /* Registers a /user/... subaction path as a device. Returns its index. */
    XRDEV_API int xrdev_note_subaction(uint64_t path, const char *path_string);

    /* xrCreateAction: remember which subactions an action is bound to. */
    XRDEV_API void xrdev_note_action(uint64_t action, const char *action_name,
                                     const uint64_t *subaction_paths, int subaction_count);

    /* xrSuggestInteractionProfileBindings: the profile tells us the exact
     * controller model, the binding paths (/user/hand/left/input/grip) say
     * which devices those bindings are for. */
    XRDEV_API void xrdev_note_profile(uint64_t profile_path,
                                      const uint64_t *binding_paths,
                                      const uint64_t *actions, int count);

    /* xrGetCurrentInteractionProfile: which controller the runtime actually
     * mapped into that subaction path. Returns the device index, -1 when the
     * paths were never seen. */
    XRDEV_API int xrdev_note_current_profile(uint64_t top_level_user_path,
                                             uint64_t profile_path);

    /* xrEnumerateBoundSourcesForAction: physical inputs behind an action. */
    XRDEV_API void xrdev_note_action_sources(uint64_t action, const uint64_t *sources, int count);

    /* xrCreateActionSpace: the space whose pose we will rewrite. */
    XRDEV_API int xrdev_note_action_space(uint64_t space, uint64_t subaction_path, uint64_t action);
    /* xrDestroySpace. */
    XRDEV_API void xrdev_forget_space(uint64_t space);

    /* Index of the device that owns that space, -1 when it owns none. */
    XRDEV_API int xrdev_index_of_space(uint64_t space);
    /* True when any device is enabled and tracking at least one space: the
     * interface uses it to say "not injecting anything yet". */
    XRDEV_API int xrdev_is_injecting(void);

    /* Stands in for a real session: registers a device with a profile and
     * some bound inputs, as if a game had just declared them. Only the test
     * interface uses this, see the "ui" make target. */
    XRDEV_API int xrdev_simulate(const char *path, const char *profile,
                                 const char *const *sources, int source_count);

    /* ------------------------------------------------------- injection */

    /* Rewrites *pose with the offsets of the device that owns `space`.
     * Returns 0 when the space belongs to no device or is disabled. */
    XRDEV_API int xrdev_apply_offset(uint64_t space, xrdev_pose *pose);

#ifdef __cplusplus
}
#endif

#endif /* XRDEVICES_H */
