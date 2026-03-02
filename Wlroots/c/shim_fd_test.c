#define _POSIX_C_SOURCE 200809L

#include "shim.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int count_open_fds(void) {
  DIR *dir = opendir("/proc/self/fd");
  if (!dir) {
    return -1;
  }

  int count = 0;
  for (struct dirent *ent = readdir(dir); ent != NULL; ent = readdir(dir)) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    count++;
  }
  closedir(dir);
  // Exclude the fd used by opendir("/proc/self/fd") itself.
  return (count > 0) ? (count - 1) : 0;
}

static void restore_env(const char *name, const char *value_snapshot) {
  if (value_snapshot) {
    setenv(name, value_snapshot, 1);
  } else {
    unsetenv(name);
  }
}

static int run_fd_balance_case(void) {
  const char *old_runtime = getenv("XDG_RUNTIME_DIR");
  const char *old_backends = getenv("WLR_BACKENDS");
  const char *old_renderer = getenv("WLR_RENDERER");
  char *snap_runtime = old_runtime ? strdup(old_runtime) : NULL;
  char *snap_backends = old_backends ? strdup(old_backends) : NULL;
  char *snap_renderer = old_renderer ? strdup(old_renderer) : NULL;

  char runtime_template[] = "/tmp/wlroots-fdtest-XXXXXX";
  char *runtime_dir = mkdtemp(runtime_template);
  if (!runtime_dir) {
    fprintf(stderr, "mkdtemp failed: errno=%d\n", errno);
    goto fail;
  }

  setenv("XDG_RUNTIME_DIR", runtime_dir, 1);
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  int before = count_open_fds();
  if (before < 0) {
    fprintf(stderr, "count_open_fds(before) failed\n");
    goto fail;
  }

  compositor_t *comp = comp_create();
  if (!comp) {
    fprintf(stderr, "comp_create failed\n");
    goto fail;
  }

  int start_rc = comp_start(comp);
  if (start_rc != 0) {
    fprintf(stderr, "comp_start failed: rc=%d\n", start_rc);
    comp_destroy(comp);
    goto fail;
  }

  comp_destroy(comp);

  int after = count_open_fds();
  if (after < 0) {
    fprintf(stderr, "count_open_fds(after) failed\n");
    goto fail;
  }

  if (after != before) {
    fprintf(stderr, "fd leak suspected: before=%d after=%d\n", before, after);
    goto fail;
  }

  restore_env("XDG_RUNTIME_DIR", snap_runtime);
  restore_env("WLR_BACKENDS", snap_backends);
  restore_env("WLR_RENDERER", snap_renderer);
  free(snap_runtime);
  free(snap_backends);
  free(snap_renderer);
  return 0;

fail:
  restore_env("XDG_RUNTIME_DIR", snap_runtime);
  restore_env("WLR_BACKENDS", snap_backends);
  restore_env("WLR_RENDERER", snap_renderer);
  free(snap_runtime);
  free(snap_backends);
  free(snap_renderer);
  return 1;
}

int main(void) {
  int rc = run_fd_balance_case();
  if (rc != 0) {
    return 1;
  }
  puts("fd lifecycle test passed");
  return 0;
}
