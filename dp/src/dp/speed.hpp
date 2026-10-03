#pragma once
#include "dp/prelude.hpp"

namespace dp {

// Tolerance for the "gap between the foot and the surface" within which the player
// still counts as standing (the static-surface part; a moving surface adds + |dcy|
// -- see the note at the support test).
//
// **0.6 is far too wide.** GD's grounding IS the collision test (do the boxes
// overlap?), so if the surface is below the foot the player falls unless touching.
// Measured (lv16 t=15,686, where it moves onto the thin floor uid7859 whose top is
// 419.95 at x=24,015):
//   GD     y 435.000 -> 434.952 (one tick of gravity) -> lands at 434.950
//   model  y 435.000 unchanged (the 0.05px gap fell inside 0.6)
// and it runs 0.05px too high from then on. --supporttol A/Bs it.
inline double kSupportTol = 0.01;
constexpr double kDx = 1.29825;
// ...but GD does NOT step by the float32 nearest to 1.29825. Read straight out
// of the dump's first increment (x(1) = 0, x(2) = 1.29825044) on lv1/3/7/10/11
// and lv12 alike, GD's per-tick advance is the float32 1.2982504367828369 --
// four ULPs above (float)1.29825 = 1.2982499599456787.
// Four ULPs sounds like nothing; it is not. The error accumulates over the
// level and lands on contact boundaries: lv12 t=3340 has the cube leaving a
// ledge whose edge + half is exactly 4335.0000, GD sits at 4335.0005 (gone) and
// the model at 4335.0000 (still supported, the test being inclusive), so the
// model fell one tick late and stayed a tick behind for the rest of the level.
// Padding the contact tests to absorb this was tried and broke five levels at
// once (see kContactEps); reproducing GD's constant is the actual fix.
constexpr float kDxF = 1.29825044f;
// Speed portals (GameObjectType 20, object ids 200/201/202/203/1334). lv1-14
// contain none, lv15 is the first: id 202 at x=2,379 and id 203 at x=19,939.
// The per-tick advance for each is GD's own x speed divided by the 240 Hz
// physics step. 0.9 and 1.1 are measured here (the dump carries a `speed`
// column): lv15 t=129 -> 130 steps 1.29825 and t=130 -> 131 steps 1.61425
// exactly, and the switch happens on the tick the player's box first touches
// the portal's (x = 2,338.95, portal box left edge 2,353.5, half 15). The
// other three rows are GD's published speeds and are NOT yet verified.
// GD's own speed multiplier at a re-anchor (the dump's `speed` column), or 0
// when the caller did not supply one. See the --start notes.
inline double g_startSpeedMul = 0.0;
// ...and the same table keyed by that multiplier instead of by object id.
// 1.3 was 1.9492188f, "measured on lv15" -- but that measurement was of the
// FLOAT ACCUMULATION, not of the constant. GD advances x with `x += dx` in a
// float32 (advanceX does the same), so the observable per-tick step is the
// constant rounded to the ulp of wherever x happens to be:
//   lv15's 1.3 section sits at x = 19,900..21,650, ulp 0.001953125
//        1.95 / 0.001953125 = 998.4  -> 998 ulps = 1.94921875   (what was read)
//   lv20's 1.3 section sits at x = 4,994..6,700, ulp 0.00048828125
//        1.95 / 0.00048828125 = 3993.6 -> 3994 ulps = 1.9501953125
// and GD's own dumps give exactly those two numbers, averaged over 899 and 855
// ticks respectively. One constant, two readings -- so the constant is
// 468/240 = 1.95, which is also GD's published x speed for 1.3, and the model
// reproduces BOTH readings for free because it accumulates the same way.
// The old value only matched at x ~ 20,000 and drifted 1 px per ~1,020 ticks
// anywhere else; that drift is lv20's wall after the teleport fix (its first
// divergence is a wave section whose dx is exactly this).
inline float dxForSpeedMul(double mul) {
    if (mul < 0.8) return 251.16f / 240.0f;   // 0.7
    if (mul < 1.0) return 1.29825044f;        // 0.9  normal
    if (mul < 1.2) return 1.6142578f;         // 1.1
    if (mul < 1.45) return 468.0f / 240.0f;   // 1.3
    return 576.0f / 240.0f;                   // 1.6
}
// ...and the inverse. Some rules need GD's `speed` multiplier itself (the rotation
// easing while grounded; it is not proportional to dx -- the dx for 1.3 is 1.95,
// not 1.29825 x 1.3/0.9 = 1.875). The thresholds are cut the same way as cubePhysFor.
inline double speedMulForDx(double dxF) {
    if (dxF > 2.2) return 1.6;
    if (dxF > 1.78) return 1.3;
    if (dxF > 1.45) return 1.1;
    if (dxF > 1.15) return 0.9;
    return 0.7;
}
inline float dxForSpeedId(int id) {
    switch (id) {
        case 200: return 251.16f / 240.0f;   // 0.7  slow      UNVERIFIED
        case 201: return kDxF;               // 0.9  normal
        case 202: return 1.6142578f;         // 1.1  measured on lv15
        case 203: return 468.0f / 240.0f;    // 1.3  = 1.95, see the note above
        case 1334: return 576.0f / 240.0f;   // 1.6            UNVERIFIED
        default: return kDxF;
    }
}
// The CUBE's jump impulse and gravity are speed-dependent too, and not
// monotonically. Measured on lv15 (each twice, at two different places in the
// level, by injecting the cube onto a floor upstream of the speed portal so it
// really passes through it, then pressing):
//     0.9   jump 11.180   gravity 0.216
//     1.1   jump 11.420   gravity 0.215
//     1.3   jump 11.230   gravity 0.216
// A model that keeps 11.18/0.216 everywhere is ~0.24 out on the very first tick
// of every jump in a 1.1 section and drifts from there.
// 0.7 and 1.6 are NOT measured -- they fall back to the 0.9 row, and the first
// level that uses one has to be re-checked. Ball, ship and UFO constants are
// also still 0.9-only; nothing has needed them at another speed yet.
// RINGS scale with the same factor as the jump; PADS do not. Measured at 1.1:
//   yellow ring  11.180 -> 11.420   (= jump, exactly as at 0.9)
//   gravity ring  4.472 ->  4.568   (ratio 1.0214669, the jump's to 7 digits)
//   yellow pad   16.000 -> 16.000   (unchanged)
// So the scale is a property of the "jump impulse" family. The pink ring is
// assumed to be in that family too (same object class) but is UNVERIFIED, and
// so are the ball-mode ring values at speeds other than 0.9.
// THE BALL'S SPIN RATE, in degrees per tick, ready to be staked into
// State::rotRate. From PlayerObject::runBallRotation (@0x38d350, grounded) and
// runBallRotation2 (@0x38d480, airborne):
//
//     m_rotationSpeed = +-120 / (S * 0.20 * V)      grounded
//     m_rotationSpeed = +-340 / (S * 0.80 * V)      airborne
//     applied as (dt/60) * speed with dt = 0.25, i.e. speed / 240
//
// S is `(scale == 1.0) ? 1.0 : 0.8` -- the scale field is read but used only as
// a boolean, and the 0.8 is a literal, so the mini sprite's 0.6 never reaches
// the arithmetic. That is why mini/full is 5/4 and not 1/0.6.
//
// V is the speed divisor, and it works out to kDxF/dx: at 0.9 it is 1 and the
// rates are the round 2.5 / 3.125 (grounded) and 1.7708333 / 2.2135417 (air).
// Writing it against dx is what makes the whole family look "proportional to
// distance travelled" -- it is not. GD stakes a RATE and spends it per tick, so
// it does NOT change on a slope, where the distance covered per tick does.
//
// Checked against the corpus: grounded full 2.5000 (7,658 ticks), grounded mini
// 3.1250 (2,366), and the 1.1 / 1.3 tiers land on 3.1085 / 3.7551 to four
// decimals. air/ground = 17/24 = 0.7083333 reproduces the measured mode over
// 24,551 airborne ticks.
inline double ballRotRate(bool mini, bool airborne, double dx) {
    const double S = mini ? 0.8 : 1.0;
    const double V = (dx > 0.0) ? ((double)kDxF / dx) : 1.0;
    const double rate = airborne ? (340.0 / (S * 0.80 * V))
                                 : (120.0 / (S * 0.20 * V));
    return rate / 240.0;
}

struct CubePhys { double jump, g; };
inline CubePhys cubePhysFor(float dxF) {
    if (dxF > 1.78f) return {11.230, -0.216};   // 1.3
    if (dxF > 1.45f) return {11.420, -0.215};   // 1.1
    if (dxF > 1.15f) return {11.180, -0.216};   // 0.9
    // 0.7. Measured on lv18 t=15,293 (full size, on flat ground at y=225,
    // speed 0.7): GD leaves at vy = 10.620 and the next ticks step down by
    // 0.212 (10.408 / 10.196 / 9.984 ...). This row used to fall back to 0.9's
    // 11.180 / 0.216, which is 0.56 vy of extra jump on every press in a 0.7
    // section -- lv18 goes 0.7 the moment it takes the size portal at
    // x=20,386, so the whole route after it was planned too high.
    // [2026-08-30] "1.6 is still unmeasured and still falls back here" USED TO
    // STAND ON THIS LINE AND WAS FALSE IN BOTH HALVES. 1.6 is GD's 4x, its dx is
    // 2.400 (measured), and 2.400 > 1.78 -- so it takes the FIRST branch, never
    // this one. And it is not unmeasured any more: calib_speedcal_cube_s4, one
    // press on flat ground, gives jump 11.230 and -0.216/tick for the whole arc,
    // which is exactly what that branch already returned. The 1x control on the
    // same rig gives 11.180 / -0.216, matching the 0.9 row to the digit.
    // So the 1.3 row covers 1.3 AND 1.6, now on measurements of both.
    // What actually falls back here is 0.5x, and that IS the measured row below.
    return {10.620, -0.212};
}
inline double ringScaleFor(float dxF) { return cubePhysFor(dxF).jump / 11.180; }
// OPEN QUESTION (2026-08-01) -- the ball's tap is NOT one number at 1.1:
//   flat ground, upward tap : 3.4260  (lv15 t=10665) = 3.354 * ring scale
//   on a slope, upward tap  : 3.7056  (lv16 t=4306 with m=1.0, t=4314 m=0.5)
//   on a slope, downward tap: 3.4260  (lv16 t=4501)
// The +0.2796 does not scale with the slope's gradient, so it is not the
// surface's own velocity, and it is not a landing-speed effect either (the two
// slope samples came in at -12.714 and -1.935 and gave the same value).
// Until that is understood the FLAT value is used everywhere: it is the one a
// cleared level (lv15) depends on, and re-anchoring can absorb the 0.28 the
// slope case is out by.
inline double ballFlipFor(float dxF) { return 3.354 * ringScaleFor(dxF); }
// ...and the slope case gets a flat +0.2796 on top, which is what the three
// measurements above actually say. lv1-15 contain no slope at all, so this
// cannot move any level that is already cleared.
constexpr double kBallSlopeTapBonus = 0.2796;
// [2026-08-21 r92] The 0.2796 above was **the sp1.1 value**. On the ceilhold
// calibration rig, the bonus in the 6 ball cells (1x, |m| 0.5/1/2 x normal/mini) is
//   0.225 / 0.417 / 0.591  (the same for normal and mini)
// = **0.075 times the ball's slope-exit launch slopeExitVy(|m|,2) at that gradient
//   and that speed** (2.999 / 5.554 / 7.880 x 0.075 = 0.2249 / 0.4166 / 0.5910).
// At sp1.1, exit(0.5)=3.729, so 0.075x3.729 = 0.2797 = the old constant itself
// (the corpus's lv16 t=4,701 reproduces as before). The old form scaled only with
// the gradient via the exit ratio and **did not scale with speed**, so it was 24%
// too high at 1x.
// 0.075 = 0.25 x 0.30 (1/4 of the launch = the cube's bonus rule x the ball's tap
// ratio 3.354/11.180), but the origin of that product is unconfirmed, so it is kept
// as the one measured constant.
constexpr double kBallSlopeExitBonus = 0.075;
inline bool isSpeedId(int id) {
    return id == 200 || id == 201 || id == 202 || id == 203 || id == 1334;
}
constexpr double kYScale = 0.225;
// GD's m_yVelocity lives on a 0.001 grid. Measured over 440,000 ticks of the 19
// verified replays (build/fidelity/fid_lv*.dump.csv, dumped at precision 9):
// 99.8% of every yvel GD reports is an exact multiple of 0.001, and of the 107
// half-grid values that do occur -- all of them the direct output of a HALVING
// (a ball tap, or the ship's leave-the-ground halving) -- not one survives to
// the next tick. So the rounding is not in the impulse and not at end of tick:
// it is inside the gravity/thrust integration, which is also the value GD's own
// position update uses (lv9 t=10571: vy 2.9555 + 0.069 = 3.0245, GD stores
// 3.024 and moves y by 0.225*3.024 = 0.6804, exactly the dy in the dump).
//
// The model integrated in full precision instead, so every halving left a
// ~0.0005 residue that then rode along untouched for thousands of ticks and
// pulled y off by 0.02..0.10 px. That is small enough to hide under the
// fidelity diff's 0.3 tolerance and large enough to decide a contact: on lv9
// the ship's head reached the block underside 0.076 px short and clamped one
// tick late, and on lv10 the same 0.023 px meant it never clamped at all (the
// block's x had gone by), which is the death at t=4026.
inline double qVy(double v) { return std::round(v * 1000.0) / 1000.0; }
constexpr double kCubeG = -0.216, kCubeJump = 11.18, kCubeTerm = -15.0;
// Ball, measured on lv10's ball zone (data/ballpress.dump, portal at x=2295):
//   gravity     -0.1290 / tick   (69 consecutive airborne ticks, no variation)
//   tap         does NOT jump -- it FLIPS gravity and starts the ball moving
//               toward the new floor at 3.354 (t=1820 grounded y=105 vy=0 ->
//               t=1821 upsideDown=1 vy=+3.354, then +0.129 per tick)
//   rest height surface + 15, same as the cube (measured y=105 on the ground)
constexpr double kBallG = -0.129;
constexpr double kBallFlip = 3.354;
// UNVERIFIED: the ball was still accelerating at |vy| = 11.997 when it left the
// zone, so no plateau was observed. The cube's -15 is used as a stand-in; a
// taller ball section is needed to measure the real cap.
constexpr double kBallTerm = -15.0;
// SWING (mode 7, portal type 41; lv22 x=4,641 is the first anywhere).
// Measured on lv22's reference replay (dx=1.6143, update 76):
//   - the tap TOGGLES gravity; holding does nothing extra (push t=3700,
//     release t=3706, accel stayed positive for 100+ ticks after the release)
//   - in the player frame the whole mode is: gravity -0.086/tick, terminal 8,
//     and each tap flips the frame with vp := -0.8 * vp. Both measured flips
//     match to 0.001: -6.424 -> -0.8*(-6.424)-0.086 = -5.053 (world -5.053)
//     and 8.000 (world, flip=1, vp=-8) -> -0.8*(-8)-0.086 = 6.314 (world 6.314)
//   - terminal: vy sat at exactly 8.000 for many ticks
// OPEN: measured at this section's speed only; the grounded
// tap (launch off a surface) untested -- treated as the same toggle.
// [2026-08-20] What used to say "mini untested" was measured on the flyramps
// calibration rig: **the mini swing is 0.129/tick** (= 0.086 x 1.5). In the free
// flight right after a ramp launch GD steps 5.554 -> 5.425 -> 5.296 -> 5.167, i.e.
// by 0.129, while the same rig at normal size steps by 0.086. The same in 3 units
// (m=1 mini n=3/n=1, m=2 mini). No counter-example in the corpus (swing exists only
// in lv22, where everything is vsize=1.0 and 0.086).
constexpr double kSwingG = 0.086;
constexpr double kSwingGMini = 0.129;
inline double swingG(bool mini) { return mini ? kSwingGMini : kSwingG; }
// [2026-08-20 r39] The ship's ladder **while riding a ramp**. Read directly off
// the fixups of hp32's lv16 cold run (6 consecutive ticks at x=23,148..23,156:
// 2.000 2.086 2.172 2.258 2.344 2.430). Same value as the normal-size swing, but
// it has a different origin so it gets its own name -- the mini ship is unmeasured
// (the mini swing is 1.5x, but there is no basis yet for applying that to the ship).
constexpr double kShipRampG = 0.086;
// [2026-08-30] MEASURED TO BE WRONG IN TWO DIRECTIONS, and left alone anyway --
// read this before "fixing" it. calib_forcedrop_swing_f10 (a swing walked into a
// force box, m_force=10, no input):
//   FULL SIZE ignores this cap entirely. vy climbs +0.8140/tick straight through
//     8.140, 8.954, ... to 17.094 at t=281 and is still climbing. The model
//     clamps at 8 unless State::boost is set, and a force box is not one of the
//     setters (those are the ramp launch and the red orb/pad, 889549e).
//   MINI clamps -- but at 9.385, not 8, and it holds there exactly.
// So the cap is not one number and not simply conditional. It is NOT changed
// here because nothing in lv1-22 reaches it this way: lv22's carpet lifts its
// swing at ~0.216/tick over a few ticks and never approaches 8. Loosening a
// clamp on a rig that no corpus level exercises is how a change passes every
// regression and breaks a level later ([[gd-loosening-rule]]). What is needed
// first is a rig where the corpus's own mechanisms (orb, pad, ramp) push a swing
// past 8, so the two readings can be told apart.
constexpr double kSwingTerm = 8.0;
constexpr double kSwingFlipDamp = 0.8;
// --dynhazpad <px>: inflation of the moving hazards' boxes (default 0 = behaviour
// unchanged). The note is where L.dyn.samples gets filled. A provisional
// prescription for lv22's 0.19px miss.
inline double g_dynHazPad = 0.0;

// THE LEVEL'S OWN COMPATIBILITY FLAGS (LevelSettingsObject). The mod writes
// them beside objrects as `levelsettings.txt`; the loader picks the file up by
// name, so nothing has to be threaded through a caller.
//
// Of the 22 official levels only lv22 has any of them set, and it has five
// (lab: flags-levelsettings-branches-2026-09-01.md, collected for all 22 --
// every other level is all-zero). All five are carried here because reading
// four of them costs nothing and the next question about lv22 will want them;
// only fixRadiusCollision has a reader today.
//
// fixRadiusCollision is kA39 at LevelSettingsObject+0x1cf, and
// GJBaseGameLayer::playerCircleCollision (0x211df0) branches on it in its first
// instruction: zero picks the player's RECT against the circle, non-zero picks
// centre distance. See hazardHit.
inline int g_fixRadiusCollision = 0;
inline int g_fixGravityBug = 0;
inline int g_fixNegativeScale = 0;
inline int g_fixRobotJump = 0;
inline int g_dynamicLevelHeight = 0;
// --maxplayy <y>: GD's MAX GAMEPLAY Y, the world-y bound above which
// checkCollisions declares the player out of bounds and (after two consecutive
// ticks, latch at player+0xc38) destroys it with a NULL object
// (GJBaseGameLayer::updateMaxGameplayY writes layer+0x36a8: dynamic-height
// levels use max(1200, max object y) + 90 + 300, every other level a fixed
// 2790; the kill gate is checkCollisions+0x999). The mod reads the layer's
// live value and passes it; without the flag the model keeps its old
// "the sky is open" behaviour. Measured on lv22 (bound 3,675 = 3,285 + 390,
// injection-bisected to 0.4px at two x) and lv9 (default 2,790: y=1,010
// lives).
inline double g_maxPlayY = 1e18;
// --goalspare (EXPERIMENT): the two search prunes that stop a body ABOVE the level -- out-of-play
// over g_yBound and escapee-prune over every surface still ahead -- keep one that reaches the goal
// first. Both assume a body up there has nothing left to do, but one that stays alive to the goal
// has cleared the level: a custom level crosses its last wall with a gravity orb and rises untouched to the
// end (GD complete at y=1,635; the model pruned the same plan at y=1,308, and g_yBound 1,410 would
// have pruned it next). A body is kept only when even the fastest rise cannot carry it over MAX
// GAMEPLAY Y before the goal, so GD's own death up there is still modelled (maxplayy) and the
// frontier of escapees the prunes were built against (lv9) does not come back mid-level. Needs
// --maxplayy: without a known ceiling nothing is kept.
// ON BY DEFAULT with the SubZero coin set (the release reports what the defaults solve);
// --no-goalspare turns it off.
inline constexpr int kDefGoalSpare = 1;
inline int g_goalSpare = kDefGoalSpare;
inline double g_goalX = 1e18;   // the search's goal, L.maxX + 60 (cli.hpp)
inline bool reachesGoalUnderMaxPlayY(double x, double y, double vy, double dx, bool rev) {
    if (!g_goalSpare || g_maxPlayY >= 1e17 || g_goalX >= 1e17) return false;
    if (rev || dx <= 1e-6) return false;
    if (x >= g_goalX) return true;
    const double n = std::ceil((g_goalX - x) / dx);
    // The fastest rise per tick: terminal 15 at the wider integration coefficient (0.25; every
    // mode but the wave uses 0.225), or a wave's line at up to twice the x advance (mini). A time
    // warp scales the x advance and the rise alike, so the ratio holds through one.
    const double rise = std::max(std::max(std::fabs(vy), 15.0) * 0.25, 2.0 * dx);
    return y + n * rise <= g_maxPlayY;
}
// --upsidecoyote (on by default since 2026-09-26; --no-upsidecoyote is the off arm): GD's upside-down cube can still jump for a few ticks after it walks
// off the surface it hangs from. playerIsFallingBugged's classic arm tests `vy > 2g` when flipped
// where the upright arm tests `vy < 2g` -- the flipped side never negates g -- and updateJump
// clears m_isOnGround only when that test says "falling". Walking off a ceiling starts at vy 0
// and gains 0.216 a tick, so og stays set until vy passes 2g (accelSwitchVyForSpeed), and the
// buttons phase, which reads og after the update, jumps from mid-air.
// Measured on a custom level (worker probe, a press swept tick by tick through t=18,414-18,431; the
// cube leaves the underside at 18,419): a press whose effect tick is 18,419 .. 18,427 jumps
// (vy -11.23 on that tick), 18,428 does not; og in the dump drops on 18,428, the first tick after
// vy(18,427) = 1.944 > 1.9224. On the air jump's tick y still moves by that tick's gravity step
// (255.0486 -> 255.1458 at k=2), where a jump off the surface leaves y where it was. The upright
// walk-off test (vy 0 < 2g) says "falling" on the first tick, which is why the upright cube
// cannot do this and why the model's support scan was right for it.
// The ball has the same window (its gravity step is 0.129, so it lasts 15 ticks). Measured on
// a custom level (mini ball, speed 0.9, leaves the underside at t=9,387): a press at every tick k=1..11
// taps with vy -2.6832 -- the mini ground tap -- and flips gravity, with y moved by that tick's
// step; k=12 there is a gravity portal. Frame 0, single body only.
// ONLY WHERE THE LEVEL KEEPS THE BUG: the level setting fixGravityBug (g_fixGravityBug, from
// --levelsettings) is what the "Bugged" in playerIsFallingBugged is about. Counted in the GD dumps of
// 201 old custom levels, every upside-down cube or ball leaving its surface with 0 < vy < 2g: with
// fixGravityBug=0, 560 kept og (the linger) and 1 dropped it; with fixGravityBug=1, 28 dropped it
// and none kept it. Official lv22 sets it, and ungated the flag jumped a mini cube off a moving
// block's edge at t=2,810 where GD had already cleared og -- the one cold wall this flag caused.
// The single fixGravityBug=0 drop (a custom level t=19,481, a mini ball) had its head 1.1 px under the
// band's top, not under an object; it is not covered here.
inline int g_upsideCoyote = 1;
// --rawvalues (EXPERIMENT): the gravity step BEFORE the 0.001 grid. The literals the cube branch
// uses (0.216 / 0.215 / 0.212, the ball's 0.129, the robot's 0.194 / 0.195) are GD's steps as
// they show on the grid, and they are right whenever vy is already on it. From an off-grid vy
// they are not: a mini ball's tap leaves at -2.6832, and GD's next vy is -2.813 where
// -2.6832 - 0.129 rounds to -2.812 (a custom level t=9,386, and the drift is where eight of the old
// custom levels' divergences start). GD steps by g x 0.225 (x0.6 ball, x0.9 robot) with g its
// per-speed gravity (accelSwitchVyForSpeed's table) and rounds after; -2.6832 - 0.129357 =
// -2.8126 -> -2.813. Every literal above is that value on the grid, so an on-grid vy is unchanged.
// The ball's is flat across speeds (0.129 measured at all four), so it takes the 0.9 row.
// The same holds for a ring's value, which ringJump sets as a raw product: the ball's gravity
// ring is 4.472 x 0.7 = 3.1304 in GD (a custom level t=18,078 stores 3.13039995), where the model's
// constant was the rounded 3.130 -- on-grid itself, so the next tick rounds the other way.
// On by default (with --padtable); --no-rawvalues turns it off.
constexpr int kRawValuesDefault = 1;
inline int g_rawValues = kRawValuesDefault;
// ...and the other half of the same grid: the launches GD writes ON it. A slope's exit launch
// (6.906 at lv16 t=5,401 / lv22 t=3,806, 2.499 at lv18 t=10,824, the ceiling release, the
// swing portal's 2.762) and a ring's jump x ratio (red 11.18 x 1.38 -> 15.428 at lv22 t=297,
// pink 8.05 / 8.222) come out on the grid, where the model's formulas leave them a fraction off
// (6.905590, 2.499375, 15.4284). With the rounded gravity that fraction happened to round away
// on the next tick; with the raw one it rounds the other way, so under --rawvalues the launch
// is rounded where it is written. A ring's POST multipliers (ball / spider x0.7, the gravity
// ring's x0.5) come after the rounding and stay raw: 3.1304 and the pink ball's 6.0263 =
// q(11.18 x 0.77) x 0.7 are both what GD stores.
// The float cast first: lv17 t=18,572's slope exit is -3.7025 exactly on paper, GD's float of it
// (-3.70250010) rounds to -3.703 where the double rounds to -3.702.
inline double rawQ(double v) { return g_rawValues ? qVy((double)(float)v) : v; }
// --portalunpin (EXPERIMENT): a gravity portal taken by a cube standing on a block leaves y
// where that tick's gravity step put it (step.hpp, at vAtPortal).
// On by default (with --portalpress); --no-portalunpin turns it off.
constexpr int kPortalUnpinDefault = 1;
inline int g_portalUnpin = kPortalUnpinDefault;
// --hazendpoint (EXPERIMENT): the cube's and the flying body's hazard test at the tick's END only,
// not at the samples between its endpoints. The in-between samples pair the old x with a partly
// moved y -- a place the body never was -- and lv9 t=11,830 / 13,078 die on one (the ship's box
// 0.01 px into spike uid 2537 at f=0.75, clear of it in x at the endpoint) where GD lives.
// Sweeping y at the new x instead was tried first and is worse: deathref lost 4 of 42 (the model
// died 112-342 ticks before GD, on (new x, old y) samples -- just as fictional). Endpoints only
// keeps deathref at 42/47 and quick_regress's exact tracking loses nothing (lv9 gains 10,416
// ticks, 8 segments stop dying). kSubSteps = 1 was rejected on 2026-07-31 because lv11's COLD got
// worse on the model of that day; that is the measurement this flag still owes.
// Swept on the game over the modes and shapes the first sweep (165 phases, 10 classes) left out,
// on phases where only an interior sample overlaps: spider 12, swing 65, a turned hazard 51 and a
// saw 16 -- GD killed none. On by default; --no-hazendpoint turns it off.
constexpr int kHazEndpointDefault = 1;
inline int g_hazEndpoint = kHazEndpointDefault;
// --portalpress (EXPERIMENT): a press on the tick a MODE portal fires is the new mode's press. GD
// consumes a press in the buttons phase, after the collision pass that fired the portal (the
// coyote sweep showed the press line P acting at P+1 after that tick's move). The model ran the
// old mode's action in its branch and the portal after it: a custom level t=7,099 cube -> ship, the model
// jumped (11.18, halved by the portal to 5.59) where GD sat as a ship with vy 0 and held from the
// next tick; another custom level t=15,425 cube -> ball, GD tapped (3.354); a third custom level t=1,606 flipped ball -> cube,
// GD jumped (-11.18). Old mode cube/ball, new mode cube/ball/ship/wave: the transitions seen.
// On by default (with --portalunpin); --no-portalpress turns it off.
constexpr int kPortalPressDefault = 1;
inline int g_portalPress = kPortalPressDefault;
// --padtable (EXPERIMENT): a pad's launch from GD's own table instead of the cube's value with a
// ball ratio. GJBaseGameLayer::bumpPlayer (0x217b66) picks a strength by pad type and mode, and
// PlayerObject::propellPlayer (0x39f850) sets vy = float(sign x strength x 16 x mini 0.8), then
// x0.6 (double) for ball, spider and swing:
//   pink (9)  ship 0.35  UFO 0.4  ball/spider 0.7  else 0.65
//   red (34)  ship 0.63 (mini 0.95)  UFO 0.6 (mini 0.98)  else 1.25
//   any other pad (yellow)  1.0
// The model gave every flying mode the cube's pink 10.4: GD launches a ship off a pink pad at
// 5.6 (old custom levels, t=4,391 and t=3,158) and a mini UFO at 5.12 (another custom level, t=13,008).
// The cube's 16 / 10.4 / 20, the ball's 9.6 / 6.72 are this table's values; the swing's x0.6 is
// new (the model had none), and red on ball/spider keeps its x0.6.
// On by default (with --rawvalues); --no-padtable turns it off.
constexpr int kPadTableDefault = 1;
inline int g_padTable = kPadTableDefault;
inline double padTableVy(int type, int mode, bool mini) {
    const bool ship = mode == 1, ufo = mode == 3, ball = mode == 2, spider = mode == 6;
    float s = 1.0f;
    if (type == 9)
        s = ship ? 0.35f : ufo ? 0.4f : (ball || spider) ? 0.7f : 0.65f;
    else if (type == 34)
        s = ship ? (mini ? 0.95f : 0.63f) : ufo ? (mini ? 0.98f : 0.6f) : 1.25f;
    double v = (double)(s * 16.0f * (mini ? 0.8f : 1.0f));
    if (ball || spider || mode == 7) v *= 0.6000000238418579;
    return v;
}
// Fly-rings (was --flyrings, always on since 2026-09-26):
// a SHIP or UFO ring fires one tick before the model's input reaches the
// body. Both modes read the button 2 ticks after the press line (planEdges' latOf), but pushButton
// calls ringJump in the buttons phase of the tick after the press, testing the ring box at that
// tick's end. So GD sets the ring's velocity on the row BEFORE the one the model fired on, runs the
// next tick's update on it, and the press is spent (the UFO does not flap). Old custom levels,
// replayed without fixups:
//   a custom level, t=2,528, ship, gravity ring: GD 4.472 then 4.364 (held); the model fired a tick later
//   another custom level, t=7,319, mini UFO, gravity ring: GD 3.5775 then 3.679; a tick later the model flapped
//     (6.648), moved, and set 3.5775 after the move
// It also gives the pink ring its ship and UFO ratios (ringJump: ship 0.37, UFO 0.42) in place of
// the cube's 0.72: a custom level t=8,678 and another custom level t=11,196, mini ship, GD -3.309 = 11.18 x 0.37 x 0.8.
inline thread_local int g_flyRingNested = 0;
// --portalslopeorder's re-run (frames.hpp; the end of stepOne): the uid of the gravity portal
// the nested step fires ahead of the ramp pass, -1 when no re-run is in progress.
inline thread_local int g_prePortalUid = -1;
// --balltapexit's re-run (frames.hpp; the top of stepOne): 1 while the nested step runs with the
// ball's fresh-press taps held back, and whether the uphill launch fired in it.
inline thread_local int g_ballTapNested = 0;
inline thread_local int g_ballTapLaunch = 0;
// Ring once (was --ringonce, always on since 2026-09-26): a fired ring never fires again in the
// run, remembered for the last five rings a body fired (usedOrb, usedOrbOld and
// State::usedOrbHist) instead of two. GD refuses a ring it has fired even after other rings in
// between (a custom level t=2,548, the field's note).
// Portal once (was --portalonce, always on since 2026-09-26): a MODE portal fires only on the
// first tick the player overlaps it -- the latch the gravity portals already have (step.hpp,
// portalLatch), for the mode portals. GD
// activates a portal on its first overlap whether or not it changes anything. A custom level t=4,300: a
// cube enters cube portal uid 637 at about t=4,275 (no change), then UFO portal uid 651 at 4,299
// while still inside 637. GD stays a UFO falling at -2.991; the model fired 637 and 651 again on
// every tick inside both, halving vy twice a tick (-2.905 -> -0.748 -> -0.209). The size portals
// the same: a custom level t=16,674, a mini cube inside mini portal uid 2359 enters normal portal 2356
// and GD stays full size at y=135; the model fired 2359 and 2356 again and re-seated to 141. It
// remembers one tick of overlaps (State::portSeen), not a run-long latch, so a portal left and
// re-entered fires again.
// Hold latch (was --holdlatch, always on since 2026-09-26):
// a ship thrusts on GD's m_jumpBuffered (+0x985), not on the button.
// updateJump's ship arm reads +0x985, and a ball's tap ends with `flipGravity, vy *= 0.6,
// +0x985 = 0` (a ball's or swing's ring clears it too), so a ball that taps and then enters a ship
// portal with the button still held flies as released until the next press. Three old custom
// levels: t=1,029 (flipped ship, GD +0.1035 a tick where the model thrust -0.0865),
// t=8,654 and t=11,780 (mini). State::holdDead carries it.
// The cube's landing re-jump reads the same byte, so it gates on holdDead instead of
// ringHold: a ring a cube fires leaves +0x985 set (a custom level t=12,551, GD re-jumps after yellow ring
// uid 2314), and a spider orb or the spider's own tap clears it (spiderTestJumpInternal) -- the
// lv22 t=10,352 case ringHold was built on. The robot keeps ringHold (its arm needs +0x986 too).
// Ring first (was --ringfirst, always on since 2026-09-26):
// a press that finds a ring is the ring's. pushButton fires the touched
// rings and returns; it calls updateJump only when there are none. The model runs the grounded
// jump or tap in its physics branch and the ring loop after it, so a grounded ball pressing inside
// a gravity ring flipped twice. A custom level t=11,833: a ball on the ceiling presses in gravity ring
// uid 1981; GD comes out upright at -3.1304 (the ring alone), the model tapped (flip) and then
// fired the ring (flip back) and stayed on the ceiling at +3.1304. The tap's flip and spin are
// undone when a ring fires on the tick of the press that made them.
// Ring order (was --ringorder, always on since 2026-09-26): which touched ring a press fires is
// GD's m_touchedRings order, carried in State::touchRing, instead of "in contact a tick ago beats
// not, then the lowest uid". The array
// keeps a ring's first-contact position; at a tick's head resetTouchedRings drops the rings the
// tick before did not touch, and the collision pass appends new contacts in ascending uid. One
// tick of history cannot tell apart two rings entered on different earlier ticks: a custom level t=2,741,
// a ship in contact with yellow 447 (entered ~2,735) and gravity 443 / yellow 446 (a tick later,
// one square), and GD fires 447 where the lowest-uid rule fired 443.
// Gravity pad chain (was --gravpadchain, always on since 2026-09-26):
// the gravity pads a tick enters are taken in uid order and every one
// the facing gate lets through flips. collisionCheckObjects (0x2158f9) sets gravity to the pad's
// own direction (!isFacingDown) when it differs, then propellPlayer and flipGravity, pad by pad
// in the bucket's uid order -- so two pads facing opposite ways flip twice. The model's "one flip
// per tick" (gravPadFlipped) is right only for pads that agree, which the gate already reduces to
// one. A custom level t=1,307 enters a wall of sideways blue pads (rot -90, flipY alternating = facing
// alternating); GD comes out upside down at +6.4 where the model stopped after the first flip at
// -6.4 (another custom level t=6,960 and a third custom level t=8,103 are the same wall kind).
// Pad first (was --padfirst, always on since 2026-09-26):
// a pad that fires on the tick of a grounded press leaves the press
// unspent. The pad fires in the collision pass and propellPlayer drops the ground flag; the buttons
// run after it, so updateJump finds the player airborne and does not jump, and +0x986 stays set
// for a ring touched later in the hold. The model jumped in its physics branch (spending the
// press) and let the pad overwrite the velocity. A custom level t=15,114: an upside-down cube presses on
// the tick a blue pad flips it (both give -6.4), and on the next tick GD fires yellow orb uid 1503
// with that press (11.18) while the model, its press spent, did not.
// One-way ceiling (was --onewayceil, always on since 2026-09-26):
// a one-way platform's world underside is a ceiling for the flying
// modes. collidedWithObjectInternal lets ship, UFO, wave and swing into its ceiling resolution
// unconditionally; the cube's head branch is gated (the arms), which is what the "transparent from
// below" measurement saw (lv13, a cube jumping up through type-21 blocks). A custom level t=9,220: an
// upright ship holding up into plate uid 840 (id 143, underside 136) stops at y=121 with vy 0 in
// GD, where the model flew through. It still never kills (the plate's side test stays off).
// Portal flap (was --portalflap, always on since 2026-09-26):
// a fresh press on the tick a cube enters a UFO portal is the UFO's, and
// it flaps on the NEXT tick. The press reaches pushButton in the buttons phase, after the collision
// pass made the player a UFO, and a UFO flaps in updateJump on the next update -- the ship->UFO
// rule's pending flap (State::pFlap). The model jumped as the cube and re-issued the flap on the
// portal tick itself, one tick early (a grounded cube), or dropped the press (an airborne one).
// A custom level t=15,402 and the calib_portalpress cells cube->ufo at offset -1: GD flaps at the press + 2,
// grounded and airborne. The reverse (ufo/ship -> cube, press one tick before the portal: GD jumps
// on the portal tick) is not representable in the model's per-tick input -- its in(t) = 1 on that
// tick already means the old mode's action -- and a plan the search writes never contains it.
// --heldall (EXPERIMENT): State::held is the button level of the tick in EVERY mode. Only the wave
// and flying branches wrote it, so a body that entered a wave from the ground read whatever the
// last flying section (or the anchor) left: the wave's first direction is `s.held`. Official lv21's
// quick_regress section from t=13,000 anchors in a robot section with the button down, the robot
// never rewrites held, and at the robot -> wave portal (t=14,391) the model climbs at +6.457 where
// GD dives at -6.457 with the button up. The ship's release re-clamp (holdRelU) reads it too.
inline int g_heldAll = 0;
// --heldcellcap (EXPERIMENT, only with --heldall): the alive cap counts CELLS, not states. Under
// --heldall a cell whose states differ only in the button (held, jumpBuf) keeps them apart, as it
// should, but each twin then takes a slot of the cap and the water-fill's stride runs over twins
// standing side by side, so the layer keeps fewer cells than it would without the flag (lv19:
// x~3,541 needs cap 2,000 with --heldall where 125 did without). With this the water-fill samples
// one state per cell and every state of a sampled cell is kept.
inline int g_heldCellCap = 0;
// --heldcelltwins N (with --heldcellcap; 0 = no limit): keep at most N states of a sampled cell --
// its first state, then the states whose held differs from it, then the rest, each in layer order.
// A cell holds up to four (held x jumpBuf), so without a limit a layer can keep four times the cap:
// measured over the official 22 at t=0 (2026-09-26), lv20's stepped children went
// 60.1M -> 346.7M and its peak frontier to 22x the cap, lv14 2.8M -> 125.7M.
inline int g_heldCellTwins = 0;
// Pad section (was --padsection, always on since 2026-09-26):
// on a tick a SIZE portal fires, a pad is judged and launched at the size
// GD had when its collision pass reached the pad, not always at the old size for the contact and
// the new one for the strength. GD walks the section cells around the player x-major (100 units),
// then y, and each cell's objects by uid (step.hpp, gdCollidesAfter); togglePlayerScale writes
// +0x9f0 at once and propellPlayer reads it. A custom level t=3,158: a cube -> ship section, pink pad uid
// 1007 at x=4,759 (section 47) and mini portal uid 1079 at x=4,815 (section 48) on one tick; the
// full-size box touches the pad by a pixel, and GD launches the ship at 5.6 (full) where the model
// gave 4.48 (mini).
// Ground-grow (was --groundgrow, always on since 2026-09-26):
// a cube or ball that a size portal grows while it stands on the WORLD
// ground is lifted to the new seat on the NEXT tick, by the ground clamp, not on the portal's own
// tick. GD's ground plane is enforced in PlayerObject::update, before checkCollisions fires the
// portal, so the grown body spends one tick embedded; a block is resolved in checkCollisions and
// can re-seat it the same tick (lv12 t=3,286, which stays). Old custom level t=17,939: a
// mini ball on the ground (y=99) meets RegularSize portal uid 2257 -- GD reads vsize 1 at y=99
// and 105 only at t=17,940, where the model went to 105 at once. The calibration rig's reading
// (cube next tick, ball and UFO the same tick; the retracted r69 note at the re-seat) is left
// alone for the flying modes, which have their own floor clamp.
// Ring press pin (was --ringpresspin, always on since 2026-09-26):
// a ring taken off a block by a FRESH press leaves y on the block. GD
// reaches a ring by two paths: a button already down fires it from the collision pass
// (playerTouchedRing), before the solid pass puts the body back on the block, so y keeps that
// tick's gravity step; a press fires it from pushButton in the buttons phase, after the solid pass
// has re-seated it. The model released the block pin for every ring. Old custom level
// t=2,126: a mini cube on block uid 204 taps (press line 2,125) inside yellow orb uid 200; GD
// leaves at y=150.000 with 8.944, the model at 149.951. The orb evidence at the release (lv19
// t=254/506, lv16 t=6,404, lv18 t=10,824) has no press at those ticks in today's plans.
// Solid order (was --solidorder, always on since 2026-09-26):
// the two main collision loops (ground and flight) take the near list
// in DESCENDING uid, the order GD resolves solids in. The cx sort leaves equal-cx objects in the
// unstable sort's order. Read from GD's own collidedWithObject calls (hbox lines, the ppre of each
// call being the previous call's result) over a custom level t=1..3,099: 306 ticks with two or more
// solids, 306 in descending uid, 0 ascending, 86 of them across two 100-unit x cells.
// The same custom level, t=9,235: slab 4959 lands the ball (y 240), one-way plate 4956 then pushes its head
// down to y=231, slab 4931 does nothing, and spike 4927 kills at 231. This is the solids only:
// the bucket walk that meets rings and pads (ascending uid, 0x205210) is a different pass.
// --solidorddbg t0:t1 (diagnostic): print `solidord t= uid=` for each solid a collision loop visits
// while the player's box overlaps it, in visiting order, for ticks t0..t1 -- the model's side of
// GD's hbox call order, for the consumer that checks the two orders tick by tick. Replays only:
// the search visits the same tick once per state.
inline long long g_solidOrdDbgT0 = -1, g_solidOrdDbgT1 = -1;
// Ball ceiling (was --ballceil, always on since 2026-09-26):
// an upright ball whose head is past a solid's underside, having been no
// more than the landing tolerance past it at the start of the tick and not falling, is put under
// the face with vy 0 -- one-way plates included. collidedWithObjectInternal lets the ball into its
// ceiling resolution unconditionally (the cube's head branch is the gated one), which the
// ground-mode loop had only for a ball entering from the side (lv9 t=18,354). The same custom level t=9,235
// (hbox lines): after slab 4959 lands the ball at y=240, one-way plate 4956 (underside 240,
// 9 px into the head, x overlap 0.28) puts it at y=231; the model passed through the plate.
// Takes GD's solid order (--solidorder) to put the landing before the push.
// The boundary, from injections under normal slab uid2511 (underside 330) in the same level, a mini
// ball at x=6,225: rising at vy +2, a head 9.9 px past the underside at the start of the tick is
// put at y=321 with vy 0 and onGround 1 (and falls from the next tick); 10.1 dies. Falling at vy -1
// it is never pushed -- the head sits 9.4 px inside and the ball falls on, alive -- and 10.3 dies.
// So the tolerance is kLandTol on the tick's start, the gate is "not falling", and the push grounds
// the ball. The falling case's survival is --faceclass's head side.
// Flipped ball ceiling (was --ballceilflip, always on since 2026-09-26):
// --ballceil's push on a flipped ball, whose head side is a solid's
// top: head past the top, no more than kLandTol past it at the start of the tick, not falling (in
// its own gravity) -> y on the top, vy 0, and NOT grounded -- GD's onGround stays 0 on this side.
// Old custom level t=7,686: a gravity portal flips a ball falling at -10.6 (7,683); it
// decelerates to -5.1 and its head reaches block 2274's top (360); GD's row is y=375, vy 0,
// onGround 0, then +0.129 a tick. The model went on into the block.
// Snap past a one-way (was --snaponeway, always on since 2026-09-26):
// the stair snap's nudge is blocked by a one-way (breakable, type 21)
// PREVIOUS snap object only, not by a one-way landing target. checkSnapJumpToObject (0x393cb0)
// skips the nudge when the stored object (+0x888) is missing, is the same uid, or its object type
// (vtable+0x660) is not 0 -- the new object's type is never read. The counted "0 moved / 108 still"
// on calls with a type 21 in them is consistent (lv13 t=2,142 had both one-ways). Old custom
// level t=12,161: a flipped cube snapped on solid 4136 lands on breakable 4127's underside
// (dx 60, dy -60 = the slow `big` stair, mirrored); GD moves x by +1.0 (snapdist 1.2129).
// Flight re-land (was --flyreland, always on since 2026-09-26):
// in the flight loop, once a ceiling has pushed the player down this tick,
// a floor may land it even though it is already grounded. GD resolves each solid on its own; the
// model's gate `!c.grounded` let the ceiling have the last word. Old custom level t=3,536
// (hbox lines), a mini ship squeezed between floors 998/969 (top 224) and ceiling 996 (underside
// 240): 998 lands it at y=233, 996 pushes it down to 231, and 969 lands it at 233 again, every tick,
// so GD stays at 233 on the floor. The model ended on the ceiling at 231. Takes --solidorder for
// GD's order. Opening the gate without the ceiling condition cost lv19 t=19,801 (a grounded UFO
// re-landed on a face 0.002 px off GD's), so the plain case keeps it.
// Pad then land (was --padthenland, always on since 2026-09-26):
// pads that fire on a landing tick, leave gravity where it was and leave
// vy pointing along it do not undo the landing; the player stays on the face with vy 0.
// checkCollisions runs the activation pass before the solid pass (releasePin's note), so the solid
// pass lands the player on the pads' vy. The model runs the pads last and undid the landing for
// any pad. Old custom level t=8,103 (fg and hbox lines): two gravity pads flip the cube
// and back (vy +6.4, then -6.4), then floors 2871 and 2870 land it at y=225 with vy 0. The model
// left it at vy -6.4 for a tick.
// Face class (was --faceclass, always on since 2026-09-26):
// a solid whose face the player's foot (in its own gravity) is inside by
// no more than the landing tolerance -- kShipLandTol for the ship, kLandTol for cube and ball -- is
// a ground contact. Neither the ship's side kill nor the crush applies to it. GD classifies a
// contact by the face before the inner box decides anything. Measured on an old custom level by
// injecting a rising player (vy +2, centre over the block, x fixed) above a top face:
//   full ship  over uid441:  dies at centre 4.487 above the face, lives at 4.507  (inner box 4.5)
//   mini ship  over uid997:  dies at 2.993, lives at 3.003 -- depth 9 - 3.0 = 6.0 = kShipLandTol
//   full cube  over uid4491, full ball over uid3860: die at 3.8, live at 4.8   (inner box 4.5)
//   mini cube  over uid1570, mini ball over uid3980: live at 1.601 and 1.621 (depth 7.4 < kLandTol)
// With the full size the inner box never reaches a face the foot is within tolerance of (15 - 10 =
// 5 > 4.5, 15 - 6 = 9); with the mini size it does, so only the mini bodies see the rule. The crush
// rig's mini cube (kCrushHalf) was measured on the HEAD side, under a ceiling, which this leaves
// alone. A custom level t=3,603: a mini ship rising past uid997's corner, 5.96 into the top face; GD's hbox
// is hit=0 and the model died on fly/solid-side, then on crush.
// The BALL's head side is a face too (it is on GD's unconditional ceiling list): a mini ball
// falling at vy -1 under slab uid2511 of a custom level lives with its head 9.45 px past the underside and
// dies at 10.05 (--ballceil's note). The cube's head side is not (the crush rig).
// Cube, ship and ball only: the wave dies 1.5 px from a face (lv17 t=7,040, lv20 t=839), well
// inside the tolerance, and the UFO (which lands even while rising), robot, spider and swing are
// unmeasured.
// Portal ceiling (was --portalceil, always on since 2026-09-26):
// when a mode portal turns a cube, ball, robot or spider into a flying
// body, the new body meets the ceilings the same tick. It uses the flight loop's ceiling-ride test
// (head no more than kShipLandTol past the underside at the start of the tick, past it now, not
// falling): y goes under the face and vy to 0. GD's activation pass switches the mode before its
// solid pass; the model resolves the solids with the old body and runs the portals afterwards.
// Old custom level t=8,382 (dhh and hbox lines): a cube jumping at 10.99 enters a ship
// portal with its head 7.4 into ceiling 2988 (4.99 at the start of the tick). GD's didHitHead puts
// the ship at y=225, vy 0. The model kept the halved 5.39 and rose inside the ceiling.
// Flip grace (was --flipgrace, always on since 2026-09-26):
// 004b's grace, re-landed narrow. Within kFlipGraceTicks of a gravity flip
// a cube, ball, robot or spider whose inner box would kill it on a static solid (the side test or
// the crush) is put on that solid's face instead -- its head-side face, vy 0 -- provided GD
// classifies the contact as head side (collidedWithObjectInternal: the foot moved 10 px toward the
// head, now or at the previous position, reaches no further than the solid's far edge;
// flipGraceSeat). The withdrawn 004b gate, and this flag until 2026-09-25, used the static proxy
// "centre outside the solid's y span", which also spares side entries GD kills (a custom level t=19,794).
// It is limited to static solids in frame 0: the classifier also widens by the surface's speed.
// Old custom level t=15,121: a blue pad flips the cube upright at 15,114, a yellow orb sends
// it up into the underside of the block at y 300, and on the tick its inner box crosses GD puts it
// at y=285 (head on the face) with vy 0; the model died there. The lv18 probe behind 004b (a block's
// top face, flip age 0/12/24 lives, 25/30/36 dies) is the same window.
// Ring-either (was --ringeither, always on since 2026-09-26):
// a ring is in contact when EITHER the previous or the current position
// is inside its box, as the note on the pair says the model means to accept both. The code picked
// one pair by the smaller |dx| + |dy| and tested only that one, so a pair that is nearer in x but
// out in y hid a pair that is in (the picked pair is still what g_nearOrb reads). Old custom level t=9,464: a cube pressing at 9,463 under
// a grid of gravity rings; GD fires uid3312, 0.47 px inside in y at the current position. The
// model picked the previous pair (out in y by 0.73, nearer in x) and fired nothing until 9,466.
// The four cells, injected against one lone yellow ring with a press at P and the button held (the
// model's `pre` is row P, `now` row P+1). Cube, ring 4884 of an old custom level, box 33.0 in y
// (33.2 does not fire with P and P+1 both there): P out 33.30 / P+1 in 32.72 fires at P+1; P in
// 32.70 / P+1 out 33.25 fires at P+1; both in fires, both out does not; out at P and P+1 and in at
// P+2 fires at P+2 (the held contact path). Ship, ring 357 of another custom level, crossing the x edge: P out
// 33.2 / P+1 in 31.9 fires; P in 32.8 / P+1 out 34.1 fires; out at P and P+1 and in at P+2 does NOT
// fire -- the contact path is closed in flight, so the ship's P+1 case is the press path's. Either
// row is a contact for the press, in both modes; a later entry while held is a ground mode's only.
// Ball buffer (was --ballbuffer, always on since 2026-09-26):
// a grounded ball taps on a held button whose press
// nothing has consumed -- GD's m_jumpBuffered (+0x985) is set by the press and cleared only by a tap,
// a release and the other consumers (State::holdDead tracks exactly that under --holdlatch), and
// updateJump's ball arm reads it, not a fresh edge. So a press made in the air and held through the
// landing flips the ball on the tick after it lands. Old custom level t=5,441..5,444: a ship
// turns ball at 5,439, the press comes at 5,441 in the air, the ball lands at 5,443 and GD flips at
// 5,444 (vy 3.354); the model, gated on the edge, sat on the floor. The same shape is the largest
// family of the 2026-09-25 pre-slope re-collection (five custom levels).
// The release does not cancel it on the tick it lands: a custom level t=8,930..8,933 presses in the air,
// releases on the landing tick 8,932, and GD still flips at 8,933 -- updateJump runs before the
// buttons phase that takes the release, so the gate reads the previous tick's button (s.jumpBuf),
// as the cube's already does. The flip is updateJump's, before the move, so y moves this tick.
// Grow-away (was --growaway, always on since 2026-09-26):
// a grounded body that a size portal makes bigger is lifted onto its floor
// with the new half only if it is not moving away from that floor (vy along gravity <= 0). Old
// custom level t=10,785..10,786 (three kitref episodes): a mini ball turns ship on a block
// (y 219 = top 210 + 9), thrusts on the held press and grows the next tick; GD leaves it at 219.03,
// 6 px into the block, climbing (vy 0.127, 0.235, ...). The model lifted it to 225, which put its
// head on the ceiling at 240. Read as: GD's re-seat of a grown body is its landing, which a body
// climbing away does not take. A ship that grows at rest IS lifted (lv12 t=3,286, lv15 t=7,026 --
// a mode-only rule "never lift a ship" broke both), as are the cube and the UFO.
// The cells the 2026-09-25 kits hold on their own (grow ticks read off the dumps):
//   UFO   at rest -> lifted (two custom levels, t=6,348 and t=9,516); flapping away -> not (two more,
//         t=8,082 and t=7,724)
//   ship  at rest, button up -> lifted (lv12 t=3,286, lv15 t=7,026); climbing -> not (three custom levels,
//         t=10,786, t=4,200 and t=11,889). UNEXPLAINED: another custom level, t=5,974, a ship pressed
//         the tick before and still at vy 0 on the grow row, is NOT lifted either. A button gate
//         was tried and did nothing: in the model the press takes effect a row later, as GD's
//         thrust does (vy 0.108 at 5,975), so neither side has the button down on the grow row.
//   cube, ball: at rest only so far; cube and ball moving away while growing are unmeasured.
// Size-press (was --sizepress, always on since 2026-09-26):
// a grounded cube or ball that grows (mini -> normal) on the tick of a
// press jumps or taps with the NEW size's value, and the cube is lifted onto its floor first. GD
// grows in the collision pass and handles the button in the buttons phase after it; the model
// jumped or tapped with the mini value and then grew. Rig (a mini body on a block row, the press
// on the grow tick G): cube GD y 159 -> 165 on G with vy 11.18 (model 159, 8.944); ball GD vy 3.354
// flipped (model 2.683). A press a tick or more earlier leaves the body in the air, not lifted, in
// both. Grow only: a press on a shrink tick is unmeasured. The cube is lifted first only where
// Multiple grow supports below lifts it on G; on one solid GD jumps from where it stands (rig
// calib_growwide, a 120-wide block under the grow: y 159 with vy 11.18 on G, 161.467 on G + 1).
// Multiple grow supports (replaces Ball-grow-lag, which was --ballgrowlag, 2026-09-26; first
// written up as a "grow seam"): a body that grows while standing on solids is lifted onto them on
// the grow tick only when its pre-grow box stands on TWO OR MORE of them -- solids whose face is at
// its foot, counted as objects -- and otherwise by the next tick's support, as on the world ground
// (Ground-grow); a body moving away by then is not lifted at all (Grow-away). It is the number of
// supports, not an edge: the same 120-wide block written twice, and two overlapping 120-wide blocks,
// both lift on G. Not the mode and not the portal either: the rigs (worker probe, mini body at rest
// on a row of 30-wide blocks, tops 150) put the box's edge on either side of a seam --
//   cube, right edge 885.33 over the 885 seam -> 165 on G; 884.52, one block -> 165 on G + 1
//   ball, right edge 2,084.89, one block -> G + 1 (what Ball-grow-lag was fitted to); 2,085.39 -> G
//   ship, 885.33 -> G; 884.52 -> G + 1, and pressed a tick before G it climbs away and is never
//   lifted (a custom level t=5,974, reproduced by placing the rig's x on the kit's)
//   the block under the portal written after the portals (higher uid) -> still G
//   one 120-wide block (scaleX 4) under the portal, no seam -> G + 1
//   left edge 851.74 back over the 855 seam -> G
// Solids only (static, their face at the foot); a moving floor under a grow is unmeasured.
// Ring buffer (was --ringbuffer, always on since 2026-09-26):
// on foot, a ring the player touches on the tick the button is released
// still fires if the press has not been spent -- the contact path (playerTouchedRing -> ringJump)
// runs in the collision pass, and GD takes the release in the buttons phase after it, so the
// button is still down for that pass (the model's s.jumpBuf, the gate the cube's buffered jump and
// the ball buffer already read). Old custom level t=11,073..11,075: a cube falling at -15 mashes
// one-tick presses past pink ring 1597; the press at 11,073 is spent on nothing (rows 11,073 and
// 11,074 are both out of the box), it is released at 11,074, and on 11,075 -- the first row in the
// box -- GD fires the ring (vy 8.05). The model, gated on this row's button, did not. Flight modes
// keep their press-only gate (the contact path is closed there).
// Ground cube jump (was --groundcubejump, always on since 2026-09-26):
// an upright cube that lands on the world ground on the tick of a
// fresh press jumps on that tick, as the robot's ground landing and the cube's block landing (lv20
// t=1,224) already do; the model's ground clamp rested it and it jumped a tick late. Measured three
// times in the 2026-09-25 re-collection, all falling cubes pressing on their landing tick:
// t=994, t=13,156 and t=17,908 -- GD's row is y=105 (the ground) with vy 11.18.
// Gravity-portal landing tap (was --gravlandtap, always on since 2026-09-26):
// a ball that lands on the tick a gravity portal fires does not tap on
// that tick. The portal fires in the collision pass (flipGravity clears m_isOnGround), the face
// the ball arrives at is then its head side, and pushButton's ball tap reads m_isOnGround in the
// buttons phase after it -- the same reason the grounded ball's tap is eaten (lv22 t=6,291). The
// model tapped at the landing (the same-tick landing taps: block, world floor, invisible ceiling)
// and let the portal halve the tap. Old custom level t=4,266: a ball resting on a block
// presses as it enters the upside-down portal uid 890; GD's row is upsideDown 1, onGround 0,
// vy 0, then +0.129 a tick. The model left at 1.677 = 3.354 / 2.
// Pad flip land (was --padflipland, always on since 2026-09-26):
// a gravity pad that flips a cube, ball, robot or spider lands it on
// a static solid it overlaps on its new floor side the same tick. GD fires pads in the activation
// pass, before the solid pass, so the solids meet the flipped body; the model's solids ran on the
// old body and the flip left it inside the block. The foot has to be past the new floor's face by
// no more than the landing reach (kLandTol) now or at the previous position -- GD's landing gate,
// foot + 10 against the face -- and the body moving toward the new floor. A body whose head only
// is in a solid above it is not landed on that solid's top (two custom levels, t=19,792 and t=17,381: a
// mini cube flipped upright under a slab, foot 23 px below the top). Old custom level
// t=986: a cube rising at 7.08 touches blue pad uid 340, which sits inside breakable block 429
// (270..300); GD's row is upsideDown 1, y=255 (underside 270 - 15), vy 0, onGround 1. The model
// flew on at 6.4 with its box 10 px into the block.
// --spentorb uid,uid,... : the rings GD fired earlier in the attempt, oldest first, seeded into the
// anchor state through noteRingFired. A ring fires once per attempt, but an anchored state starts
// with usedOrb empty, so the model re-fired a ring GD had spent just before t0. Old custom level
// six kitref episodes anchored at t=2,561 in a field of stacked yellow and gravity rings:
// GD fired a ring at 2,555..2,558 and, on the next press, the one beside it; the model fired the
// spent one again. The mod's cfg dpspentorb passes the list (ringseed).
inline std::vector<int> g_spentOrb;
// --histstat <file> (print only): after a replay, one line appended to <file> with how full the
// fixed-length histories got and how often one dropped an entry: touchRing
// (4 slots, --ringorder), portSeen (3, --portalonce), usedPad (4) and the fired-ring memory
// (usedOrb + usedOrbOld + usedOrbHist = 5, --ringonce); plus how many ring fires in the replay were
// of a ring that had already fired in it -- what a memory without a limit would have refused.
inline bool g_histStatOn = false;
inline std::string g_histStatPath;
struct HistStat {
    int touchMax = 0, touchDrop = 0, portMax = 0, portFull = 0, padMax = 0, padFull = 0;
    int orbFired = 0, orbDrop = 0, orbRefire = 0;
    // ...and pads fired a second time in the replay: usedPad forgets a pad once the player is
    // clear of it in x, where GD's latch holds for the whole attempt (cli.hpp's --spentpad note)
    int padRefire = 0;
};
inline thread_local HistStat g_hist;
inline thread_local std::vector<int> g_histFired;
inline thread_local std::vector<int> g_histPads;
inline double pinkRingRatio(int mode) {
    return mode == 2 ? 0.77 : mode == 1 ? 0.37 : mode == 3 ? 0.42 : 0.72;
}
inline double rawGravStep(bool ballLike, bool robot, double dx) {
    const double mul = speedMulForDx(dx);
    const double g = mul < 0.8 ? 0.940199 : mul < 1.0 ? 0.958199024 : mul < 1.2 ? 0.957199 : 0.961199;
    if (ballLike) return -0.958199024 * 0.6 * 0.225;
    return -(robot ? 0.9 : 1.0) * g * 0.225;
}
// --offboard <margin> (EXPERIMENT): the repair loop's own definition of a run that has left the
// playfield (repair.hpp offBoardTick: world y more than `margin` outside the recorded band at that
// tick), applied in the search. GD does not kill there; the loop credits the death there instead.
// Without it the search keeps offering arcs through the sky that the loop scores as dead on
// arrival -- lv22 t~2677, 70 rounds with nothing learnt (2026-09-19). 0 = off.
inline double g_offBoardMargin = 0.0;
inline int g_shiftDbgUid = -1;       // --shiftdbg <uid>
inline bool g_shiftDbgDone = false;

}  // namespace dp
