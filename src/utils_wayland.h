// SPDX-License-Identifier: GPL-3.0-only

#ifndef __UTILS_WAYLAND_H_INCLUDED__
#define __UTILS_WAYLAND_H_INCLUDED__

#include "state.h"

void move_pointer(
    struct state *state, uint32_t x, uint32_t y, enum click click
);

void drag_pointer(
    struct state *state, uint32_t start_x, uint32_t start_y, uint32_t end_x,
    uint32_t end_y
);

/**
 * Create a virtual pointer on the current output. Returns NULL if the
 * compositor does not support wlr-virtual-pointer.
 */
struct zwlr_virtual_pointer_v1 *virtual_pointer_create(struct state *state);

void virtual_pointer_move_absolute(
    struct state *state, struct zwlr_virtual_pointer_v1 *pointer, uint32_t x,
    uint32_t y
);

void virtual_pointer_move_relative(
    struct zwlr_virtual_pointer_v1 *pointer, uint32_t time, int32_t dx,
    int32_t dy
);

void virtual_pointer_button(
    struct zwlr_virtual_pointer_v1 *pointer, uint32_t time, uint32_t button,
    uint32_t button_state
);

#endif
