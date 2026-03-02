#ifndef WLR_LAYER_SHELL_UNSTABLE_V1_PROTOCOL_H
#define WLR_LAYER_SHELL_UNSTABLE_V1_PROTOCOL_H

/*
 * Minimal fallback definitions for builds where wlroots doesn't install
 * wlr-layer-shell-unstable-v1-protocol.h. wlroots type headers only require
 * these enums.
 */

enum zwlr_layer_shell_v1_layer {
  ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND = 0,
  ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM = 1,
  ZWLR_LAYER_SHELL_V1_LAYER_TOP = 2,
  ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY = 3,
};

enum zwlr_layer_surface_v1_keyboard_interactivity {
  ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE = 0,
  ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE = 1,
  ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND = 2,
};

#endif
