# Results

The tables are cold regression runs of the v0.4.0 build (2026-10-02; the two
changes after it — the overlay's layout and the play menu's warning — do not touch
solving) — `python py/cold_regress.py --one-session`, the whole suite inside a
single game session, no plan and no solution file to start from — one with coins
(`--cfg coinroute=1 coins=1`) and one for the end of the level alone. The coin
runs ended every level with every coin, as the game itself counted them. The
repairs are those of the runs the regression baseline was taken from; the times
are those of another one-session run of the same build, made earlier the same day,
whose repairs were the same on every level.

Every cell reads **with every coin**, then in brackets **the end of the level
alone**. A repair cell splits into **divergence + search** (see below).

| # | Level | Repairs | Time | | # | Level | Repairs | Time |
|--:|---|--:|--:|---|--:|---|--:|--:|
| 1 | Stereo Madness | 0 (0) | 7 s (5 s) | | 12 | Theory of Everything | 0 (0) | 6 s (6 s) |
| 2 | Back On Track | 0 (0) | 4 s (4 s) | | 13 | Electroman Adventures | 0 (0) | 5 s (5 s) |
| 3 | Polargeist | 0 (0) | 5 s (4 s) | | 14 | Clubstep | 0 (0) | 6 s (14 s) |
| 4 | Dry Out | 0 (0) | 4 s (4 s) | | 15 | Electrodynamix | 0 (0) | 6 s (6 s) |
| 5 | Base After Base | 0 (0) | 4 s (4 s) | | 16 | Hexagon Force | 0+1 (0+1) | 40 s (41 s) |
| 6 | Can't Let Go | 0 (0) | 4 s (4 s) | | 17 | Blast Processing | 0+2 (1+1) | 11 s (9 s) |
| 7 | Jumper | 0 (0) | 6 s (5 s) | | 18 | Theory of Everything 2 | 0+4 (0+4) | 16 s (13 s) |
| 8 | Time Machine | 0 (0) | 6 s (5 s) | | 19 | Geometrical Dominator | 0+1 (0+8) | 1 m 10 s (3 m 1 s) |
| 9 | Cycles | 0 (0) | 8 s (5 s) | | 20 | Deadlocked | 0+21 (0+8) | 4 m 31 s (48 s) |
| 10 | xStep | 0 (0) | 7 s (5 s) | | 21 | Fingerdash | 4+10 (1+7) | 3 m 57 s (2 m 11 s) |
| 11 | Clutterfunk | 0 (0) | 10 s (9 s) | | 22 | Dash | 7+20 (7+16) | 5 m 24 s (4 m 32 s) |

The two numbers in a cell are two separate searches, not a total and a part:
with coins the goal is a different one, so the search takes a different route
from the first round on, and it is not always the longer one.

Against the one-session runs made before v0.3.2's release (v0.3.2 kept the
v0.3.0 baseline, so these are its only full runs), the 22 levels went from 98 to
54 repairs and 1,215 to 780 s without coins, and from 104 to 70 repairs and
2,065 to 1,057 s with them. The large moves: Hexagon Force 15 → 1 without coins;
Fingerdash 23 → 8 and Deadlocked 14 → 8 without coins; Dash 30 → 23 without coins
and 41 → 27 with them, where its time fell from 19 m 48 s to 5 m 24 s; and
Clutterfunk 5 → 0 with coins. Against the run of the trend, Geometrical Dominator
went 5 → 8 without coins and Fingerdash 12 → 14 with them. The
[changelog](../changelog.md) has what changed.

### The spin-off levels

The official levels of Meltdown, World and SubZero, and The Challenge, run the
same way, one game session per set. World's levels and The Challenge have no
coins, so they have one number. These levels are not in the main game's level
list; the suite reads them from level files (cfg `leveldir=<dir>`, one
`<id>.lvl` per level), which are not distributed here.

