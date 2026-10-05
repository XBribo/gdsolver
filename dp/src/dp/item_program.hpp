#pragma once
#include "dp/triggers.hpp"
#include <map>

namespace dp {

enum class ItemKind { Player, Spawn, Edit, Compare, Press, TimerStart, TimerEvent, TimerControl };
struct ItemNode {
    ItemKind kind = ItemKind::Spawn;
    PlayerTriggerMeta meta;
    ItemExpr expr;
    PlayerEffect effect;
    std::vector<int> yes, no;
    int delay = 0;
    int pressBit[2] = {-1, -1};
    TrigRow row{};
};
struct ItemProgram {
    std::vector<ItemNode> nodes;
    std::vector<int> roots;
    std::vector<std::pair<int, int>> values;
    std::unordered_map<int, int> byUid;
    bool clock = false;
    int points = 0, attempts = 1;
};
inline ItemProgram g_itemProgram;

// Extract the complete arithmetic layout, rather than the old coin-gate special case.
inline ItemExpr itemExpression(const TrigRow& r) {
    return {r.item, r.item2, r.target, r.i1mode, r.i2mode, r.targetMode,
        r.res1, r.res2, r.res3, r.round1, r.round2, r.sign1, r.sign2,
        (float)r.mod1, (float)r.mod2, (float)r.tolerance};
}

// A value's ID is irrelevant for the global points, clock and attempt sources.
inline std::pair<int, int> itemValueKey(int mode, int id) {
    return {mode, mode == 1 || mode == 2 ? itemID(id) : 0};
}

// Compile only Item components that reach a player write; unrelated art stays outside the VM.
inline bool loadItemProgram(Level& L, const std::string& trigPath, const std::string& groupPath,
                            long long anchorTick = 0, const std::string& anchor = {}) {
    g_itemProgram = {};
    if (L.playerEffects.empty() || !L.unsupported.empty() || !L.playerFallback.empty()
        || trigPath.empty() || groupPath.empty()) return false;
    const auto* rows = trigRowsCached(trigPath);
    const auto* groups = groupMembersCached(groupPath);
    if (!rows || !groups) return false;
    std::set<int> keep;
    for (const auto& e : L.playerEffects) keep.insert(e.teleport.uid);
    auto edges = [&](const TrigRow& r) {
        std::vector<int> result;
        if (r.id != 1268 && r.id != 3620 && r.id != 1594 && r.id != 3614 && r.id != 3615) return result;
        for (int group : {r.target, r.id == 3620 ? r.center : 0}) {
            const auto g = groups->find(group);
            if (group > 0 && g != groups->end()) for (int uid : g->second) {
                const auto row = rows->find(uid);
                const auto meta = L.playerSources.find(uid);
                if ((row != rows->end() && row->second.spawn)
                    || (meta != L.playerSources.end() && meta->second.spawn)) result.push_back(uid);
            }
        }
        return result;
    };
    bool changed = true, compared = false;
    while (changed) {
        changed = false;
        for (const auto& p : *rows) for (int child : edges(p.second))
            if (keep.count(child) && keep.insert(p.first).second) changed = true;
    }
    for (int uid : keep) {
        const auto r = rows->find(uid);
        compared |= r != rows->end() && r->second.id == 3620;
    }
    if (!compared) return false;   // existing one-shot player levels keep their original path
    std::set<std::pair<int, int>> variables;
    auto value = [&](int mode, int id) {
        if (mode > 0 && mode <= 5) variables.insert(itemValueKey(mode, id));
    };
    changed = true;
    while (changed) {
        const size_t before = keep.size() + variables.size();
        std::vector<int> scan(keep.begin(), keep.end());
        for (int uid : scan) {
            const auto r = rows->find(uid);
            if (r == rows->end()) continue;
            const auto& t = r->second;
            for (int child : edges(t)) keep.insert(child);
            if (t.id == 3619 || t.id == 3620) {
                if (t.id == 3620 || itemOperand(t.i1mode, t.item)) value(t.i1mode <= 0 ? 1 : t.i1mode, t.item);
                if (itemOperand(t.i2mode, t.item2)) value(t.i2mode, t.item2);
                if (t.id == 3619) value(t.targetMode <= 0 ? 1 : t.targetMode, t.target);
            } else if (t.id == 3614 || t.id == 3615 || t.id == 3617) value(2, t.item);
        }
        for (const auto& p : *rows) {
            const auto& t = p.second;
            if ((t.id == 3619 && variables.count(itemValueKey(t.targetMode <= 0 ? 1 : t.targetMode, t.target)))
                || ((t.id == 3614 || t.id == 3615 || t.id == 3617) && variables.count(itemValueKey(2, t.item))))
                keep.insert(p.first);
            for (int child : edges(t)) if (keep.count(child)) keep.insert(p.first);
        }
        changed = before != keep.size() + variables.size();
    }
    ItemProgram program;
    const auto geometryTouch = g_touch;
    // No partial graph or press-only slots escape a failed admission.
    auto fallback = [&](const std::string& reason) {
        L.playerFallback = "Item program: " + reason;
        g_touch = geometryTouch;
        g_playerRoots.clear();
        g_itemProgram = {};
        return true;
    };
    if (anchorTick > 0 && (anchor.find("itemstate=1|") == std::string::npos
        || anchor.find("player=") == std::string::npos))
        return fallback("native Item anchor unavailable; continuing replay repair without this graph");
    for (int uid : keep) {
        const auto r = rows->find(uid);
        const auto m = L.playerSources.find(uid);
        if (m == L.playerSources.end() || (r == rows->end()
            && m->second.id != 2066 && m->second.id != 3022))
            return fallback("missing source metadata for uid " + std::to_string(uid) + "; refresh exports");
        ItemNode n;
        n.meta = m->second;
        if (r != rows->end()) n.row = r->second;
        else { n.row.uid = uid; n.row.id = n.meta.id; }
        const auto& t = n.row;
        if (n.meta.onExit || n.meta.disabled || n.meta.noTouch || n.meta.channel < 0 || n.meta.channel > 15)
            return fallback("exit, disabled or invalid-channel source uid " + std::to_string(uid));
        if (n.meta.touch && n.meta.multi && t.id != 1594)
            return fallback("repeating touch source needs a measured re-entry latch, uid " + std::to_string(uid));
        if (t.id == 3022 || t.id == 2066) {
            n.kind = ItemKind::Player;
            const auto e = std::find_if(L.playerEffects.begin(), L.playerEffects.end(),
                [&](const auto& e) { return e.teleport.uid == uid; });
            if (e == L.playerEffects.end()) return fallback("missing player effect uid " + std::to_string(uid));
            n.effect = *e;
        } else if (t.id == 1268) {
            n.kind = ItemKind::Spawn;
            if (n.meta.delayRange || n.meta.ordered || !t.remap.empty())
                return fallback("random, ordered or remapped Spawn uid " + std::to_string(uid));
            const double hops = std::ceil(n.meta.delay / (double)(float)(1.0 / 240.0));
            if (!std::isfinite(hops) || hops < 0 || hops > INT32_MAX)
                return fallback("invalid Spawn delay uid " + std::to_string(uid));
            n.delay = (int)hops;
            if (n.delay && !g_timeWarps.empty()) return fallback("delayed Spawn with TimeWarp needs a measured event clock");
        } else if (t.id == 3619 || t.id == 3620) {
            n.kind = t.id == 3619 ? ItemKind::Edit : ItemKind::Compare;
            if (!t.cmpCols || !t.itemCols) return fallback("missing arithmetic fields for uid " + std::to_string(uid));
            n.expr = itemExpression(t);
            if (!std::isfinite((float)t.mod1) || !std::isfinite((float)t.mod2) || !std::isfinite((float)t.tolerance))
                return fallback("non-finite arithmetic fields for uid " + std::to_string(uid));
        } else if (t.id == 1594 && t.sponly) n.kind = ItemKind::Press;
        else if (t.id == 3614 || t.id == 3615 || t.id == 3617) {
            n.kind = t.id == 3614 ? ItemKind::TimerStart : t.id == 3615 ? ItemKind::TimerEvent : ItemKind::TimerControl;
            if (!t.timerCols || !std::isfinite(t.timerStart) || !std::isfinite(t.timerTarget) || !std::isfinite(t.timerRate))
                return fallback("missing or invalid timer fields for uid " + std::to_string(uid));
            if (t.item < 0 || t.item > 9999 || !g_timeWarps.empty())
                return fallback("timer ID or TimeWarp combination needs a measured clock, uid " + std::to_string(uid));
        } else return fallback("unmodelled group member uid " + std::to_string(uid));
        program.byUid.emplace(uid, (int)program.nodes.size());
        program.nodes.push_back(std::move(n));
    }
    for (const auto& p : *rows) {
        const auto& t = p.second;
        if ((t.id == 1817 || t.id == 3641 || t.id == 1611 || t.id == 1811)
            && variables.count(itemValueKey(1, t.item)))
            return fallback("unmodelled counter writer/subscriber uid " + std::to_string(p.first));
        if (t.id != 901 && t.id != 1346 && t.id != 1347 && t.id != 1049 && t.id != 1616
            && t.id != 1814 && t.id != 2067 && !(t.id >= 3006 && t.id <= 3016)) continue;
        const auto g = groups->find(t.target);
        if (g != groups->end()) for (int uid : g->second)
            if (keep.count(uid) || std::any_of(L.playerSources.begin(), L.playerSources.end(),
                [&](const auto& e) { return e.second.exitUid == uid; }))
                return fallback("group-controlled source/effect uid " + std::to_string(uid));
    }
    auto groupNodes = [&](int group) {
        std::vector<int> result;
        const auto g = groups->find(group);
        if (group > 0 && g != groups->end()) for (int uid : g->second) {
            const auto n = program.byUid.find(uid);
            if (n != program.byUid.end() && program.nodes[(size_t)n->second].meta.spawn) result.push_back(n->second);
        }
        return result;
    };
    for (size_t i = 0; i < program.nodes.size(); ++i) {
        auto& n = program.nodes[i];
        if (n.kind == ItemKind::Spawn || n.kind == ItemKind::Compare || n.kind == ItemKind::Press
            || n.kind == ItemKind::TimerStart || n.kind == ItemKind::TimerEvent) n.yes = groupNodes(n.row.target);
        if (n.kind == ItemKind::Compare) n.no = groupNodes(n.row.center);
        if (!n.meta.spawn || n.meta.touch) program.roots.push_back((int)i);
        if (n.kind == ItemKind::Press && !n.meta.spawn) {
            if (n.meta.multi) return fallback("repeating press-ring admission needs a measured native latch");
            const int copies = n.meta.singleTouch ? 1 : 2;
            for (int body = 0; body < copies; ++body) {
                if (g_touch.size() >= (size_t)kTouchBits) return fallback("press sources exceed geometry's shared source slots");
                TouchTrig t{};
                t.uid = n.meta.uid; t.id = 1594; t.cx = n.meta.cx; t.cy = n.meta.cy;
                t.hw = n.meta.hw; t.hh = n.meta.hh;
                t.spawnRing = true; t.press = true; t.playerOnly = true;
                t.playerBody = copies == 2 ? body + 1 : 0;
                t.itemNode = (int)i;
                n.pressBit[body] = (int)g_touch.size();
                g_touch.push_back(std::move(t));
            }
        }
    }
    std::vector<uint8_t> visiting(program.nodes.size());
    auto acyclic = [&](auto&& self, int i) -> bool {
        if (visiting[(size_t)i] == 1) return false;
        if (visiting[(size_t)i] == 2) return true;
        visiting[(size_t)i] = 1;
        const auto& node = program.nodes[(size_t)i];
        // One-shot latches and asynchronous sources break immediate recursive activation.
        if (!node.meta.multi || node.delay || node.kind == ItemKind::TimerStart || node.kind == ItemKind::TimerEvent) {
            visiting[(size_t)i] = 2;
            return true;
        }
        for (const auto* children : {&node.yes, &node.no})
            for (int child : *children) if (!self(self, child)) return false;
        visiting[(size_t)i] = 2;
        return true;
    };
    for (size_t i = 0; i < program.nodes.size(); ++i)
        if (!acyclic(acyclic, (int)i)) return fallback("zero-delay multi-activation cycle has no finite transition");
    std::set<int> reached;
    auto reach = [&](auto&& self, int i) -> void {
        if (!reached.insert(i).second) return;
        for (const auto* children : {&program.nodes[(size_t)i].yes, &program.nodes[(size_t)i].no})
            for (int child : *children) self(self, child);
    };
    for (int root : program.roots) reach(reach, root);
    for (size_t i = 0; i < program.nodes.size(); ++i)
        if (program.nodes[i].kind == ItemKind::Player && !reached.count((int)i))
            return fallback("player effect uid " + std::to_string(program.nodes[i].meta.uid) + " has no modelled activation source");
    program.values.assign(variables.begin(), variables.end());
    if (std::count_if(program.values.begin(), program.values.end(), [](const auto& v) { return v.first == 2; }) > 1)
        return fallback("interacting timer callback order needs native measurement");
    for (const auto& v : program.values) program.clock |= v.first == 4;
    bool globals = false;
    for (const auto& n : program.nodes) if (n.row.globalsCols && n.row.attempts >= 0) {
        program.points = n.row.points; program.attempts = n.row.attempts; globals = true;
    }
    if ((variables.count({3, 0}) || variables.count({5, 0}) || program.clock) && !globals)
        return fallback("global value sources need refreshed platform-specific exports");
    std::stable_sort(program.roots.begin(), program.roots.end(), [&](int a, int b) {
        const auto& x = program.nodes[(size_t)a].meta; const auto& y = program.nodes[(size_t)b].meta;
        if (x.touch != y.touch) return !x.touch;
        return x.touch ? x.uid < y.uid : std::tie(x.order, x.cx, x.uid) < std::tie(y.order, y.cx, y.uid);
    });
    if (!g_rotTrig.empty()) return fallback("autonomous Item sources share an unmeasured gameplay-rotation queue");
    buildTouchMoveTicks();
    buildPressWindows();
    g_playerRoots.clear();
    g_itemProgram = std::move(program);
    return true;
}

// Build semantic words once per mutation, with UIDs so independently compiled maps remain comparable.
inline const ItemMemory* saveItems(ItemMemory m) {
    m.words.clear();
    for (const auto& v : m.values) {
        m.words.insert(m.words.end(), {(uint64_t)v.mode, (uint64_t)v.id, itemBits(v.value),
            (uint64_t)v.active, (uint64_t)v.ignoreWarp, (uint64_t)v.stop, itemBits(v.rate),
            itemBits(v.target), v.group < 0 ? UINT64_MAX : (uint64_t)g_itemProgram.nodes[(size_t)v.group].meta.uid,
            (uint64_t)v.present});
    }
    m.words.push_back(UINT64_MAX);
    for (size_t i = 0; i < g_itemProgram.nodes.size(); ++i) {
        m.words.push_back((uint64_t)g_itemProgram.nodes[i].meta.uid);
        for (int body = 0; body < 3; ++body) m.words.push_back(m.fired[i * 3 + (size_t)body]);
    }
    m.words.push_back(UINT64_MAX);
    for (const auto& e : m.pending) {
        m.words.push_back((uint64_t)g_itemProgram.nodes[(size_t)e.node].meta.uid);
        m.words.push_back((uint64_t)e.tick);
    }
    m.words.push_back(UINT64_MAX);
    for (const auto& w : m.watches) {
        m.words.push_back((uint64_t)g_itemProgram.nodes[(size_t)w.node].meta.uid);
        m.words.push_back(itemBits(w.last));
    }
    return keepItemMemory(std::move(m));
}

// Empty counters are real zero values; global inputs come from the native export.
inline const ItemMemory* initialItems() {
    if (g_itemProgram.nodes.empty()) return nullptr;
    ItemMemory m;
    for (const auto& k : g_itemProgram.values) {
        ItemValue v; v.mode = k.first; v.id = k.second;
        if (v.mode == 3) v.value = g_itemProgram.points;
        if (v.mode == 5) v.value = g_itemProgram.attempts;
        m.values.push_back(v);
    }
    m.fired.resize(g_itemProgram.nodes.size() * 3);
    return saveItems(std::move(m));
}

} // namespace dp
