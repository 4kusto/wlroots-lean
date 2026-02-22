import Lake

open System Lake DSL

private def splitWS (s : String) : Array String :=
  (s.splitToList (fun c => c.isWhitespace)).filter (fun t => t != "") |>.toArray

private def readPkgConfig (args : Array String) : IO (Array String) := do
  try
    let out ← IO.Process.output { cmd := "pkg-config", args := args }
    if out.exitCode == 0 then
      return splitWS out.stdout
    return #[]
  catch _ =>
    return #[]

private def readPkgConfigFirst (argSets : Array (Array String)) : IO (Array String) := do
  for args in argSets do
    let out ← readPkgConfig args
    if !out.isEmpty then
      return out
  return #[]

private def wlrootsCFlags : Array String :=
  run_io readPkgConfigFirst #[
    #["--cflags", "wlroots"],
    #["--cflags", "wlroots-0.19"]
  ]

private def wlrootsLibs : Array String :=
  run_io do
    let wlr ← readPkgConfigFirst #[
      #["--libs", "wlroots"],
      #["--libs", "wlroots-0.19"]
    ]
    let wl ← readPkgConfig #["--libs", "wayland-server"]
    let xkb ← readPkgConfig #["--libs", "xkbcommon"]
    return wlr ++ wl ++ xkb

package Wlroots where
  moreLinkArgs := wlrootsLibs

@[default_target]
lean_lib Wlroots

extern_lib leanshim (pkg : NPackage __name__) := do
  let buildCDir : FilePath := pkg.buildDir / "c"
  let srcShim : FilePath := pkg.dir / "Wlroots" / "c" / "shim.c"
  let oShim : FilePath := buildCDir / "shim.o"

  let jSrcShim : Job FilePath ← JobM.runSpawnM <| inputFile srcShim false

  let leanInc ← getLeanIncludeDir

  let cflags : Array String :=
    #[
      "-I", leanInc.toString,
      "-I", (pkg.dir / "Wlroots" / "c").toString,
      "-fPIC",
      "-O2",
      "-std=c11",
      "-DWLR_USE_UNSTABLE"
    ] ++ wlrootsCFlags

  let jOShim : Job FilePath ←
    JobM.runSpawnM <| buildO oShim jSrcShim (weakArgs := cflags) (compiler := "cc")

  let libFile : FilePath := buildCDir / nameToStaticLib "leanshim"
  JobM.runSpawnM <| buildStaticLib libFile #[jOShim]
