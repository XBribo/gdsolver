#pragma once
#include <cstddef>
#include <cstdint>

namespace dp {

// Shared input: a direction change counts once even when both bodies are waves.
inline bool waveActive(uint8_t mode, bool dual, uint8_t mode2) {
    return mode == 4 || (dual && mode2 == 4);
}

// Keeping either button level costs nothing; other modes never add turns.
inline uint32_t waveTurnCost(uint32_t turns, int held, int input, bool wave) {
    return turns + (wave && held != input ? 1u : 0u);
}

// Wave turns rank first, clearance breaks ties, and exact ties keep the incumbent.
inline bool preferWaveRoute(uint32_t turns, uint16_t tight, uint32_t oldTurns,
                            uint16_t oldTight, bool enabled) {
    if (enabled && turns != oldTurns) return turns < oldTurns;
    return tight < oldTight;
}

// Pick locally within one spatial sampling window, never across cap families.
template<class TurnsAt>
inline size_t waveSample(size_t begin, size_t end, size_t fallback, TurnsAt turnsAt) {
    size_t best = fallback;
    for (size_t i = begin; i < end; ++i)
        if (turnsAt(i) < turnsAt(best)) best = i;
    return best;
}

}  // namespace dp
