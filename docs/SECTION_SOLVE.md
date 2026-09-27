# The section solver

The repair loop (see [ARCHITECTURE.md](ARCHITECTURE.md#3-the-solve-loop-one-level))
fixes the model one transition at a time. On a stretch the model is simply wrong
about, that can grind: every death teaches a fixup that applies to one state, and
the next plan dies a few ticks further on. The section solver answers the question
underneath — *is there a way through here at all?* — by dropping the model for one
short stretch and letting **the game itself be the transition function**.

Since v0.3.0 it is part of the loop: the loop starts it on its own, splices its
answer back into the plan and carries on. In the release's runs it was spliced on
lv16 (coins off) and lv22 (coins on) among the official levels, and on most of the
spin-off levels — SubZero's more than any other.

Code: `src/solver/secsolve.hpp` (the search's settings and helpers),
`runSectionSolve` in `src/mod/hooks_gamelayer.cpp` (the search),
`autoFire` / `autoWindow` and the handoff in `src/mod/repair.hpp` (when and
where the loop starts one), and `src/solver/psnap.hpp` (the snapshot).

## 1. Why searching the real game is affordable here

Searching against the game is not tractable in general. It is here because the
section makes the question narrow:

* the **entry is fixed** — the search starts at a practice-mode checkpoint the game
  itself placed, not at a state it has to reach;
* the **exit is binary** — the section's goal was reached alive, or it was not;
* the **horizon is short** — a few hundred ticks.

So there is no fitness function to choose.

## 2. When the loop starts one

With cfg `dpsecauto` on (the default), each death at the run's deepest wall is a
candidate. A section solve — a *rung* — is started when one of these holds:

* **the wall has cost as much as a rung would** (`dpsecrent`, on by default): the
  work the loop has spent at this wall, counted in solver calls and states rather
  than wall-clock time, reaches the average cost of this run's earlier rungs
  (before the first one, an assumed cost, `dpsecrungprior`);
* **the ladder has nothing left**: every anchor past the last splice has been tried;
* optionally: a plan that claimed the goal died at the wall (`dpsecsolved`), the
  loop stalled for N rounds at the same wall (`dpsecstall`), or N deaths in a row
  taught no fixup (`dpsecnorec`) — all off by default.

With coins on, the wall a rung goes to is the *coin wall* — where the plan was last
cut for a coin — and the rung must take that coin (`dpseccoinrung`, on by default;
see [COINS.md](COINS.md)).

**The window.** The head of the section is placed before the wall and moves back on
each new try at the same wall: 200 ticks, then 400, then 800, then 800 more per try.
It is never placed before the last splice's pin unless that pin is within 100 ticks
of the wall (then the head starts `dpsecpinback` ticks before the pin), and never
before tick 1. The search must stay alive `dpsecmargin` ticks (54) past the wall; a
wall right behind a previous splice doubles that margin (`dpsecchain`). The frontier
is capped at `dpseccap` states per layer (100). A wall whose windows are spent — the
head would fall before tick 1, or further back than `dpsecmaxback` if that is set —
gets no more rungs.

## 3. The handoff

1. **The loop stops.** New solver jobs are blocked first, then the handoff waits for
   the one in flight to finish.
2. **The prefix.** The deepest plan the game has verified is installed and replayed
   from the start of the level. A start past the deepest verified tick is refused.
3. **The checkpoint.** Practice mode is switched on 100 ticks before the head and a
   checkpoint is placed on the head's exact tick (the last few ticks before it run
   one physics tick per frame, so it lands exactly).
4. **When the prefix does not get there.** On a level where the game does not
   replay the same plan the same way twice (a random teleport, for example), the
   prefix can die before the head, or reach it dead. Each such attempt counts once;
   the second one abandons the rung: the loop gets its settings and its deepest plan
   back and carries on as if the search had found nothing.

A rung belongs to the session that raised it. Leaving the level mid-rung ends it
there, and the next level starts with the section solver's settings as they were
at the start of the session.

## 4. The search

The search is per-layer breadth-first over the two inputs (release, press), the
same shape as the DP so that truncation behaves the way it does there.

* **A node holds its own restorable state.** Each frontier node keeps either a game
  checkpoint or a compact snapshot of the player and the layer (`psnap`, a few KB);
  expanding it is one restore and one physics step. Both are released as soon as
  the layer moves on. The node's identity is its parent and the input it took, so
  walking the parents gives the plan.
* **Nothing is killed.** A death is recorded, not carried out: collisions, portals
  and triggers still fire, but the level never ends the attempt.
* **Deduplication** keys on mode, size, gravity, held input, dash, y, vy, x, the
  coins taken and which objects (portals, pads, rings) have been used. The grid is
  fine on purpose (0.25 px in y, 0.1 in vy): a bucket keeps its first arrival and
  the insertion order puts the lower branches first, so a coarse grid silently
  discards the top of the reachable band.
* **The cap** is applied once per layer, spread evenly across families of states
  (by x, dash, coins and used objects), or along whichever axis the layer is spread
  on — y in a normal section, x in a rotated one.
* **The goal** can be written in x, in y with a direction, or as a survived depth.
  Every condition that is set must hold: "cross the wall, then stay alive K ticks"
  can only be written as an AND.
* **Coins.** With coins on, each node carries the coins it has taken (the player's
  box over the coin's live box). A branch that leaves a coin it still owes behind
  for good is dead — judged while the player runs forward in gameplay frame 0, and
  only for a coin whose passing is final. Coins taken before the head come from the
  recording, not from the search. A rung started for a particular coin only accepts
  leaves that took it.

**Snapshots or checkpoints.** A rung sweeps the section head once and picks the
primitive: the snapshot where it reproduces the game, the checkpoint where
something moves that the snapshot cannot carry (a moving portal always means the
checkpoint). Under snapshots, every 20 layers each frontier node is replayed from
the last checkpoint and dropped if it does not match: the snapshot's only known
error is a missed death, so this can only remove false survivors.

## 5. Verification and the splice

**Every leaf is replayed** from the section checkpoint the plain way — no dedupe,
no cap — the moment it reaches the goal:

* `SOLVED` — the replay reaches the goal alive and at least one follow-up line
  (no press, held, periodic taps) survives `secgrace` ticks past it;
* `DOOMED` — it reaches the goal but every follow-up line dies there: a dead end,
  so the search keeps going;
* `UNVERIFIED` — the plain replay does not reproduce the leaf. A rung passes over
  it and keeps searching; only a leaf the replay reproduces is spliced.

**The splice.** The leaf's inputs replace the plan from the head on, and the loop's
own plan is updated to match. The plan the rung was started from is not pasted back
after the splice (its inputs assume states the splice no longer produces); it stays
the *rejoin target* of the next search (`dpsecreuse`).

**The pin.** The splice is pinned at the later of its end and the wall it was
started for. The ladder never anchors before the pin — that would hand the crossing
back to the model that could not do it — and adds a rung at the pin itself, because
whatever follows a splice was flown with no plan at all. If every rung left is before
the pin, the wall goes to another section solve; only if none can be started is the
pin dropped.

Nothing the search finds is taken on its word: the spliced plan is flown by the
game from the start of the level like every other plan, and only a plan the game
clears is filed.

## 6. What it cannot see

A branch is one restore plus one step, so a death the game reaches only by
accumulating state across consecutive ticks — an out-of-bounds latch that needs two
of them, a one-shot trigger the restore puts back — can be reset before it fires.
The search then believes a lethal corridor is passable, and the leaf replay refuses
the answer. An `UNVERIFIED` leaf is that guard working: no false plan is spliced.

It also searches the game as it is, random numbers included. The model plans an
Area Move with a random variance against the whole box it can land in; the search
sees the block wherever the game's seeds put it. While the bot drives, the mod sets
those seeds to the same values before every attempt and every checkpoint restore,
so every run from the start of the level meets the same draw. A restore therefore
starts the search from the attempt's first values, not from the ones the run from
the start has reached by then, and a stretch it solves after a Random trigger can
depend on that difference; the replay from the start of the level catches such a
plan. The play menu says when a level has a Random or Advanced Random trigger, or
an area or enter effect with a random variance; a spawn delay's or an Advanced
Follow's variance is not detected yet.

## 7. Cost

* A checkpoint restore's cost follows the level's object count, most of it the game
  re-sorting its section lists: about 2 ms on the whole of lv20, tens of
  microseconds on a sliced copy (see [LEVEL_SLICE.md](LEVEL_SLICE.md)). A snapshot
  restore costs about 1.4 µs.
* **A restore must not get slower as the search goes on.** Three things used to make
  it do so on heavy levels, and each is handled:
  * clearing the checkpoint list used to leave each checkpoint's object in the
    layer — one more child for the game to sort on every restore (5,710 to 41,726
    children over one run, the restore 24 to 72 µs). Cleared checkpoints now leave
    the layer the way the game's own removals do, and the restore cost stays flat;
  * a released node's game state, touch state and activation record are freed, so
    memory follows the live frontier rather than every node ever made (on a heavy
    level the working set went from 4.7 GB to 710 MB; `secmem:` prints the
    footprint at the end of a search);
  * the game re-parents every sprite layer through its shader layer twice per
    restore, and on a long search that was the part that grew. It is skipped
    while the search runs and done again by the first real reset after it
    (`secshaderskip`, on by default); the search itself is unchanged (identical
    layer fingerprints). On a heavy custom level solved cold with and without it,
    the restore went from 74 to 12 µs and the whole solve from 470 to 328 s, with
    the same rounds and the same search layers.
* Rendering and the visibility pass are held off for the whole search: the search
  moves the world back and forth through geometry it does not restore.
* The search yields a frame every `secslicems` (12 ms) so the game window stays
  responsive.

## 8. Running one by hand

* **At session open:** `secsolve=1` with `checkpointat=<tick>` (and `sectarget`,
  `sechorizon`, `seccap`, ...). Without a checkpoint it says so and does not run.
* **During an in-process solve**, through `cmd.txt`:
  * `secrung <startTick> <targetX> [horizon] [cap]` — a rung, exactly as the loop
    starts one: spliced back, and the loop resumes;
  * `secsolve <startTick> <targetX> [horizon] [cap]` — one-way: the search takes
    over and ends the session with its verdict. For interrogating a single wall.

## 9. Settings

The loop's keys (autorun.cfg / the play menu's cfg):

| key | default | meaning |
|---|---|---|
| `dpsecauto` | 1 | the loop starts section solves itself |
| `dpsecrent` | 1 | start one when the work at a wall reaches a rung's cost |
| `dpsecrungprior` | 15 | assumed rung cost before this run has measured one |
| `dpsecsolved` | 0 | start one when a plan that claimed the goal dies at the wall |
| `dpsecstall` | 0 | start one after N rounds at the same wall (0 = off) |
| `dpsecnorec` | 0 | start one after N deaths with no fixup (0 = off) |
| `dpsectierfirst` | 0 | with no anchor left, try a section solve before a larger DP |
| `dpsecmargin` | 54 | ticks past the wall the search must survive |
| `dpsecchain` | 1 | double the margin for a wall right behind a splice |
| `dpsecchainspan` | 30 | how close to a pin counts as "right behind" |
| `dpsecpinback` | 60 | start a window this far before a nearby pin |
| `dpseccap` | 100 | frontier cap per layer |
| `dpsecmaxback` | 0 | refuse windows starting more than N ticks before the wall (0 = no limit) |
| `dpsecreuse` | 1 | keep the started-from plan as the rejoin target |
| `dpseccoinrung` | 1 | with coins, start rungs at the coin wall and require that coin |

The search's own keys (by hand; a rung sets what it needs): `sectarget`,
`sectargety` / `sectargetydir`, `sectargetdepth`, `sechorizon`, `seccap`, `secgrace`
(600), `secmaxdoomed` (500), `seccoins` (1), `secsnap` (0 checkpoint, 1 snapshot,
2 sweep and decide), `secverifyevery` (0; a rung uses 20), `secslicems` (12),
`secshaderskip` (1). The remaining `sec*` keys are measurement switches.

## 10. What it writes to result.txt

* `dpsolve:   [secauto] ... - a section solve from t=... alive to t=... horizon ... cap ...`
* `secrung handoff: loop suspended after iter N - replaying M inputs to t=..., ...`
* `secrung: the prefix died at t=... before the head t=... (try N of 2)` and
  `secrung: abandoned - ...`
* `secsolve: SOLVED|UNVERIFIED|DOOMED|EXHAUSTED ...` — the search's verdict
* `secrung: spliced N ticks from t=... pinned at t=... - the loop carries on`, or
  `secrung: the search found nothing - the loop carries on from the deepest plan`
* `seclayer: ...` — one line per layer (always on for a rung): frontier, deaths,
  duplicates, capped states, and where the time went
* `secmem: ...` — the search's memory at its end
