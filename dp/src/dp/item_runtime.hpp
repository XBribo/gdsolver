#pragma once
#include <optional>

namespace dp {

inline void applyPlayerEffect(State& c, const PlayerEffect& e, int triggeringPlayer);

// Look up a typed value; Item IDs and timer IDs occupy different namespaces.
inline ItemValue* itemValue(ItemMemory& m, int mode, int id) {
    const auto key = itemValueKey(mode, id);
    for (auto& v : m.values) if (v.mode == key.first && v.id == key.second) return &v;
    return nullptr;
}

// Native getItemValue: unsupported source modes yield zero, not another counter.
inline double readItem(const ItemMemory& m, double clock, int mode, int id) {
    if (mode == 4) return clock;
    const auto key = itemValueKey(mode, id);
    for (const auto& v : m.values) if (v.mode == key.first && v.id == key.second) return v.value;
    return 0.0;
}

// Execute a compiled group synchronously; delayed Spawn groups enter the scheduler instead.
inline void executeItem(State& c, ItemMemory& m, int index, long long tick, int body) {
    const auto& n = g_itemProgram.nodes[(size_t)index];
    if (n.meta.silent) return;
    const size_t latch = (size_t)index * 3 + (size_t)body;
    if (m.fired[latch] && !n.meta.multi) return;
    m.fired[latch] = 1;
    auto read = [&](int mode, int id) { return readItem(m, c.itemClock, mode, id); };
    auto group = [&](const std::vector<int>& children) {
        for (int child : children) executeItem(c, m, child, tick, 0);
    };
    switch (n.kind) {
        case ItemKind::Player: applyPlayerEffect(c, n.effect, n.meta.touch ? body : 0); break;
        case ItemKind::Spawn:
            if (n.delay) m.pending.push_back({index, tick + n.delay});
            else group(n.yes);
            break;
        case ItemKind::Press: group(n.yes); break;
        case ItemKind::Compare: group(itemCompare(n.expr, read) ? n.yes : n.no); break;
        case ItemKind::Edit: {
            const int mode = n.expr.targetMode <= 0 ? 1 : n.expr.targetMode;
            if ((n.expr.target <= 0 && mode != 3) || mode > 3) break;
            if (auto* v = itemValue(m, mode, n.expr.target)) {
                const double x = itemEdit(n.expr, read);
                v->value = mode == 2 ? std::fmin(9999999.0, std::fmax(-9999999.0, x)) : (double)itemInteger(x);
                v->present = mode == 2 || (mode == 1 && v->value != 0.0);
            }
            break;
        }
        case ItemKind::TimerStart: {
            if (auto* v = itemValue(m, 2, n.row.item)) {
                if (!v->present || !n.row.timerKeep)
                    v->value = v->present ? n.row.timerStart : (double)(float)n.row.timerStart;
                v->target = v->present ? n.row.timerTarget : (double)(float)n.row.timerTarget;
                v->present = true; v->active = !n.row.timerPaused;
                v->stop = n.row.timerStop; v->ignoreWarp = n.row.timerIgnore;
                v->rate = n.row.timerRate; v->group = index;
            }
            break;
        }
        case ItemKind::TimerControl:
            if (auto* v = itemValue(m, 2, n.row.item); v && v->present && n.row.timerControl >= 0 && n.row.timerControl <= 1)
                v->active = n.row.timerControl == 0;
            break;
        case ItemKind::TimerEvent: m.watches.push_back({index, 0.f}); break;
    }
}

// Apply scheduler/timer updates before either player moves (native GJEffectManager::update).
inline void itemPendingTick(State& c, const StepCtx& K) {
    if (!c.item) return;
    const float dt = (float)(1.0 / 240.0);
    if (g_itemProgram.clock) c.itemClock += 1.0 / 240.0;
    const bool timers = std::any_of(c.item->values.begin(), c.item->values.end(),
        [](const auto& v) { return v.mode == 2 && v.present && v.active; });
    if (!timers && c.item->watches.empty() && std::none_of(c.item->pending.begin(), c.item->pending.end(),
        [&](const auto& e) { return e.tick <= K.t; })) return;
    ItemMemory m = *c.item;
    std::vector<ItemEvent> due;
    for (const auto& e : m.pending) if (e.tick <= K.t) due.push_back(e);
    m.pending.erase(std::remove_if(m.pending.begin(), m.pending.end(),
        [&](const auto& e) { return e.tick <= K.t; }), m.pending.end());
    std::stable_sort(due.begin(), due.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    for (const auto& e : due)
        for (int child : g_itemProgram.nodes[(size_t)e.node].yes) executeItem(c, m, child, K.t, 0);
    for (auto& v : m.values) if (v.mode == 2 && v.present) {
        const double old = v.value;
        if (v.active) v.value += (double)(float)(dt * v.rate);
        if (v.active && v.stop && ((v.target > old && v.value >= v.target) || (old > v.target && v.value <= v.target))) {
            v.value = v.target; v.active = false;
            if (v.group >= 0 && v.group < (int)g_itemProgram.nodes.size())
                for (int child : g_itemProgram.nodes[(size_t)v.group].yes) executeItem(c, m, child, K.t, 0);
        }
        for (size_t i = 0; i < m.watches.size();) {
            auto& w = m.watches[i];
            const auto& n = g_itemProgram.nodes[(size_t)w.node];
            if (itemID(n.row.item) != v.id) { ++i; continue; }
            const float target = (float)n.row.timerTarget;
            const bool fire = (target > w.last && v.value >= target && v.rate > 0.f)
                || (w.last > target && v.value <= target && v.rate < 0.f);
            w.last = (float)v.value;
            const int node = w.node;
            if (fire && !n.row.timerMulti) m.watches.erase(m.watches.begin() + (ptrdiff_t)i);
            else ++i;
            if (fire) for (int child : g_itemProgram.nodes[(size_t)node].yes) executeItem(c, m, child, K.t, 0);
        }
    }
    c.item = saveItems(std::move(m));
}

// Collision and crossing phases use the same admission as player triggers; press rings fire in buttons.
inline void itemSourceTick(State& c, const State& before, const StepCtx& K, int body, bool automatic,
                           double touchY = 0.0) {
    if (!c.item) return;
    double wx, wy;
    fromFrame(c.frame, body == 2 ? c.xAbs2 : c.xAbs,
              automatic ? (body == 2 ? c.y2 : c.y) : touchY, wx, wy);
    const double half = playerHalf(body == 2 ? c.mode2 : c.mode, (body == 2 ? c.mini2 : c.mini) != 0);
    std::optional<ItemMemory> edited;
    for (int index : g_itemProgram.roots) {
        const auto& n = g_itemProgram.nodes[(size_t)index];
        const int latchBody = automatic || n.meta.singleTouch ? 0 : body;
        const auto& memory = edited ? *edited : *c.item;
        if (memory.fired[(size_t)index * 3 + (size_t)latchBody]) continue;
        bool fire = false;
        if (n.kind == ItemKind::Press) {
            if (automatic) continue;
            const int bit = n.pressBit[n.meta.singleTouch ? 0 : body - 1];
            fire = bit >= 0 && c.trig.test(bit) && !before.trig.test(bit);
        } else if ((!n.meta.touch) == automatic) {
            if (automatic) {
                if (n.meta.channel != c.rotChan) continue;
                const bool reverse = ((c.rotRev >> n.meta.channel) & 1u) != 0;
                const double p = (c.frame & 1) ? n.meta.cy : n.meta.cx;
                const double ref = (c.frame & 1) ? wy : wx;
                fire = reverse ? ref <= p : ref >= p;
            } else fire = std::fabs(wx - n.meta.cx) <= n.meta.hw + half
                && std::fabs(wy - n.meta.cy) <= n.meta.hh + half;
        }
        if (!fire) continue;
        if (!edited) edited = *c.item;
        executeItem(c, *edited, index, K.t, latchBody);
        fromFrame(c.frame, body == 2 ? c.xAbs2 : c.xAbs, body == 2 ? c.y2 : c.y, wx, wy);
    }
    if (edited) c.item = saveItems(std::move(*edited));
}

// Split the versioned native payload without accepting partial numeric fields.
inline std::vector<std::string> itemFields(const std::string& text, char delimiter) {
    std::vector<std::string> fields;
    std::istringstream in(text);
    for (std::string field; std::getline(in, field, delimiter);) fields.push_back(std::move(field));
    if (!text.empty() && text.back() == delimiter) fields.emplace_back();
    return fields;
}

// Stream conversion rejects overflow, suffixes and non-finite real values.
template <class T> inline bool itemNumber(const std::string& text, T& value) {
    std::istringstream in(text);
    if (!(in >> value)) return false;
    in >> std::ws;
    return in.eof() && std::isfinite((double)value);
}

// Restore values, native source latches and outstanding callbacks without replaying old writes.
inline bool restoreItems(State& c, const std::string& text, const std::vector<PlayerFire>& history,
                         long long tick) {
    if (!c.item) return false;
    const auto fields = itemFields(text, '|');
    int points, attempts;
    double clock;
    std::vector<PlayerFire> nativeHistory;
    if (fields.size() != 8 || fields[0] != "1" || !itemNumber(fields[1], clock)
        || !itemNumber(fields[2], points) || !itemNumber(fields[3], attempts)) return false;
    if (!parsePlayerHistory(fields[7], tick, nativeHistory)) return false;
    (void)history;   // native Item snapshots carry their own attempt's latches
    ItemMemory m = *initialItems();
    for (const auto& node : g_itemProgram.nodes) if (node.kind == ItemKind::Press)
        for (int bit : node.pressBit) if (bit >= 0) { c.trig &= ~touchBit(bit); c.fireB[bit] = 0; }
    for (auto& v : m.values) {
        if (v.mode == 3) v.value = points;
        if (v.mode == 5) v.value = attempts;
    }
    std::set<std::pair<int, int>> seen;
    for (const auto& entry : itemFields(fields[4], ',')) {
        if (entry.empty()) continue;
        const auto f = itemFields(entry, ':');
        ItemValue v;
        int active, ignore, stop, present, uid;
        if ((f.size() != 10 && f.size() != 11) || !itemNumber(f[0], v.mode) || !itemNumber(f[1], v.id)
            || !itemNumber(f[2], v.value) || !itemNumber(f[3], active) || !itemNumber(f[4], ignore)
            || !itemNumber(f[5], stop) || !itemNumber(f[6], v.rate) || !itemNumber(f[7], v.target)
            || !itemNumber(f[8], uid) || !itemNumber(f[9], present)
            || (v.mode != 1 && v.mode != 2) || v.id < 0 || v.id > 9999
            || active < 0 || active > 1 || ignore < 0 || ignore > 1 || stop < 0 || stop > 1
            || present < 0 || present > 1 || !seen.insert({v.mode, v.id}).second) return false;
        v.active = active != 0; v.ignoreWarp = ignore != 0;
        v.stop = stop != 0; v.present = present != 0; v.group = -1;
        const auto n = g_itemProgram.byUid.find(uid);
        if (n != g_itemProgram.byUid.end()) v.group = n->second;
        if (auto* dest = itemValue(m, v.mode, v.id)) {
            if (f.size() == 11 && f[10] != "0") return false;
            if (v.active && v.stop && v.group < 0) return false;
            *dest = v;
        }
    }
    for (const auto& e : nativeHistory) {
        const auto n = g_itemProgram.byUid.find(e.uid);
        if (n == g_itemProgram.byUid.end()) continue;
        m.fired[(size_t)n->second * 3 + (size_t)e.body] = 1;
        const auto& node = g_itemProgram.nodes[(size_t)n->second];
        if (node.kind == ItemKind::Press) {
            const int bit = node.pressBit[e.body == 2 && !node.meta.singleTouch ? 1 : 0];
            if (bit >= 0) { c.trig.set(bit); c.fireB[bit] = (uint16_t)e.tick; }
        }
    }
    for (const auto& entry : itemFields(fields[5], ',')) {
        if (entry.empty()) continue;
        const auto f = itemFields(entry, ':');
        int uid; double delay;
        if ((f.size() != 2 && f.size() != 3) || !itemNumber(f[0], uid) || !itemNumber(f[1], delay) || delay < 0) return false;
        const auto n = g_itemProgram.byUid.find(uid);
        if (n == g_itemProgram.byUid.end()) continue;
        if (f.size() == 3 && f[2] != "0") return false;
        if (g_itemProgram.nodes[(size_t)n->second].kind != ItemKind::Spawn) return false;
        const double hops = std::ceil(delay / (double)(float)(1.0 / 240.0));
        if (hops > INT32_MAX) return false;
        m.pending.push_back({n->second, tick + std::max(1LL, (long long)hops)});
    }
    for (const auto& entry : itemFields(fields[6], ',')) {
        if (entry.empty()) continue;
        const auto f = itemFields(entry, ':');
        int uid; float last;
        if ((f.size() != 2 && f.size() != 3) || !itemNumber(f[0], uid) || !itemNumber(f[1], last)) return false;
        const auto n = g_itemProgram.byUid.find(uid);
        if (n == g_itemProgram.byUid.end()) continue;
        if (f.size() == 3 && f[2] != "0") return false;
        if (g_itemProgram.nodes[(size_t)n->second].kind != ItemKind::TimerEvent) return false;
        m.watches.push_back({n->second, last});
    }
    c.itemClock = g_itemProgram.clock ? clock : 0.0;
    c.item = saveItems(std::move(m));
    return true;
}

} // namespace dp
