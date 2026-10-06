#pragma once
#include <cstdint>

namespace gdinput {

// Bit 0 controls P1; bit 1 controls P2. Legacy plans use only bit 0.
using Mask = uint8_t;

// Independent controls exist only inside a dual of a two-player level.
inline constexpr bool independent(bool twoPlayer, bool dual) { return twoPlayer && dual; }

// Keep the old two children outside the explicitly active two-player interval.
inline constexpr int branches(bool twoPlayer, bool dual) { return independent(twoPlayer, dual) ? 4 : 2; }

// Ignore P2 plan changes outside the interval, including on the dual's birth tick.
inline constexpr Mask canonical(int mask, bool twoPlayer, bool dual) {
    return (Mask)(mask & (independent(twoPlayer, dual) ? 3 : 1));
}

// Native handleButton reverses its player argument when game variable 0010 is enabled.
inline constexpr bool playerArgument(int player, bool swapped) { return (player == 1) != swapped; }

} // namespace gdinput
