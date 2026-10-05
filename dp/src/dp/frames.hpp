#pragma once
#include "dp/object.hpp"
#include <atomic>

namespace dp {

// ---- MOVING GEOMETRY ------------------------------------------------------
//
// Everything above is a STATIC grid: the objrects dump is one snapshot taken at
// level entry, and the whole model treats it as the truth for all 30,000 ticks.
// That holds for lv1-18, which contain exactly ZERO moving objects
// (scripts/mechanic_census.ps1, "movable" column), and it collapses at lv19,
// where the same column reads 820 / 1,015 / 1,731 / 761 for lv19-22.
//
// The failure is not subtle. lv19's cold DP dies at x=1,459 because the static
// grid has nothing to stand on there -- while GD, replaying a working plan, has
// the player GROUNDED at (1,352, 131.6). The thing it is standing on is
// `id 470 type 0 groups=1`, which the dump lists at (1,365, **45**), i.e. below
// the floor: it rises into place before the player arrives. lv22 is the same
// shape at x=525 and it is what stops that level at x=604.
//
// Rather than implement GD's trigger system (move / rotate / toggle / spawn,
// with easing), the MOD records what actually happened: `grouptrace=1` writes
// `tick,uid,cx,cy,w,h` for every grouped object, every tick it changes. That
// makes rotation and scaling fall out for free -- they change the rect, and the
// rect is all the model ever asks for.
//
// Between two samples an object holds its last rect, and past the final sample
// it holds it forever. That is exactly right for a platform that has finished
// moving, which is the common case: the trigger fires when the object comes on
// SCREEN, so a platform is normally already in place by the time the player
// reaches it.
// `on` is GD's own toggle state (GameObject::m_isGroupDisabled). A toggle does
// NOT move the object, so a recorder that watched positions alone would report
// a wall that never changes -- which is exactly what lv19 x=27,705 looked like:
// two 30x30 blocks filling the only 60 px gap in a wall the player flies
// straight through.
// ---- ROTATED GAMEPLAY (id 2900) --------------------------------------------
//
// GD 2.2 turns the whole gameplay frame: the world does not move, the player's
// travel direction and gravity do. lv22 has 20 of these (no other level in the
// suite has any, so all of this is inert there).
//
// Measured on lv22 with the reference replay (2026-08-13):
//   - `rot` is the ABSOLUTE screen angle, not a delta. Eight of lv22's twenty
//     are rot=0, i.e. "turn it back".
//   - travel = R(-rot) * xhat:  0 -> +X,  90 -> -Y,  180 -> -X,  270 -> +Y.
//     uid 1343 (2265,316, rot=90): from t=1,747 x freezes at 2,266.5 and y
//     falls 1.2982/tick (= the section's dx). uid 6286 (16125,489, rot=-90):
//     x freezes at 16,131 and y RISES 1.298/tick.
//   - it fires on the tick AFTER the player's forward coordinate crosses the
//     object's -- the same crossing+1 rule every autonomous trigger uses. The
//     boxes overlap for ~15 ticks before that and nothing happens.
//   - the world VELOCITY carries over: the forward speed becomes the new
//     perpendicular one, and the old perpendicular is dropped (the forward
//     speed is the section's, not a free variable).
//
// The model keeps its state in the CURRENT frame's coordinates, so every
// physics rule stays written as "x is the clock, y is height"; what changes is
// the geometry handed to it. (u,v) = R(-rot)*(X,Y), which for 90-degree
// multiples is a pure index swap with signs -- an AABB stays an AABB with
// hw/hh swapped.
struct RotTrig {
    double cx, cy;     // WORLD position of the trigger
    int frame;         // rot/90 mod 4
    // ONE SHOT, like every other trigger GD activates from the level: the tick
    // it fired, -1 while it is still live. Measured on lv22 (2026-08-13): the
    // player leaves the first rotated section at world x=2,143.5 heading +X and
    // re-crosses uid 1343 (2265,316) at t=1,911 -- injected back up to y=315.9,
    // i.e. INSIDE the trigger's own box -- and GD does nothing at all.
    // Without this the model turns again, exits at 2,143.5, walks back to
    // 2,265, turns again: a closed loop the frontier can never leave (x pinned
    // at <=2,266 for 1,000 ticks). It only looked like progress before because
    // the broken frame floor dropped the player 700 px out of the section.
    int firedT = -1;
    // A separate one-shot for the reverse run (a 2900 pointing at the same
    // frame). **Must NOT be shared with firedT**: if shared, a trigger consumed
    // at the entrance as a reverse-run toggle can no longer be used later as its
    // real frame change. Measured 2026-08-15: lv22's cold run could no longer
    // get out of x=2,343 (the old exe was at 2,441 by iter 11).
    int revT = -1;
    // The gravity direction this trigger sets (from mvdir), in the model's local
    // convention. -1 = an old export without the column, in which case flip is
    // carried over as before.
    int setFlip = -1;
    // The reverse run this trigger sets (from gnddir). -1 = an old export
    // without the column; only then does it fall back to the old "toggle if it
    // is the same frame" rule.
    int setRev = -1;
    // objrects' uid. Used only to match --spentrot (the driver naming the 2900s
    // that had already fired before the anchor).
    int uid = -1;
    // Effective multiplier on vy [2026-08-19]: m_velocityModY(583, member
    // default 0.0) if m_editVelocity(169) is set, 1.0 (pass-through) if not.
    // Folded at parse time. An old export (no column) falls back to 1.0 -- lv22
    // is the only level with a 2900, and its objrects was re-dumped the same day.
    float vmodY = 1.0f;
    // m_overrideVelocity(584). When set, vmodY is an absolute assignment, not a
    // multiplier. lv22 has no instance (carried only).
    uint8_t ovrVel = 0;
    // The object's RAW rotation and flipX, kept because the queue's sort order
    // is a function of them and of nothing else. `frame` above cannot serve:
    // it is overridden from gnddir, and GD's own sort reads the rotation.
    // `determineSlopeDirection` truncates the rotation to an int before its
    // exact comparisons against 0 / +-180 / 90 / 270, so a non-cardinal
    // rotation matches no branch and the direction stays x-ascending.
    double rawRot = 0.0;
    uint8_t flipX = 0;
    // The raw gnddir, for the reverse predicate. The frame table above folds
    // this into (frame, setRev) through a FITTED mapping; the queue needs the
    // value itself, because GD's reverse flag is the pure predicate
    // `gnddir - 2 <u 2` (i.e. gnddir is 2 or 3) and no mapping is involved.
    int gndDir = 0;
};
inline std::vector<RotTrig> g_rotTrig;
// --rotwatch <lo>,<hi>: print, per search tick in [lo, hi], the rotations the
// search's stepKid actually applied (uid, frame before/after, children). Print
// only: applyRotation writes the uid it adopted into the thread-local below
// when the window is set, and nothing reads it for a decision. -1 = off.
inline long long g_rotWatchLo = -1, g_rotWatchHi = -1;
inline thread_local int g_rotWatchUid = -1;
// --qfoldwatch <lo>,<hi>: print, per search tick in [lo, hi], how wide the
// frontier is and how many rotation-queue states (rotSpent, rotChan, rotRev)
// the dedupe merged away. keyOf does not include the queue, so two children
// that differ only in it land in one cell. Print only. -1 = off.
inline long long g_qfoldLo = -1, g_qfoldHi = -1;
// --rotqtoggle: a queue entry whose object sits in a group a touched Toggle
// (1049, togon=0) has switched off is consumed without firing -- the cursor
// advances, no channel switch, no rotation. GD does exactly this: toggleGroup
// (0x223bc0) sets the object's +0x28e when its toggle counter goes negative,
// and checkSpawnObjects (0x21aad8) then skips triggerObject while still moving
// the cursor on (0x21ab62). Per state, because the touch is: the off/on
// masks below are over State::trig. Off by default.
inline bool g_rotQToggle = false;
// ...its masks, built once the queue and the touch boxes are both loaded
// (cli.hpp): for queue entry k (and pre-queue trigger k), the touch-box bits
// whose Toggle switches that object's group off (togOn 0) or on (togOn 1). An
// entry counts as disabled for a state when it has entered more "off" boxes
// than "on" ones -- GD's per-object toggle counter below zero.
inline std::vector<uint32_t> g_rotQOff, g_rotQOn, g_rotTrigOff, g_rotTrigOn;
// --touchseed uid:tick,...: touch boxes the anchored attempt had already
// entered by t0, from the mod's geometric test on its own recorded positions.
// OR-ed into the anchor state's trig/fireB without claiming ownership, so the
// recording-derived seeding still runs. Exists because GD's touch recorder
// (activatedByPlayer) never sees a touch Toggle: 0 of lv22's three.
inline std::string g_touchSeedArg;
// --touchcensus: print every touch-box entry markTouched makes (step.hpp),
// labelled by which test admitted it -- the player's y this tick, or only the
// `preY` its callers pass (the parent state's y, which the comment there calls
// "before this tick's button effects"). For --replay: a search would print once
// per child, from the worker threads. Print only. Off by default.
inline bool g_touchCensus = false;
// ...and which of stepOne's y-moving branches this tick took, so the census can
// name an entry by the model's own path instead of a threshold on dy: 1 = the
// spider's tap warp, 2 = a spider orb, 4 = a teleport portal. Written only under
// --touchcensus; thread_local because phase 1 steps the layer in parallel.
// g_tcBranchP1 is p1's value, saved by stepBoth before p2's stepOne overwrites it.
inline thread_local int g_tcBranch = 0;
inline thread_local int g_tcBranchP1 = 0;
// --touchprey=button: the y markTouched's preY test reads is p1's y before this
// tick's button effects, carried out of stepOne -- the spider tap warp's
// pre-warp y -- or this tick's own y when no button effect moved it
// (g_preBtnSet/g_preBtnY, written by the warp branch). The old reading, the
// parent state's y (--touchprey=parent), is a tick too early: the cross of
// "this tick's x" with "last tick's y" fired lv20's uid5625 on a position the
// player never occupied, moving a saw 113.7 px onto a lane GD leaves open.
// On since 2026-09-21; always on since the flag
// clean-up, which removed --touchprey.
// --rotpretap: the same tick order for a 2900. GD fires a rotation
// in the collision pass, BEFORE the tick's button, so the trigger's
// perpendicular-window test (applyRotation, step.hpp) sees the spider where it
// stood before its tap warp. The model ran the tap first and tested the warped
// position. Measured on lv22 (2026-09-21), a plan both sides replay alike up to
// t=1,816: GD fires uid1215 (2143.5,225) with the player at (2143.5, 224.3) and
// then warps it in frame 0 to y=316.5; the model had already warped it in frame
// 1 to world x=2,926.5, read |dv| = 783 > kRotPerpWin, dropped the rotation and
// planned on in a frame GD had left. applyRotation already restores the pre-tap
// y once a rotation has fired (its re-tap); this makes the DECISION use it too.
// ON by default since 2026-09-21, as ONE unit with
// --spiderstrip: this alone still left a wrong landing, the pair made the
// target plan match the game for 400 ticks. Both always on since the flag
// clean-up.
// --spiderstrip: the spider's teleport search in frame 0 uses
// GD's own rects instead of a window centred on the player. Read off
// spiderTestJumpInternal's two queries (cfg hitboxtrace=1, `srect:`/`drect:`)
// on lv22, both frame-0 taps of one replay (t=1,816 on the rotation tick and
// t=1,854), with x the player's position on the tap's tick:
//   srect o.x = x        s.x = 14.5   -> solids  [x, x + 14.5]
//   drect o.x = x - 4    s.x = 8.0    -> hazards [x - 4, x + 4]
// The centred window was |x - cx| <= hw + 14.5 for solids (lv21's seven
// injections pin 29.5 -- on the LEADING side, where the two agree) and
// hw + 13.5 for hazards. Behind the player it reached 14.5 px too far: at
// t=1,816 it took uid1212 (x 2,100..2,130, the player at 2,143.5) and landed
// the spider at 226.5 where GD, which never looks behind, lands at 316.5.
// Forward travel at normal size only: reverse travel and the mini are not
// measured, and keep the old window.
// ON by default since 2026-09-21, paired with
// --rotpretap (see there).
// --ceilpush (default off): seat a body that overlaps a ceiling ramp's rect at
// GD's ceiling seat even where the ramp window has dropped the ramp. See the
// branch beside the push-out gate in step.hpp.
inline bool g_ceilPush = false;
// A rule below marked "always on since 2026-10" was a switch until the 2026-10 slope clean-up
// (cli.hpp, kSlopeAlwaysOn): the release turned it on by default, and as a slope rule it then
// lost the switch, so only its on behaviour is left. Its --name is kept here to be grepped for.
// --flipceilride (always on since 2026-10): a flipped cube riding the gravity-facing side of a
// ceiling ramp -- its floor -- keeps riding. See the hang snap in step.hpp.
// --flipceilland (always on since 2026-10): ...and a single flipped cube LANDING on that side
// stops, as the dual one already does. See the landing zeroing beside the hang snap in step.hpp.
// --seattoldual (always on since 2026-10): the falling cube's 1.0 px seat snap on a downhill
// ramp applies in a dual section too. See seatTolOk in step.hpp.
// --flipceilend (always on since 2026-10): a flipped cube's ride on a ceiling ramp's floor side
// ends the way GD's does -- no x window on a continuing contact, and the tick whose
// seat repeats the last one is dropped. See the window in step.hpp's flipped branch.
// --relkeepblock (always on since 2026-10): the downhill release keeps a solid's pin from the
// same tick. See the downhill release in step.hpp.
// --lawfrombelow (always on since 2026-10): a ramp contact the window accepts and the "came
// from below" gate rejects still goes to GD's two-stage law. See that gate in step.hpp.
// --shrinkpress (on by default since 2026-10): a grounded cube jump on the tick a mini portal fires
// is the mini jump. See the size portal in step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --repellpolarity (on by default since 2026-10): the dual ball bounce fires only between bodies of
// the same polarity, GD's own gate, in place of "moving toward the partner". See the bounce
// in step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --rampjumptick (always on since 2026-10): a jump taken on a ramp reads the ride's ramp factor
// one tick further on than an exit on the same tick. See the on-ramp bonus in step.hpp.
// --ufobanddir (always on since 2026-10): a UFO's above-the-seat landing band on a ramp opens
// only when the ramp falls away in the body's own travel direction (GD's bVar22). See the
// landAllow note in step.hpp.
// --flyhazafterslope (always on since 2026-10): a ship or UFO the ramp pass moved is tested
// against the hazards again where the ramps left it. See the end of the ramp pass in step.hpp.
// --slopetopveto (always on since 2026-10): GD's first m_wasOnSlope veto -- a body climbing the
// ramp it rides skips a ramp of the same top-ness that falls away. See above the ramp loop in
// step.hpp.
// --gravportalseat (on by default since 2026-10): a gravity portal is tested at the body's
// post-collision y (a ramp's seat included) rather than the free y. See the portal pass's yPort in
// step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --slopevelexact (always on since 2026-10): a ramp's exit velocity from GD's own formula
// instead of the three rounded anchors. See slopeExitVy in slopes.hpp.
// --undernudge (always on since 2026-10): a flying body the game's slope law takes from the
// underside gets the underside's own vy := min(vy, -2) in its frame when bVar22 holds. See the law
// seat's application in step.hpp.
// --portalslopeorder (always on since 2026-10): a gravity portal with a smaller uid than the
// ramp the body touches on the same tick fires before that ramp, as it does in the game's object
// loop. See the end of stepOne in step.hpp.
// --seatkeepfirst (always on since 2026-10): a ramp falling away in the body's travel, met
// after another ramp has already seated the body on this tick, takes it only from strictly below
// its seat (no acquisition tolerance), so the earlier ramp stays the one the body rides and leaves.
// See the ramp loop in step.hpp.
// --upceilband (always on since 2026-10): a flying body held up under a ceiling ramp whose
// bVar22 is set gets no band to be lifted onto the line from below. See the upright underside
// branch in step.hpp.
// --lawunderrelease (always on since 2026-10): --slopelaw's underside seat of an upright flying
// body arms the same release as the ceiling push-down. See the law seat in step.hpp.
// --dualspeedp2 (on by default since 2026-10): a speed portal the second body of a dual touches
// sets the pair's speed. See the merge in stepBoth (fixup.hpp).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --speedafterseat (always on since 2026-10): a speed portal with a larger uid than the ramp
// that seated the body on this tick is judged at the seated y. See the speed portals in step.hpp.
// --rideseatrepeat (always on since 2026-10): a continuing upright ride keeps the ramp past its
// x window until the clamped seat repeats. See sampleAt in step.hpp.
// --slopeinset (always on since 2026-10): a fresh contact with a ceiling ramp from below needs
// the player's rect more than 1 px into the ramp's rect. See the ceiling press and upceil in
// step.hpp.
// --preslopegate (always on since 2026-10): the game's preSlopeCollision gate in front of every
// ramp but the one the body is on. See the top of the ramp loop in step.hpp.
// --exitbeatsvy (always on since 2026-10): the uphill exit launch only when it beats the body's
// own vy on its side. See the launch in step.hpp.
// --wavepadhalf (on by default since 2026-10): pads meet a mini wave with its 6x6 object rect. See
// the pad loop in step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --dualremirror (on by default since 2026-10): a mode portal re-mirrors the body that switched
// into the partner's mode, which can be p1. See the end of stepBoth in fixup.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --undersidev3 (always on since 2026-10): the seat's underside clamp, min(vy,0) upright /
// max(vy,0) flipped, for every mode. See the ride's velocity chain in step.hpp.
// --upceilrepeat (always on since 2026-10): the ceiling push-out drops a continuing contact
// whose seat repeats the last one. See the upceil clamp in step.hpp.
// --ballceilveto (always on since 2026-10): the two ball ceilings ask the landing's slope veto.
// See
// --ballceilflip in step.hpp.
// --waverotactual (on by default since 2026-10): the wave's sprite eases toward the angle it
// actually moved at. See the end of stepOne in step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --padclearsride (always on since 2026-10): a pad other than the gravity pad ends the ride it
// fires on. See the pad loop in step.hpp.
// --swingpushflight (on by default since 2026-10, with --swingpushtol): the swing's face reach is
// the flight 6.0. See the swing push-out in step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ceillimv4 (always on since 2026-10): the ceiling press places the release nudge on every
// contact, not only from a rising body. See slope/ceillim in step.hpp.
// --bandcarryvy (on by default since 2026-10): a ship carried up by the recorded band's floor
// thrusts on top of its own vy. See the ship's vp in step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --waveslopelowend (always on since 2026-10): the wave's corner-sampled ramp kill skips a
// sample past the ramp's thin end. See the wave branch in step.hpp.
// A flipped body seated on a ceiling ramp's gravity side lands only below hitGround's 5.0. See the
// hang seat in step.hpp. On by default; --no-hanglandgate restores the old grounding
// (--hanglandgate is still accepted and changes nothing).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --wavegrowclamp (on by default since 2026-10): a grounded wave that a mode portal makes a bigger
// body grows from its resting height (kWaveClamp), not from its 5.0 hitbox half. See the portal
// re-seat in step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --portalspidertap (on by default since 2026-10): a mode portal hands the button to a spider -- a
// fresh press teleports it on the portal's tick if grounded, a press it cannot use yet is kept
// while held (State::pSpiderTap). See the portal pass in step.hpp.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --spiderairbuffer (on by default since 2026-10): a spider's press made in the air is kept while
// held and teleports it on the first grounded tick (State::pSpiderTap, calib_spiderairhold).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --preslopesolid (always on since 2026-10): the wall / strip
// preSlopeCollision hits is resolved as a 1-px solid (land / head / crush). See the preslopegate
// block in step.hpp.
// --ceiltaponce (on by default since 2026-10): the ball's same-tick tap at the invisible ceiling is
// skipped when this tick already took an impulse, as the floor-side mirror always was (one press,
// one tap).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ceilcubejump: a flipped cube that lands on the invisible ceiling (the band's) with a press
// not yet spent jumps on that landing tick, as the upright cube does on the ground plane.
// On by default since 2026-10 (--no-ceilcubejump turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ceilcontv4: --ceilcont's seat past a ceiling ramp's span also takes the release nudge V4
// (-2.0 along gravity on the underside), as the in-span branch does through slopeNudge (step.hpp,
// slope/ceilcont). On by default since 2026-10 (--no-ceilcontv4 turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --shipceiltol4: a flipped ship's continuing ceiling-ramp ride takes GD's tolerance 4 while its
// band is open, as the cube's does (step.hpp, contRide). On by default since 2026-10
// (--no-shipceiltol4 turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --upceilfreshend: an upright flight body that meets a descending ceiling ramp's low end from
// below for the first time -- its contact point already past the span, its centre within pH of
// the end -- is seated at the end's flat (line(x1) - pH) as a continuing one is (step.hpp, contU).
// GD acquires on the box overlapping the ramp past its 1-px inset, not on m_wasOnSlope. Custom
// custom level A t=9,826: a ship rising under ceiling ramp uid4181 (x 14,970..15,000, line
// 300 -> 270), contact point 2.75 px past x1, head 1.49 px into the ramp: GD y 255.000 exactly
// (the model's own limit), the model flew on 7.5 px to the next block. On by default
// (--no-upceilfreshend turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --uforideflap: a UFO flapping off a ramp it rode last tick (rising in the travel direction)
// takes updateJump's slope term with the ride's velocity, min(target + v/2, 1.4 x target), in
// place of the constant kUfoRampFlap, mini included (step.hpp, the UFO flap). On by default
// (--no-uforideflap turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --lawunderstack: a flying body pushed down by one ceiling ramp's underside and acquired on the
// same tick by the next one's (a law seat lower still) takes the lower seat, as GD's collision
// pass leaves the last write that intrudes (step.hpp, the law seat). On by default
// (--no-lawunderstack turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ridenoblockrest: a ship riding a floor ramp upright is not given the resting slide by a
// block the support scan finds beside the ramp's end; its ride vy carries on (step.hpp, after
// the support scan). On by default (--no-ridenoblockrest turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --groundflushflat: a downhill ramp whose low end lies on the ground ends an upright cube's
// ride by the flush flat's window (kStickGap), as a block's face there does (step.hpp, the
// cube's extrapolated window). On by default (--no-groundflushflat turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --lawfreeclamp: the law seat reads which side of a ramp the body is on against the line
// clamped to the ramp's span, as the seat itself is (step.hpp, the law's freeSide). On by
// default (--no-lawfreeclamp turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --lawseatlaunch: a ship the law seat held on its gravity side, on a ramp climbing in the travel,
// leaves it with the ride's uphill launch scaled by the seat's age (step.hpp, the seat and the end
// of the step; State::lawSv). On by default (--no-lawseatlaunch turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --flipcubecrush: a flipped cube meeting a floor ramp's top afresh takes the underside rule
// (dies more than 2 px past the seat outside the flip grace, else left unmoved) instead of a
// landing (step.hpp, the ride's seat). On by default (--no-flipcubecrush turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --balltapstep: an upright ball tapping off a ramp that falls in the travel takes the seat's
// step on the tap tick, as the cube's jump does (step.hpp, the impulse tick's seat). On by
// default (--no-balltapstep turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ballunderv3: a ball meeting a ceiling ramp's underside takes V3 alone (no -2.000 push
// velocity), and an underside law seat arms the ball's release as it does the flight modes'
// (step.hpp, upceil and the law seat). On by default (--no-ballunderv3 turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --jumpafterland: a cube jumping off its support on the tick its box first enters a solid's
// face within the landing tolerance is put on that face first, keeping the jump (step.hpp, the
// solid landing). On by default (--no-jumpafterland turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --dualflipclock: the second body of a dual keeps its own gravity-flip and mode-switch clocks
// (State::flipT2 / modeT2, swapped by swapHalves), started when it is born; without it the
// second body read the first body's. On by default (--no-dualflipclock turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --spiderbandsup: a spider resting on the band's face (a wall band) counts as supported at the
// head of the tick, so its tap is taken (step.hpp, the cube family's support scan). On by
// default (--no-spiderbandsup turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --thinsupport (always on since 2026-10): the support scan counts the
// 1-px strip along a ramp's flat edge, so a body standing on it is grounded for the press
// (step.hpp).
// --shipslopecap (always on since 2026-10): a ship's outline around a spiked ramp as measured
// on the calib_slopespike ship rigs (either gravity) -- caps ph - 1 past the box, window sx0 - ph
// .. sx1 + ph, the flat side following the inner box (step.hpp, the spiked-ramp kill).
// --waveslopecap (always on since 2026-10): an upright wave's caps past a ramp's ends sit ph -
// 1 past the box's edge, not at the line's ends (step.hpp, the spiked-ramp kill;
// calib_slopespike_wave).
// --hangrunoff (always on since 2026-10): a ship hanging under a ramp whose contact point has
// run off the end it travels toward no longer holds the launch-suppression window (step.hpp,
// rampWindowHere).
// --dualcubeband (on by default since 2026-10): in a dual, a cube or robot stands on the band's
// floor (upright) and ceiling (flipped) as the ball and spider do (step.hpp, ceilHere / floorHere).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --dualband (default off): the band inside a dual is updateDualGround's, for either body's mode
// portal. GD (toggleDualMode, updateDualGround, animateInDualGroundNew, 2.2081):
//   * the portal that turns the dual ON is kept at layer+0x408 (a dual portal met while the dual is
//     already on returns early and changes nothing), and every later band is placed from ITS y;
//   * a mode portal taken by EITHER body calls updateDualGround, whose height is the larger of the
//     new mode's and the other body's class (240 reads as 270).
// So (1) p2's own mode portal writes the pair's band -- stepBoth kept only p1's -- with H the max of
// both bodies' dual heights (step.hpp, the portal band; fixup.hpp stepBoth), and (2) an anchor inside
// a dual seeds the reference y from the band track's rows, which the portal replay cannot: a dual
// portal in a group is not in L.portals, so the seed left it at 0 and the next in-dual mode portal
// wrote bandFor(0, 300) = [90,390] (cli.hpp, the anchor's band seed). Measured on custom level C
// t=10,570: the dual was turned on at x=6,915 by uid 36984 (cy 465, group 1); GD's band read
// [330,600] with H 270 and [300,600] with H 300, both bandFor(465, H); the model wrote [90,390].
inline bool g_dualBand = false;
// ...its two per-step channels, thread_local because phase 1 steps the layer in parallel.
// g_bandPortalWrote: set by the portal band write in stepOne (step.hpp), so stepBoth can tell a
// band the second half WROTE from the one it only carried -- the band's tween (bandAnim) moves
// every tick, so comparing fields cannot. g_dualOtherMode: the first body's FINISHED mode, handed
// to the second half's height (GD runs p1's collisions first); -1 = read s.mode2 as before.
inline thread_local bool g_bandPortalWrote = false;
inline thread_local int g_dualOtherMode = -1;
// --dualslide (default off): each dual body keeps its own DART SLIDE arm (State::slideT2 beside
// slideT; modifiers.hpp slideBoxTouch). GD's is per player: collisionCheckObjects sets THAT body's
// +0xb78 to 2 when it touches an id-1755 box (0x215ab7), PlayerObject::update steps it down, and
// collidedWithObjectInternal gives a wave the solid push-out only while it is >= 1. The model had one
// arm for the pair, so the second body's step read the first's. Measured on custom level C
// t=10,677-10,680 (hitboxtrace): p1, inside the lower lane's 1755 boxes, is pushed out of block
// 56076's top at 0.01 px; p2, in the upper lane with no 1755, sinks 0.25-3.48 px into block 56066's
// top with collidedWithObject returning 0 and dies at 5.1 px -- where the model, on p1's arm, seated
// p2 at 575 (step.hpp swapHalves, fixup.hpp stepBoth, the dual entry).
inline bool g_dualSlide = false;
// --dualdash: each dual body keeps its own dash (State::dashing2 / dashSlope2): GD's m_isDashing is
// per player, and with one shared field the second body ran the first body's dash (step.hpp
// swapHalves, fixup.hpp stepBoth, the dual entry, search_key.hpp).
// On by default since 2026-10 (--no-dualdash turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --hangseatrepeat (always on since 2026-10): a flipped cube's or robot's hang ends on the tick
// its seat repeats, as a floor ride does under --rideseatrepeat (step.hpp, the hang branch of
// sampleAt).
// --sloperot (always on since 2026-10): a cube seated on a ramp turns toward the ramp's own
// angle, and the ramp's launch spins it the other way at half the take-off size (step.hpp, the
// cube's spin).
// --portalpressorder (always on since 2026-10): a mode portal met while a ramp presses the body
// keeps the press when the ramp comes first in uid order (step.hpp, the free step after a mode
// portal).
// --ceilreleaseball (always on since 2026-10): a ball leaving a ceiling ramp's press gets the
// release's launch as the flying modes do (step.hpp, ceil/release).
// --undercrush (always on since 2026-10): a cube, robot or spider that takes a ramp's underside
// outside the 0.1 s grace is not seated -- it dies if it is more than 2 px past the seat, and is
// left where it is otherwise (step.hpp, the upright ceiling push and the --slopelaw seat).
// --ufoslopekill (always on since 2026-10): a UFO dies on the ship's measured outline around a
// spiked ramp (the sloped side at ph, and with --shipslopecap its caps and flat side), instead of
// by the box overlap the unmeasured modes keep (step.hpp, the spiked-ramp kill).
// --spikebottom (always on since 2026-10): a spiked ramp's flat-edge
// strip and wall are the same 1-px solid a plain ramp's are (preSlopeCollision does not read
// m_slopeIsHazard); the ramp still never seats the body (step.hpp, the preslopegate block and the
// support scan).
// --balltapexit (always on since 2026-10): a ball's fresh press on the tick it leaves a ramp
// with the uphill launch does not tap -- the launch has taken the body off the ground before the
// buttons run, so it keeps its gravity and the launch's velocity (step.hpp, the top of stepOne).
// --extseatrepeat (always on since 2026-10): an upright cube riding a downhill ramp's line past
// its low end keeps the ride while the seat moves and leaves on the tick the clamped seat repeats,
// the --rideseatrepeat test, instead of on the first tick past the extC window (step.hpp, the extC
// branch of sampleAt).
// --tponce (on by default since 2026-10): a teleport portal fires once per contact -- not again
// while the body is still inside the box it was inside at the previous tick (GD's activatedByPlayer
// mark; step.hpp, the portal pass).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --tplatch (on by default since 2026-10): ...and once per ATTEMPT, as GD's mark is: a teleport
// portal takes a bit of State::portalLatch beside the gravity portals (level_loader.hpp numbers
// them, static and dyn) and does not fire again once set, however the body comes back into its box
// (step.hpp, the portal pass).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --teleoldpos: on a teleport tick, a portal with a smaller uid than the teleport is judged (and
// fires) at the position the teleport found the player, as GD's ascending-uid pass reaches it first,
// instead of being skipped (step.hpp, the portal pass).
// On by default since 2026-10 (--no-teleoldpos turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --stripvetoorder (OFF by default): a ramp's 1-px top / bottom strip (--preslopesolid) is vetoed
// only by the ramps GD's slope map holds at that point -- the one last ridden and those with a
// smaller uid -- not by every ramp in the level (step.hpp, thinSolid). Off because its model-only check A/B is
// negative: on its own it broke ten custom level A episodes (a ramp that takes the body must veto its own
// strip, --stripownacq), and with --stripownacq it still breaks two of custom level F (t=6,513, a falling
// ball GD puts on a ceiling ramp's top strip) that nothing told apart from the witness; with it off
// the rest of the branch fixes the same 22 and breaks none, the witness (custom level B, iteration 33) included.
inline bool g_stripVetoOrder = false;
// --ballungroundramp (on by default; --no-ballungroundramp is the off arm): a ship / UFO that a ramp
// with a smaller uid than the ball portal seated is not on the ground on the tick it becomes a ball
// (step.hpp, the mode change in the portal pass).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --contseatclamp (on by default; --no-contseatclamp is the off arm): a ship's CONTINUING ride along a
// ceiling ramp sits one tick on the corner where it passes onto the next same-sign ramp, as the old
// ramp's clamped seat puts it in GD (step.hpp, the flipped ceiling pin).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --bandfloorunstick (on by default; --no-bandfloorunstick is the off arm): a grounded ship the flying
// band's floor puts back on it is off the ramp it was riding -- the slope stick does not pull it
// back down the line on the same tick (step.hpp, fly/bandcarry and stickHere).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ceilgracerider (on by default; --no-ceilgracerider is the off arm): the flipped ceiling push's
// exit-side grace (|dx| past the ramp's end) is for a body that rode the ramp last tick only
// (step.hpp, the flipped ceiling pin).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --lawuplaunch (on by default; --no-lawuplaunch is the off arm): a --slopelaw seat dropped as a
// repeat on a ramp that rises in the travel launches the body as a ride's uphill exit does
// (step.hpp, the --lawcontact release).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --dualseatt (on by default; --no-dualseatt is the off arm): each body of a dual keeps its own law-
// seat count (State::seatT2, swapHalves, the merge in fixup.hpp); shared, the first body's step wrote
// it over the second's.
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ceilfreshrect (on by default; --no-ceilfreshrect is the off arm): the flipped ceiling push takes
// a fresh contact only through GD's inset-rect test, as the ramp window does (step.hpp, the flipped
// ceiling pin).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ceilpushreach (on by default; --no-ceilpushreach is the off arm): a fresh flipped ceiling push
// reaches a ramp by box overlap, as GD's acquisition does, not by the rotated contact point
// (step.hpp, the flipped ceiling pin).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --stripownacq (on by default; --no-stripownacq is the off arm): under --stripvetoorder a ramp's
// strip is also vetoed by its own ramp when that ramp takes the body on this tick (step.hpp,
// thinSolid's veto).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --escufo (on by default since 2026-10): escapee-prune leaves the UFO alone, as it does the ship
// and the wave (step.hpp, where the measurement is).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --spentdyn (on by default since 2026-10): --spentorb also names rings in dyn (cli.hpp, the
// seeding).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --radreach (on by default since 2026-10): the cube branch's hazard x window reaches a circle's
// radius when it is wider than the object's rect (step.hpp, the hazard walk).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --uforot (on by default since 2026-10): the UFO's tilt follows GD's updateShipRotation UFO branch
// (step.hpp, the rotation update, where the constants and the measurement are).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --spdonce (on by default since 2026-10): a speed portal does not fire again while the body stays
// in its box (step.hpp, the speed portal pass).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --dualorbflip (on by default since 2026-10): a ring that flips a dual body's gravity couples to
// the partner the way a gravity portal does (step.hpp, after the ring pass; fixup.hpp stepBoth).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ufolawflap (always on since 2026-10): a UFO the --slopelaw seat held on a ramp rising in
// its travel the tick before flaps at GD's ramp flap, min(flap + v/2, 1.4 x flap), v the ramp's
// exit velocity (step.hpp, the seat and the UFO flap; State::lawSv; UfoModel::stepVy).
// --spiderminigd (on by default since 2026-10): a MINI spider's teleport search in frame 0 is GD's
// own too (spiderTargetGd), with the strip hung off the centre at the mini's half + 1 and the
// hazard strip at +-4 x 0.6 (step.hpp, spiderTargetY).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --spawnringjump (on by default since 2026-10, with --orbspawn): the spawn ring's own jump, given
// on the move of the tick after the press (step.hpp, the end of stepOne).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ballstake (on by default since 2026-10): the ball's spin rate is also written where GD writes
// it -- 0 on entering the ball (toggleRollMode), the ground rate off a pad (propellPlayer), on a
// speed portal (updateTimeMod) and on a size portal over a ground stake (togglePlayerScale), the
// air rate on a gravity portal (flipGravity), airborne or not -- and a ring's stake turns from the
// next tick (step.hpp, the ball branch and the portal pass).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --boostcarry (on by default since 2026-10): the velocity-limit exemption (State::boost) is kept
// through every mode and set by its writers in every mode, as GD's byte is, and a pad other than
// the red one clears it (step.hpp, where the per-tick drop was).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --dualflyring (on by default since 2026-10): a dual ship or UFO takes its fly-ring on the press's
// tick through the early path, as a single one does (step.hpp, the head of stepOne).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --dyngravseen (on by default since 2026-10): a gravity portal without a latch bit (a grouped one)
// does not fire again while the body stays in its box, through the mode portals' portSeen slots
// (step.hpp, the portal pass).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --dropfall (on by default since 2026-10): a drop ring skips the next tick's terminal clamp when
// GD's playerIsFallingBugged says "not falling" on the firing tick, flipped arm and all (step.hpp,
// the late ring loop's drop ring).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --repelland (on by default since 2026-10): the dual balls' repel is also decided after BOTH
// bodies' collisions and before either tap, as GD's checkRepellPlayer is -- which a body that lands
// on the tick its partner closes in needs (fixup.hpp stepBoth, after the second half).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --repela0c (on by default since 2026-10): the dual balls' repel decided once per tick in stepBoth
// with GD's own choice of body -- p1's +0xa0c up flips p2, else p2's up flips p1 -- from
// State::hitG, in place of the rule inside each half (and of --repelland's one case).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --spentaction (on by default since 2026-10): an --start anchor whose hist payload says the press
// is held and spent seeds action = 1 for a UFO or swing, so the first tick does not read a consumed
// press as a fresh edge (cli.hpp, the hist seeding).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --rot2 (on by default since 2026-10): each dual body keeps its own sprite angle and spin
// (State::rot2 / rotStep2 / rotNeg2), so the second body's tests against turned objects read its
// own angle (step.hpp swapHalves, fixup.hpp stepBoth).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --cubeentryspin: a cube entered in the air from the robot or the spider does not turn until a
// spin is staked (updateJump's fall stake or the usual ones); see the cube's spin block in step.hpp.
// On by default since 2026-10 (--no-cubeentryspin turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ringrotsign: a rotated ring's firing box is tested at the ring's own angle (-rot in the math
// sense), not its mirror; see the late ring loop in step.hpp.
// On by default since 2026-10 (--no-ringrotsign turns it off).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --a1clatch (on by default since 2026-10): GD's +0xa1c per body (State::a1cLatch) -- raised by the
// jump, a ring, a pad, a ramp's launch, the head-hit flip and a gameplay rotation, lowered by
// updateJump the first tick playerIsFallingBugged holds -- and a ramp's exit does not launch a
// cube, robot or spider, nor release any mode, while it is up (step.hpp, the ramp exit and the
// ceiling release).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --lawcontact (always on since 2026-10): the tick after the slope law seats an upright body on
// a floor ramp is GD's continuing contact (m_wasOnSlope): no solid's face the ramp vetoes holds the
// body, the law re-seats it within the 4 px band and drops a target equal to the last one, and a
// dropped downhill contact releases at the face's velocity (step.hpp, the law seat and the ramp
// exit).
// --padentry (on by default since 2026-10): with all four pad slots taken, a pad the body did not
// overlap at the previous tick's position fires instead of being skipped (step.hpp, the pad loop).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --forceboxdir (on by default since 2026-10): a force box (2069) pushes along its own direction --
// rotation and flipY, from calculateForceToTarget -- instead of always up (level_loader.hpp).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --ceilwallnopush (always on since 2026-10): an upright ship on a rising ceiling ramp's wall
// side (dirs {2,3,4,6} with the centre left of the box; the others travelling left, right of it) is
// not pushed by that ramp -- preSlopeCollision returns the wall first -- so neither the V-valley
// corner (ceilLimSeat's min with the neighbour) nor cornerReg applies there (step.hpp).
// --heldtolhead (always on since 2026-10): the spiked-ramp kill's head-side band. A flight mode
// holding the button, against a ramp bVar22 leaves clear and while not on a slope, dies 1 px (2
// when it was on a slope last tick) short of the line and cap (step.hpp, the spiked-ramp kill;
// 0x38ff71..).
// --heldnodown (always on since 2026-10): the descending floor ramp's +1 of the spiked-ramp
// kill is the feet side's band, which a ship holding the button does not get (step.hpp;
// 0x38fe6b-0x38fe7d).
// --capinsetgate (always on since 2026-10): the spiked-ramp kill's caps sit at the box's edge
// plus ph; the pixel the rigs measured short of that is the fresh-contact 1-px inset, which a body
// that was on a slope last tick does not face (step.hpp; 0x38fc0e-0x38fc7b).
// --ballslopekill (always on since 2026-10): a ball dies on the ship's
// outline around a spiked ramp instead of by the box overlap (step.hpp, the spiked-ramp kill).
// --flipcldbg N: print the first N states where the --flipceilland rule acts (print
// only; it says where in a search the rule takes effect).
inline std::atomic<int> g_flipLandDbgLeft{0};
// slope law: the acquisition GD actually performs, in place of --ceilpush's
// one-sided push-out. See step.hpp's site for what is measured and what is not.
// On since v0.1.4; always on since the flag clean-up.
// ship slope kill: the ship's spiked/plain ramp kill on the measured outline
// (perpendicular d = playerHalf, flat side +kCubeInner) instead of the interval
// overlap. See the note at the ramp kill in step.hpp. On since v0.1.4; always on
// since the flag clean-up.
// --killslopeside: the slope KILL test decides which half of the
// box is solid with GD's own set, {1,3,5,6} (slopeIsCeiling, read off
// collidedWithObjectInternal at 0x3921B5), for every mode -- not just the ship.
// The kill test is the last place in the tree still on the {1,3} subset: the
// ride path dropped the mode whitelist in r63 (step.hpp, 2026-08-21) on the
// grounds that "the reason was the same every time and has nothing to do with
// the player's mode", and step.hpp's ceiling-ramp branch reads ceilRampSide.
// 447 of the corpus's 3,091 type-25 slopes sit in the difference set {5,6}, so
// this is NOT a one-object change and it moves kills in BOTH directions -- a
// slope GD calls ceiling is currently judged as floor, which empties one
// interval and fills the other.
// ON by default since 2026-09-21. The enumeration came
// back with one row changed -- lv21's t=14,999 over-kill, the same rule and the
// same direction as lv20's -- and nothing moved the other way over 22 whole-run
// replays. deathref 41/47 -> 42/47 with nothing lost, cold 22/22. Always on
// since the flag clean-up.
// flipped wave slope kill: a flipped wave's ceiling-type spiked ramp kill at the
// measured perpendicular distance kWaveFlipSlopeKillD instead of playerHalf. See
// the note at the ramp kill in step.hpp. On since v0.1.4; always on since the
// flag clean-up.
// --latgap: the search does not change the input on a tick no button edge can reach --
// the tick after a mode portal from a latency-1 mode (cube/ball/wave/...) into a
// latency-2 one (ship/UFO). The emitter maps such an edge to the press one tick earlier
// (cli.hpp, the plan writer's two-pass latency), which GD and --replay apply one tick
// LATE. Measured in cold runs: lv12 t=19,034 (ball -> ship at 19,033) and lv14 t=19,638
// (cube -> ship at 19,637), both SOLVED plans whose own witness lived while --replay of
// the emitted plan followed GD to its death. On by default since 2026-10 (was opt-in).
// (The switch is gone since the 0.4.0 clean-up; its on behaviour is fixed.)
// --waveslopeside: the wave's floor-ramp kill with the two things the plain rig
// measured on 2026-09-17 (calib_slopespike_wave and _wave_mini, bisected to 0.006 px,
// positions converted to the kill tick): a MINI wave's perpendicular distance is 3.0
// (its rect's half), not the 2.0 the model used, and on a DESCENDING floor ramp the
// boundary sits exactly 1.0 px higher than on an ascending one, for both sizes and both
// slopes (normal 5.000 up / +1.000 down, mini 3.000 up / +1.000 down, on m=1 and m=0.5).
// lv21 t=14,674 is the cold run's case: a mini wave 3.54 px (perpendicular) above
// descending plain ramp 1338, killed by GD and not by the model.
// On since 2026-09-20, always on since the flag clean-up. The high-end cap was
// re-confirmed on a third object -- id1339 rotated 90, m=2 -- by a coin route on lv21 that GD
// kills at t=16,725 and the model flew 95 ticks past; with the flag the model kills on the same
// uid at the same place. Gates: the replay suite moves 38 of 1,116 traces with the reference
// tracking unchanged, a cold run clears 22/22 for +8 on lv20 and +1 on lv17, deathref keeps
// 41/47 with no reference lost, and refaudit adds no over-kill (lv19 improves from 153
// differing rows to 3).
constexpr double kWaveSlopeDescendDy = 1.0;
// ...and past the high end the kill is flat at objMaxY + half - 1.0 (both directions).
// The downhill +1.0 is the source's tolerance xmm10 = (m_slopeUphill == 0), x4 while
// m_wasOnSlope, plus a velocity term for a moving ramp (0x38fab3-0x38fb94); only the
// static, not-sliding case is modelled here. (That 1.0 was kWaveSlopeCapInset; since
// --waveslopecap and --capinsetgate lost their switches the upright wave's cap is the
// capped outline's, whose inset is the fresh contact's 1 px -- insetK in step.hpp.)
// The overlap of the two flipped-wave brackets (m=0.5: 5.83-5.97, m=1: 5.69-5.86).
constexpr double kWaveFlipSlopeKillD = 5.845;
// (--nofreeside, which dropped --slopelaw's one empirical conjunct, is gone since the flag clean-up.)
// --escrotahead: do not escapee-prune a body in a turned frame while a rotation
// is still ahead on the active queue channel. See the prune in step.hpp. Always
// on; the switch is gone since the flag clean-up.
inline thread_local bool g_preBtnSet = false;
inline thread_local double g_preBtnY = 0.0;
// Did the last stepOne's ball tap? Written by stepOne (cleared on entry, set from its own
// ballFlippedThisTick near the end), read by stepBoth right after each half (--repelland).
inline thread_local bool g_ballTapped = false;
// ...and the body's +0xa0c as the repel reads it: after this tick's collisions, before its tap
// (--repela0c; stepOne writes it next to g_ballTapped).
inline thread_local uint8_t g_hitGRepel = 0;

// ---- THE 2.2 TRIGGER QUEUE (channel / ord) ---------------------------------
//
// GD does not fire these on proximity. Every trigger with m_objectType==1 sits
// in a per-channel bucket, sorted once at load, and `checkSpawnObjects` walks
// ONE channel per tick from a per-channel cursor, stopping at the first element
// whose firing point the player has not passed. So an unconsumed element blocks
// everything behind it, and a channel switch can release several at once -- not
// because bulk firing is a rule, but because the loop keeps going until it
// breaks.
//
// The model had none of this: it tested "did the player cross this trigger's
// axis coordinate" against every 2900, with a perpendicular window standing in
// for channel separation. That is why lv22's t=6,315 is missing -- uid5957 is
// released by channel 3's REVERSE flag, which uid5809 set 335 ticks earlier.
struct RotQEntry {
    int uid = -1;
    // 2900 or 2899. Both are queue residents -- they carry m_channelValue and
    // m_ordValue and are consumed the same way -- but ONLY a 2900 touches the
    // channel machinery: EffectGameObject::triggerObject sends 2900 to
    // rotateGameplay and 2899 to processOptionsTrigger, and the writers of the
    // active channel (+0x33c) are rotateGameplay, resetSpawnChannelIndex,
    // loadUpToPosition, createCheckpoint and init -- no Options path. Ten of
    // lv22's thirty are 2899, so reading that field without checking the id
    // would switch the channel on objects that cannot switch it.
    //
    // [2026-09-10] This used to say "two of those carry m_changeChannel". None
    // of them do -- a 2899 is a GameOptionsTrigger, which has no such field.
    // The dumper read the offset off EffectGameObject, the common base, so the
    // column held whatever memory happened to sit past the end of a 2899: five
    // rows of 1/0/1 and five of 255/255/-1, on objects spread over five
    // channels. The id check was right; the number in the comment was reading
    // that garbage back.
    int id = 0;
    int chan = 0;     // m_channelValue: the bucket this object lives in
    int ord = 0;      // m_ordValue: the first sort key, stronger than position
    double px = 0.0, py = 0.0;   // the firing point: the LOAD position, frozen
    int swarm = 0;    // m_changeChannel: only these switch the active channel
    int swch = 0;     // m_targetChannelID
    // 1 = switches the channel WITHOUT rotating the player.
    //
    // 0 MEANS "ROTATES", NOT "ABSENT". A 2899 has no such field at all (it is a
    // GameOptionsTrigger), and the loader gives it 0 here because there is no
    // third value to give -- unlike swarm, whose 0, and swch, whose -1, are
    // refused by every consumer on their own. What actually keeps a 2899 out of
    // the rotation branch is `rotIdx >= 0`, and the authoritative predicate is
    // `id != 2900`; chanOnly is not carrying that and cannot.
    //
    // So a NEW consumer of chanOnly has to sit behind `id == 2900` or
    // `rotIdx >= 0`. Reading it alone would take "does not switch the channel
    // only" from an object that has no opinion on the question.
    int chanOnly = 0;
    int rotIdx = -1;  // index into g_rotTrig
};
inline std::vector<RotQEntry> g_rotQ;      // grouped by channel, sorted in it
// channel -> [beg, end) in g_rotQ. Two arrays rather than one of size 17,
// because the channels a level uses are not consecutive (lv22 uses 15 of them
// but not 0..14), so "the next channel's begin" is not this one's end.
inline std::array<int, 16> g_rotQBeg{};
inline std::array<int, 16> g_rotQEnd{};
// Bits of State::rotSpent that belong to each channel, so "how many of this
// channel have been consumed" is one popcount.
inline std::array<uint32_t, 16> g_rotQChanMask{};
// --rotqueue: consume rotations from the queue instead of the pre-queue
// selection (the travel-axis crossing, the perpendicular window, the
// last/nearest rule). OPT-IN, and the reason is measured rather than cautious:
//
// the queue is right from t=0 and WRONG AT AN ANCHOR. State::rotSpent /
// rotChan / rotRev are built up tick by tick, so a state handed to --start
// mid-level begins on channel 0 with nothing consumed and re-fires everything
// the run had already passed. Measured: from t=0 the queue fixes the
// transition it was built for (lv22 t=6,315, frame AND gravity, 191 of 192
// transitions agreeing), while quick_regress -- which is anchored sections
// throughout -- loses tracking in 12 of lv22's, worst 400 -> 17 at t=1,800.
//
// This is the third instance today of the same hole: a per-state value the
// anchor scan does not seed (State::fireB, State::lockOff, and now these).
// The old path had --spentrot for exactly it, fed from the GD dump's frame
// transitions before t0; the queue needs the equivalent before it can be the
// default, and until then it is what the flag turns on.
inline bool g_rotQueue = false;
// --startrotq <chan>,<revHex>[,<uid>...]: the queue's equivalent of --spentrot,
// which is the thing the paragraph above says is missing. It seeds the three
// per-state values the anchor scan does not: the active channel, the per-channel
// reverse bits, and WHICH QUEUE ENTRIES the run had already consumed before t0.
//
// The consumed set is given as UIDs, not as a raw rotSpent mask, for the reason
// this campaign spent a day learning: a bit index is a proxy that depends on the
// order buildRotQueue happened to produce, while a uid is the identity of the
// object itself. A mask handed to a differently-ordered queue is wrong in a way
// nothing can detect; a uid that is not in the queue can be reported, and is.
// --spentrot names uids for the same reason.
//
// -1 = not given, which is not the same as "given as channel 0": the anchor
// starting on channel 0 with nothing consumed is precisely the broken state
// described above, so it must be distinguishable from an explicit seed.
inline int g_startRotChan = -1;
inline unsigned g_startRotRev = 0;
inline std::vector<int> g_startRotSpent;
// --seeddump <t>: print the state's accumulated fields at tick t. The
// self-check for anchor seeding -- see the print site in cli.hpp.
inline int g_seedDump = -1;
// --seedevery <n>: print them every n ticks instead. The check needs the truth
// at MANY ticks and the seed at one each, and the truth comes from a single
// whole run -- so this turns the expensive half into one run rather than one
// per tick.
inline int g_seedEvery = 0;
// --p2touch: count the touch boxes the SECOND player enters. Diagnostic only.
// markTouched reads p1's position alone, so a box only p2 reaches is one the
// model can never fire -- see the print site in step.hpp.
inline bool g_p2Touch = false;

// ---- THE ANCHOR PAYLOAD ----------------------------------------------------
//
// `--start` carries the physics state. It does NOT carry the values whose
// worth at tick t depends on the ticks before t, so an anchor begins those at
// their defaults and unmakes what the run had already done: State::fireB read
// as "fired at tick 0", State::lockOff as "never rode anything", the rotation
// queue's three as "channel 0, nothing consumed". Three defects on 2026-09-04,
// one shape.
//
// Seeding them from the recording goes as far as the recording goes and no
// further -- 768 unexpected differences on lv22 became 163, and the remainder
// is boxes whose objects have no recording at all. Those have to come from GD,
// which knows.
//
// NAMED, NOT POSITIONAL. The positional --start fields (30 today, 26 when this was written) cannot take another
// one without every reader changing, and appending after an optional field is
// its own trap. So: `--anchor-state key=value;key=value`.
//
// TRIGGERS ARE NAMED BY UID, NOT BY BIT. Bit numbering is a property of this
// build's 32-box window; a bit index crossing the boundary would mean a
// different box whenever the window moved.
//
// AND THE TOUCH IS p1's ONLY. markTouched reads p1's position, so a bit set
// from "either player entered it" would be one the step function can never set
// going forward -- an anchor claiming what a whole run of the same plan would
// not. The payload's meaning is fixed as "what GD observed under the model's
// own convention", which on today's corpus is the same set (p2 enters no box)
// and is the safe side if that ever stops being true.
// EVERY VALUE IS THE STATE AT t0, like every other --start field. The first
// simulated tick is t0+1, so a payload written at t0+1 is one tick of motion
// too far along -- measured while testing this: lockOff handed in at t0+1 came
// out a whole dx (1.615 px) high at the first compared tick.
inline std::string g_anchorState;
// Which keys this build understands. A payload naming anything else is a
// payload from a different build, and the run stops rather than quietly
// dropping it -- see the refusal in cliMain.
//
// NO VERSION FIELD: the key set IS the version. Printing both sides' keys
// diagnoses the mismatch and says which is older (the shorter one), with no
// build-stamp plumbing to keep in step. If a human-readable identity is ever
// wanted, the mod version and the exe's mtime are already free.
// A PAYLOAD DECLARES THE SUBSYSTEMS IT OWNS, not a bag of keys. `owns=touch`
// means the touch seeding (trig + fireB) comes from GD and everything else is
// left to the path that already seeds it.
//
// The reason is a property this code's own first test established: a payload
// REPLACES the recording-derived seed rather than topping it up, so a key it
// omits is set by nobody -- three fire ticks perfect, the ride at zero, dead
// 45 ticks later. If the payload were just a key list, dropping a key would
// mean either that death by default, or a silent fall back to the recording
// for that one value -- a hybrid seed, and the opposite of what the refusal
// says. Declaring ownership keeps "replaces wholesale" true WITHIN a
// subsystem and leaves the others honestly alone.
inline const char* const kAnchorKeys[] = {"owns", "touch", "portal", "portal2",
                                          "hist", "position2"};
// `owns=hist` -> the per-body history values that --start does not carry, as
// ONE versioned value: `hist=<version>|<count>|name:value,name:value`. The
// version and the count are checked, and a name this build does not know is
// refused like an unknown key (every new history value goes
// through this one transport, never another positional --start field).
// Version 1 knows pressSpent and pressSpent2 (GD's +0x986 negated, per body).
inline bool g_ownsHist = false;
inline const char* const kHistNames[] = {"pressSpent", "pressSpent2"};
// Version 2 adds `freeMode`: GD's [layer+0x311], the Free Mode byte each mode
// portal copies in and checkCollisions reads before it clamps anything to the
// band (see kBandFreeKnown in bands.hpp). A version carries every name it knows,
// so a version-1 payload is still complete and leaves the byte unknown.
// nullptr pads the shorter version.
// Version 3 adds `spiderJumpT`: ticks since GD's spider teleport stamp
// (player+0x820 against the clock at +0xaa0), State::spiderJumpT. An anchor
// taken inside that window otherwise starts saturated and kills on the solid
// side GD spares.
// Version 4 adds p1's slope ride as GD's raw facts: `slopeOn` (+0x9b0),
// `slopeUnder` (+0x9b8), `slopeUid` (the ramp at +0x678), `slopeAge` (ticks
// since the ride's clock +0x598 was stamped, read against +0xaa0) and
// `slopeLanded` (a grounded row since then). -1 = not said. Parsed always,
// seeded only under --anchorride (cli.hpp maps them onto the ride fields).
// Version 5 carries each body's native m_wasTeleported arrival byte (+0x560).
inline std::array<const char*, 11> histNamesFor(int version) {
    if (version == 5)
        return {"pressSpent", "pressSpent2", "freeMode", "spiderJumpT", "slopeOn",
                "slopeUnder", "slopeUid", "slopeAge", "slopeLanded", "teleported", "teleported2"};
    if (version == 4)
        return {"pressSpent", "pressSpent2", "freeMode", "spiderJumpT", "slopeOn",
                "slopeUnder", "slopeUid", "slopeAge", "slopeLanded"};
    if (version == 3)
        return {"pressSpent", "pressSpent2", "freeMode", "spiderJumpT", nullptr,
                nullptr, nullptr, nullptr, nullptr};
    if (version == 2)
        return {"pressSpent", "pressSpent2", "freeMode", nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr};
    return {kHistNames[0], kHistNames[1], nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr};
}
// --anchorride / --no-anchorride (default on since 2026-09-30): seed the anchor's
// slope-ride fields from the hist payload's version-4-or-later values (cli.hpp, after the
// geometric guess rideAtAnchor). A payload before version 4 seeds nothing, so a
// run without the recording keeps the guess. On together with the mod's cfg
// histride, which includes the ride values and passes one of the two flags on every
// anchored call (config.hpp).
inline bool g_anchorRide = true;
// Which subsystems this payload claims. The lock and the rotation queue keep
// their own seeding until someone measures a reason to move them.
inline bool g_ownsTouch = false;
// `owns=portal` -> State::portalLatch / portalLatch2 come from GD. The mask is
// per player, so the subsystem takes TWO keys and claiming it requires both:
// a single-player level writes `portal2=` empty, which says "p2 spent nothing"
// out loud instead of leaving it to a default that means the same thing by
// accident. Uids rather than bit indices, for the reason the touch payload
// gives -- a bit index is this build's ordinal and a uid is the level's.
inline bool g_ownsPortal = false;
// --seed-partial-ok: run anyway when a key this build wants is absent, and
// stamp the outcome so the result carries it. A warning on stderr does not
// survive into the place results are compared.
inline bool g_seedPartialOk = false;
inline std::string g_seedPartial;   // what was missing, for the outcome line

// Split `a=1;b=2` into pairs, rejecting a key this build does not know. The
// rejection is the point: a payload naming an unknown key was written by a
// build that carries something this one would silently drop, and dropping it
// is how a run degrades without saying so.
inline bool parseAnchorState(const std::string& s,
                             std::vector<std::pair<std::string, std::string>>& out,
                             std::string& unknown) {
    size_t i = 0;
    while (i < s.size()) {
        size_t semi = s.find(';', i);
        if (semi == std::string::npos) semi = s.size();
        const std::string tok = s.substr(i, semi - i);
        i = semi + 1;
        if (tok.empty()) continue;
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) { unknown = tok; return false; }
        const std::string k = tok.substr(0, eq);
        bool known = false;
        for (const char* kk : kAnchorKeys) if (k == kk) { known = true; break; }
        if (!known) { unknown = k; return false; }
        out.emplace_back(k, tok.substr(eq + 1));
    }
    return true;
}
// `uid:tick,uid:tick` -> pairs. Uids, not bit indices: see g_anchorState.
inline std::vector<std::pair<int, int>> parseTouchPayload(const std::string& v) {
    std::vector<std::pair<int, int>> out;
    size_t i = 0;
    while (i < v.size()) {
        size_t comma = v.find(',', i);
        if (comma == std::string::npos) comma = v.size();
        const std::string tok = v.substr(i, comma - i);
        i = comma + 1;
        const size_t colon = tok.find(':');
        if (colon == std::string::npos) continue;
        out.emplace_back(std::atoi(tok.substr(0, colon).c_str()),
                         std::atoi(tok.substr(colon + 1).c_str()));
    }
    return out;
}
// `uid,uid,uid` -> uids. The portal payload carries no tick: a spent portal is
// spent, and unlike a touch box nothing downstream asks WHEN.
inline std::vector<int> parseUidList(const std::string& v) {
    std::vector<int> out;
    size_t i = 0;
    while (i < v.size()) {
        size_t comma = v.find(',', i);
        if (comma == std::string::npos) comma = v.size();
        const std::string tok = v.substr(i, comma - i);
        i = comma + 1;
        if (!tok.empty()) out.push_back(std::atoi(tok.c_str()));
    }
    return out;
}
// std::popcount is C++20 and this tree builds as C++17.
inline int popCount32(uint32_t v) {
    int n = 0;
    while (v) { v &= v - 1; ++n; }
    return n;
}
inline int g_rotQChans = 0;                // how many channels actually appear

