import Wlroots.Runtime
import Std

namespace Wlroots

structure Rect where
  x : Nat
  y : Nat
  w : Nat
  h : Nat
  deriving Repr

structure WMState where
  cols : Array (Array UInt64) := #[]
  focus : Option UInt64 := none
  weights : Std.HashMap UInt64 Int := {}
  screenW : Nat := 1280
  screenH : Nat := 720
  lastEvent : String := "init"
  deriving Repr

inductive WMAction where
  | toggleSplit
  | focusPrev
  | focusNext
  | moveLeft
  | moveDown
  | moveUp
  | moveRight
  | resizeShrink
  | resizeGrow
  | closeFocused
  deriving Repr

private def clampWeight (w : Int) : Int :=
  max 20 (min 300 w)

private def idWeight (st : WMState) (id : UInt64) : Nat :=
  Int.toNat <| clampWeight (st.weights.getD id 100)

private def insertCol (cols : Array (Array UInt64)) (idx : Nat) (col : Array UInt64) : Array (Array UInt64) :=
  Id.run do
    let pos := Nat.min idx cols.size
    let mut out : Array (Array UInt64) := #[]
    let mut i := 0
    while i < cols.size do
      if i == pos then
        out := out.push col
      out := out.push cols[i]!
      i := i + 1
    if pos == cols.size then
      out := out.push col
    return out

private def removeEmptyCols (cols : Array (Array UInt64)) : Array (Array UInt64) :=
  cols.filter (fun c => !c.isEmpty)

private def findPos (cols : Array (Array UInt64)) (id : UInt64) : Option (Nat × Nat) :=
  Id.run do
    let mut ci := 0
    while ci < cols.size do
      let col := cols[ci]!
      let mut ri := 0
      while ri < col.size do
        if col[ri]! == id then
          return some (ci, ri)
        ri := ri + 1
      ci := ci + 1
    return none

private def removeIdFromCols (cols : Array (Array UInt64)) (id : UInt64) : Array (Array UInt64) :=
  removeEmptyCols <| cols.map (fun col => col.filter (fun x => x != id))

private def removeColAt (cols : Array (Array UInt64)) (idx : Nat) : Array (Array UInt64) :=
  Id.run do
    let mut out : Array (Array UInt64) := #[]
    let mut i := 0
    while i < cols.size do
      if i != idx then
        out := out.push cols[i]!
      i := i + 1
    return out

private def flattenOrder (cols : Array (Array UInt64)) : Array UInt64 :=
  Id.run do
    let mut out : Array UInt64 := #[]
    for col in cols do
      for id in col do
        out := out.push id
    return out

private def firstId? (cols : Array (Array UInt64)) : Option UInt64 :=
  match cols[0]? with
  | some col => col[0]?
  | none => none

private def swapAt (arr : Array UInt64) (i j : Nat) : Array UInt64 :=
  if i == j || i >= arr.size || j >= arr.size then
    arr
  else
    let ai := arr[i]!
    let aj := arr[j]!
    (arr.set! i aj).set! j ai

private def rowLayout (st : WMState) (col : Array UInt64) (x : Nat) (w : Nat) : List (UInt64 × Rect) :=
  if col.isEmpty then
    []
  else
    let n := col.size
    let sumW := Id.run do
      let mut i := 0
      let mut s := 0
      while i < n do
        s := s + idWeight st col[i]!
        i := i + 1
      if s == 0 then 1 else s
    let rec go (i : Nat) (cursor : Nat) (acc : List (UInt64 × Rect)) :=
      if i >= n then
        acc.reverse
      else
        let id := col[i]!
        let wi := idWeight st id
        let next := if i + 1 == n then st.screenH else cursor + (st.screenH * wi) / sumW
        let r : Rect := { x := x, y := cursor, w := w, h := next - cursor }
        go (i + 1) next ((id, r) :: acc)
    go 0 0 []

private def layoutRects (st : WMState) : List (UInt64 × Rect) :=
  let cols := st.cols
  let n := cols.size
  if n == 0 then
    []
  else
    let rec go (i : Nat) (cursor : Nat) (acc : List (UInt64 × Rect)) :=
      if i >= n then
        acc.reverse
      else
        let next := if i + 1 == n then st.screenW else (st.screenW * (i + 1)) / n
        let rs := rowLayout st cols[i]! cursor (next - cursor)
        go (i + 1) next (rs.reverse ++ acc)
    go 0 0 []

