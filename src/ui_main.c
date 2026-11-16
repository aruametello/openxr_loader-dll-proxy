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

    Standalone build of the interface, so it can be opened and checked
    without a game, a runtime or even an OpenXR SDK: see the "ui" make
    target.

        ui_debug.exe                       a made up session, to see the
                                           interface with devices in it
        ui_debug.exe --live                only what the config file holds
        ui_debug.exe --config path.txt     read and write that file
        ui_debug.exe --shot out.png        save a PNG and quit
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "devices.h"

#define MOCK_CONFIG "ui_test_config.txt"

    static void log_to_console(const char *message)
{
    printf("%s\n", message);
}

/* Stands in for a game that has just declared its controllers: something gets
 * mapped at each hand space, and the panels name whatever that something is.
 * Pass a different interaction profile to see the panels say so. */
static void seed_mock_session(const char *left_profile, const char *right_profile)
{
    static const char *left_sources[] = {"/user/hand/left/input/grip",
                                         "/user/hand/left/input/aim"};
    static const char *right_sources[] = {"/user/hand/right/input/grip",
                                          "/user/hand/right/input/aim"};

    xrdev_note_application("Mock Session", "Unity");
    xrdev_note_runtime("Valve", "Index HMD");

    xrdev_simulate(XRDEV_PATH_HAND_LEFT, left_profile, left_sources, 2);
    xrdev_simulate(XRDEV_PATH_HAND_RIGHT, right_profile, right_sources, 2);
}

static void usage(const char *argv0)
{
    printf("usage: %s [--live] [--config <file>] [--shot <png>]\n"
           "              [--profile <interaction profile>] [--frames <n>]\n"
           "              [--exit-after <ms>] [--quiet]\n",
           argv0);
}

int main(int argc, char **argv)
{
    xrui_options options;
    const char *config = MOCK_CONFIG;
    const char *profile = "/interaction_profiles/valve/index_controller";
    int live = 0;
    int quiet = 0;
    int i;

    memset(&options, 0, sizeof(options));

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--live") == 0)
            live = 1;
        else if (strcmp(argv[i], "--quiet") == 0)
            quiet = 1;
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            config = argv[++i];
        else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc)
            profile = argv[++i];
        else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc)
            options.screenshot = argv[++i];
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            options.screenshot_frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--exit-after") == 0 && i + 1 < argc)
            options.exit_after_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            options.width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            options.height = atoi(argv[++i]);
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    xrdev_init();

    if (!quiet)
        xrdev_set_logger(log_to_console);

    if (xrdev_load(config))
        printf("read %s\n", config);

    if (!live)
        seed_mock_session(profile, profile);

    options.config_path = config;
    if (options.screenshot_frames == 0)
        options.screenshot_frames = 60;

    return xrui_run(&options);
}
