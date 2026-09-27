# Coins

With the play menu's **Coins** switch on (cfg `coinroute=1 coins=1`, dp `--coins`) the objective is
survival *and every coin in the level*. This document is how the three layers — the search (`dp/`),
the repair loop (`src/mod/repair.hpp`) and the section search (`src/solver/secsolve.hpp`) — each
treat coins, and how the loop finds what a coin needs to have happened first.

Nothing here steers the model toward a coin by hand. Every rule below reads the level's own objects
and GD's own verdicts; the coins and what gates them come out of the level at load time.

## 1. In the search

- **The collected set is part of the state.** A coin is collected when the player's own rect
  overlaps the coin where its group has put it (a coin carried by a Move is read off the moving-
  geometry recording). A state that reaches the end without every coin is not a goal.
- **What makes a coin collectable.** A coin whose group is switched off cannot be collected until
  something switches it on. The search knows four ways a level does that:
  - a Toggle in the chain of a touch box (the coin comes on when the box is entered);
  - an item counter — a Count (`item == N`) or an Item Compare (`item ≥ N`) fed by pickups or by
    a Tap that counts presses inside its window (official lv21's and lv22's third coins);
  - a pickup or key, or a toggle block pressed inside, that switches a group on (activators);
  - a Count or Tap that a box's chain spawns, armed by that box (spawn roots).
- **A coin left behind is final only where it provably is.** In gameplay frame 0 a state past a
  coin's far edge without it is dropped — but only when nothing past the coin turns the player
  round (a reversing ring or pad) and the level does not come back to frame 0 behind it. Otherwise
  passing a coin says nothing and no state is dropped for it.
- **The few lineages that took a coin are kept.** When the frontier is thinned to the cap, the
  collected set (and which activators have fired) is a class of its own, so a minority that went
  for a coin is not thinned at the majority's stride.
- At a horizon, memory or PARTIAL cut, the emitted state is the first one holding the most coins,
  not simply the first (coin pick).

## 2. In the repair loop

- **A replay that passes a coin GD did not credit ends there** and is repaired from there — for a
  plan that claims the goal, outside section searches, and not on levels with rotated gameplay (the
  section search judges those itself, §3). A coin that a Move still ahead of the player can carry
  back is left alone (cfg `coinmissmove`).
- **A clear refused for a missing coin** (cfg `coinmisspost`) is filed at that attempt's closest
  approach to the coin instead of at the finish, and every later attempt without the coin is ranked
  the same way, the deepest plan included. A route to an all-coins clear has to pass that coin.
- cfg `coinoverdepth`: a plan holding a coin the deepest plan missed counts as progress even when it
  died earlier.
- cfg `coinapproachoff`: the closest approach is also taken over the ticks the coin's group was
  switched off, and the later of the two is used (a route can switch a coin back on).
- cfg `dpseccoinrung`: the *coin wall* — where the plan was last cut for a coin — is where the
  section-solve rung goes, and a rung fired there must take that coin.
- cfg `groupsretime`: the recording of the moving geometry is indexed by tick, and a route that
  skips a speed portal reaches the same x hundreds of ticks later. When a shallower replay's x
  parted from the deepest one's before it died, its recording replaces the deepest one's, so a
  coin route is not planned against geometry that has already moved.

All of these are on by default (with the Coins switch or without it; without coins most of them
have nothing to act on).

## 3. In the section search

- Each node carries the coins its path has touched (the player's box over the coin's live box); the
  bits are part of the dedupe key and of the cap's buckets.
- **At the head**, a coin counts as taken only if GD credited it, or the attempt touched it, *at or
  before the head tick*. The flight that goes on past the head before the checkpoint is restored
  touches coins too, and a checkpoint restore keeps the attempt's coin record.
- **Left behind** is judged while the player runs forward in gameplay frame 0 — the head's frame,
  with no gameplay rotation since the head — and only for a coin whose passing is final. A Camera
  Rotate turns the view, not the travel, and its angle is not wrapped: over official lv22's second
  coin it stands at 360.0 in frame 0.

## 4. What a coin needs first

Some coins cannot be had without something entered long before them. SubZero 4002's second coin
sits over ten platforms that a Toggle switches off at the start and a key switches on — at
x=13,647, 4,314 px before the coin. Ranked at its closest approach to the coin, an attempt without
the key is repaired around the coin, where nothing can help it.

### The census (level start, `src/solver/route.hpp`)