// GD's `getObjectDirection`, which is what decides a bucket's sort axis:
// 1 = y ascending, 2 = y descending, 3 = x descending, 4/default = x ascending.
// flipY does not reach the result and is not read here. The rotation is
// truncated to an int and then compared for exact equality, so 89.5 degrees
// matches nothing and falls through to x-ascending -- deliberately, that is
// GD's own behaviour rather than a tolerance this code chose.
inline int rotQDirection(double rot, bool flipX) {
    const int r = ((int)rot) % 360;
    if (r == 0)                  return flipX ? 3 : 4;   // x desc : x asc
    if (r == 180 || r == -180)   return flipX ? 4 : 3;
    if (r == 90 || r == -270)    return flipX ? 1 : 2;   // y asc : y desc
    if (r == 270 || r == -90)    return flipX ? 2 : 1;
    return 4;
}

// The signed coordinate a bucket is ordered along.
inline double rotQAxis(int dir, double px, double py) {
    switch (dir) {
        case 1:  return  py;    // y ascending
        case 2:  return -py;    // y descending
        case 3:  return -px;    // x descending
        default: return  px;    // x ascending
    }
}

// Sort each channel's bucket the way LevelTools::sortChannelOrderObjects does:
// ord first, then the axis coordinate TRUNCATED TO AN INT (so two objects in
// the same integer bucket are not separated by position at all), then uid.
// The direction comes from the FIRST 2900 in uid order that switches TO this
// channel -- not from the objects in it, and not from the level's rotation.
inline void buildRotQueue() {
    g_rotQBeg.fill(0);
    g_rotQEnd.fill(0);
    g_rotQChans = 0;
    if (g_rotQ.empty()) return;
    // Pass 1: the direction of each channel, from the first switcher that
    // names it. "First" is array order, which is uid order.
    std::array<int, 17> dir{};
    dir.fill(0);
    std::vector<const RotQEntry*> byUid;
    byUid.reserve(g_rotQ.size());
    for (const RotQEntry& e : g_rotQ) byUid.push_back(&e);
    std::sort(byUid.begin(), byUid.end(),
              [](const RotQEntry* a, const RotQEntry* b) { return a->uid < b->uid; });
    for (const RotQEntry* e : byUid) {
        if (!e->swarm || e->swch < 0 || e->swch > 15) continue;
        if (dir[e->swch]) continue;                       // first one wins
        if (e->rotIdx < 0 || (size_t)e->rotIdx >= g_rotTrig.size()) continue;
        const RotTrig& rt = g_rotTrig[(size_t)e->rotIdx];
        dir[e->swch] = rotQDirection(rt.rawRot, rt.flipX != 0);
    }
    // Pass 2 + 3: bucket by channel, then sort inside each bucket.
    std::stable_sort(g_rotQ.begin(), g_rotQ.end(),
                     [](const RotQEntry& a, const RotQEntry& b) {
                         return a.chan < b.chan;
                     });
    size_t i = 0;
    while (i < g_rotQ.size()) {
        size_t j = i;
        const int ch = g_rotQ[i].chan;
        while (j < g_rotQ.size() && g_rotQ[j].chan == ch) ++j;
        const int d = (ch >= 0 && ch <= 15 && dir[ch]) ? dir[ch] : 4;
        std::sort(g_rotQ.begin() + (long long)i, g_rotQ.begin() + (long long)j,
                  [d](const RotQEntry& a, const RotQEntry& b) {
                      if (a.ord != b.ord) return a.ord < b.ord;
                      const int ka = (int)((float)a.ord
                                           + (float)rotQAxis(d, a.px, a.py));
                      const int kb = (int)((float)b.ord
                                           + (float)rotQAxis(d, b.px, b.py));
                      if (ka != kb) return ka < kb;
                      return a.uid < b.uid;
                  });
        if (ch >= 0 && ch <= 15) {
            g_rotQBeg[(size_t)ch] = (int)i;
            g_rotQEnd[(size_t)ch] = (int)j;
            // `k < 32` is the same 32-bit cursor limit step.hpp's `idx < 32`
            // obeys. A queue longer than that is REFUSED before it is used
            // (cli.hpp, right after loadRotQueue), so the truncation here is
            // unreachable rather than merely unlikely.
            uint32_t m = 0;
            for (size_t k = i; k < j && k < 32; ++k) m |= (uint32_t)1 << k;
            g_rotQChanMask[(size_t)ch] = m;
            ++g_rotQChans;
        }
        i = j;
    }
}

