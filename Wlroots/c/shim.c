#define _POSIX_C_SOURCE 200809L
#include "shim.h"

#include <inttypes.h>
#ifndef SHIM_NO_LEAN_FFI
#include <lean/lean.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#if !defined(SHIM_DISABLE_XWAYLAND) && defined(__has_include)
#if __has_include(<wlr/xwayland.h>) && __has_include(<xcb/xcb.h>)
#define WLRLEAN_HAS_XWAYLAND 1
#endif
#endif
#ifndef WLRLEAN_HAS_XWAYLAND
#define WLRLEAN_HAS_XWAYLAND 0
#endif

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#if WLRLEAN_HAS_XWAYLAND
#include <wlr/xwayland.h>
#else
struct wlr_xwayland;
struct wlr_xwayland_surface;
#endif
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define EVQ_CAP 256

struct compositor;
struct registered_app {
  uint64_t id;
  char *command;
  struct registered_app *next;
};

struct wlrlean_view {
  struct compositor *comp;
  struct wl_list link;

  struct wlr_xdg_surface *xdg_surface;
  struct wlr_scene_tree *scene_tree;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener commit;
  struct wl_listener destroy;

  bool mapped;
  bool initial_configure_sent;
  uint64_t id;
};

struct wlrlean_xwayland_view {
  struct compositor *comp;
  struct wl_list link;

#if WLRLEAN_HAS_XWAYLAND
  struct wlr_xwayland_surface *xsurface;
  struct wlr_scene_tree *scene_tree;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener set_geometry;
  struct wl_listener request_configure;
  struct wl_listener request_activate;
  struct wl_listener associate;
  struct wl_listener dissociate;
  struct wl_listener destroy;

  bool mapped;
  bool associated;
#else
  void *xsurface;
  void *scene_tree;
  bool mapped;
#endif
  uint64_t id;
};

struct wlrlean_layer_surface {
  struct compositor *comp;
  struct wl_list link;

  struct wlr_layer_surface_v1 *layer_surface;
  struct wlr_scene_layer_surface_v1 *scene_layer_surface;
  bool mapped;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener commit;
  struct wl_listener destroy;
};

struct wlrlean_output {
  struct compositor *comp;
  struct wl_list link;

  struct wlr_output *wlr_output;
  int width;
  int height;
  struct wl_listener frame;
  struct wl_listener destroy;
};

struct wlrlean_keyboard {
  struct compositor *comp;
  struct wl_list link;

  struct wlr_input_device *device;
  struct wl_listener key;
  struct wl_listener modifiers;
  struct wl_listener destroy;
};

struct compositor {
  struct wl_display *display;
  struct wl_event_loop *event_loop;
  struct wlr_backend *backend;
  struct wlr_compositor *wlr_compositor;
  struct wlr_renderer *renderer;
  struct wlr_allocator *allocator;

  struct wlr_output_layout *output_layout;
  struct wlr_scene *scene;
  struct wlr_scene_tree *background_tree;
  struct wlr_scene_tree *bottom_tree;
  struct wlr_scene_tree *workspace_tree;
  struct wlr_scene_tree *top_tree;
  struct wlr_scene_tree *overlay_tree;
  struct wlr_cursor *cursor;
  struct wlr_xcursor_manager *cursor_mgr;

  struct wlr_seat *seat;
  struct wlr_xdg_shell *xdg_shell;
  struct wlr_layer_shell_v1 *layer_shell;
  struct wlr_xwayland *xwayland;

  struct wl_listener new_output;
  struct wl_listener new_input;
  struct wl_listener new_xdg_surface;
  struct wl_listener new_xdg_toplevel;
  struct wl_listener new_layer_surface;
  struct wl_listener new_xwayland_surface;
  struct wl_listener xwayland_ready;
  struct wl_listener request_cursor;

  struct wl_listener cursor_motion;
  struct wl_listener cursor_motion_absolute;
  struct wl_listener cursor_button;
  struct wl_listener cursor_axis;
  struct wl_listener cursor_frame;

  struct wl_list outputs;   // struct wlrlean_output::link
  struct wl_list views;     // struct wlrlean_view::link
  struct wl_list xwayland_views; // struct wlrlean_xwayland_view::link
  struct wl_list layer_surfaces; // struct wlrlean_layer_surface::link
  struct wl_list keyboards; // struct wlrlean_keyboard::link

  struct wlrlean_view *focused_view;
  struct wlrlean_xwayland_view *focused_xwayland_view;
  bool destroying;
  uint32_t pointer_focus_mode;

  event_t evq[EVQ_CAP];
  size_t evq_head;
  size_t evq_tail;

  uint64_t tick_count;
  uint64_t next_output_id;
  uint64_t next_surface_id;
  uint64_t next_app_id;
  struct registered_app *apps;
  int layout_width;
  int layout_height;

  event_t last_event;
};

static void arrange_layer_surfaces(struct compositor *comp);

static bool setup_renderer_allocator(struct compositor *comp) {
  comp->renderer = wlr_renderer_autocreate(comp->backend);
  comp->allocator = comp->renderer ? wlr_allocator_autocreate(comp->backend, comp->renderer) : NULL;

  if (!comp->renderer || !comp->allocator) {
    return false;
  }

  wlr_renderer_init_wl_display(comp->renderer, comp->display);
  comp->wlr_compositor = wlr_compositor_create(comp->display, 6, comp->renderer);
  return comp->wlr_compositor != NULL;
}

static bool env_has_word(const char *name, const char *word) {
  const char *v = getenv(name);
  if (!v || !word) {
    return false;
  }
  return strstr(v, word) != NULL;
}

static int add_wlroots_socket(struct wl_display *display, char *out_name, size_t out_name_len) {
  const char *base = "tutra-0";
  if (out_name_len == 0) {
    return -1;
  }

  snprintf(out_name, out_name_len, "%s", base);
  if (wl_display_add_socket(display, out_name) == 0) {
    return 0;
  }

  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (runtime && runtime[0] != '\0') {
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/%s", runtime, base);
    if (n > 0 && (size_t)n < sizeof(path)) {
      unlink(path);
      if (wl_display_add_socket(display, base) == 0) {
        snprintf(out_name, out_name_len, "%s", base);
        return 0;
      }
    }
  }

  const char *auto_name = wl_display_add_socket_auto(display);
  if (!auto_name) {
    return -1;
  }
  snprintf(out_name, out_name_len, "%s", auto_name);
  return 0;
}

static uint64_t detect_start_failure_reason(void) {
  if (env_has_word("WLR_BACKENDS", "headless")) {
    return BACKEND_FAIL_START;
  }

  if (access("/run/seatd.sock", F_OK) != 0) {
    const char *session = getenv("XDG_SESSION_ID");
    const char *seat = getenv("XDG_SEAT");
    const char *vtnr = getenv("XDG_VTNR");
    if (!session && !seat && !vtnr) {
      return BACKEND_FAIL_SEAT_NOT_FOUND;
    }
  }

  return BACKEND_FAIL_START;
}

static void push_event(struct compositor *comp, uint32_t tag, uint64_t a, uint64_t b) {
  if (!comp) {
    return;
  }

  size_t next_tail = (comp->evq_tail + 1) % EVQ_CAP;
  if (next_tail == comp->evq_head) {
    if (tag == EV_TICK) {
      // Prefer dropping ticks when saturated.
      return;
    }
    // Try to free one slot by discarding the oldest tick.
    event_t oldest = comp->evq[comp->evq_head];
    if (oldest.tag == EV_TICK) {
      comp->evq_head = (comp->evq_head + 1) % EVQ_CAP;
      next_tail = (comp->evq_tail + 1) % EVQ_CAP;
    } else {
      fprintf(stderr, "[c] event queue full, dropping tag=%u\n", tag);
      return;
    }
  }

  event_t ev = {
      .tag = tag,
      ._pad = 0,
      .a = a,
      .b = b,
  };
  comp->evq[comp->evq_tail] = ev;
  comp->evq_tail = next_tail;

  if (tag != EV_TICK || (a % 60) == 0) {
    fprintf(stderr, "[c] push event tag=%u a=%" PRIu64 " b=%" PRIu64 "\n", tag, a, b);
  }
}

