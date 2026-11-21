// SPDX-License-Identifier: GPL-3.0-only

#ifndef __MODE_H_INCLUDED__
#define __MODE_H_INCLUDED__

#include "state.h"

#include <cairo.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

struct mode_interface {
    char *name;
    void *(*enter)(struct state *, struct rect area);
    void (*reenter)(struct state *, void *mode_state);
    bool (*key)(struct state *, void *mode_state, xkb_keysym_t, char *text);
    // Optional: called when a key is released.
    void (*key_release)(struct state *, void *mode_state, xkb_keysym_t);
    // Optional: called when the repeat timer (state->repeat_timer_fd) fires.
    void (*timer)(struct state *, void *mode_state);
    void (*render)(struct state *, void *mode_state, cairo_t *);
    void (*free)(void *mode_state);
    // Called when the selection is restarted for a second drag pick.
    // Should reset the mode to its initial state without re-doing setup
    // (e.g., clear label selections but keep loaded areas).
    void (*restart)(struct state *, void *mode_state);
};

extern struct mode_interface *mode_interfaces[];

/**
 * Load modes from given coma seperated list of mode names.
 * Returns 0 on success.
 */
int load_modes(struct state *, char *);

void enter_next_mode(struct state *, struct rect area);
bool has_last_mode_returned(struct state *);
bool reenter_prev_mode(struct state *);
void free_mode_states(struct state *);
bool mode_handle_key(struct state *, xkb_keysym_t, char *text);
void mode_handle_key_release(struct state *, xkb_keysym_t);
void mode_handle_timer(struct state *);
void mode_render(struct state *, cairo_t *);

#endif