// Read the MOD's rotgameplay.txt (solver.hpp has written it for a while; until
// now nothing read it). Joins to g_rotTrig by uid, so it runs AFTER the level.
inline bool loadRotQueue(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::unordered_map<int, int> byUid;
    for (size_t k = 0; k < g_rotTrig.size(); ++k)
        byUid[g_rotTrig[k].uid] = (int)k;
    std::string line;
    std::getline(in, line);                       // header
    g_rotQ.clear();
    while (std::getline(in, line)) {
        RotQEntry e{};
        int sord = 0, sordd = 0, spx = 0, target = 0, chanChanged = 0;
        const int n = std::sscanf(
            line.c_str(), "%d,%d,%lf,%lf,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
            &e.uid, &e.id, &e.px, &e.py, &e.chan, &e.ord, &sord, &sordd, &spx,
            &target, &chanChanged, &e.swarm, &e.chanOnly, &e.swch);
        // The last three columns are "-,-,-" for an object that HAS no such
        // fields -- a 2899 is a GameOptionsTrigger and carries none of
        // m_changeChannel / m_channelOnly / m_targetChannelID (the dumper wrote
        // whatever memory sat at those offsets until 2026-09-10). Such a row is
        // still a queue resident and must not be dropped: `n < 14` alone would
        // silently remove all ten of lv22's 2899s from the queue, which is a
        // behaviour change and not the hygiene this was.
        //
        // The sentinels are the values every consumer already refuses --
        // swarm 0 (frames.hpp:384, cli.hpp:1825), swch -1 (step.hpp:390's
        // `swch >= 0`) -- so an absent field cannot be read as a present one.
        static const std::string kAbsent = ",-,-,-";
        const bool absent = line.size() >= kAbsent.size()
                            && line.compare(line.size() - kAbsent.size(),
                                            kAbsent.size(), kAbsent) == 0;
        if (n == 11 && absent) {
            e.swarm = 0;
            e.chanOnly = 0;
            e.swch = -1;
        } else if (n < 14) {
            continue;
        }
        const auto it = byUid.find(e.uid);
        e.rotIdx = (it == byUid.end()) ? -1 : it->second;
        g_rotQ.push_back(e);
    }
    buildRotQueue();
    return true;
}
// --spentrot uid,uid,...: the uids of the 2900s this run had already fired
// before the anchor (--start's t0). The one-shot (firedT) cannot be carried
// through --start, so re-anchoring behind the maze re-fires a spent trigger at
// the next crossing and only the model turns (lv22 t=14,321 uid6337: GD had
// consumed it at t=12,795 and passes straight through; the model turned into
// frame 3 and the whole world broke. A fixup cannot carry the frame, so the
// driver re-stacked the same fixup for 30 iterations and marked time). The
// driver derives it from the gframe transitions (t<t0) in the GD dump and
// passes it in.
inline std::vector<int> g_spentRot;
// --spentpad uid,uid,...: the uids of the PADS this run had already fired
// before the anchor. The same hole one shape down: GD's activatedByPlayer latch
// is permanent for the attempt (collisionCheckObjects drops a latched object at
// the vf560 test, before any shape test), the model keeps it in
// State::usedPad, and --start cannot carry a pointer table. The seeding that
// was there read the boxes the player OVERLAPS at t0, which catches a contact
// still in progress and nothing else; a pad fired earlier and stepped off comes
// back live, and the anchored arm fires it a second time where GD never does.
//
// Measured on lv18 x~25,000, a six blue-pad zig-zag the player re-enters about
// 32 ticks after each first contact: anchoring at t0=18,200 lands between uid
// 12805's two contact runs (18,184 and 18,216-217), the fresh anchor re-fires
// it at 18,216 and edvy closes exactly on kPadBlueVy (0.215 - (-15.595) =
// +15.810). Corpus-wide: 502 static pads, 18 re-entries (lv13/14/18/21, all id
// 67 / type 10), GD fires the second run 0 of 18 and the WHOLE-RUN model 0 of
// 18 -- so this is an instrument defect and not a physics one, and the only
// thing that may move is an anchored comparison.
//
// The producer walks the recording rather than the geometry (py/gdtas/
// padhistory.py). Overlap is the portal latch's rule and NOT a pad's: the blue
// pad's polarity gate in step.hpp runs BEFORE `c.usedPad[slot] = pd`, so a
// gravity pad met with the wrong gravity is neither fired nor consumed, and
// seeding it would suppress a firing GD performs.
inline std::vector<int> g_spentPad;
// A/B switch (--no-spentpad): ignore the list above, i.e. seed only from the
// boxes overlapped at t0, which is what every build before 2026-09-06 did.
inline bool g_spentPadSeed = true;
// (--norevtoggle, which turned off the reverse-run toggle of a same-frame 2900,
// is gone since the flag clean-up.)

