#pragma once
#include "dp/object.hpp"

namespace dp {

// ItemTriggerGameObject's arithmetic fields (2.2081 win 0x234250 / 0x234630).
struct ItemExpr {
    int item = 0, item2 = 0, target = 0;
    int mode1 = 0, mode2 = 0, targetMode = 1;
    int op1 = 0, op2 = 0, op3 = 0;
    int round1 = 0, round2 = 0, sign1 = 0, sign2 = 0;
    float mod1 = 0.f, mod2 = 0.f, tolerance = 0.f;
};

// Round authored floats before widening: doing this in double changes boundary comparisons.
inline double itemModifier(float x) { return (double)(std::round(x * 1000.f) / 1000.f); }

// Native arithmetic uses zero for division by zero and for unknown operators.
inline double itemArithmetic(double a, double b, int op) {
    switch (op) {
        case 1: return a + b;
        case 2: return a - b;
        case 3: return a * b;
        case 4: return b == 0.0 ? 0.0 : a / b;
        default: return 0.0;
    }
}

// Rounding precedes sign conversion on both sides of either Item operation.
inline double itemTransform(double x, int rounding, int sign) {
    if (rounding == 1) x = std::round(x);
    else if (rounding == 2) x = std::floor(x);
    else if (rounding == 3) x = std::ceil(x);
    if (sign == 1) x = std::fabs(x);
    else if (sign == 2) x = -std::fabs(x);
    return x;
}

// Optional Item operands are absent for mode zero, even with a nonzero item ID.
inline bool itemOperand(int mode, int id) { return mode != 0 && (id > 0 || mode == 3 || mode == 4); }

// Evaluate both transformed operands with the native asymmetric tolerance rules.
template <class Read>
inline bool itemCompare(const ItemExpr& e, Read read) {
    const double a = itemTransform(itemArithmetic(read(e.mode1 <= 0 ? 1 : e.mode1, e.item),
        itemModifier(e.mod1), e.op1 <= 0 ? 3 : e.op1), e.round1, e.sign1);
    const double b = itemTransform(itemOperand(e.mode2, e.item2)
        ? itemArithmetic(read(e.mode2, e.item2), itemModifier(e.mod2), e.op2 <= 0 ? 3 : e.op2)
        : itemModifier(e.mod2), e.round2, e.sign2);
    const double tol = itemModifier(e.tolerance);
    switch (e.op3) {
        case 0: return std::fabs(a - b) <= tol;
        case 1: return a + tol > b;
        case 2: return a + tol >= b;
        case 3: return a - tol < b;
        case 4: return a - tol <= b;
        case 5: return std::fabs(a - b) > tol;
        default: return false;
    }
}

// Edit's operator positions differ from Compare's: op1 combines with the destination last.
template <class Read>
inline double itemEdit(const ItemExpr& e, Read read) {
    bool has1 = itemOperand(e.mode1, e.item), has2 = itemOperand(e.mode2, e.item2);
    double x = read(e.mode1, e.item);
    if (!has1 && has2) { x = read(e.mode2, e.item2); has1 = true; has2 = false; }
    if (has2) x = itemArithmetic(x, read(e.mode2, e.item2), e.op2 <= 0 ? 1 : e.op2);
    x = has1 ? itemArithmetic(x, itemModifier(e.mod1), std::max(3, e.op3)) : itemModifier(e.mod1);
    x = itemTransform(x, e.round1, e.sign1);
    if (e.op1) x = itemArithmetic(read(e.targetMode <= 0 ? 1 : e.targetMode, e.target), x, e.op1);
    return itemTransform(x, e.round2, e.sign2);
}

// Match CVTTSD2SI, including its indefinite integer on overflow or NaN.
inline int32_t itemInteger(double x) {
    return !std::isfinite(x) || x < -2147483648.0 || x >= 2147483648.0
        ? INT32_MIN : (int32_t)x;
}

// Item and timer IDs are clamped by GJEffectManager, not by the expression evaluator.
inline int itemID(int id) { return std::clamp(id, 0, 9999); }

struct ItemValue {
    int mode = 1, id = 0;
    double value = 0.0;
    bool active = false, ignoreWarp = false, stop = false;
    bool present = false;
    float rate = 1.f;
    double target = 0.0;
    int group = -1;
};
struct ItemEvent { int node = -1; long long tick = 0; };
struct ItemTimerWatch { int node = -1; float last = 0.f; };
struct ItemMemory {
    std::vector<ItemValue> values;
    std::vector<uint8_t> fired;
    std::vector<ItemEvent> pending;
    std::vector<ItemTimerWatch> watches;
    // Canonical semantic words, never pointers, padding, or a digest-only identity.
    std::vector<uint64_t> words;
};
inline std::mutex g_itemMemoryMutex;
inline std::unordered_map<uint64_t, std::vector<std::unique_ptr<ItemMemory>>> g_itemMemories;
inline std::vector<std::unique_ptr<std::vector<uint64_t>>> g_itemParsedKeys;

// Encode doubles without decimal round trips or floating equality losing signed zero.
inline uint64_t itemBits(double x) { uint64_t v; std::memcpy(&v, &x, sizeof(v)); return v; }

// Intern immutable snapshots; the lock is taken only when an Item event changes state.
inline const ItemMemory* keepItemMemory(ItemMemory m) {
    uint64_t h = 1469598103934665603ull;
    for (uint64_t x : m.words) h = (h ^ x) * 1099511628211ull;
    std::lock_guard<std::mutex> lock(g_itemMemoryMutex);
    auto& bucket = g_itemMemories[h];
    for (const auto& old : bucket) if (old->words == m.words) return old.get();
    bucket.push_back(std::make_unique<ItemMemory>(std::move(m)));
    return bucket.back().get();
}

} // namespace dp
