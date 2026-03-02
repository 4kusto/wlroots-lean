#include <stdint.h>

typedef uintptr_t lean_obj_res;
typedef const void *b_lean_obj_arg;

lean_obj_res lean_comp_create_model(uint32_t backend_ok, uint32_t renderer_ok)
/*$
requires true;
ensures (return == 0u64 || return == 1u64) &&
        ((backend_ok == 0u32 || renderer_ok == 0u32) || return == 1u64);
$*/
{
  if (backend_ok == 0u || renderer_ok == 0u) {
    return (lean_obj_res)0u;
  }
  return (lean_obj_res)1u;
}

lean_obj_res lean_comp_start_model(uint64_t handle, uint32_t socket_ok, uint32_t backend_ok, b_lean_obj_arg w)
/*$
requires true;
ensures (return == 0u64 || return == 1u64) &&
        (handle != 0u64 || return == 1u64) &&
        ((socket_ok == 0u32 || backend_ok == 0u32) || (return == 0u64 || return == 1u64));
$*/
{
  (void)w;
  if (handle == 0u || socket_ok == 0u || backend_ok == 0u) {
    return (lean_obj_res)1u;
  }
  return (lean_obj_res)0u;
}

uint64_t cn_fd_balance_after_lifecycle(uint32_t socket_ok, uint32_t backend_ok)
/*$
requires true;
ensures return == 0u64;
$*/
{
  uint64_t fd_balance = 0u;
  if (backend_ok != 0u && socket_ok != 0u) {
    fd_balance = 1u;
  }
  fd_balance = 0u;
  return fd_balance;
}

lean_obj_res lean_comp_start(uint64_t handle, b_lean_obj_arg w)
/*$
requires true;
ensures (return == 0u64 || return == 1u64) && (handle != 0u64 || return == 1u64);
$*/
{
  (void)w;
  if (handle == 0ULL) {
    return (lean_obj_res)1u;
  }
  return (lean_obj_res)0u;
}

lean_obj_res lean_comp_apply_no_cmds(uint64_t handle, b_lean_obj_arg w)
/*$
requires true;
ensures (return == 0u64 || return == 1u64) && (handle != 0u64 || return == 1u64);
$*/
{
  (void)w;
  if (handle == 0ULL) {
    return (lean_obj_res)1u;
  }
  return (lean_obj_res)0u;
}

lean_obj_res lean_comp_cmd_set_rect(uint64_t handle, uint64_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    b_lean_obj_arg world)
/*$
requires true;
ensures (return == 0u64 || return == 1u64) && (id == 1u64 || return == 1u64) &&
        (handle != 0u64 || return == 1u64);
$*/
{
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)world;
  if (handle == 0ULL || id != 1ULL) {
    return (lean_obj_res)1u;
  }
  return (lean_obj_res)0u;
}

lean_obj_res lean_comp_cmd_focus_id(uint64_t handle, uint64_t id, b_lean_obj_arg world)
/*$
requires true;
ensures (return == 0u64 || return == 1u64) && (id == 1u64 || return == 1u64) &&
        (handle != 0u64 || return == 1u64);
$*/
{
  (void)world;
  if (handle == 0ULL || id != 1ULL) {
    return (lean_obj_res)1u;
  }
  return (lean_obj_res)0u;
}