// ---- CONTROL-DISABLED WINDOWS (--ctrlwin t0:t1,...) -------------------------
//
// **id 2899 is not a reverse run but an Options trigger** (GD's
// GameOptionsTrigger). All 10 of lv22's raise/lower m_disableP1Controls, and
// while it is raised **the button is ignored entirely** -- neither a press nor
// the cube's held-button re-jump happens. The windows measured in GD
// (2026-08-18, cfg endtrace=1) are four:
//   t=13,461..14,224 / 18,259..18,612 / 19,152..19,576 / 20,110..20,487
// 1,921 ticks in total. The model thought it could jump there, and this was the
// identity of fixcensus's `m0/mini0/g1/gdg1/sp0.9/air/in1` family (edy=0.000 /
// edvy=-11.180, only the model jumps).
//
// **Why not derive it from the triggers ourselves**: which x crossing makes GD
// raise it is still unsolved. Crossing the same x of the same trigger many times
// fires it only once, and that once is not necessarily the first crossing
// (uid 18316 on the 5th, uid 18086 on the 3rd). Nor is it the distance to the
// player's y (no fire at Δ=1,236, fires at Δ=1,324). The gate is GD's camera or
// something like it, which the current model does not have. **Firing on a guess
// does more harm than good** (burning uid 18315 at its first crossing t=10,673
// creates 393 ticks of control loss that GD does not have, and on top of that
// misses the real window). So the windows are passed only "when GD knows them":
// section-anchor replays (fixcensus / quick_regress / the driver's re-anchor)
// already receive gframe and pmin/pmax from the GD dump, so this fits the same
// scheme. A cold run from the head has no such information and the model jumps
// as before (an unresolved hole).
inline std::vector<std::pair<long long, long long>> g_ctrlWin;