static void push_output_size_if_changed(struct compositor *comp) {
  if (!comp || comp->destroying || !comp->output_layout) {
    return;
  }
  struct wlr_box box = {0};
  wlr_output_layout_get_box(comp->output_layout, NULL, &box);
  int w = box.width;
  int h = box.height;
  if (w <= 0 || h <= 0) {
    return;
  }
  if (w == comp->layout_width && h == comp->layout_height) {
    return;
  }
  comp->layout_width = w;
  comp->layout_height = h;
  arrange_layer_surfaces(comp);
  push_event(comp, EV_OUTPUT_SIZE, (uint64_t)w, (uint64_t)h);
}

static int pop_event(struct compositor *comp, event_t *out) {
  if (!comp || !out) {
    return 0;
  }
  if (comp->evq_head == comp->evq_tail) {
    return 0;
  }

  *out = comp->evq[comp->evq_head];
  comp->evq_head = (comp->evq_head + 1) % EVQ_CAP;
  return 1;
}

static void remove_listener_if_linked(struct wl_listener *listener) {
  if (!listener) {
    return;
  }
  if (listener->link.prev && listener->link.next) {
    wl_list_remove(&listener->link);
    listener->link.prev = NULL;
    listener->link.next = NULL;
  }
}

static void close_extra_fds_in_child(void) {
  long max_fd = sysconf(_SC_OPEN_MAX);
  if (max_fd < 0) {
    max_fd = 1024;
  }
  for (int fd = 3; fd < max_fd; fd++) {
    close(fd);
  }
}

static void handle_sigchld(int signo) {
  (void)signo;
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }
}

static void install_sigchld_reaper(void) {
  static bool installed = false;
  if (installed) {
    return;
  }
  struct sigaction sa = {0};
  sa.sa_handler = handle_sigchld;
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGCHLD, &sa, NULL) == 0) {
    installed = true;
  } else {
    fprintf(stderr, "[c] warning: failed to install SIGCHLD handler\n");
  }
}

static void spawn_command(const char *cmd) {
  pid_t pid = fork();
  if (pid != 0) {
    return;
  }

  setsid();
  // When running nested (wlr wayland backend), child processes may inherit
  // WAYLAND_SOCKET and accidentally connect to the parent compositor.
  // Force children to use WAYLAND_DISPLAY exported by comp_start().
  unsetenv("WAYLAND_SOCKET");
  close_extra_fds_in_child();
  const char *wd = getenv("WAYLAND_DISPLAY");
  const char *xd = getenv("DISPLAY");
  const char *xr = getenv("XDG_RUNTIME_DIR");
  const char *path = getenv("PATH");
  if (!path) {
    path = "/usr/bin:/bin";
  }
  if (!wd || !xr) {
    fprintf(stderr, "[c] spawn env missing: WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s\n", wd ? wd : "(null)",
            xr ? xr : "(null)");
    execl("/bin/sh", "sh", "-lc", cmd, (char *)NULL);
    _exit(127);
  }

  char wrapped[1400];
  int n;
  if (xd && xd[0] != '\0') {
    n = snprintf(wrapped, sizeof(wrapped),
                 "env -u WAYLAND_SOCKET WAYLAND_DISPLAY='%s' DISPLAY='%s' XDG_RUNTIME_DIR='%s' PATH='%s' %s", wd, xd,
                 xr, path, cmd);
  } else {
    n = snprintf(wrapped, sizeof(wrapped), "env -u WAYLAND_SOCKET WAYLAND_DISPLAY='%s' XDG_RUNTIME_DIR='%s' PATH='%s' %s",
                 wd, xr, path, cmd);
  }
  if (n < 0 || (size_t)n >= sizeof(wrapped)) {
    execl("/bin/sh", "sh", "-lc", cmd, (char *)NULL);
    _exit(127);
  }

  fprintf(stderr, "[c] spawn env WAYLAND_DISPLAY=%s cmd=%s\n", wd, cmd);
  execl("/bin/sh", "sh", "-lc", wrapped, (char *)NULL);
  _exit(127);
}

static struct registered_app *find_app_by_id(struct compositor *comp, uint64_t id) {
  if (!comp) {
    return NULL;
  }
  struct registered_app *it = comp->apps;
  while (it) {
    if (it->id == id) {
      return it;
    }
    it = it->next;
  }
  return NULL;
}

static struct registered_app *find_app_by_command(struct compositor *comp, const char *command) {
  if (!comp || !command) {
    return NULL;
  }
  struct registered_app *it = comp->apps;
  while (it) {
    if (strcmp(it->command, command) == 0) {
      return it;
    }
    it = it->next;
  }
  return NULL;
}

static uint64_t register_app_command(struct compositor *comp, const char *command) {
  if (!comp || !command || command[0] == '\0') {
    return 0;
  }

  struct registered_app *existing = find_app_by_command(comp, command);
  if (existing) {
    return existing->id;
  }

  struct registered_app *app = calloc(1, sizeof(*app));
  if (!app) {
    return 0;
  }

  app->command = strdup(command);
  if (!app->command) {
    free(app);
    return 0;
  }

  app->id = ++comp->next_app_id;
  app->next = comp->apps;
  comp->apps = app;
  fprintf(stderr, "[c] register app id=%" PRIu64 " cmd=%s\n", app->id, app->command);
  return app->id;
}

static int spawn_by_id(struct compositor *comp, uint64_t app_id) {
  struct registered_app *app = find_app_by_id(comp, app_id);
  if (!app || !app->command || app->command[0] == '\0') {
    fprintf(stderr, "[c] spawn: unknown app_id=%" PRIu64 "\n", app_id);
    return -1;
  }
  fprintf(stderr, "[c] spawn app_id=%" PRIu64 " cmd=%s\n", app_id, app->command);
  spawn_command(app->command);
  return 0;
}

static void focus_view(struct compositor *comp, struct wlrlean_view *view);
static void focus_xwayland_view(struct compositor *comp, struct wlrlean_xwayland_view *view);
static void maybe_setup_toplevel(struct wlrlean_view *view);
static struct wlrlean_view *find_view_by_id(struct compositor *comp, uint64_t id);
static struct wlrlean_xwayland_view *find_xwayland_view_by_id(struct compositor *comp, uint64_t id);
static void process_cursor_motion(struct compositor *comp, uint32_t time_msec);
static struct wlrlean_view *view_at_cursor(struct compositor *comp);
static struct wlrlean_xwayland_view *xwayland_view_at_cursor(struct compositor *comp);
static void set_default_cursor(struct compositor *comp);
static struct wlr_scene_tree *layer_tree_for(struct compositor *comp, enum zwlr_layer_shell_v1_layer layer);

static struct wlrlean_keyboard *first_keyboard(struct compositor *comp) {
  if (wl_list_empty(&comp->keyboards)) {
    return NULL;
  }
  return wl_container_of(comp->keyboards.next, (struct wlrlean_keyboard *)NULL, link);
}

