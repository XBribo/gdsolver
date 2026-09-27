# Custom levels

Custom levels are **not supported yet**. Nothing in the loop is specific to the
official levels — level selection is the game's own, and the level model is built
out of whatever `PlayLayer` loaded — so any level can be picked, and many custom
levels solve as they are. What "not supported" means is that nothing keeps them
honest: the regression covers the official levels only, and the physics is
measured against what those levels, and the calibration rigs in `data/rigs`, use.

A level that leans on a mechanic the model has not been measured against is a
level the model is wrong about. Being wrong does not produce a wrong answer —
every plan is flown by the game, and only a plan the game clears is saved — but
it is expensive: the loop finds each disagreement by dying in a replay and
repairs one at a time, so an unmodelled level does not fail, it crawls.

The rule is to clear what the game lets the loop clear. Where the model has no
answer — a mechanic it lacks, a stretch it cannot represent — the section solver
drops it and searches the game itself ([SECTION_SOLVE.md](SECTION_SOLVE.md)), and a
plan the game clears is a solution however it was found. The difference is in the
cost: a level the model covers is fast and predictable; one that clears only with
the section solver's help is slower, and may take a different route each time.

This page says what that looks like in practice: which levels usually solve,
which gimmicks the model does not cover, and what a failure looks like. No level
is named; the levels measured are other people's work.

## Trying one

Open the level's own page in the game, press its play button and pick **Solve**,
as for an official level. Platformer levels get no menu (below). The safety rules
are the same as everywhere: while the mod drives, nothing is recorded.

## What the measurements say

Rated levels were drawn at random from the level server and solved cold with the
same loop as the official ones. They are grouped by the game version the level was
last saved in, which is the best available guess at which parts it uses:

| built with | parts it can contain | levels | cleared | the rest |
|---|---|--:|--:|---|
| 1.7 and before | cube, ship, ball, UFO; the early portals, pads and rings | 78 | 71 | 4 gave up at a wall, 2 were refused (too many gravity portals, since accepted), 1 not run to the end |
| 1.8 | slopes and dual added | 25 | 19 | 3 ran out of time, 2 gave up at a wall, 1 could not make a first plan |
| 1.9 – 2.2 | wave, robot, spider, swing; the 2.x triggers | 97 | — | not run to the end (below) |

The early levels solve, often at once: 32 of the 71 cleared on the first or second
plan. The 1.8 levels take several times as many rounds (a median of 8 against 2).
1.8 is where slopes and dual arrive, and in the same survey the model disagreed
with the game about ten times as often per minute of play on a 1.8 level as on an
earlier one.

The newer levels were only run until the first disagreement, to catalogue where
the model is wrong rather than to clear them. Almost every one of them had one
(81 of 97; 1 cleared without any), and 13 could not make a first plan at all. So
for a 2.x level the honest expectation is "it will need repairs, and it may not
get through"; how often it does has not been measured.

*When and with what:* the surveys ran from 2026-09-22 to 2026-09-25, on builds
from the week before v0.3.0. The section solver running on its own (it searches
the game itself where the repairs are not getting across a wall) and several of
the fixes in v0.3.0 came after, so with this release the numbers may well be
different. They have not been measured again yet.

## Levels that usually solve

* Built from **1.7-era parts**: cube, ship, ball and UFO, the early portals
  (gravity, size, speed, mirror, mode), yellow/pink/blue pads and rings, blocks,
  spikes, saws and moving geometry driven by the plain move, rotate and toggle
  triggers.
* **Not too long and not too heavy.** Every round replays the level from the
  start, and the model is built from every object; a few thousand objects is
  typical of the levels above.
* Where the model has been measured most: the **cube** disagreed with the game
  least often; the ship, the ball and especially the mini UFO more, and over half
  of the disagreements found were in mid-air or next to a ring.

## Gimmicks the model does not cover

### Refused

* **Platformer mode.** Both searches — the model's and the section solver's — rest
  on there being no steering input, one bit per tick, so a platformer level is
  outside the formulation. The mod refuses one (see the README's *What's next*).