inline bool ctrlOffAt(long long t) {
    for (const auto& w : g_ctrlWin)
        if (t >= w.first && t <= w.second) return true;
    return false;
}
// Jump ticks that come from a window's re-push (= w1+1). GD's buffered jump has
// holding already at 0 when it fires, so **no hover starts** -- the model side,
// which mirrors it with a synthetic press, would create s.action=1, so the jump
// on exactly this tick does not accumulate rHover. Filled only when a replay is
// loaded (always empty during the search).
inline std::vector<long long> g_winRePushJump;
inline bool rePushNoHoverAt(long long t) {
    for (long long v : g_winRePushJump)
        if (t == v) return true;
    return false;
}

inline void toFrame(int f, double X, double Y, double& u, double& v) {
    switch (f & 3) {
        case 0:  u =  X; v =  Y; break;
        case 1:  u = -Y; v =  X; break;   // rot 90:  travel -Y
        case 2:  u = -X; v = -Y; break;   // rot 180: travel -X (the reverse runs)
        default: u =  Y; v = -X; break;   // rot 270 (= -90): travel +Y
    }
}
inline void fromFrame(int f, double u, double v, double& X, double& Y) {
    switch (f & 3) {
        case 0:  X =  u; Y =  v; break;
        case 1:  X =  v; Y = -u; break;
        case 2:  X = -u; Y = -v; break;
        default: X = -v; Y =  u; break;
    }
}
// The travel coordinate of a world point in this frame (what `cx` becomes).
inline double frameU(int f, double X, double Y) {
    double u, v;
    toFrame(f, X, Y, u, v);
    return u;
}
// ...and the PERPENDICULAR one (what `cy` becomes). A 2900 fires only when the
// player's box overlaps it on this axis -- see the gate in applyRotation.
inline double frameV(int f, double X, double Y) {
    double u, v;
    toFrame(f, X, Y, u, v);
    return v;
}