static void update_seat_capabilities(struct compositor *comp) {
  uint32_t caps = 0;
  if (comp->cursor) {
    caps |= WL_SEAT_CAPABILITY_POINTER;
  }
  if (!wl_list_empty(&comp->keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(comp->seat, caps);
}

static struct wlr_scene_tree *layer_tree_for(struct compositor *comp, enum zwlr_layer_shell_v1_layer layer) {
  if (!comp) {
    return NULL;
  }
  switch (layer) {
  case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
    return comp->background_tree;
  case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
    return comp->bottom_tree;
  case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
    return comp->top_tree;
  case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
    return comp->overlay_tree;
  default:
    return comp->top_tree;
  }
}

static void arrange_layer_surfaces(struct compositor *comp) {
  if (!comp || !comp->output_layout) {
    return;
  }

  struct wlr_box full = {0};
  wlr_output_layout_get_box(comp->output_layout, NULL, &full);
  if (full.width <= 0 || full.height <= 0) {
    return;
  }

  struct wlr_box usable = full;
  struct wlrlean_layer_surface *layer;
  enum zwlr_layer_shell_v1_layer pass[] = {
      ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
      ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
      ZWLR_LAYER_SHELL_V1_LAYER_TOP,
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
  };
  for (size_t i = 0; i < sizeof(pass) / sizeof(pass[0]); i++) {
    wl_list_for_each(layer, &comp->layer_surfaces, link) {
      if (!layer->scene_layer_surface || !layer->layer_surface) {
        continue;
      }
      if (layer->layer_surface->pending.layer != pass[i] && layer->layer_surface->current.layer != pass[i]) {
        continue;
      }
      wlr_scene_layer_surface_v1_configure(layer->scene_layer_surface, &full, &usable);
    }
  }
}

static void set_default_cursor(struct compositor *comp) {
  if (!comp || !comp->cursor || !comp->cursor_mgr) {
    return;
  }
  wlr_cursor_set_xcursor(comp->cursor, comp->cursor_mgr, "left_ptr");
}

static struct wlr_surface *surface_at_cursor(struct compositor *comp, double *sx, double *sy) {
  if (!comp || !comp->cursor || !comp->scene) {
    return NULL;
  }

  // Layer-shell surfaces must get pointer hit-testing priority over tiled views.
  enum zwlr_layer_shell_v1_layer pass[] = {
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      ZWLR_LAYER_SHELL_V1_LAYER_TOP,
      ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
      ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
  };
  for (size_t i = 0; i < sizeof(pass) / sizeof(pass[0]); i++) {
    struct wlrlean_layer_surface *layer;
    wl_list_for_each(layer, &comp->layer_surfaces, link) {
      if (!layer->mapped || !layer->layer_surface || !layer->scene_layer_surface) {
        continue;
      }
      enum zwlr_layer_shell_v1_layer layer_kind = layer->layer_surface->current.layer;
      if (layer_kind != pass[i]) {
        continue;
      }
      int lx = 0, ly = 0;
      if (!wlr_scene_node_coords(&layer->scene_layer_surface->tree->node, &lx, &ly)) {
        continue;
      }
      double local_x = comp->cursor->x - lx;
      double local_y = comp->cursor->y - ly;
      struct wlr_surface *s =
          wlr_layer_surface_v1_surface_at(layer->layer_surface, local_x, local_y, sx, sy);
      if (s) {
        return s;
      }
    }
  }

  struct wlr_scene_node *node = wlr_scene_node_at(&comp->scene->tree.node, comp->cursor->x, comp->cursor->y, sx, sy);
  if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
    return NULL;
  }

  struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
  if (!scene_buffer) {
    return NULL;
  }
  struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
  if (!scene_surface) {
    return NULL;
  }
  return scene_surface->surface;
}

static struct wlrlean_view *view_at_cursor(struct compositor *comp) {
  double sx = 0, sy = 0;
  struct wlr_surface *surface = surface_at_cursor(comp, &sx, &sy);
  if (!surface) {
    return NULL;
  }

  struct wlr_surface *root = wlr_surface_get_root_surface(surface);
  struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root);
  if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
    return NULL;
  }

  struct wlrlean_view *view;
  wl_list_for_each(view, &comp->views, link) {
    if (view->xdg_surface == xdg_surface) {
      return view;
    }
  }
  return NULL;
}

static struct wlrlean_xwayland_view *xwayland_view_at_cursor(struct compositor *comp) {
#if !WLRLEAN_HAS_XWAYLAND
  (void)comp;
  return NULL;
#else
  double sx = 0, sy = 0;
  struct wlr_surface *surface = surface_at_cursor(comp, &sx, &sy);
  if (!surface) {
    return NULL;
  }

  struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
  if (!xsurface) {
    return NULL;
  }

  struct wlrlean_xwayland_view *view;
  wl_list_for_each(view, &comp->xwayland_views, link) {
    if (view->xsurface == xsurface) {
      return view;
    }
  }
  return NULL;
#endif
}

static void process_cursor_motion(struct compositor *comp, uint32_t time_msec) {
  if (!comp || !comp->seat) {
    return;
  }

  double sx = 0, sy = 0;
  struct wlr_surface *surface = surface_at_cursor(comp, &sx, &sy);
  if (!surface) {
    wlr_seat_pointer_notify_clear_focus(comp->seat);
    set_default_cursor(comp);
    return;
  }

  wlr_seat_pointer_notify_enter(comp->seat, surface, sx, sy);
  wlr_seat_pointer_notify_motion(comp->seat, time_msec, sx, sy);
  if (comp->pointer_focus_mode == POINTER_FOCUS_HOVER) {
    struct wlrlean_view *view = view_at_cursor(comp);
    if (view) {
      focus_view(comp, view);
      return;
    }
    struct wlrlean_xwayland_view *xview = xwayland_view_at_cursor(comp);
    if (xview) {
      focus_xwayland_view(comp, xview);
    }
  }
}

static void handle_request_cursor(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, request_cursor);
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  if (!comp || !comp->cursor || !event) {
    return;
  }

  if (comp->seat->pointer_state.focused_client == event->seat_client) {
    wlr_cursor_set_surface(comp->cursor, event->surface, event->hotspot_x, event->hotspot_y);
  }
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, cursor_motion);
  struct wlr_pointer_motion_event *event = data;
  if (!comp || !comp->cursor || !event) {
    return;
  }

  wlr_cursor_move(comp->cursor, &event->pointer->base, event->delta_x, event->delta_y);
  process_cursor_motion(comp, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, cursor_motion_absolute);
  struct wlr_pointer_motion_absolute_event *event = data;
  if (!comp || !comp->cursor || !event) {
    return;
  }

  wlr_cursor_warp_absolute(comp->cursor, &event->pointer->base, event->x, event->y);
  process_cursor_motion(comp, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, cursor_button);
  struct wlr_pointer_button_event *event = data;
  if (!comp || !comp->seat || !event) {
    return;
  }

  process_cursor_motion(comp, event->time_msec);
  wlr_seat_pointer_notify_button(comp->seat, event->time_msec, event->button, event->state);
  if (comp->pointer_focus_mode == POINTER_FOCUS_CLICK && event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
    struct wlrlean_view *view = view_at_cursor(comp);
    if (view) {
      focus_view(comp, view);
      return;
    }
    struct wlrlean_xwayland_view *xview = xwayland_view_at_cursor(comp);
    if (xview) {
      focus_xwayland_view(comp, xview);
    }
  }
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, cursor_axis);
  struct wlr_pointer_axis_event *event = data;
  if (!comp || !comp->seat || !event) {
    return;
  }

  wlr_seat_pointer_notify_axis(comp->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete,
                               event->source, event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data) {
  (void)data;
  struct compositor *comp = wl_container_of(listener, comp, cursor_frame);
  if (!comp || !comp->seat) {
    return;
  }
  wlr_seat_pointer_notify_frame(comp->seat);
}

static void focus_view(struct compositor *comp, struct wlrlean_view *view) {
  if (!comp || !view || !view->mapped || !view->xdg_surface || !view->xdg_surface->toplevel) {
    return;
  }

#if WLRLEAN_HAS_XWAYLAND
  if (comp->focused_xwayland_view && comp->focused_xwayland_view->xsurface) {
    wlr_xwayland_surface_activate(comp->focused_xwayland_view->xsurface, false);
    comp->focused_xwayland_view = NULL;
  }
#endif

  if (comp->focused_view && comp->focused_view != view && comp->focused_view->xdg_surface &&
      comp->focused_view->xdg_surface->toplevel) {
    wlr_xdg_toplevel_set_activated(comp->focused_view->xdg_surface->toplevel, false);
  }

  comp->focused_view = view;
  wlr_scene_node_raise_to_top(&view->scene_tree->node);
  wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);

  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(comp->seat);
  if (keyboard) {
    wlr_seat_keyboard_notify_enter(comp->seat, view->xdg_surface->surface, keyboard->keycodes,
                                   keyboard->num_keycodes, &keyboard->modifiers);
  }
}

static void focus_xwayland_view(struct compositor *comp, struct wlrlean_xwayland_view *view) {
#if !WLRLEAN_HAS_XWAYLAND
  (void)comp;
  (void)view;
  return;
#else
  if (!comp || !view || !view->mapped || !view->xsurface || !view->xsurface->surface) {
    return;
  }

  if (comp->focused_view && comp->focused_view->xdg_surface && comp->focused_view->xdg_surface->toplevel) {
    wlr_xdg_toplevel_set_activated(comp->focused_view->xdg_surface->toplevel, false);
    comp->focused_view = NULL;
  }

  if (comp->focused_xwayland_view && comp->focused_xwayland_view != view && comp->focused_xwayland_view->xsurface) {
    wlr_xwayland_surface_activate(comp->focused_xwayland_view->xsurface, false);
  }

  comp->focused_xwayland_view = view;
  wlr_scene_node_raise_to_top(&view->scene_tree->node);
  wlr_xwayland_surface_activate(view->xsurface, true);

  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(comp->seat);
  if (keyboard) {
    wlr_seat_keyboard_notify_enter(comp->seat, view->xsurface->surface, keyboard->keycodes,
                                   keyboard->num_keycodes, &keyboard->modifiers);
  }
#endif
}