private def toLayoutCmds (st : WMState) : Array Cmd :=
  let rectCmds := (layoutRects st).toArray.map (fun (id, r) =>
    .setRect id (UInt32.ofNat r.x) (UInt32.ofNat r.y) (UInt32.ofNat r.w) (UInt32.ofNat r.h))
  match st.focus with
  | some fid => rectCmds.push (.focusId fid)
  | none => rectCmds

private def adjustFocusedWeight (st : WMState) (delta : Int) : WMState :=
  match st.focus with
  | none => st
  | some fid =>
      let w := st.weights.getD fid 100
      { st with weights := st.weights.insert fid (clampWeight (w + delta)) }

private def focusRotate (st : WMState) (next : Bool) : WMState :=
  let ord := flattenOrder st.cols
  if ord.isEmpty then
    { st with focus := none }
  else
    let curr :=
      match st.focus with
      | some id =>
          match ord.findIdx? (fun x => x == id) with
          | some i => i
          | none => 0
      | none => 0
    let n := ord.size
    let idx := if next then (curr + 1) % n else (curr + n - 1) % n
    { st with focus := some ord[idx]! }

private def moveFocused (st : WMState) (dir : WMAction) : WMState :=
  match st.focus with
  | none => st
  | some fid =>
      match findPos st.cols fid with
      | none => st
      | some (ci, ri) =>
          let col := st.cols[ci]!
          match dir with
          | .moveUp =>
              if ri > 0 then
                let col' := swapAt col ri (ri - 1)
                { st with cols := st.cols.set! ci col' }
              else if ci > 0 then
                let colsRemoved := removeIdFromCols st.cols fid
                let tgt := colsRemoved[ci - 1]!
                { st with cols := colsRemoved.set! (ci - 1) (#[fid] ++ tgt) }
              else
                st
          | .moveDown =>
              if ri + 1 < col.size then
                let col' := swapAt col ri (ri + 1)
                { st with cols := st.cols.set! ci col' }
              else
                let colsRemoved := removeIdFromCols st.cols fid
                let tgtIdx := if col.size == 1 then ci else ci + 1
                if tgtIdx < colsRemoved.size then
                  let tgt := colsRemoved[tgtIdx]!
                  { st with cols := colsRemoved.set! tgtIdx (tgt.push fid) }
                else
                  st
          | .moveLeft =>
              if ci > 0 then
                let left := st.cols[ci - 1]!
                if left.isEmpty then
                  st
                else
                  let lj := Nat.min ri (left.size - 1)
                  let lhs := st.cols[ci]!
                  let rhs := left
                  let a := lhs[ri]!
                  let b := rhs[lj]!
                  let cols1 := st.cols.set! ci (lhs.set! ri b)
                  { st with cols := cols1.set! (ci - 1) (rhs.set! lj a) }
              else
                st
          | .moveRight =>
              if ci + 1 < st.cols.size then
                let right := st.cols[ci + 1]!
                if right.isEmpty then
                  st
                else
                  let rj := Nat.min ri (right.size - 1)
                  let lhs := st.cols[ci]!
                  let rhs := right
                  let a := lhs[ri]!
                  let b := rhs[rj]!
                  let cols1 := st.cols.set! ci (lhs.set! ri b)
                  { st with cols := cols1.set! (ci + 1) (rhs.set! rj a) }
              else
                st
          | _ => st

private def toggleSplitFocused (st : WMState) : WMState :=
  match st.focus with
  | none => st
  | some fid =>
      match findPos st.cols fid with
      | none => st
      | some (ci, _) =>
          let col := st.cols[ci]!
          if col.size > 1 then
            let col' := col.filter (fun x => x != fid)
            let cols1 := st.cols.set! ci col'
            { st with cols := insertCol cols1 (ci + 1) #[fid] }
          else if ci > 0 then
            let left := st.cols[ci - 1]!
            let cols1 := st.cols.set! (ci - 1) (left.push fid)
            { st with cols := removeColAt cols1 ci }
          else if ci + 1 < st.cols.size then
            let right := st.cols[ci + 1]!
            let cols1 := st.cols.set! (ci + 1) (#[fid] ++ right)
            { st with cols := removeColAt cols1 ci }
          else
            st

private def hasMod (mods mask : UInt32) : Bool :=
  (mods &&& mask) != 0

private def actionOfKey? (mods sym : UInt32) : Option WMAction :=
  let modLogo : UInt32 := 64
  let modShift : UInt32 := 1
  let keyH : UInt32 := 104
  let keyJ : UInt32 := 106
  let keyK : UInt32 := 107
  let keyL : UInt32 := 108
  let keyQ : UInt32 := 113
  let keyTab : UInt32 := 65289
  let keySpace : UInt32 := 32
  let keyMinus : UInt32 := 45
  let keyEqual : UInt32 := 61
  if !hasMod mods modLogo then
    none
  else if sym == keyQ then
    some .closeFocused
  else if sym == keyTab then
    if hasMod mods modShift then some .focusPrev else some .focusNext
  else if sym == keySpace then
    some .toggleSplit
  else if sym == keyMinus then
    some .resizeShrink
  else if sym == keyEqual then
    some .resizeGrow
  else if sym == keyH then
    some .moveLeft
  else if sym == keyJ then
    some .moveDown
  else if sym == keyK then
    some .moveUp
  else if sym == keyL then
    some .moveRight
  else
    none

def applyAction (st : WMState) (a : WMAction) : WMState × Array Cmd :=
  match a with
  | .toggleSplit =>
      let st' := toggleSplitFocused st
      ({ st' with lastEvent := "toggleSplit" }, toLayoutCmds st')
  | .focusPrev =>
      let st' := focusRotate st false
      ({ st' with lastEvent := "focusPrev" }, toLayoutCmds st')
  | .focusNext =>
      let st' := focusRotate st true
      ({ st' with lastEvent := "focusNext" }, toLayoutCmds st')
  | .moveLeft =>
      let st' := moveFocused st .moveLeft
      ({ st' with lastEvent := "moveLeft" }, toLayoutCmds st')
  | .moveDown =>
      let st' := moveFocused st .moveDown
      ({ st' with lastEvent := "moveDown" }, toLayoutCmds st')
  | .moveUp =>
      let st' := moveFocused st .moveUp
      ({ st' with lastEvent := "moveUp" }, toLayoutCmds st')
  | .moveRight =>
      let st' := moveFocused st .moveRight
      ({ st' with lastEvent := "moveRight" }, toLayoutCmds st')
  | .resizeShrink =>
      let st' := adjustFocusedWeight st (-10)
      ({ st' with lastEvent := "resizeShrink" }, toLayoutCmds st')
  | .resizeGrow =>
      let st' := adjustFocusedWeight st 10
      ({ st' with lastEvent := "resizeGrow" }, toLayoutCmds st')
  | .closeFocused =>
      ({ st with lastEvent := "closeFocused" }, #[.closeFocused])

def handleEvent (st : WMState) (ev : Event) : WMState × Array Cmd :=
  match ev with
  | .tick id =>
      ({ st with lastEvent := s!"tick#{id}" }, #[])
  | .newOutput id =>
      ({ st with lastEvent := s!"newOutput#{id}" }, toLayoutCmds st)
  | .outputSize w h =>
      let w' := Nat.max 1 w.toNat
      let h' := Nat.max 1 h.toNat
      let st' := { st with screenW := w', screenH := h', lastEvent := s!"outputSize({w'}x{h'})" }
      (st', toLayoutCmds st')
  | .newXdgSurface id =>
      let cols :=
        if st.cols.isEmpty then
          #[#[id]]
        else
          match st.focus with
          | some fid =>
              match findPos st.cols fid with
              | some (ci, _) =>
                  insertCol st.cols (ci + 1) #[id]
              | none =>
                  st.cols.push #[id]
          | none =>
              st.cols.push #[id]
      let st' := {
        st with
          cols := cols
          focus := some id
          weights := st.weights.insert id 100
          lastEvent := s!"newXdgSurface#{id}"
      }
      (st', toLayoutCmds st')
  | .viewUnmap id =>
      let cols := removeIdFromCols st.cols id
      let focus :=
        match st.focus with
        | some fid =>
            if fid == id then firstId? cols
            else
              let ord := flattenOrder cols
              if ord.any (fun x => x == fid) then some fid else firstId? cols
        | none => firstId? cols
      let st' := { st with cols, focus, weights := st.weights.erase id, lastEvent := s!"viewUnmap#{id}" }
      (st', toLayoutCmds st')
  | .backendStarted =>
      ({ st with lastEvent := "backendStarted" }, #[])
  | .backendFailed reason =>
      ({ st with lastEvent := s!"backendFailed(reason={reason})" }, #[])
  | .unknown tag a b =>
      ({ st with lastEvent := s!"unknown(tag={tag},a={a},b={b})" }, #[])
  | .key mods sym =>
      match actionOfKey? mods sym with
      | some a => applyAction st a
      | none => ({ st with lastEvent := s!"key(mods={mods},sym={sym})" }, #[])

end Wlroots