// ---- turning one object into a frame's coordinates -------------------------
//
// What has to come along:
//   - the box: centre through toFrame, hw/hh swapped on the odd frames
//   - `rot`: the object's own turn is measured against the screen, so it moves
//     with the frame (used by the oriented rings and the dash angle)
//   - the slope line (sy0/sy1 are absolute heights at the box's left/right
//     edge): a turned slope is a different line, so the two endpoints are
//     carried as points and re-read
//   - tpY (an absolute target y for a teleport) becomes the target's v
inline void turnObj(Obj& o, int f) {
    if ((f & 3) == 0) return;
    const double cx = o.cx, cy = o.cy;
    double u, v;
    toFrame(f, cx, cy, u, v);
    // the slope's two surface points, in world, before the box moves
    const double sxL = cx - o.hw, sxR = cx + o.hw;
    const double syL = o.sy0, syR = o.sy1;
    o.cx = u; o.cy = v;
    if (f & 1) std::swap(o.hw, o.hh);
    o.rot += 90.0 * (f & 3);
    if (o.slope) {
        double uL, vL, uR, vR;
        toFrame(f, sxL, syL, uL, vL);
        toFrame(f, sxR, syR, uR, vR);
        if (uL <= uR) { o.sy0 = vL; o.sy1 = vR; }
        else          { o.sy0 = vR; o.sy1 = vL; }
    }
    if (o.tpY != 0.0) {
        double tu, tv;
        toFrame(f, cx, o.tpY, tu, tv);
        o.tpY = tv;
    }
    // the exit half is a POINT of its own (its x is not the portal's cx), so
    // it turns as one -- the teleport block reads its v as the target
    if (o.tpEx != 0.0 || o.tpEy != 0.0) {
        double eu, ev;
        toFrame(f, o.tpEx, o.tpEy, eu, ev);
        o.tpEx = eu; o.tpEy = ev;
    }
    double du, dv;
    toFrame(f, o.tpEntryDx, o.tpEntryDy, du, dv);
    o.tpEntryDx = du; o.tpEntryDy = dv;
}