static struct wlrlean_view *find_view_by_id(struct compositor *comp, uint64_t id) {
  if (!comp) {
    return NULL;
  }
  struct wlrlean_view *view;
  wl_list_for_each(view, &comp->views, link) {
    if (view->id == id) {
      return view;
    }
  }
  return NULL;
}

static struct wlrlean_xwayland_view *find_xwayland_view_by_id(struct compositor *comp, uint64_t id) {
#if !WLRLEAN_HAS_XWAYLAND
  (void)comp;
  (void)id;
  return NULL;
#else
  if (!comp) {
    return NULL;
  }
  struct wlrlean_xwayland_view *view;
  wl_list_for_each(view, &comp->xwayland_views, link) {
    if (view->id == id) {
      return view;
    }
  }
  return NULL;
#endif
}

static void handle_view_map(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_view *view = wl_container_of(listener, view, map);
  maybe_setup_toplevel(view);
  if (!view->xdg_surface || !view->xdg_surface->toplevel) {
    fprintf(stderr, "[c] view_map ignore non-toplevel id=%" PRIu64 "\n", view->id);
    return;
  }
  fprintf(stderr, "[c] view_map id=%" PRIu64 "\n", view->id);
  view->mapped = true;
  wlr_scene_node_set_enabled(&view->scene_tree->node, true);

  push_event(view->comp, EV_NEW_XDG_SURFACE, view->id, 0);
  focus_view(view->comp, view);
}

static void handle_view_unmap(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_view *view = wl_container_of(listener, view, unmap);
  fprintf(stderr, "[c] view_unmap id=%" PRIu64 "\n", view->id);
  view->mapped = false;
  wlr_scene_node_set_enabled(&view->scene_tree->node, false);

  if (view->comp->focused_view == view) {
    view->comp->focused_view = NULL;
  }
  push_event(view->comp, EV_VIEW_UNMAP, view->id, 0);
}

static void maybe_setup_toplevel(struct wlrlean_view *view) {
  if (!view || !view->xdg_surface) {
    return;
  }

  if (view->xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL || !view->xdg_surface->toplevel) {
    return;
  }
  if (!view->xdg_surface->initialized) {
    return;
  }

  if (!view->initial_configure_sent) {
    struct wlr_box box = {0};
    wlr_output_layout_get_box(view->comp->output_layout, NULL, &box);
    int w = box.width > 0 ? box.width : 1280;
    int h = box.height > 0 ? box.height : 720;
    wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, w, h);
    wlr_xdg_surface_schedule_configure(view->xdg_surface);
    view->initial_configure_sent = true;
    fprintf(stderr, "[c] initial_configure id=%" PRIu64 " size=%dx%d\n", view->id, w, h);
  }
}

static void handle_view_commit(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_view *view = wl_container_of(listener, view, commit);
  maybe_setup_toplevel(view);
}

static void handle_view_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_view *view = wl_container_of(listener, view, destroy);
  fprintf(stderr, "[c] view_destroy id=%" PRIu64 "\n", view->id);

  wl_list_remove(&view->link);
  wl_list_remove(&view->map.link);
  wl_list_remove(&view->unmap.link);
  wl_list_remove(&view->commit.link);
  wl_list_remove(&view->destroy.link);

  if (view->comp->focused_view == view) {
    view->comp->focused_view = NULL;
  }

  free(view);
}

static void handle_new_xdg_surface(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, new_xdg_surface);
  struct wlr_xdg_surface *xdg_surface = data;
  if (!xdg_surface) {
    return;
  }

  // Keep this listener only for diagnostics. Some clients may emit
  // new_surface before role assignment, so the actual view creation path uses
  // xdg_shell.events.new_toplevel.
  fprintf(stderr, "[c] xdg new_surface role=%d\n", (int)xdg_surface->role);
  if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL || !xdg_surface->toplevel) {
    return;
  }
}

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, new_xdg_toplevel);
  struct wlr_xdg_toplevel *toplevel = data;
  if (!toplevel || !toplevel->base || !toplevel->base->surface) {
    return;
  }
  struct wlr_xdg_surface *xdg_surface = toplevel->base;

  struct wlrlean_view *view = calloc(1, sizeof(*view));
  if (!view) {
    return;
  }

  view->comp = comp;
  view->xdg_surface = xdg_surface;
  view->id = ++comp->next_surface_id;
  fprintf(stderr, "[c] new_xdg_toplevel id=%" PRIu64 " role=%d\n", view->id, xdg_surface->role);
  view->scene_tree = wlr_scene_xdg_surface_create(comp->workspace_tree, xdg_surface);
  if (!view->scene_tree) {
    fprintf(stderr, "[c] new_xdg_toplevel: failed to create scene tree for id=%" PRIu64 "\n", view->id);
    free(view);
    return;
  }
  wlr_scene_node_set_enabled(&view->scene_tree->node, false);

  view->map.notify = handle_view_map;
  wl_signal_add(&xdg_surface->surface->events.map, &view->map);

  view->unmap.notify = handle_view_unmap;
  wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);

  view->commit.notify = handle_view_commit;
  wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);

  view->destroy.notify = handle_view_destroy;
  wl_signal_add(&xdg_surface->surface->events.destroy, &view->destroy);

  wl_list_insert(&comp->views, &view->link);
}

static struct wlr_output *first_output_wlr(struct compositor *comp) {
  if (!comp || wl_list_empty(&comp->outputs)) {
    return NULL;
  }
  struct wlrlean_output *output = wl_container_of(comp->outputs.next, (struct wlrlean_output *)NULL, link);
  return output->wlr_output;
}

static void handle_layer_surface_map(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_layer_surface *layer = wl_container_of(listener, layer, map);
  layer->mapped = true;
  arrange_layer_surfaces(layer->comp);
}

static void handle_layer_surface_unmap(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_layer_surface *layer = wl_container_of(listener, layer, unmap);
  layer->mapped = false;
  arrange_layer_surfaces(layer->comp);
}

static void handle_layer_surface_commit(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_layer_surface *layer = wl_container_of(listener, layer, commit);
  arrange_layer_surfaces(layer->comp);
}

static void handle_layer_surface_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_layer_surface *layer = wl_container_of(listener, layer, destroy);
  wl_list_remove(&layer->link);
  wl_list_remove(&layer->map.link);
  wl_list_remove(&layer->unmap.link);
  wl_list_remove(&layer->commit.link);
  wl_list_remove(&layer->destroy.link);
  free(layer);
}

static void handle_new_layer_surface(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, new_layer_surface);
  struct wlr_layer_surface_v1 *layer_surface = data;
  if (!comp || !layer_surface) {
    return;
  }

  if (!layer_surface->output) {
    layer_surface->output = first_output_wlr(comp);
  }

  struct wlr_scene_tree *parent = layer_tree_for(comp, layer_surface->pending.layer);
  if (!parent) {
    parent = comp->top_tree;
  }
  struct wlr_scene_layer_surface_v1 *scene_layer = wlr_scene_layer_surface_v1_create(parent, layer_surface);
  if (!scene_layer) {
    fprintf(stderr, "[c] layer_surface: failed to create scene helper\n");
    return;
  }

  struct wlrlean_layer_surface *layer = calloc(1, sizeof(*layer));
  if (!layer) {
    wlr_scene_node_destroy(&scene_layer->tree->node);
    return;
  }

  layer->comp = comp;
  layer->layer_surface = layer_surface;
  layer->scene_layer_surface = scene_layer;
  layer->mapped = layer_surface->surface && layer_surface->surface->mapped;

  layer->map.notify = handle_layer_surface_map;
  wl_signal_add(&layer_surface->surface->events.map, &layer->map);
  layer->unmap.notify = handle_layer_surface_unmap;
  wl_signal_add(&layer_surface->surface->events.unmap, &layer->unmap);
  layer->commit.notify = handle_layer_surface_commit;
  wl_signal_add(&layer_surface->surface->events.commit, &layer->commit);
  layer->destroy.notify = handle_layer_surface_destroy;
  wl_signal_add(&layer_surface->events.destroy, &layer->destroy);

  wl_list_insert(&comp->layer_surfaces, &layer->link);
  arrange_layer_surfaces(comp);
}