A coin's **prerequisite** is the root of a chain that switches a group ON, where an x-crossing
Toggle (or the level start) switched that group OFF at or before the coin — for the coin's own
groups, and for solids, platforms, orbs and pads within 300 px of it (hazards excluded: a hazard
switched on is a trap). A root is a touch box, a key/pickup that activates its group, or a toggle
block. The walk follows spawn chains backwards; a Count, Item Compare or Tap ends it (those are the
search's own gates, §1), as does an x-crossing trigger (every route passes it). The prerequisites of
a prerequisite's box are walked too, up to two more levels, so a key on a ledge whose step only
another key brings lists both keys.

The session log names each one: `route: coin 1 needs uid 6166 (id 1275) entered at (13647,985) ...
-- it switches on the gate object uid 9037`.

Each attempt records the tick each prerequisite's object came on (off → on), outside section
searches.

### Ranking and the search

An attempt that misses a coin and had not switched its prerequisite on by the time it came closest
to the coin is ranked at its **closest approach to the prerequisite's box** instead — the earliest
such box, if there are several. The deepest plan is re-ranked the same way, so the wall moves to the
box. While the wall is a box, every search is passed `--needtrig-uid <uid>`: the search has to enter
that box, and states that pass it without entering it are dropped. dp says so and drops the
requirement when the box is outside the call's window, already entered, or behind the anchor. The
coin wall (§2) is set aside while the wall is a box.

### When it starts

The loop's own repairs at the coin come first. A coin's prerequisites start to rank only when
(cfg `routeprereqafter`, default 10) the coin is still missing that many rounds after its miss was
filed, or when the ladder finds no anchor at all (it is the first thing `escalate` tries). With `0`
they rank from the filing on.

On SubZero 4002 the loop's own repairs got the second coin eight rounds after its filing; ranking at
the key from the start cost those runs their clock. On the chain rig below the loop found no anchor
at all past the second key and gave up — that is the case the fallback is for.

### Defaults

`routeprereq` is off. Only a coin session builds the census, and until a coin is engaged nothing
reads it: the loop and the solver's arguments are the old ones. With the rest of the coin set on
by default, SubZero 4001's coin-on run took the same 33 rounds with it on and off, and none of
the official 22 engaged it; the chain rig below is where it decides the outcome.

### How the bounds were chosen

Both were derived from the census re-run off the level dumps with the radius and the depth as
parameters, over the six official levels whose coins are switched (lv20, lv21, lv22, SubZero
4001–4003) and the two rigs:

- **Radius.** Every prerequisite that is real needs at most 200 px (route2's step under its second
  key; 176 for SubZero 4003's first coin, 166 for lv20's second). The first box not shown to be
  needed comes in at 392 (SubZero 4002, third coin). Any radius in [200, 392) gives the same census
  on all eight; 300 is in it.
- **Depth.** At that radius one extra level is all any of them needs (route2), and one, two or
  three levels give the same census on every one. The limit only keeps a pathological level from
  walking its whole trigger map.

### Rigs

`py/mklevel.py route1` and `route2` build two small, synthetic cube levels (not copies of any
published level):

- **route1**: a plain coin; a coin switched on by a touch box one jump before it; a coin over a
  platform that only a key 1,850 px earlier switches on.
- **route2**: a chain — the last coin's platform comes from key K2, and K2's ledge is reachable
  only from a step that key K1 switches on, 5,600 px before the coin.

At full horizon the search plans the detours itself; planned 1,200 ticks at a time
(cfg `dpstephorizon=1200`), with coins on:

| rig | off | on |
|---|---|---|
| route1 | 3/3, 7 rounds (122 s, 157 s) | 3/3, 6 rounds (26 s, 28 s) |
| route2 | gave up at 2/3 (twice) | 3/3, 6 rounds (32 s, 31 s) |

## Configuration

| cfg | default | what it does |
|---|---|---|
| `coinroute` | 0 | coins are part of the objective (dp `--coins`); the play menu's Coins switch |
| `coinmissmove` | 1 | the attempt cut spares a coin a Move ahead can still carry (§2) |
| `coinmisspost` | 1 | a refused clear is filed at the missed coin (§2) |
| `coinoverdepth` | 1 | a plan with a coin the deepest missed is progress |
| `coinapproachoff` | 1 | closest approach also over the ticks the coin was off |
| `dpseccoinrung` | 1 | section-solve rungs go to the coin wall and must take the coin |
| `groupsretime` | 1 | a shallower replay's moving-geometry recording replaces an out-of-phase one |
| `routeprereq` | 0 | rank a missed coin at its prerequisite's box (§4) |
| `routeprereqafter` | 10 | rounds after the filing before §4 engages (0 = at once) |

dp flags: `--coins`, `--needtrig-uid <uid>`. Activators, spawn roots and coin pick are on by
default (`--no-activators`, `--no-spawnroots`, `--no-coinpick` turn them off).