| Game | # | Level | Repairs | Time |
|---|--:|---|--:|--:|
| Meltdown | 1001 | The Seven Seas | 0+1 (2+4) | 11 s (21 s) |
| Meltdown | 1002 | Viking Arena | 0+2 (0+1) | 16 s (12 s) |
| Meltdown | 1003 | Airborne Robots | 0+1 (0+1) | 12 s (14 s) |
| World | 2001 | Payload | 0 | 3 s |
| World | 2002 | Beast Mode | 0 | 4 s |
| World | 2003 | Machina | 0 | 4 s |
| World | 2004 | Years | 0 | 5 s |
| World | 2005 | Frontlines | 0 | 5 s |
| World | 2006 | Space Pirates | 0 | 8 s |
| World | 2007 | Striker | 0 | 4 s |
| World | 2008 | Embers | 0+3 | 4 s |
| World | 2009 | Round 1 | 0 | 4 s |
| World | 2010 | Monster Dance Off | 0 | 5 s |
| — | 3001 | The Challenge | 4+4 | 25 s |
| SubZero | 4001 | Press Start | 6+2 (4+6) | 2 m 4 s (56 s) |
| SubZero | 4002 | Nock Em | 19+44 (6+14) | 8 m 28 s (1 m 52 s) |
| SubZero | 4003 | Power Trip | 28+9 (25+6) | 2 m 0 s (1 m 59 s) |

Against the same pre-release runs of v0.3.2, the 17 levels without coins went
from 116 to 80 repairs and 732 to 408 s, and the six with coins from 204 to 112
repairs and 3,396 to 792 s: Press Start 28 → 10 without coins and 33 → 8 with
them, and Power Trip 104 → 37 with coins. Nock Em with coins went the other way,
57 → 63.

**Repairs** is how many times the loop had to go back: solve, replay, die,
re-anchor on the game's real state, solve the tail. `0` means the very first
plan the DP produced cleared the level. Each one is split by what the loop's
recorder found when it replayed the dead plan in the model against what the game
did:

* **divergence** — the model and the game had come apart before the death: the
  first difference the recorder found in position or velocity is over 0.01. The
  model is wrong there, and the repair is the game correcting it.
* **search** — the two agreed up to the death. The model, given what the game
  showed, dies there too; the plan was wrong, not the model — typically a route
  that only looked open because the moving geometry past the last replay was not
  yet recorded, or a plan that dies in the model's own walk of it.

On the official levels 45 of the 54 repairs without coins and 59 of the 70 with
them are search; on the spin-off levels it is about half (39 of 80, and 59 of 112
with coins). For a given build the count is normally reproducible:
`py/cold_regress.py` prints each level's count against `data/cold_baseline.json`
(`data/cold_baseline_coins.json` for a coin run), and a change is reported, not
failed. The `[fp]` line each round prints is compared with the baseline run's
separately when a release is accepted. The spin-off levels have baselines of their own,
kept with their level files.

It is not a fidelity score. It counts what the loop had to do, and that depends
on which corridor the search happens to walk as much as on where the model is
wrong — the search is deepest-first, so touching one rule reorders the frontier
and the run takes a different route. Making the model *more* correct can raise
it: closing a route the model only believed in sends the search off to find the
real one.

**Time** comes from the runs above (for the 22, the earlier ones) — 8 solver
threads on a 16-core desktop — and it counts everything from the level being built
to the solution being written (the game's own start-up is not in it); the 22 add up
to 18 minutes with coins and 13 without. The four suites ran side by side on the
machine, with two more solves beside them for their first two minutes (Bloodbath
and Amethyst, in the README), so read the clock as a guide and not as a contract:
it is not what the regression compares, and a level's time is read from outside the
game about once a second.

Almost all of it is the search. Broken down on the 2026-08-27 run, before the
section solver joined the loop, the DP calls were 86 % of the total and 88–93 % on
the four expensive levels; on the ones that finish inside half a minute, most of
what is left is starting the game and loading the level. On a level where section solves run (Dash
with coins, SubZero) a good part of the time is now spent searching inside the
game instead; how large a part has not been measured.
