// SPDX-License-Identifier: GPL-3.0-only

#include "log.h"
#include "mode.h"
#include "utils_wayland.h"

#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <time.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define DIR_DOWN  (1 << 0)
#define DIR_UP    (1 << 1)
#define DIR_RIGHT (1 << 2)
#define DIR_LEFT  (1 << 3)

#define NUM_BUTTONS 3

// Index into `mouse_button_codes` and `mouse_button_keys`.
#define BUTTON_LEFT   0
#define BUTTON_RIGHT  1
#define BUTTON_MIDDLE 2

static const uint32_t mouse_button_codes[NUM_BUTTONS] = {
    BTN_LEFT,
    BTN_RIGHT,
    BTN_MIDDLE,
};

struct mouse_mode_state {
    struct zwlr_virtual_pointer_v1 *pointer;
    unsigned                        held_dirs;
    unsigned                        held_buttons;
};

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static unsigned dir_for_key(xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_j:
        return DIR_DOWN;
    case XKB_KEY_k:
        return DIR_UP;
    case XKB_KEY_l:
        return DIR_RIGHT;
    case XKB_KEY_h:
        return DIR_LEFT;
    default:
        return 0;
    }
}

// Returns the button index for a key, or -1 if the key is not a button.
static int button_for_key(xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_f:
        return BUTTON_LEFT;
    case XKB_KEY_d:
        return BUTTON_RIGHT;
    case XKB_KEY_s:
        return BUTTON_MIDDLE;
    default:
        return -1;
    }
}

static void
move_by_dirs(struct state *state, struct mouse_mode_state *ms, unsigned dirs) {
    int step = state->config.mode_mouse.step;
    int dx   = 0;
    int dy   = 0;

    if (dirs & DIR_DOWN) {
        dy += step;
    }
    if (dirs & DIR_UP) {
        dy -= step;
    }
    if (dirs & DIR_RIGHT) {
        dx += step;
    }
    if (dirs & DIR_LEFT) {
        dx -= step;
    }

    if (dx != 0 || dy != 0) {
        virtual_pointer_move_relative(ms->pointer, now_ms(), dx, dy);
    }
}

// Starts or stops key repeat for held direction keys. The compositor's
// repeat rate and delay are used, as Wayland clients don't receive repeats.
static void set_repeat(struct state *state, bool on) {
    struct itimerspec its = {0};

    if (on && state->repeat_rate > 0 && state->repeat_timer_fd >= 0) {
        int delay_ms    = state->repeat_delay > 0 ? state->repeat_delay : 1;
        int interval_ms = 1000 / state->repeat_rate;
        if (interval_ms < 1) {
            interval_ms = 1;
        }

        its.it_value.tv_sec     = delay_ms / 1000;
        its.it_value.tv_nsec    = (delay_ms % 1000) * 1000000L;
        its.it_interval.tv_sec  = interval_ms / 1000;
        its.it_interval.tv_nsec = (interval_ms % 1000) * 1000000L;
    }

    timerfd_settime(state->repeat_timer_fd, 0, &its, NULL);
}

static void release_held_buttons(struct mouse_mode_state *ms) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (ms->held_buttons & (1u << i)) {
            virtual_pointer_button(
                ms->pointer, now_ms(), mouse_button_codes[i],
                WL_POINTER_BUTTON_STATE_RELEASED
            );
            ms->held_buttons &= ~(1u << i);
        }
    }
}

static void *mouse_mode_enter(struct state *state, struct rect area) {
    struct mouse_mode_state *ms = calloc(1, sizeof(struct mouse_mode_state));

    ms->pointer = virtual_pointer_create(state);
    if (ms->pointer == NULL) {
        LOG_ERR("Could not create a virtual pointer for mouse mode.");
        state->running = false;
        return ms;
    }

    // Warp to the selected area, then move relative from there.
    virtual_pointer_move_absolute(
        state, ms->pointer, area.x + area.w / 2, area.y + area.h / 2
    );

    return ms;
}

static void mouse_mode_reenter(struct state *state, void *mode_state) {}

static bool mouse_mode_key(
    struct state *state, void *mode_state, xkb_keysym_t keysym, char *text
) {
    struct mouse_mode_state *ms = mode_state;

    if (keysym == XKB_KEY_Escape) {
        release_held_buttons(ms);
        set_repeat(state, false);
        zwlr_virtual_pointer_v1_destroy(ms->pointer);
        ms->pointer    = NULL;
        state->running = false;
        return false;
    }

    unsigned dir = dir_for_key(keysym);
    if (dir != 0) {
        bool was_idle  = ms->held_dirs == 0;
        ms->held_dirs |= dir;
        move_by_dirs(state, ms, dir);
        if (was_idle) {
            set_repeat(state, true);
        }
        return false;
    }

    int btn = button_for_key(keysym);
    if (btn >= 0 && !(ms->held_buttons & (1u << btn))) {
        ms->held_buttons |= 1u << btn;
        virtual_pointer_button(
            ms->pointer, now_ms(), mouse_button_codes[btn],
            WL_POINTER_BUTTON_STATE_PRESSED
        );
    }

    return false;
}

static void mouse_mode_key_release(
    struct state *state, void *mode_state, xkb_keysym_t keysym
) {
    struct mouse_mode_state *ms = mode_state;

    unsigned dir = dir_for_key(keysym);
    if (dir != 0) {
        ms->held_dirs &= ~dir;
        if (ms->held_dirs == 0) {
            set_repeat(state, false);
        }
        return;
    }

    int btn = button_for_key(keysym);
    if (btn >= 0 && (ms->held_buttons & (1u << btn))) {
        virtual_pointer_button(
            ms->pointer, now_ms(), mouse_button_codes[btn],
            WL_POINTER_BUTTON_STATE_RELEASED
        );
        ms->held_buttons &= ~(1u << btn);
    }
}

static void mouse_mode_timer(struct state *state, void *mode_state) {
    struct mouse_mode_state *ms = mode_state;
    if (ms->pointer != NULL) {
        move_by_dirs(state, ms, ms->held_dirs);
    }
}

static void
mouse_mode_render(struct state *state, void *mode_state, cairo_t *cairo) {
    // Keep the overlay fully transparent so the windows below stay visible.
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cairo, 0, 0, 0, 0);
    cairo_paint(cairo);
}

static void mouse_mode_free(void *mode_state) {
    free(mode_state);
}

static void mouse_mode_restart(struct state *state, void *mode_state) {}

struct mode_interface mouse_mode_interface = {
    .name        = "mouse",
    .enter       = mouse_mode_enter,
    .reenter     = mouse_mode_reenter,
    .key         = mouse_mode_key,
    .key_release = mouse_mode_key_release,
    .timer       = mouse_mode_timer,
    .render      = mouse_mode_render,
    .free        = mouse_mode_free,
    .restart     = mouse_mode_restart,
};