#if WLRLEAN_HAS_XWAYLAND
static void handle_xwayland_surface_map(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_xwayland_view *view = wl_container_of(listener, view, map);
  view->mapped = true;
  wlr_scene_node_set_enabled(&view->scene_tree->node, true);
  push_event(view->comp, EV_NEW_XDG_SURFACE, view->id, 0);
  focus_xwayland_view(view->comp, view);
}

static void handle_xwayland_surface_unmap(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_xwayland_view *view = wl_container_of(listener, view, unmap);
  view->mapped = false;
  wlr_scene_node_set_enabled(&view->scene_tree->node, false);
  if (view->comp->focused_xwayland_view == view) {
    view->comp->focused_xwayland_view = NULL;
  }
  push_event(view->comp, EV_VIEW_UNMAP, view->id, 0);
}

static void handle_xwayland_set_geometry(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_xwayland_view *view = wl_container_of(listener, view, set_geometry);
  if (!view->scene_tree || !view->xsurface) {
    return;
  }
  wlr_scene_node_set_position(&view->scene_tree->node, view->xsurface->x, view->xsurface->y);
}

static void handle_xwayland_request_configure(struct wl_listener *listener, void *data) {
  struct wlrlean_xwayland_view *view = wl_container_of(listener, view, request_configure);
  struct wlr_xwayland_surface_configure_event *event = data;
  if (!view || !view->xsurface || !event) {
    return;
  }
  wlr_xwayland_surface_configure(view->xsurface, event->x, event->y, event->width, event->height);
  if (view->scene_tree) {
    wlr_scene_node_set_position(&view->scene_tree->node, event->x, event->y);
  }
}

static void handle_xwayland_request_activate(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_xwayland_view *view = wl_container_of(listener, view, request_activate);
  focus_xwayland_view(view->comp, view);
}

static void handle_xwayland_associate(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_xwayland_view *view = wl_container_of(listener, view, associate);
  if (!view || !view->xsurface || !view->xsurface->surface || view->associated) {
    return;
  }

  view->associated = true;
  view->scene_tree = wlr_scene_tree_create(view->comp->workspace_tree);
  if (!view->scene_tree) {
    return;
  }
  struct wlr_scene_surface *scene_surface = wlr_scene_surface_create(view->scene_tree, view->xsurface->surface);
  if (!scene_surface) {
    wlr_scene_node_destroy(&view->scene_tree->node);
    view->scene_tree = NULL;
    return;
  }
  wlr_scene_node_set_position(&view->scene_tree->node, view->xsurface->x, view->xsurface->y);
  wlr_scene_node_set_enabled(&view->scene_tree->node, false);

  view->map.notify = handle_xwayland_surface_map;
  wl_signal_add(&view->xsurface->surface->events.map, &view->map);
  view->unmap.notify = handle_xwayland_surface_unmap;
  wl_signal_add(&view->xsurface->surface->events.unmap, &view->unmap);
}

static void handle_xwayland_dissociate(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_xwayland_view *view = wl_container_of(listener, view, dissociate);
  view->associated = false;
  view->mapped = false;
  if (view->map.link.prev && view->map.link.next) {
    wl_list_remove(&view->map.link);
  }
  if (view->unmap.link.prev && view->unmap.link.next) {
    wl_list_remove(&view->unmap.link);
  }
  if (view->scene_tree) {
    wlr_scene_node_destroy(&view->scene_tree->node);
    view->scene_tree = NULL;
  }
}

static void handle_xwayland_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_xwayland_view *view = wl_container_of(listener, view, destroy);
  wl_list_remove(&view->link);
  if (view->associate.link.prev && view->associate.link.next) {
    wl_list_remove(&view->associate.link);
  }
  if (view->dissociate.link.prev && view->dissociate.link.next) {
    wl_list_remove(&view->dissociate.link);
  }
  if (view->set_geometry.link.prev && view->set_geometry.link.next) {
    wl_list_remove(&view->set_geometry.link);
  }
  if (view->request_configure.link.prev && view->request_configure.link.next) {
    wl_list_remove(&view->request_configure.link);
  }
  if (view->request_activate.link.prev && view->request_activate.link.next) {
    wl_list_remove(&view->request_activate.link);
  }
  if (view->destroy.link.prev && view->destroy.link.next) {
    wl_list_remove(&view->destroy.link);
  }
  if (view->scene_tree) {
    wlr_scene_node_destroy(&view->scene_tree->node);
  }
  if (view->comp->focused_xwayland_view == view) {
    view->comp->focused_xwayland_view = NULL;
  }
  free(view);
}

static void handle_new_xwayland_surface(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, new_xwayland_surface);
  struct wlr_xwayland_surface *xsurface = data;
  if (!comp || !xsurface) {
    return;
  }

  struct wlrlean_xwayland_view *view = calloc(1, sizeof(*view));
  if (!view) {
    return;
  }
  view->comp = comp;
  view->xsurface = xsurface;
  view->id = ++comp->next_surface_id;

  view->associate.notify = handle_xwayland_associate;
  wl_signal_add(&xsurface->events.associate, &view->associate);
  view->dissociate.notify = handle_xwayland_dissociate;
  wl_signal_add(&xsurface->events.dissociate, &view->dissociate);
  view->set_geometry.notify = handle_xwayland_set_geometry;
  wl_signal_add(&xsurface->events.set_geometry, &view->set_geometry);
  view->request_configure.notify = handle_xwayland_request_configure;
  wl_signal_add(&xsurface->events.request_configure, &view->request_configure);
  view->request_activate.notify = handle_xwayland_request_activate;
  wl_signal_add(&xsurface->events.request_activate, &view->request_activate);
  view->destroy.notify = handle_xwayland_destroy;
  wl_signal_add(&xsurface->events.destroy, &view->destroy);

  wl_list_insert(&comp->xwayland_views, &view->link);
  if (xsurface->surface) {
    handle_xwayland_associate(&view->associate, NULL);
  }
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data) {
  (void)data;
  struct compositor *comp = wl_container_of(listener, comp, xwayland_ready);
  if (!comp || !comp->xwayland || !comp->xwayland->display_name) {
    return;
  }
  setenv("DISPLAY", comp->xwayland->display_name, 1);
  fprintf(stderr, "[c] xwayland ready display=%s\n", comp->xwayland->display_name);
}
#endif

static void handle_output_frame(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_output *output = wl_container_of(listener, output, frame);
  struct compositor *comp = output->comp;

  struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(comp->scene, output->wlr_output);
  if (!scene_output) {
    scene_output = wlr_scene_output_create(comp->scene, output->wlr_output);
    if (!scene_output) {
      return;
    }
  }

  if (!wlr_scene_output_commit(scene_output, NULL)) {
    return;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_output *output = wl_container_of(listener, output, destroy);
  struct compositor *comp = output->comp;

  wl_list_remove(&output->link);
  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->destroy.link);
  free(output);
  if (comp->destroying) {
    return;
  }
  arrange_layer_surfaces(comp);
  push_output_size_if_changed(comp);
}

