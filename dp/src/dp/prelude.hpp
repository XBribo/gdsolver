#pragma once

// Standard-library and model-table includes shared by every dp/ header.
// (Stage A-2 mechanical split of leveldp.cpp; the chain of includes keeps
// the translation unit in its historical order.)

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <set>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "models/ship_model.hpp"
#include "models/ufo_model.hpp"
#include "models/ship_params.hpp"

namespace dp {

// ============================================================================
// HOW MANY TOUCH BOXES A STATE CAN CARRY. The mask type, State::trig,
// State::fireB, the loader's cap, the proximity tables and the --needtrig bit
// range derive from it.
//
// THIS COMMENT USED TO SAY "change kTouchBits and nothing else". IT WAS WRONG,
// and it cost two sessions a day of measurement on 2026-09-21: both raised the
// width, both measured lv22 failing, and neither result was about the width.
// level_loader.hpp built TrigOf::mask as a uint32_t and set it with
// `(uint32_t)1 << b` over a touch BOX INDEX, so at a width of 64 the shift was
// undefined for b >= 32 and wrapped mod 32 on x86 -- boxes 32..63 answered for
// boxes 0..31 while dyn.trigMask, the consumer, was already TouchMask. The
// producer truncated and the widening into the consumer said nothing.
//
// So before believing any measurement that moves this constant: grep the tree
// for `uint32_t` next to a mask, for the literal 32, and for loops over bit
// indices, and READ each hit. Not every 32 is this constant -- the rotation
// queue's cursor (frames.hpp, paired with step.hpp's `idx < 32`) and a gravity
// portal's bit index are their own limits and must NOT be converted. The grep
// is a list to read, not a list to rewrite.
//
// IT LIVES HERE, in the root of the include chain, because the mask is held in
// three places that see each other only through this file: State (state.hpp),
// the recording's per-object masks (dynamics.hpp) and the run's outcome
// (progress.hpp). Putting it next to the touch triggers themselves was tried
// and does not compile -- two of those three are included first.
//
// Why it is a compile-time number: State is a POD copied for every node the
// search keeps, initialised positionally in places, sized by a static_assert,
// parsed out of --start and carried in the anchor payload. A runtime width
// would put an indirection in the hottest structure in the program.
//
// Why widening is not the whole answer: on the one corpus level that exceeds
// this, 152 boxes reduce to 61 once the ones that cannot reach the player are
// dropped (--trigrelevant), and before that filter existed 24 of these 32 bits
// went to boxes that only tint their group while 52 that move floors were
// discarded. Narrow first, then widen.
//
// UNMEASURED FOR CUSTOM LEVELS: the corpus tops out at 61 after filtering, and
// the two custom levels with dumps in the lab have empty trigger dumps, so
// nothing here says what a custom level needs. The loader still caps, and a run
// that hits the cap prints so.
// [2026-09-20, coin routing] 64 WAS TRIED AND IS HELD, NOT REJECTED. It fixes
// lv22's coverage outright (61 relevant, 61 kept, maxKeptX 3,675 -> 20,111) and
// the level then fails to solve. Measured with the variable isolated, same tree
// and flags, one worker each:
//
//   32   lv22 CLEARED  41 iterations,  962 s, deepest x=21,897
//   64   lv22 stuck   201 iterations, 3516 s, deepest x=10,755
//
// WHY IT FAILS IS NOT ESTABLISHED, and the first answer written here was wrong.
// It said the cost lands in the dedupe key, which divides on every box in the
// window while that box is moving (search_key.hpp). The same run refutes that:
// at 32 and 64 the key divides 2,891,350 and 3,798,737 times -- 31% more -- yet
// maxAlive is 5,518 against 5,513, capHits 14,439 against 14,465, and BOTH
// SOLVE. A single anchorless search is nearly indifferent to the width. The
// 41 -> 201 happens in the repair loop.
//
// THE AUTO-WINDOW GATE IS NOT THE MECHANISM, and this file said it was. The
// gate's threshold really does move with the width -- the `plain` probe in
// cli.hpp is capped at kTouchBits, so it fires above x0 2,483 at 32 and above
// 4,187 at 64 -- but on lv22 at 64 THE GATE CHANGES NOTHING IT LOADS. 61
// relevant boxes fit in 64, so nothing is ever dropped, and the fromX argument
// (the only thing winTouch feeds) has nothing left to select:
//
//   width  anchor    gate     relevant  kept  droppedRelevant  maxKeptX
//     64    3,000    silent      61      61          0          20,111
//     64   20,200    fires       61      61          0          20,111
//     32    3,000    fires       61      32         29           7,875
//     32   20,200    fires       61      32         29          20,111
//
// The bottom pair is the positive control: at a width where the set cannot fit,
// the loaded set does move with the anchor, so the top pair is a real null and
// not an instrument that reports nothing. (Coin routing off, so this is the
// population the 41/201 arms above actually ran on.)
//
// WHAT THE TABLE SAYS INSTEAD INVERTS THE OBVIOUS READING. At 32 every call on
// this level runs with 29 relevant boxes missing and the level SOLVES; at 64
// every call has the whole world and it does not. Width is not costing the
// search something. It is SHOWING the model geometry the narrow run was blind
// to -- and lv22 carries twelve Stop triggers (1616) that this model does not
// implement at all. The cheap next test is static, with no run in it: are the
// 29 boxes a width of 32 drops the ones whose chains reach what is unmodelled?
//
// AND THE 41 -> 201 DOES NOT SAY WHICH BUILD FAILED. Its number is quoted in
// the message of the commit that fixed seven width sites -- three of them
// truncate every box above bit 31 onto bit 31 inside markTouched, a fault that
// is a no-op at 32 and corrupts every tick at 64 -- so the run finished before
// that commit landed, but whether its exe already carried the fixes cannot be
// recovered: the exe is gone, and the same message retracts an EARLIER 64-bit
// number on exactly these grounds, which cuts the other way. Reading the
// history does not settle this. Re-running it does.
//
// --keycensus stays useful for what it measures: the cost per box. Of lv22's
// 61, TWENTY-FIVE never divide the key at all and 22 carry 90% of the dividing.
// So boxes differ by orders of magnitude in what their bit costs, and a rule
// that keeps the free ones costs nothing. It is NOT evidence about why the cold
// run failed.
constexpr int kTouchBits = 32;
// (TouchMask itself is declared below Bits, which it is.)
// ============================================================================

// ---- A FIXED-WIDTH BIT SET THAT STAYS POD ------------------------------------
// State is copied with memcpy and compared with memcmp (refwatch.hpp), and its
// size is pinned by a static_assert, so a member of it must be trivially copyable
// with no padding of its own and no heap. std::bitset promises none of that. This
// is N bits in (N + 63) / 64 words, zero when value-initialised (`Bits<N> b{}`).
// word(i) is for the hash: a key that mixes word 0 exactly as it mixed the old
// integer stays bit-identical for everything that fits in the first word.
template <int N>
struct Bits {
    static_assert(N > 0, "Bits<0>");
    static constexpr int kWords = (N + 63) / 64;
    uint64_t w[kWords];
    void set(int i) { w[i >> 6] |= (uint64_t)1 << (i & 63); }
    bool test(int i) const { return ((w[i >> 6] >> (i & 63)) & 1u) != 0; }
    bool any() const {
        for (int k = 0; k < kWords; ++k)
            if (w[k]) return true;
        return false;
    }
    uint64_t word(int k) const { return w[k]; }
    // The lowest set bit, or -1 when empty.
    int lowest() const {
        for (int k = 0; k < kWords; ++k)
            if (w[k])
                for (int b = 0; b < 64; ++b)
                    if ((w[k] >> b) & 1u) return k * 64 + b;
        return -1;
    }
    // A set with exactly bit i.
    static Bits bit(int i) {
        Bits r{};
        r.set(i);
        return r;
    }
    // The bit operators a mask is written with, so code that treated the mask as
    // an integer reads the same. `~` is masked to N bits: the words carry spare
    // high bits past N, and a complement that set them would make any() and ==
    // lie about sets that agree on every real bit.
    explicit operator bool() const { return any(); }
    Bits& operator|=(const Bits& o) {
        for (int k = 0; k < kWords; ++k) w[k] |= o.w[k];
        return *this;
    }
    Bits& operator&=(const Bits& o) {
        for (int k = 0; k < kWords; ++k) w[k] &= o.w[k];
        return *this;
    }
    Bits& operator^=(const Bits& o) {
        for (int k = 0; k < kWords; ++k) w[k] ^= o.w[k];
        return *this;
    }
    friend Bits operator|(Bits a, const Bits& b) { return a |= b; }
    friend Bits operator&(Bits a, const Bits& b) { return a &= b; }
    friend Bits operator^(Bits a, const Bits& b) { return a ^= b; }
    friend Bits operator~(Bits a) {
        for (int k = 0; k < kWords; ++k) a.w[k] = ~a.w[k];
        if (N % 64) a.w[kWords - 1] &= ((uint64_t)1 << (N % 64)) - 1;
        return a;
    }
    friend bool operator==(const Bits& a, const Bits& b) {
        for (int k = 0; k < kWords; ++k)
            if (a.w[k] != b.w[k]) return false;
        return true;
    }
    friend bool operator!=(const Bits& a, const Bits& b) { return !(a == b); }
};
static_assert(std::is_trivially_copyable_v<Bits<128>>
                  && std::is_standard_layout_v<Bits<128>>,
              "Bits must stay POD: State is memcpy'd and memcmp'd");

// ---- THE TOUCH MASK ----------------------------------------------------------
// [2026-09-22] A Bits<kTouchBits>, where it was the smallest unsigned integer
// that held kTouchBits (uint32_t at 32). The type changes; the width does NOT:
// widening to 64 stalled lv22 (41 -> 201 iterations) and stays out until that is
// understood. The mod's side of the boundary (unsigned long long masks in
// dp_bridge.hpp) is fed word(0), which is the whole mask while the width is 64
// or less.
using TouchMask = Bits<kTouchBits>;
static_assert(kTouchBits <= 64, "the mod's boundary masks are one word");
// `1 << bit` in the mask's own width. Writing `1u << bit` against a 64-bit mask
// is the silent truncation this helper exists to prevent.
inline TouchMask touchBit(int b) { return TouchMask::bit(b); }
// ...and the way back: which bit a single-bit mask is. THE POINT IS THE TYPE.
// Three sites wrote `uint32_t mbit = tb.second; while (!(mbit & 1u) && b < 31)`,
// which at a width of 64 truncates every box above 31 to zero AND stops the
// walk at 31, so all of them reported bit 31 -- one shared fire tick for
// thirty-two different boxes, in markTouched, every tick. Taking a TouchMask
// and bounding by kTouchBits makes that shape impossible to write again.
// An empty mask answers kTouchBits - 1, as the shifting loop this replaces did.
inline int touchBitIndex(const TouchMask& m) {
    const int b = m.lowest();
    return (b < 0 || b > kTouchBits - 1) ? kTouchBits - 1 : b;
}

// ---- HOW MANY GRAVITY PORTALS A LEVEL MAY HOLD (State::portalLatch) ----------
// Its own constant, NOT tied to kTouchBits: the touch window is a search-cost
// question with its own measured stall (lv22 at 64), this is a capacity one. It
// was 32 (a uint32) until custom levels with 43, 68 and 72 made the loader stop,
// and the stop was a std::exit inside the game's process. Obj::gpBit is int8_t,
// so 127 is the last bit it can name.
constexpr int kGravPortalBits = 128;
static_assert(kGravPortalBits - 1 <= 127, "Obj::gpBit is int8_t");
using GravLatch = Bits<kGravPortalBits>;
// ...and past it, on a level with no reversal, a bit is SHARED: the gravity portal kGravPortalBits
// later in x order takes the bit of the one before it, and the step clears the bit where x crosses
// the point between the two (the earlier portal is behind the player for good by then, and GD's
// latch on it can no longer matter). One entry per handed-on bit; empty on every level that fits,
// so those levels step exactly as before. Filled by the loader, cleared per call (reset.hpp).
// Two custom levels (135 and 147 gravity portals respectively) were refused whole without it.
struct GpHandoff {
    double x;   // where the bit changes hands
    int bit;
};
inline std::vector<GpHandoff> g_gpHandoff;
// The latch as hex, printed EXACTLY as the uint32 was ("0x%x") while it fits in
// the first word -- --seeddump's line is parsed by the lab's seedcheck -- and with
// the higher words prepended (most significant first) only once they are used.
inline std::string gravLatchHex(const GravLatch& b) {
    int top = GravLatch::kWords - 1;
    while (top > 0 && b.word(top) == 0) --top;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)b.word(top));
    std::string s = buf;
    for (int k = top - 1; k >= 0; --k) {
        std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)b.word(k));
        s += buf;
    }
    return s;
}

}  // namespace dp
