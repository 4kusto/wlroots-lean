namespace Wlroots

inductive Event where
  | tick (id : UInt64)
  | newOutput (id : UInt64)
  | outputSize (w : UInt32) (h : UInt32)
  | newXdgSurface (id : UInt64)
  | viewUnmap (id : UInt64)
  | key (mods : UInt32) (sym : UInt32)
  | backendStarted
  | backendFailed (reason : UInt64)
  | unknown (tag : UInt32) (a : UInt64) (b : UInt64)
  deriving Repr

inductive Cmd where
  | noop
  | quit
  | spawn (command : String)
  | closeFocused
  | setRect (id : UInt64) (x : UInt32) (y : UInt32) (w : UInt32) (h : UInt32)
  | focusId (id : UInt64)
  deriving Repr

abbrev Handle := UInt64

@[extern "lean_comp_create"]
opaque compCreate : IO Handle

@[extern "lean_comp_start"]
opaque compStart : Handle → IO UInt32

@[extern "lean_comp_run_once"]
opaque compRunOnce : Handle → IO Unit

@[extern "lean_comp_poll_event"]
opaque compPollEvent : Handle → IO UInt32

@[extern "lean_comp_last_event_tag"]
opaque compLastEventTag : Handle → IO UInt32

@[extern "lean_comp_last_event_a"]
opaque compLastEventA : Handle → IO UInt64

@[extern "lean_comp_last_event_b"]
opaque compLastEventB : Handle → IO UInt64

@[extern "lean_comp_apply_no_cmds"]
opaque compApplyNoCmds : Handle → IO UInt32

@[extern "lean_comp_cmd_quit"]
opaque compCmdQuit : Handle → IO UInt32

@[extern "lean_comp_register_app"]
opaque compRegisterApp : Handle → @& String → IO UInt64

@[extern "lean_comp_cmd_spawn_id"]
opaque compCmdSpawnId : Handle → UInt64 → IO UInt32

@[extern "lean_comp_cmd_close_focused"]
opaque compCmdCloseFocused : Handle → IO UInt32

@[extern "lean_comp_cmd_set_rect"]
opaque compCmdSetRect : Handle → UInt64 → UInt32 → UInt32 → UInt32 → UInt32 → IO UInt32

@[extern "lean_comp_cmd_focus_id"]
opaque compCmdFocusId : Handle → UInt64 → IO UInt32

@[extern "lean_comp_destroy"]
opaque compDestroy : Handle → IO Unit

def decodeEvent (tag : UInt32) (a b : UInt64) : Event :=
  if tag == 1 then
    .tick a
  else if tag == 2 then
    .newOutput a
  else if tag == 8 then
    .outputSize a.toUInt32 b.toUInt32
  else if tag == 3 then
    .newXdgSurface a
  else if tag == 7 then
    .viewUnmap a
  else if tag == 4 then
    .backendStarted
  else if tag == 5 then
    .backendFailed a
  else if tag == 6 then
    .key a.toUInt32 b.toUInt32
  else
    .unknown tag a b

def readEvent (h : Handle) : IO Event := do
  let tag ← compLastEventTag h
  let a ← compLastEventA h
  let b ← compLastEventB h
  pure <| decodeEvent tag a b

def pollEvent (h : Handle) : IO (Option Event) := do
  let has ← compPollEvent h
  if has == 0 then
    pure none
  else
    some <$> readEvent h

def applyCmd (h : Handle) (cmd : Cmd) : IO UInt32 :=
  match cmd with
  | .noop => compApplyNoCmds h
  | .quit => compCmdQuit h
  | .spawn command => do
      let appId ← compRegisterApp h command
      if appId == 0 then
        pure 1
      else
        compCmdSpawnId h appId
  | .closeFocused => compCmdCloseFocused h
  | .setRect id x y w h' => compCmdSetRect h id x y w h'
  | .focusId id => compCmdFocusId h id

end Wlroots
