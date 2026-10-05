#pragma once
namespace orbtrace {
inline long long g_lastPress = -1; // most recent press tick
inline int g_lines = 0;            // total log lines (cap against flooding)
inline void reset() { g_lastPress = -1; }
}
namespace padtrace {
inline int g_lines = 0;            // total log lines (cap against flooding)
inline void reset() { g_lines = 0; }
}
// What the anchor payload is built from: the tick GD activated each touch
// trigger. dp's --start carries the physics state and not the values whose
// worth depends on earlier ticks, so a re-anchored solve begins with every
// door shut; reading it back out of the moving-geometry recording works only
// for objects the recording contains, which left 163 differences on lv22.
//
// The activation itself is observable, so nothing has to be inferred:
// EnhancedGameObject::activatedByPlayer is the only setter of the flag the
// spawn queue reads (+0x5b3), it is cleared only by resetObject, and triggers
// reach it because EffectGameObject derives from EnhancedGameObject.
//
// FIRST activation, and p1's only. First, because the flag latches within an
// attempt and dp writes its own fire tick once (markTouched skips a box whose
// bit is set). p1's only, because dp's markTouched reads p1's position -- a
// bit set from p2 would be one the model can never set going forward, so an
// anchor would claim what a whole run of the same plan would not.
// First native invocation of each effect/Spawn source, independent of touch admission.
namespace playerseed {
inline std::map<std::pair<int, int>, int> g_first;
// A new attempt cannot inherit pending player writes from the previous one.
inline void reset() { g_first.clear(); }
}
namespace touchseed {
inline std::unordered_map<int, int> g_first;   // trigger uid -> first tick
// Activations by the SECOND player, counted and never carried. dp cannot
// express them, and the model-side check cannot even look: it compares against
// the model's own whole run, and lv20 -- the only level with both a dual
// portal and touch boxes -- dies at t=1,157 while its dual section starts near
// t=17,119. This counter runs inside GD, which replays lv20 to the end, so it
// is the one observer that reaches the case.
inline int g_p2 = 0;
// ...and how often the hook ran at all, so the p2 count has a denominator. A
// zero on its own cannot tell "watched and saw none" from "never watched",
// which is the reading that stalled three separate measurements on
// 2026-09-04. The reported line carries this beside it.
inline int g_calls = 0;
inline void reset() { g_first.clear(); g_p2 = 0; g_calls = 0; playerseed::reset(); }
}
// ...and the same question for GRAVITY PORTALS, which have the same hole:
// State::portalLatch accumulates over the run, so a state handed to dp's
// --start begins with an empty mask and believes every portal the run has
// already spent is still live. Unlike the rotation queue's version the wrong
// value is the LENIENT one (0 = nothing spent = the behaviour before the latch
// existed), so it degrades to blind rather than to inventing work -- but blind
// still means an anchored solve plans firings the whole run does not have.
//
// WHETHER THIS HOOK IS THE RIGHT SOURCE IS THE FIRST THING TO MEASURE, not to
// assume. sweep.hpp's note above says activatedByPlayer is the only setter of
// +0x5b3, the flag the SPAWN QUEUE reads. The portal latch was measured through
// hasBeenActivated() / hasBeenActivatedByPlayer(), which are virtual methods and
// need not be backed by that same byte. There is ground truth to check against:
// the ccl probe put lv22 uid 13833's transition at exactly t=6,300.
// Both halves, because the mask is per player (dp's State::portalLatch2).
namespace portalseed {
inline std::unordered_map<int, int> g_first;    // gravity-portal uid -> first tick, p1
inline std::unordered_map<int, int> g_first2;   // ...and p2
inline int g_calls = 0;   // a denominator for a zero -- see touchseed::g_calls
inline void reset() { g_first.clear(); g_first2.clear(); g_calls = 0; }
}
// ...and PADS (object types 8/9/10/34, dp's L.pads), for dp's --spentpad (cfg dpspentpad).
// GD's activatedByPlayer latch holds for the attempt and collisionCheckObjects drops a latched
// object before any shape test, so a pad fired before an anchor is dead after it -- while the
// model's State::usedPad starts empty there. Recorded here, where GD sets the latch, rather than
// rebuilt from the recording (py/gdtas/padhistory.py): the blue pad's polarity gate keeps a pad
// met with the wrong gravity live, and only the latch itself says which were fired. p1's only:
// dp seeds the first body's usedPad.
namespace padseed {
inline std::unordered_map<int, int> g_first;    // pad uid -> first tick, p1
inline int g_calls = 0;   // a denominator for a zero -- see touchseed::g_calls
inline void reset() { g_first.clear(); g_calls = 0; }
}
// ...and RINGS, for dp's --spentorb (dp --spentorb). A ring fires once per attempt, and an
// anchored model starts with no memory of which ones went. Recorded in the ringJump hook on the
// fire itself: a successful ringJump sets one of the per-tick fire latches 0x98b / 0x98c / 0x98d
// (0x398e08..0x398e26), and a call that returns early leaves them as they were. p1's only, in fire
// order.
namespace ringseed {
inline std::vector<std::pair<int, int>> g_fired;   // (ring uid, tick), p1, oldest first
inline int g_calls = 0;   // ringJump calls on p1 -- a denominator for a zero
inline void reset() { g_fired.clear(); g_calls = 0; }
}