// teleportPlayer (2.2081, 0x20fdb0) chooses a world point before applying ignore axes.
inline void teleportTarget(const Obj& p, int frame, double x, double y,
                           double& tx, double& ty) {
    const bool hasExit = p.tpExitCount > 0
        || (p.tpExitCount < 0 && (p.tpEx != 0.0 || p.tpEy != 0.0));
    if (p.tpExitCount == 0) { tx = x; ty = y; return; }
    // Preserve old exports' unlinked target convention, including turned frames.
    if (!hasExit && p.tpExitCount < 0) {
        tx = x; ty = p.tpIgnoreY ? y : p.tpY; return;
    }
    double wx, wy, ex, ey;
    fromFrame(frame, x, y, wx, wy);
    if (hasExit) fromFrame(frame, p.tpEx, p.tpEy, ex, ey);
    else { ex = wx; ey = p.tpWorldY; }
    if (p.id == 747) ex = wx;
    if (p.tpSaveOffset) {
        double ix, iy;
        fromFrame(frame, p.cx + p.tpEntryDx, p.cy + p.tpEntryDy, ix, iy);
        ex += wx - ix; ey += wy - iy;
    }
    if (p.tpIgnoreX) ex = wx;
    if (p.tpIgnoreY) ey = wy;
    toFrame(frame, ex, ey, tx, ty);
}