static void handle_new_output(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, new_output);
  struct wlr_output *output = data;

  if (!comp->renderer || !comp->allocator) {
    fprintf(stderr, "[c] new_output: renderer/allocator unavailable, ignoring output\n");
    return;
  }

  if (!wlr_output_init_render(output, comp->allocator, comp->renderer)) {
    fprintf(stderr, "[c] new_output: wlr_output_init_render failed\n");
    return;
  }

  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  int mode_w = 0, mode_h = 0;
  struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
  if (mode) {
    mode_w = mode->width;
    mode_h = mode->height;
    wlr_output_state_set_mode(&state, mode);
  }
  if (!wlr_output_commit_state(output, &state)) {
    fprintf(stderr, "[c] new_output: commit state failed\n");
    wlr_output_state_finish(&state);
    return;
  }
  wlr_output_state_finish(&state);

  struct wlrlean_output *toutput = calloc(1, sizeof(*toutput));
  if (!toutput) {
    return;
  }

  toutput->comp = comp;
  toutput->wlr_output = output;
  wlr_output_effective_resolution(output, &toutput->width, &toutput->height);
  if (toutput->width <= 0 || toutput->height <= 0) {
    toutput->width = mode_w;
    toutput->height = mode_h;
  }

  toutput->frame.notify = handle_output_frame;
  wl_signal_add(&output->events.frame, &toutput->frame);

  toutput->destroy.notify = handle_output_destroy;
  wl_signal_add(&output->events.destroy, &toutput->destroy);

  wl_list_insert(&comp->outputs, &toutput->link);
  wlr_output_layout_add_auto(comp->output_layout, output);
  if (comp->cursor_mgr) {
    float scale = output->scale > 0.f ? output->scale : 1.f;
    (void)wlr_xcursor_manager_load(comp->cursor_mgr, scale);
    set_default_cursor(comp);
  }
  arrange_layer_surfaces(comp);

  uint64_t id = ++comp->next_output_id;
  fprintf(stderr, "[c] new_output id=%" PRIu64 "\n", id);
  push_event(comp, EV_NEW_OUTPUT, id, 0);
  push_output_size_if_changed(comp);
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_keyboard *kbd = wl_container_of(listener, kbd, modifiers);
  wlr_seat_keyboard_notify_modifiers(kbd->comp->seat, &wlr_keyboard_from_input_device(kbd->device)->modifiers);
}

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
  struct wlrlean_keyboard *kbd = wl_container_of(listener, kbd, key);
  struct compositor *comp = kbd->comp;
  struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(kbd->device);
  struct wlr_keyboard_key_event *event = data;

  wlr_seat_keyboard_notify_key(comp->seat, event->time_msec, event->keycode, event->state);

  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    uint32_t mods = wlr_keyboard_get_modifiers(keyboard);
    const xkb_keysym_t *syms = NULL;
    int nsyms = xkb_state_key_get_syms(keyboard->xkb_state, event->keycode + 8, &syms);
    for (int i = 0; i < nsyms; i++) {
      xkb_keysym_t sym = xkb_keysym_to_lower(syms[i]);
      push_event(comp, EV_KEY, mods, sym);
      break;
    }
  }
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct wlrlean_keyboard *kbd = wl_container_of(listener, kbd, destroy);
  struct compositor *comp = kbd->comp;

  wl_list_remove(&kbd->link);
  wl_list_remove(&kbd->key.link);
  wl_list_remove(&kbd->modifiers.link);
  wl_list_remove(&kbd->destroy.link);
  free(kbd);

  struct wlrlean_keyboard *next = first_keyboard(comp);
  if (next) {
    wlr_seat_set_keyboard(comp->seat, wlr_keyboard_from_input_device(next->device));
  } else {
    wlr_seat_set_keyboard(comp->seat, NULL);
  }
  update_seat_capabilities(comp);
}

static void handle_new_input(struct wl_listener *listener, void *data) {
  struct compositor *comp = wl_container_of(listener, comp, new_input);
  struct wlr_input_device *device = data;

  if (device->type == WLR_INPUT_DEVICE_POINTER) {
    if (comp->cursor) {
      wlr_cursor_attach_input_device(comp->cursor, device);
      update_seat_capabilities(comp);
    }
    return;
  }

  if (device->type != WLR_INPUT_DEVICE_KEYBOARD) {
    return;
  }

  struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(device);

  struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!ctx) {
    fprintf(stderr, "[c] new_input: xkb_context_new failed\n");
    return;
  }
  struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!keymap) {
    fprintf(stderr, "[c] new_input: xkb_keymap_new_from_names failed\n");
    xkb_context_unref(ctx);
    return;
  }
  wlr_keyboard_set_keymap(keyboard, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(ctx);
  wlr_keyboard_set_repeat_info(keyboard, 25, 600);

  struct wlrlean_keyboard *kbd = calloc(1, sizeof(*kbd));
  if (!kbd) {
    return;
  }

  kbd->comp = comp;
  kbd->device = device;

  kbd->key.notify = handle_keyboard_key;
  wl_signal_add(&keyboard->events.key, &kbd->key);

  kbd->modifiers.notify = handle_keyboard_modifiers;
  wl_signal_add(&keyboard->events.modifiers, &kbd->modifiers);

  kbd->destroy.notify = handle_keyboard_destroy;
  wl_signal_add(&device->events.destroy, &kbd->destroy);

  wl_list_insert(&comp->keyboards, &kbd->link);

  wlr_seat_set_keyboard(comp->seat, keyboard);
  update_seat_capabilities(comp);
}

