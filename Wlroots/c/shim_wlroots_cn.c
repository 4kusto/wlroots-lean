#include <stdint.h>

/*
 * CN-only model for wlroots-dependent lifecycle boundaries in shim.c:
 * - setup_renderer_allocator()
 * - add_wlroots_socket()
 * - wl_display_destroy()
 *
 * This file intentionally models contracts only, without real wlroots calls.
 */

uint64_t setup_renderer_allocator_model(uint32_t renderer_ok, uint32_t allocator_ok)
/*$
requires true;
ensures (return == 0u64 || return == 1u64) &&
        ((renderer_ok != 0u32 && allocator_ok != 0u32) == (return == 1u64));
$*/
{
  if (renderer_ok != 0u && allocator_ok != 0u) {
    return 1u;
  }
  return 0u;
}

uint64_t add_wlroots_socket_fd_delta_model(uint32_t socket_ok)
/*$
requires true;
ensures (return == 0u64 || return == 1u64) &&
        ((socket_ok != 0u32) == (return == 1u64));
$*/
{
  if (socket_ok != 0u) {
    return 1u;
  }
  return 0u;
}

uint64_t wl_display_destroy_fd_after_model(uint64_t fd_balance_before_destroy)
/*$
requires true;
ensures return == 0u64;
$*/
{
  (void)fd_balance_before_destroy;
  return 0u;
}

uint64_t wlroots_lifecycle_fd_balance_model(uint32_t backend_ok, uint32_t renderer_ok, uint32_t allocator_ok,
                                            uint32_t socket_ok, uint32_t destroy_called)
/*$
requires true;
ensures (destroy_called == 0u32 || return == 0u64);
$*/
{
  uint64_t initialized = setup_renderer_allocator_model(renderer_ok, allocator_ok);
  if (backend_ok == 0u || initialized == 0u) {
    return 0u;
  }

  uint64_t fd_balance = add_wlroots_socket_fd_delta_model(socket_ok);
  if (destroy_called != 0u) {
    fd_balance = wl_display_destroy_fd_after_model(fd_balance);
  }
  return fd_balance;
}