* **More than 128 gravity portals**, in a level that reverses the player or puts
  them close together. Past 128 the portals share the model's slots, which works
  only when each is behind the player for good before the next one needs it; a
  level where that does not hold is refused, and the log says why
  (`unsupported: gravity portals ...`).

### Not in the model: left to the section solver

The model cannot plan through these, so the loop can get past them only where the
section solver finds a way through the game itself — slowly, and not always.

* **The gravity trigger (2.2).** A level that changes the strength of gravity is
  flown by the game with the new value and planned by the model with the old one.
  Three of the 13 2.x levels that could not make a first plan use it.
* **Randomness the model cannot box in.** An Area Move with a random variance is
  planned against the whole box it can land in. Area Rotate and Area Scale with a
  variance, and an Advanced Follow's, are not covered yet; the session's
  `areaenv:` line counts them. While the bot drives, the game's random seeds are
  set to the same values before every attempt, so each attempt meets the same draw
  and a solution is one for that draw; the play menu says when a level has a
  Random or Advanced Random trigger, or an area or enter effect with a random
  variance (a spawn delay's or an Advanced Follow's variance is not detected yet).
  The section solver's checkpoint restores start it from the attempt's
  first values too, so a stretch it solves after a Random trigger can still fail
  on the replay from the start, which then refuses it.

### In the model, but measured less

* **Slopes**, above all where they meet other slopes, blocks or the ceiling: the
  1.8 row above.
* **Mechanics no official level uses.** The model is measured one mechanic at a
  time against the official levels and the rigs; where a mechanic-and-mode
  combination has never been measured, the code carries a fallback that says so
  in a comment. The 2.x triggers are where most of these live.
* **Dual sections** beyond the few the official levels use: a dual ball in one
  custom level is known to disagree with the game.

### Too much for the model's memory

The model's state is small on purpose. A rule that needs one bit per object, for a
kind of object a level places by the hundred — a pad the game fires only once per
attempt, repeated hundreds of times; blocks that break and must stay broken — cannot
be carried. Such a stretch is left to the section solver, which uses the game's own
physics and gets through where the model cannot follow.

### Very heavy levels

Levels with tens of thousands of objects (the 2.x sample's median was about 26,000,
the largest over 150,000) are slow: the game's own update, the recording of the
moving geometry and the model all grow with the object count. A level with at least
10,000 objects its run cannot depend on is solved on a copy without them and
verified on the level itself ([LEVEL_SLICE.md](LEVEL_SLICE.md)), which helps; a
level whose gameplay objects are themselves that many is still slow.

## What a clear and a failure look like

A clear is a plan the game flew from start to finish, whether the model planned
every stretch of it or the section solver searched some. The log says which: each
stretch the section solver found is a `secrung: spliced ...` line.

* **Refused** — the session ends at once and says what it cannot handle.
* **No first plan** (`dpsolve_stuck` at the start) — the model finds no way through
  from the very beginning, often because of a mechanic it does not have.
* **Gives up at a wall** — the loop keeps dying at one place and runs out of things
  to try there. The iteration map (`F10`, [ITERMAP.md](ITERMAP.md)) shows where the
  rounds went, and whether the model or the plan was wrong there.
* **Runs out of time** — in the surveys, still making progress when the level's
  clock ran out; a crawl.

In every case nothing wrong is saved: a solution is only ever a plan the game
itself cleared.

## Coins

The coin routing is measured on the official levels: the pickup box, and the
counters, keys and gates their coins wait for. A custom level's coins can hang on
triggers the official ones never use, and the search holds at most eight coins — a
level with more turns the routing off and says so. See [COINS.md](COINS.md).

## What "supported" will take

"Supported" will mean the model covers a level's mechanics, so a solve is fast
and predictable and the regression keeps it that way. Getting there means picking
that subset, saying plainly which objects and modes are in it, and measuring the
ones that are not yet. The early-era levels above are the natural first subset:
they already clear nine times in ten, and what remains there is being worked on
one disagreement at a time.

Outside the subset the loop still tries, and a level clears if the game lets it.
What is planned is to say so up front and afterwards: a warning in the play menu
when a level uses something the model does not have, and a clear reported as
either the model's alone or one with stretches the section solver found. (The
menu already says when a level has a Random trigger or an effect with a random
variance.)
