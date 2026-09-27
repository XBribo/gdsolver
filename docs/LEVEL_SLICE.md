# Level slice

A heavy level is solved on a copy of itself that keeps only the objects the
player's run can depend on, and the plan that clears the copy is verified on
the level itself before anything is filed. The copy is one more approximation
in front of the game, like the model: a wrong cut costs rounds, never a wrong
answer, because a solution is only ever the plan the original level cleared.

Code: `src/solver/slice.hpp` (the rule and the cut string) and
`src/mod/level_slice.hpp` (the session: when to slice, the swaps, the
verification and the add-back).

## Why

What a solve costs grows with the level's object count: the game's own update
(moving decorations along with the gameplay they share nothing with), the
recording of the moving geometry, the input the model is built from, and the
section search's restore of the whole level. On a heavy custom level most of
those objects are decorations nothing in the run reads.

## Which objects are kept

Every trigger, area trigger, keyframe point and every object the game does not
class as a decoration is kept. A decoration is kept only when something reads
its group as a **position**:

- a trigger's centre group or its move-to-target reference, and for a
  move-to-target or dynamic move the moved group itself (its reference point can
  be any member) — counted only for a trigger acting on a relevant group;
- an area trigger's target group (the effect is timed off the group as a whole);
- a teleport's destination;
- a camera trigger's target (edge, static, guide), read from the level string as
  well, since the static camera keeps its centre group in a key of its own; in
  free mode the camera sets the flight band;
- the level's spawn group (the player appears at its object's height).

A decoration is also kept when it is a group parent or when it sets the level's
extent. Sharing a group with a gameplay object is not a reason to keep it: a
Move, Rotate, Scale, Alpha, Toggle or Area effect applies to each member on its
own. "Relevant" groups start as every group a trigger or a gameplay object is in
and grow, to a fixed point, by the positions read by triggers acting on them.

Types, groups and trigger fields are read from the objects the game built when
it loaded the level; the cut is made in the level string by index, so every kept
object keeps its bytes and its order. An id the loaded level has no object for,
or built with more than one type, is kept.

Before the port, the rule was checked on the level itself against the cut copy:
the player's state agreed on every tick of known solutions of the four heaviest
official levels and along a 24,733-tick route of a heavy custom level, and a
plan solved on the cut copy of lv22 cleared the level itself. Each fix to
the rule came from a column that disagreed there (the spawn group, the camera
targets, the area-trigger targets).

## When a level is sliced

When the cut removes at least **10,000** objects (`slicemin`), and the run is
not one that has to take coins the copy may not credit (below).

The number is where the copy pays for itself. What an object costs per round
of the loop was measured on a heavy custom level solved twice from the same
build, once on the level (109,663 objects) and once on its cut (4,827). The two
runs made identical decisions for 44 rounds (their `[fp]` lines equal line for
line), so they did the same work and only its price differs:

| | the level | the cut |
|---|---:|---:|
| 39 solver calls | 175 s | 75 s |
| 221,738 attempt ticks | 17.1 s | 10.2 s |

That is about 24 µs per removed object per round: the level the model reads is
built from every object, and the game moves every object a trigger moves. The
section search's restores, which also follow the object count, come on top and
are not counted. What the copy costs is paid once: the cut (tens of
milliseconds; 162 ms on the level above), a second load of the level, and one
flight of the plan on it. On lv22 everything after the last solve returned
— the copy's clearing flight, the swap back, the flight on the level and the
session's end — took under 10 s. A cut of 10,000 objects saves about 0.24 s a
round and has repaid that within some twenty rounds; a solve shorter than that
is a short solve, and the most it loses is those few seconds.