compositor_t *comp_create(void) {
  static bool logging_initialized = false;
  if (!logging_initialized) {
    wlr_log_init(WLR_INFO, NULL);
    logging_initialized = true;
  }

  struct compositor *comp = calloc(1, sizeof(*comp));
  if (!comp) {
    fprintf(stderr, "[c] comp_create: calloc failed\n");
    return NULL;
  }

  wl_list_init(&comp->outputs);
  wl_list_init(&comp->views);
  wl_list_init(&comp->xwayland_views);
  wl_list_init(&comp->layer_surfaces);
  wl_list_init(&comp->keyboards);
  comp->pointer_focus_mode = POINTER_FOCUS_CLICK;
  install_sigchld_reaper();

  comp->display = wl_display_create();
  if (!comp->display) {
    fprintf(stderr, "[c] comp_create: wl_display_create failed\n");
    free(comp);
    return NULL;
  }

  comp->event_loop = wl_display_get_event_loop(comp->display);
  comp->backend = wlr_backend_autocreate(comp->event_loop, NULL);
  if (!comp->backend) {
    fprintf(stderr, "[c] comp_create: wlr_backend_autocreate failed, trying headless\n");
    comp->backend = wlr_headless_backend_create(comp->event_loop);
  }
  if (!comp->backend) {
    fprintf(stderr, "[c] comp_create: backend creation failed\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }

  if (!setup_renderer_allocator(comp)) {
    fprintf(stderr, "[c] comp_create: renderer init failed, retrying with WLR_RENDERER=pixman\n");
    setenv("WLR_RENDERER", "pixman", 0);
    if (!setup_renderer_allocator(comp)) {
      fprintf(stderr,
              "[c] comp_create: renderer/allocator init failed "
              "(renderer=%p allocator=%p). "
              "Try WLR_RENDERER=pixman or WLR_BACKENDS=headless.\n",
              (void *)comp->renderer, (void *)comp->allocator);
      wl_display_destroy(comp->display);
      free(comp);
      return NULL;
    }
  }

  (void)wlr_subcompositor_create(comp->display);
  (void)wlr_data_device_manager_create(comp->display);

  comp->output_layout = wlr_output_layout_create(comp->display);
  if (!comp->output_layout) {
    fprintf(stderr, "[c] comp_create: wlr_output_layout_create failed\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }
  comp->scene = wlr_scene_create();
  if (!comp->scene) {
    fprintf(stderr, "[c] comp_create: wlr_scene_create failed\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }
  if (!wlr_scene_attach_output_layout(comp->scene, comp->output_layout)) {
    fprintf(stderr, "[c] comp_create: wlr_scene_attach_output_layout failed\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }

  comp->background_tree = wlr_scene_tree_create(&comp->scene->tree);
  comp->bottom_tree = wlr_scene_tree_create(&comp->scene->tree);
  comp->workspace_tree = wlr_scene_tree_create(&comp->scene->tree);
  comp->top_tree = wlr_scene_tree_create(&comp->scene->tree);
  comp->overlay_tree = wlr_scene_tree_create(&comp->scene->tree);
  if (!comp->background_tree || !comp->bottom_tree || !comp->workspace_tree || !comp->top_tree || !comp->overlay_tree) {
    fprintf(stderr, "[c] comp_create: failed to create scene layer trees\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }

  comp->cursor = wlr_cursor_create();
  if (!comp->cursor) {
    fprintf(stderr, "[c] comp_create: wlr_cursor_create failed\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }
  wlr_cursor_attach_output_layout(comp->cursor, comp->output_layout);

  comp->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
  if (!comp->cursor_mgr) {
    fprintf(stderr, "[c] comp_create: wlr_xcursor_manager_create failed\n");
  } else {
    (void)wlr_xcursor_manager_load(comp->cursor_mgr, 1.f);
    set_default_cursor(comp);
  }

  comp->seat = wlr_seat_create(comp->display, "seat0");
  if (!comp->seat) {
    fprintf(stderr, "[c] comp_create: wlr_seat_create failed\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }
  comp->xdg_shell = wlr_xdg_shell_create(comp->display, 6);
  if (!comp->xdg_shell) {
    fprintf(stderr, "[c] comp_create: wlr_xdg_shell_create failed\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }
  comp->layer_shell = wlr_layer_shell_v1_create(comp->display, 4);
  if (!comp->layer_shell) {
    fprintf(stderr, "[c] comp_create: wlr_layer_shell_v1_create failed\n");
    wl_display_destroy(comp->display);
    free(comp);
    return NULL;
  }

#if WLRLEAN_HAS_XWAYLAND
  comp->xwayland = wlr_xwayland_create(comp->display, comp->wlr_compositor, false);
  if (!comp->xwayland) {
    fprintf(stderr, "[c] comp_create: wlr_xwayland_create failed (X11 apps unavailable)\n");
  } else {
    wlr_xwayland_set_seat(comp->xwayland, comp->seat);
  }
#endif

  comp->new_output.notify = handle_new_output;
  wl_signal_add(&comp->backend->events.new_output, &comp->new_output);

  comp->new_input.notify = handle_new_input;
  wl_signal_add(&comp->backend->events.new_input, &comp->new_input);

  comp->new_xdg_surface.notify = handle_new_xdg_surface;
  wl_signal_add(&comp->xdg_shell->events.new_surface, &comp->new_xdg_surface);
  comp->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
  wl_signal_add(&comp->xdg_shell->events.new_toplevel, &comp->new_xdg_toplevel);

  comp->new_layer_surface.notify = handle_new_layer_surface;
  wl_signal_add(&comp->layer_shell->events.new_surface, &comp->new_layer_surface);

  if (comp->xwayland) {
#if WLRLEAN_HAS_XWAYLAND
    comp->new_xwayland_surface.notify = handle_new_xwayland_surface;
    wl_signal_add(&comp->xwayland->events.new_surface, &comp->new_xwayland_surface);
    comp->xwayland_ready.notify = handle_xwayland_ready;
    wl_signal_add(&comp->xwayland->events.ready, &comp->xwayland_ready);
#endif
  }

  comp->request_cursor.notify = handle_request_cursor;
  wl_signal_add(&comp->seat->events.request_set_cursor, &comp->request_cursor);

  comp->cursor_motion.notify = handle_cursor_motion;
  wl_signal_add(&comp->cursor->events.motion, &comp->cursor_motion);

  comp->cursor_motion_absolute.notify = handle_cursor_motion_absolute;
  wl_signal_add(&comp->cursor->events.motion_absolute, &comp->cursor_motion_absolute);

  comp->cursor_button.notify = handle_cursor_button;
  wl_signal_add(&comp->cursor->events.button, &comp->cursor_button);

  comp->cursor_axis.notify = handle_cursor_axis;
  wl_signal_add(&comp->cursor->events.axis, &comp->cursor_axis);

  comp->cursor_frame.notify = handle_cursor_frame;
  wl_signal_add(&comp->cursor->events.frame, &comp->cursor_frame);

  update_seat_capabilities(comp);

  fprintf(stderr, "[c] comp_create: ok (build=2026-02-21c)\n");
  return comp;
}

int comp_start(compositor_t *base) {
  struct compositor *comp = base;
  if (!comp) {
    return -1;
  }

  char socket_name[128];
  if (add_wlroots_socket(comp->display, socket_name, sizeof(socket_name)) != 0) {
    fprintf(stderr, "[c] comp_start: failed to add wayland socket\n");
    push_event(comp, EV_BACKEND_FAILED, BACKEND_FAIL_SOCKET, 0);
    return -1;
  }

  setenv("WAYLAND_DISPLAY", socket_name, 1);
#if WLRLEAN_HAS_XWAYLAND
  if (comp->xwayland && comp->xwayland->display_name) {
    setenv("DISPLAY", comp->xwayland->display_name, 1);
  }
#endif
  // Ensure spawned clients don't keep using a parent compositor socket FD.
  unsetenv("WAYLAND_SOCKET");

  if (!wlr_backend_start(comp->backend)) {
    uint64_t reason = detect_start_failure_reason();
    if (reason == BACKEND_FAIL_SEAT_NOT_FOUND) {
      fprintf(stderr,
              "[c] comp_start: backend start failed (seat not found). "
              "Try running seatd/logind or use WLR_BACKENDS=headless.\n");
    } else {
      fprintf(stderr, "[c] comp_start: wlr_backend_start failed\n");
    }
    push_event(comp, EV_BACKEND_FAILED, reason, 0);
    return -1;
  }

  fprintf(stderr, "[c] comp_start: backend started, socket=%s pid=%d\n", socket_name, getpid());
  push_event(comp, EV_BACKEND_STARTED, 0, 0);
  return 0;
}

void comp_run_once(compositor_t *base) {
  struct compositor *comp = base;
  if (!comp) {
    return;
  }

  int rc = wl_event_loop_dispatch(comp->event_loop, 16);
  if (rc < 0) {
    fprintf(stderr, "[c] comp_run_once: dispatch returned %d\n", rc);
  }

  wl_display_flush_clients(comp->display);
  push_output_size_if_changed(comp);

  comp->tick_count++;
  push_event(comp, EV_TICK, comp->tick_count, 0);
}

int comp_poll_event(compositor_t *base, event_t *out) {
  struct compositor *comp = base;
  if (!comp || !out) {
    return 0;
  }

  int has = pop_event(comp, out);
  if (has) {
    comp->last_event = *out;
    if (out->tag != EV_TICK || (out->a % 60) == 0) {
      fprintf(stderr, "[c] poll event tag=%u a=%" PRIu64 " b=%" PRIu64 "\n", out->tag, out->a, out->b);
    }
  } else {
    memset(&comp->last_event, 0, sizeof(comp->last_event));
  }
  return has;
}

int comp_apply_cmds(compositor_t *base, const cmd_t *cmds, size_t n) {
  struct compositor *comp = base;
  if (!comp) {
    return -1;
  }

  if (n == 0) {
    return 0;
  }

  fprintf(stderr, "[c] apply_cmds n=%zu\n", n);
  int rc = 0;
  for (size_t i = 0; i < n; i++) {
    fprintf(stderr, "[c] cmd[%zu] tag=%u a=%" PRIu64 " b=%" PRIu64 "\n", i, cmds[i].tag, cmds[i].a,
            cmds[i].b);
    switch (cmds[i].tag) {
    case CMD_QUIT:
      wl_display_terminate(comp->display);
      break;
    case CMD_SPAWN_ID:
      if (spawn_by_id(comp, cmds[i].a) != 0) {
        rc = -1;
      }
      break;
    case CMD_CLOSE_FOCUSED:
      if (comp->focused_view && comp->focused_view->xdg_surface && comp->focused_view->xdg_surface->toplevel) {
        wlr_xdg_toplevel_send_close(comp->focused_view->xdg_surface->toplevel);
      }
#if WLRLEAN_HAS_XWAYLAND
      else if (comp->focused_xwayland_view && comp->focused_xwayland_view->xsurface) {
        wlr_xwayland_surface_close(comp->focused_xwayland_view->xsurface);
      }
#endif
      break;
    default:
      break;
    }
  }

  return rc;
}

int comp_set_pointer_focus_mode(compositor_t *base, uint32_t mode) {
  struct compositor *comp = base;
  if (!comp) {
    return -1;
  }
  if (mode != POINTER_FOCUS_CLICK && mode != POINTER_FOCUS_HOVER) {
    return -1;
  }
  comp->pointer_focus_mode = mode;
  return 0;
}

void comp_destroy(compositor_t *base) {
  struct compositor *comp = base;
  if (!comp) {
    return;
  }

  fprintf(stderr, "[c] comp_destroy\n");
  comp->destroying = true;

  if (comp->display) {
    remove_listener_if_linked(&comp->new_layer_surface);
    remove_listener_if_linked(&comp->new_xwayland_surface);
    remove_listener_if_linked(&comp->xwayland_ready);
    remove_listener_if_linked(&comp->request_cursor);
    remove_listener_if_linked(&comp->cursor_motion);
    remove_listener_if_linked(&comp->cursor_motion_absolute);
    remove_listener_if_linked(&comp->cursor_button);
    remove_listener_if_linked(&comp->cursor_axis);
    remove_listener_if_linked(&comp->cursor_frame);
    remove_listener_if_linked(&comp->new_output);
    remove_listener_if_linked(&comp->new_input);
    remove_listener_if_linked(&comp->new_xdg_surface);
    remove_listener_if_linked(&comp->new_xdg_toplevel);
    if (comp->xwayland) {
#if WLRLEAN_HAS_XWAYLAND
      wlr_xwayland_destroy(comp->xwayland);
#endif
      comp->xwayland = NULL;
    }
    if (comp->cursor) {
      wlr_cursor_destroy(comp->cursor);
      comp->cursor = NULL;
    }
    if (comp->cursor_mgr) {
      wlr_xcursor_manager_destroy(comp->cursor_mgr);
      comp->cursor_mgr = NULL;
    }
    wl_display_destroy_clients(comp->display);
    wl_display_destroy(comp->display);
  }

  struct registered_app *it = comp->apps;
  while (it) {
    struct registered_app *next = it->next;
    free(it->command);
    free(it);
    it = next;
  }

  free(comp);
}

static compositor_t *comp_from_handle(uint64_t handle) {
  return (compositor_t *)(uintptr_t)handle;
}

#ifndef SHIM_NO_LEAN_FFI
LEAN_EXPORT lean_obj_res lean_comp_create(b_lean_obj_arg w) {
  (void)w;
  compositor_t *comp = comp_create();
  return lean_io_result_mk_ok(lean_box_uint64((uint64_t)(uintptr_t)comp));
}

LEAN_EXPORT lean_obj_res lean_comp_start(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  int rc = comp_start(comp_from_handle(handle));
  uint32_t out = (rc == 0) ? 0u : 1u;
  return lean_io_result_mk_ok(lean_box_uint32(out));
}

LEAN_EXPORT lean_obj_res lean_comp_run_once(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  comp_run_once(comp_from_handle(handle));
  return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res lean_comp_poll_event(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  compositor_t *comp = comp_from_handle(handle);
  event_t ev;
  memset(&ev, 0, sizeof(ev));
  int has = comp_poll_event(comp, &ev);
  return lean_io_result_mk_ok(lean_box_uint32((uint32_t)has));
}

LEAN_EXPORT lean_obj_res lean_comp_last_event_tag(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  compositor_t *comp = comp_from_handle(handle);
  uint32_t tag = comp ? comp->last_event.tag : 0;
  return lean_io_result_mk_ok(lean_box_uint32(tag));
}

LEAN_EXPORT lean_obj_res lean_comp_last_event_a(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  compositor_t *comp = comp_from_handle(handle);
  uint64_t a = comp ? comp->last_event.a : 0;
  return lean_io_result_mk_ok(lean_box_uint64(a));
}

LEAN_EXPORT lean_obj_res lean_comp_last_event_b(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  compositor_t *comp = comp_from_handle(handle);
  uint64_t b = comp ? comp->last_event.b : 0;
  return lean_io_result_mk_ok(lean_box_uint64(b));
}

LEAN_EXPORT lean_obj_res lean_comp_apply_no_cmds(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  int rc = comp_apply_cmds(comp_from_handle(handle), NULL, 0);
  uint32_t out = (rc == 0) ? 0u : 1u;
  return lean_io_result_mk_ok(lean_box_uint32(out));
}

LEAN_EXPORT lean_obj_res lean_comp_cmd_quit(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  cmd_t cmd = {.tag = CMD_QUIT, ._pad = 0, .a = 0, .b = 0};
  int rc = comp_apply_cmds(comp_from_handle(handle), &cmd, 1);
  uint32_t out = (rc == 0) ? 0u : 1u;
  return lean_io_result_mk_ok(lean_box_uint32(out));
}

LEAN_EXPORT lean_obj_res lean_comp_register_app(uint64_t handle, b_lean_obj_arg command, b_lean_obj_arg w) {
  (void)w;
  struct compositor *comp = comp_from_handle(handle);
  const char *cmd = lean_string_cstr(command);
  uint64_t app_id = register_app_command(comp, cmd);
  return lean_io_result_mk_ok(lean_box_uint64(app_id));
}

LEAN_EXPORT lean_obj_res lean_comp_cmd_spawn_id(uint64_t handle, uint64_t app_id, b_lean_obj_arg w) {
  (void)w;
  cmd_t cmd = {.tag = CMD_SPAWN_ID, ._pad = 0, .a = app_id, .b = 0};
  int rc = comp_apply_cmds(comp_from_handle(handle), &cmd, 1);
  uint32_t out = (rc == 0) ? 0u : 1u;
  return lean_io_result_mk_ok(lean_box_uint32(out));
}

LEAN_EXPORT lean_obj_res lean_comp_cmd_close_focused(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  cmd_t cmd = {.tag = CMD_CLOSE_FOCUSED, ._pad = 0, .a = 0, .b = 0};
  int rc = comp_apply_cmds(comp_from_handle(handle), &cmd, 1);
  uint32_t out = (rc == 0) ? 0u : 1u;
  return lean_io_result_mk_ok(lean_box_uint32(out));
}

LEAN_EXPORT lean_obj_res lean_comp_cmd_set_rect(uint64_t handle, uint64_t id, uint32_t x, uint32_t y, uint32_t w,
                                                 uint32_t h, b_lean_obj_arg world) {
  (void)world;
  struct compositor *comp = comp_from_handle(handle);
  struct wlrlean_view *view = find_view_by_id(comp, id);
  if (view && view->mapped && view->xdg_surface && view->xdg_surface->toplevel) {
    wlr_scene_node_set_position(&view->scene_tree->node, (int)x, (int)y);
    wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, (int)w, (int)h);
    wlr_xdg_toplevel_set_tiled(view->xdg_surface->toplevel, WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP |
                                                           WLR_EDGE_BOTTOM);
    wlr_xdg_surface_schedule_configure(view->xdg_surface);
    return lean_io_result_mk_ok(lean_box_uint32(0));
  }

  struct wlrlean_xwayland_view *xview = find_xwayland_view_by_id(comp, id);
#if WLRLEAN_HAS_XWAYLAND
  if (xview && xview->mapped && xview->xsurface) {
    wlr_xwayland_surface_configure(xview->xsurface, (int)x, (int)y, (int)w, (int)h);
    if (xview->scene_tree) {
      wlr_scene_node_set_position(&xview->scene_tree->node, (int)x, (int)y);
    }
    return lean_io_result_mk_ok(lean_box_uint32(0));
  }
#else
  (void)xview;
#endif
  return lean_io_result_mk_ok(lean_box_uint32(1));
}

LEAN_EXPORT lean_obj_res lean_comp_cmd_focus_id(uint64_t handle, uint64_t id, b_lean_obj_arg world) {
  (void)world;
  struct compositor *comp = comp_from_handle(handle);
  struct wlrlean_view *view = find_view_by_id(comp, id);
  if (view && view->mapped && view->xdg_surface && view->xdg_surface->toplevel) {
    focus_view(comp, view);
    return lean_io_result_mk_ok(lean_box_uint32(0));
  }
  struct wlrlean_xwayland_view *xview = find_xwayland_view_by_id(comp, id);
#if WLRLEAN_HAS_XWAYLAND
  if (xview && xview->mapped && xview->xsurface) {
    focus_xwayland_view(comp, xview);
    return lean_io_result_mk_ok(lean_box_uint32(0));
  }
#else
  (void)xview;
#endif
  return lean_io_result_mk_ok(lean_box_uint32(1));
}

LEAN_EXPORT lean_obj_res lean_comp_destroy(uint64_t handle, b_lean_obj_arg w) {
  (void)w;
  comp_destroy(comp_from_handle(handle));
  return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res lean_comp_set_pointer_focus_mode(uint64_t handle, uint32_t mode, b_lean_obj_arg w) {
  (void)w;
  int rc = comp_set_pointer_focus_mode(comp_from_handle(handle), mode);
  uint32_t out = (rc == 0) ? 0u : 1u;
  return lean_io_result_mk_ok(lean_box_uint32(out));
}
#endif
