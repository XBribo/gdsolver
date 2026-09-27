# v0.3.1

 * **The release workflow builds again.** v0.3.0 turned the touch mask into a
   fixed-width bit set, and one line still initialised it from `0`. The compiler
   used for desk builds (MSVC) accepted that; the one the release workflow uses
   (clang) does not, so the workflow failed, and the package on the v0.3.0
   release is a desk build rather than one the workflow made from the public
   tree. The line is now standard C++, and the desk build of it is the same
   machine code as v0.3.0's apart from its build timestamp: nothing the solver
   does changes, and the v0.3.0 results stand. This release's package is built
   by the workflow from this commit.

# v0.3.0

 * **The spin-off levels.** Beyond the 22 main levels, the 17 official levels of
   Meltdown, World and SubZero (with The Challenge) are now part of what a release
   claims: all 17 clear cold, and the six that have coins — Meltdown's three and
   SubZero's three — clear with every coin, each counted by the game itself. Their
   results are in the README. The suite reads them from level files (cfg
   `leveldir`), which are not distributed here.
 * **The section solver is part of the loop.** When the loop has spent at a wall
   as much as a section solve costs, or has run out of anchors, it starts one
   itself: the level is replayed to a practice checkpoint before the wall, the
   game is searched directly from there, and a plan the game reproduces is
   spliced back and pinned, and the loop carries on. A section whose prefix does
   not replay the same way twice is given up after two tries. A restore no longer
   gets slower as a search goes on, and a search's memory follows its live
   frontier. See [docs/SECTION_SOLVE.md](docs/SECTION_SOLVE.md).
 * **Coin search.** A clear refused for a missing coin is filed at the attempt's
   closest approach to that coin rather than at the finish, and ranked there
   until a route passes it; a plan that takes a coin the deepest plan missed
   counts as progress; section solves go to the coin wall and must take the coin;
   and a replay that skipped a speed portal is no longer planned against another
   route's moving geometry. The loop also works out what a coin needs first — the
   key, touch box or switch whose chain turns it on — and can route through it
   (cfg `routeprereq`, off by default). See [docs/COINS.md](docs/COINS.md).
 * **Level slicing.** A level with at least 10,000 objects its run cannot depend
   on is solved on a copy without them, and every plan that clears the copy is
   flown on the level itself before it is filed; where the two part, the objects
   around that point go back into the copy. Coin runs are not sliced (the copy
   credits no secret coin). See [docs/LEVEL_SLICE.md](docs/LEVEL_SLICE.md).
 * **The play menu.** A level's play button opens a menu — Normal, Replay, Solve
   and a Coins switch — in place of the panel. It opens on Replay when a solution
   is stored (with Coins set when the coin solution is) and on Solve otherwise.
 * **Leaving a level stops its solve.** A solve left mid-search used to run on in
   the background, and the next level's solve waited behind it while the overlay
   showed the old search's progress. The section solver's settings no longer
   carry from one level to the next either.
 * **Measured model corrections**, among them: a grown body standing on two or
   more supports is lifted on the grow tick; an upside-down cube keeps a moment of
   ground walking off a ledge where the level keeps the old gravity behaviour, as
   the game does; pads and rings a run has already fired are not fired again after
   a re-anchor; the id-1859 arm is read where it rides the player; and an
   autonomous Move whose group is switched off when the player reaches it does not
   fire.
 * **Custom levels** have a page of their own
   ([docs/CUSTOM_LEVELS.md](docs/CUSTOM_LEVELS.md)): what random samples of rated
   levels showed by the version they were built with, the kinds of level that
   usually solve, and the gimmicks the model does not cover. The rule is to clear
   what the game lets the loop clear; they are still not a supported set.
 * **Regression and release tooling.** A cold run's manifest now records every
   cfg value the mod held and the core's built-in defaults, and
   `cold_manifest.py` compares two runs (refusing any difference not named) and
   checks a run against the expected settings before it is blessed. New
   calibration tests pin the grow lift and the upside-down ledge against the
   game's own rows.