// Port the classic-mode force branches of teleportPlayer/redirectPlayerForce (0x39fc60).
// Native m_xVelocity is platformer-only; classic forces never change travel speed.
inline void teleportForce(const Obj& p, int frame, float& vy, uint8_t& boost) {
    if (!p.tpStaticForce && !p.tpRedirectForce) return;
    if (!p.tpRedirectForce && p.tpForce == 0.f && !p.tpForceAdditive) {
        vy = 0.f; boost = 0; return;
    }
    constexpr float pi = 3.141592741012573242f;
    const float angle = p.tpForceAngle * 0.01745329238474369f;
    float fx = std::cos(angle), fy = std::sin(angle);
    const float nativeVy = frame == 3 ? -vy : vy;
    if (p.tpRedirectForce) {
        float turn = angle - std::atan2(nativeVy, 0.f);
        if (turn < -pi || turn > pi) {
            const float turns = std::ceil(std::floor(std::fabs(turn) / pi) * 0.5f);
            turn += (turn < -pi ? 1.f : -1.f) * turns * (pi + pi);
        }
        fx = 0.f; fy = nativeVy;
        if (turn != 0.f) {
            fx = -nativeVy * std::sin(turn);
            fy = nativeVy * std::cos(turn);
        }
        fx *= p.tpRedirectMod; fy *= p.tpRedirectMod;
        const float length = std::sqrt(fx * fx + fy * fy);
        if (p.tpRedirectMax > 0.f && length > p.tpRedirectMax) {
            const float scale = p.tpRedirectMax / length;
            fx *= scale; fy *= scale;
        } else if (p.tpRedirectMin > 0.f && length < p.tpRedirectMin) {
            if (length == 0.f) {
                fx = std::cos(angle) * p.tpRedirectMin;
                fy = std::sin(angle) * p.tpRedirectMin;
            } else {
                const float scale = p.tpRedirectMin / length;
                fx *= scale; fy *= scale;
            }
        }
    } else {
        const float scale = p.tpForce / std::sqrt(fx * fx + fy * fy);
        fx *= scale; fy *= scale;
    }
    float out = (frame & 1) ? fx : fy;
    if (!p.tpRedirectForce && p.tpForceAdditive) out += nativeVy;
    vy = frame == 3 ? -out : out;
    boost = 1;
}

}  // namespace dp
