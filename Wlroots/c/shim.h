#ifndef MYCOMP_SHIM_H
#define MYCOMP_SHIM_H

#include <stddef.h>
#include <stdint.h>

typedef struct compositor compositor_t;

enum event_tag {
  EV_NONE = 0,
  EV_TICK = 1,
  EV_NEW_OUTPUT = 2,
  EV_NEW_XDG_SURFACE = 3,
  EV_BACKEND_STARTED = 4,
  EV_BACKEND_FAILED = 5,
  EV_KEY = 6,
  EV_VIEW_UNMAP = 7,
  EV_OUTPUT_SIZE = 8
};

enum backend_fail_reason {
  BACKEND_FAIL_UNKNOWN = 0,
  BACKEND_FAIL_SOCKET = 1,
  BACKEND_FAIL_SEAT_NOT_FOUND = 2,
  BACKEND_FAIL_START = 3
};

typedef struct event {
  uint32_t tag;
  uint32_t _pad;
  uint64_t a;
  uint64_t b;
} event_t;

typedef struct cmd {
  uint32_t tag;
  uint32_t _pad;
  uint64_t a;
  uint64_t b;
} cmd_t;

enum cmd_tag {
  CMD_NONE = 0,
  CMD_QUIT = 1,
  CMD_SPAWN_ID = 2,
  CMD_CLOSE_FOCUSED = 3,
  CMD_SET_RECT = 4,
  CMD_FOCUS_ID = 5
};

enum pointer_focus_mode {
  POINTER_FOCUS_CLICK = 0,
  POINTER_FOCUS_HOVER = 1
};

compositor_t *comp_create(void);
int comp_start(compositor_t *comp);
void comp_run_once(compositor_t *comp);
int comp_poll_event(compositor_t *comp, event_t *out);
int comp_apply_cmds(compositor_t *comp, const cmd_t *cmds, size_t n);
int comp_set_pointer_focus_mode(compositor_t *comp, uint32_t mode);
void comp_destroy(compositor_t *comp);

#endif