Where the official and spin-off levels stand (the cut's census, `slicecount`):

| levels | objects | removable | sliced |
|---|---:|---:|---|
| lv1–18 | 1,444–14,400 | 121–9,630 | no |
| lv19 | 16,131 | 11,571 | yes |
| lv20 | 19,494 | 13,179 | yes |
| lv21 | 27,283 | 22,506 | yes |
| lv22 | 18,329 | 11,788 | yes |
| Meltdown 1001, 1002 | 9,694 / 10,746 | 7,107 / 7,766 | no |
| Meltdown 1003 | 15,477 | 11,585 | yes |
| World 2001–2010, 3001 | 839–3,876 | 243–2,747 | no |
| SubZero 4001, 4002, 4003 | 20,576 / 18,317 / 15,269 | 15,649 / 11,915 / 11,616 | yes |

### Coins

A run that has to take the level's coins is not sliced when the level has
coins the copy may not credit. The copy is an editor-type level, and the game
credits no secret coin (id 142) in one: lv22 with coins on cleared its copy
103 times, each with 0 of 3 coins by the game's own count, where the level
itself credits all three (the game builds no secret coin at all in an
editor-type level). Whether a user coin (1329) is credited in the copy has not
been measured, so it is treated the same way until it is. Every official and
spin-off level with coins has secret coins, so no coin run of them is sliced.

## The flow

1. The session's first solve cuts the level. Below the threshold nothing
   changes. Above it, the level is swapped for an editor-type copy with the
   original's id, song and version (the game turns a modified main level away
   as soon as it starts), and the solve starts there as on any level. The copy
   lives in memory only: it is never added to the player's created levels. The swap
   waits for any scene transition to finish: a level entered with a fade starts
   its first solve while the fade still runs, and the fade's end would put the
   original's scene back over the copy.
2. The plan that clears the copy is not filed. The level itself is swapped back
   in and the plan is flown on it. The copy's clear is not held to the
   "short of the goal" gate either, because the copy's progress percentage is
   not the level's (lv22's copy read 90.73% where the level read 96.44% at
   the same x and tick); the gate judges the flight on the level.
3. A clear there is the ordinary clear of a solve: filed and shown under the
   level's own id.
4. A death there means the cut was wrong somewhere. The attempt is compared tick
   by tick with the copy's clearing attempt — the player's position, vertical
   velocity and mode; not the flight band, which the game carries from one
   attempt to the next, so the first attempt on a layer just swapped in starts
   with another band whatever the cut — and the objects dropped within 1,500 px
   of where the two first parted go back into the copy, and the solve goes on
   there from the same plan. After `sliceaddbacks` add-backs, or when the
   parting did not move further on, the solve goes on on the level itself.
5. A copy that is wrong only at a wall would never clear to be verified. So when
   the copy has gone eight rounds without getting deeper, its deepest plan is
   flown on the level itself, once per wall, and compared with the copy's
   attempt of the same plan. Where the two part, or where the level gets past
   the copy's death, the objects around it go back as in step 4. A death on the
   same tick, the same way, means the wall is the level's own, and the solve goes
   back to the copy where it was: the check is a flight, not a new run, and the
   copy is rebuilt from the same string, so its objects keep their uids and the
   loop's recordings, anchors and plans stay valid.
6. A solve that gives up on the copy does not end there: giving up is the copy's
   verdict, not the level's. It goes on on the level itself from the copy's
   deepest plan.

## What carries across a swap

A swap is one run, not a new one: choosing a level, solving it and playing the
solution back are one frame, and a plan is not thrown away because some objects
came back. The plan, the fixups (keyed by the player's state, not by any
object), the phantom vetoes, the coin margin and the round count carry over.
What is keyed by an object's uid does not — the recordings of the moving
geometry, the anchor rows, the pads and rings an attempt spent — because the
copy numbers its objects differently; they are recorded again on the new level.
The deepest plan is not carried as the deepest: its depth was measured on the
level the run left, so the carried plan is flown first and measured again.

The level record the session reports on (`level record changed:`) is always the
one of the level the session was started on.

## Configuration

| key | default | meaning |
|---|---|---|
| `slice` | 1 | 0 = never slice |
| `slicemin` | 10000 | objects the cut must remove for the copy to be used |
| `sliceaddbacks` | 2 | add-backs before the solve moves to the level itself |
| `slicecount` | 0 | 1 = print the cut's census line and end the session (no solve) |
| `slicenoposition` | 0 | 1 = drop the decorations read as positions too: a cut that is wrong on purpose, to exercise the verification and the add-back |

## What the log says

`slice:` lines: the cut (objects, how many the run cannot depend on, per rule),
the copy's size, the copy's clear, the verification's outcome with where the
level parted from the copy, each wall check and add-back, and at the session's
end where it finished (`slice: ended ...`, with the counts of verifications,
wall checks and add-backs).

## Limits

- A wall is checked on the level itself only once the copy has stalled at it
  for eight rounds, so a wrong cut costs those rounds before it is found.
- An object whose id the game rewrites when it loads the level has no loaded
  object of the same id to be classed by, and is kept.
- The add-back puts back a stretch of level around where the runs parted. A cut
  that is wrong in a way that is not local (an effect whose per-object random
  index shifts when any object is removed) fails that way every time, and the
  solve then moves to the level itself.