# v0.2.0

 * **Coins.** A **Coins** switch on the panel makes the goal the end of the level
   *and* every coin in it (cfg `coinroute`, dp `--coins`). The collected set is
   part of the search state, a coin counts when the player's own box overlaps it
   where its group has put it, and a state that leaves one behind is dead; the
   item counters that gate a coin (Fingerdash's and Dash's third) are modelled,
   and a replay that passes a coin the game did not credit is ended there and
   repaired. All 22 official levels clear cold with 3/3 coins, and those
   solutions are tracked as `data/solution_lv<N>_coins.txt` — each replays to a
   clear with the game's own count at 3/3. Coins collected while the mod drives
   are never awarded.
 * **The seam between recordings.** Past the tick where the last replay died,
   the model plans against a recording of the level played with no input. That
   recording reaches each trigger a little later than a real run, so an object
   still moving at the seam is now joined to it at the shift where the two agree
   (Fingerdash's rotating bars had jumped eight ticks back there); and the tick
   the game killed the player on is now held one row, since the model decides
   that kill on its next row (Deadlocked's spikes follow the player, and in the
   input-free recording they were elsewhere and switched off). Deaths that had
   repeated round after round in both places are gone.
 * **Moving geometry is dated more faithfully** — an object moved by both a
   touch box and an autonomous trigger dates each from its own motion, a touch
   box the attempt entered is dated from its entry tick, and an anchor in a
   rotated section windows the touch boxes by world x — along with many smaller
   measured corrections to the model since v0.1.6. The repair counts moved on
   ten levels as a result (see the table in the README).
 * **Removed switches.** Switches whose off arm had become dead code are gone,
   and passing one is refused by name instead of being ignored: dp exits with
   code 2, and a session whose `autorun.cfg` names a removed key stops before it
   solves.
 * **Regression.** Coin runs have their own baseline
   (`data/cold_baseline_coins.json`, with each level's coin count). Iteration
   counts are reported and no longer fail a run. A run records the game
   profile's resolution and refuses to compare with a baseline measured at
   another one: GD's saw radius follows that setting (one of Deadlocked's saws
   reaches 21.87 px at index 25 and 21.96 at index 8), and the published results
   and solutions were measured at index 25. A baseline can be adopted from a
   reviewed one-session run (`--adopt`) instead of being blessed in the same run.

# v0.1.6

 * **Nothing the game's own random numbers decide is planned against.** An Area
   Move can give its distance, length, offset, angle or x/y moves a variance, and
   the game takes the value from two random seeds it never resets. Such a block
   sits somewhere else on every attempt, after every level played before it in
   the same game, and on every machine that replays the solution: on Dash, 13
   blocks landed up to 278 px apart, and solving Fingerdash first moved them by
   up to 2 px, so the suite in one session and a game per level were not solving
   quite the same level. The mod now records, for such an object, the box it can
   be anywhere in — the game's own arithmetic, read from the binary and checked
   against the game on every object it moves (the `areaenv:` line) — and the
   model treats the whole box as deadly while the object is solid. Dash clears in
   46 repairs instead of 39; the other 21 levels make the same decisions as
   before. cfg `areaenv=0` records the rects as before.
# v0.1.5

 * **Each repair costs less.** The whole suite in one game session went from 30
   minutes to 19, and Dash from twelve minutes to under four. Two changes to the
   loop, both on by default and both switchable off for a diagnostic run:
   * *The plan length follows the game's last verdict.* While replays keep dying
     within 3,000 ticks of their anchor the loop plans one such step at a time and
     lets the game check it; once a plan outlives its step, or a wall stops the run
     twice, it plans the rest of the level (`dpstephorizon`, `dpadaptivehorizon`).
     A round that flies exactly the previous round's plan and dies on the same
     tick closes that spot at once instead of on the fourth try: the search is
     deterministic, so the repeat is not new evidence (`dpfastveto`).
   * *A repair rejoins the plan that died.* The next search is handed the model's
     trace of that plan, and when a state that did not go through the death comes
     back onto it, the search stops there and the old plan's inputs are kept from
     that point on (`dprejoinwatch`, `dprejoinuse`, `dprejoinchain`). The game
     still flies all of it. On Hexagon Force and Dash, 37-45 % of the search's
     layers had been re-deriving what came after such a point. Its one measured loss is
     Deadlocked, where the old plan's continuation died a few dozen ticks past the
     join, in ground the game had never flown.
 * **Checkpoint flights** (cfg `dpcheck`, off): while a search runs, the game can
   fly the search's checkpoints and cancel it on a death, deterministically. Two
   faults on levels whose first search reaches the goal are fixed -- a completion
   left over from a held flight read as a death at tick 0, and a flight that
   cleared the level left the next level's start waiting forever. It stays off:
   with the new plan length it made the suite slower, not faster.
 * A solved branch that is being followed gets its grace per wall, not per round:
   two tails alternating between a shallow and a deep death reset it forever.
 * The search bar's denominator is the layer the search will actually stop at.
 * Workers run a frozen copy of the game build, checked at every launch, so a
   Steam update cannot change the game under them unnoticed.
 * The iteration map draws every round's tail to its end, the clearing round
   included.
# v0.1.4

 * **Slopes are acquired the way the game acquires them.** There was never a
   contact point: the game probes a pixel either side, tests the rect one pixel
   in, and seats the body on the line extended past the ramp's end. The model now
   does the same, and the ramp kills follow their measured outlines -- a spiked
   ramp kills at a perpendicular distance from the line, not by box overlap, for
   the ship and for a flipped wave as well as the upright wave.
 * **A spawned trigger acts on the group its remap names.** A Spawn can send the
   moves it starts to a different group, and the model used to move the group the
   triggers were written for. On Dash that left a sinking platform under no
   trigger at all, playing to the clock of whichever run recorded it.
 * **A touched platform moves when this run touches it**, not when the run that
   recorded it did -- except where something else also moves it.
 * **A flying band ends where its recording ends.** The band the camera held
   was kept for every tick after the last one recorded, so a mode portal further
   on could not change it: the solver planned against a floor or a ceiling the
   game no longer had, and the plan died in the replay.
 * A swing re-anchored on the tick its press takes effect keeps the flip that
   press started, and a robot re-anchored in mid-air no longer gets a full hover
   it does not have.
 * Every one of these was measured first (calibration levels in `data/rigs`, the
   game's own traces, and the disassembly) and each can still be turned off:
   `--no-slopelaw`, `--no-spawnremap`, `--no-touchretime`, `--no-shipslopekill`,
   `--no-waveflipkill`, and `dpswingpending=0`, `dphoverstrict=0`, `dpbandend=0`.
 * The death counter counts deaths rather than calls to the function that kills,
   and every death names the object that killed and which body died.

# v0.1.3

 * **Solving a second level without restarting the game works.** One level's
   recording of its moving geometry was still being offered to the next one, so
   a level that has no moving parts of its own was planned in the previous
   level's world -- solve Stereo Madness after Clubstep and the search died a
   fifth of the way in, on a level it clears in eight rounds. The recorder now
   publishes "nothing was recorded" as an answer instead of leaving the previous
   answer standing, and a solve deletes the file it would have read.
 * **The music follows the speed again after the screen comes back.** Raising the
   spectating notch and then turning rendering back on left the song playing at
   1x for the rest of the session: resuming rendering restarts the level, the
   game starts the song again unpaused, and the mod had latched on what it last
   asked for rather than on what the song was actually doing.
 * A platformer level is refused rather than answered. It is outside the
   formulation the search rests on -- there is steering, so "the input at tick t"
   does not describe the run -- and it used to be solved anyway, badly and
   silently.
 * The force block is modelled from its own settings rather than from a table of
   the object ids that happened to be measured, and its push is read off the real
   object. Calibration levels for it are in `data/rigs`, one per game mode.
 * The cold regression can solve every level in one game (`--one-session`), and
   blessing a baseline now does. Under one launch per level nothing about a
   second level is ever exercised, which is why the leak above went unseen.

# v0.1.2

 * **The keys can be rebound.** All thirteen are Geode keybind settings now,
   with the keys they already had as defaults, and the on-screen legend reads
   the bindings rather than naming keys it can no longer be sure of. They were
   read straight from the OS before, which meant hardcoded keys and a keystroke
   meant for another window could reach the mod.
 * **A bad number in a config or command file no longer takes the game down.**
   Every setting was parsed with a function that raises on malformed input and
   nothing caught it, so one bad value killed the game at launch or mid-frame.
   A value that will not parse is now left alone and reported.
 * The iteration map keeps the paths each round flew, and is filed however the
   session ended -- leaving a level mid-solve used to discard the whole record.
 * The collision trace can see the second body in a dual, which is the half
   every dual question is actually about.
 * The panel rides Geode's own overlay node instead of following scene changes
   by hand.
 * Releases are built by CI from the public source rather than on a desk.

# v0.1.1

 * **The dual's second body.** The loop was walled on Deadlocked where the game
   kills the second body inside a corridor a mode portal opens. Five holes fed
   it, each measured against the game rather than reasoned from the level: the
   body was born at its resting position instead of the pre-collision one,
   portals fired against the same wrong y, a mode portal did not end a ceiling
   press the way it already ended a slope ride, a ceiling release fired a tick
   early, and the two halves shared one gamemode and one ceiling-press counter
   although each is tested at its own height.
 * **The iteration map (`F10`).** Draws what the rounds cost and where: each
   round's death, the fixups, the vetoes, the re-anchors, and the path each
   round flew. It is there to tell a wall the model gets wrong from a wall with
   nothing through it.
 * **A seek bar**, always on in a replay — click, drag, or arrow keys, stepping
   frames while the game is stopped.
 * **Which plan comes out.** Among the states that reach the end, the one
   emitted is now the one whose route kept the most vertical room, rather than
   whichever was enumerated first.
 * A moving circular hazard takes the same collision branch as a static one.
 * All 22 official levels still solve cold, and the suite now needs **235
   repair rounds instead of 322**. Largest moves: Hexagon Force 102 to 55,
   Blast Processing 5 to 1, Clutterfunk 3 to 1.

# v0.1.0

 * Initial release. All 22 official levels solved cold.
