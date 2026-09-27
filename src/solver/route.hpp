#pragma once
// ---- What a coin needs first (cfg routeprereq) ----------------------------------------------------
//
// A coin that is switched on only by something the player has to enter first -- a touch box, a key,
// a toggle block -- or that sits on a platform, or behind an orb, that only such a thing switches
// on. The loop's coin ranking (repair.hpp coinmisspost) measures an attempt at its closest approach
// to the COIN, so it repairs the stretch around the coin however many times it takes, while the
// thing that decides the coin can be thousands of pixels earlier. SubZero 4002's second coin: the
// ten platforms under it (group 683) are switched off by a Toggle at x=-29 and on by the key uid
// 6166 at x=13,647 -- 4,314 px before the coin -- and every repair went to the coin.
//
// Read off the level's own objects at level start, the way the DP reads its touch boxes
// (dp/src/dp/triggers.hpp): a trigger with touch=1 is its own box, one with spawn=1 fires only when
// something whose target group holds it fires, and one with neither fires on its own crossing. A
// prerequisite is the ROOT of a chain that switches a group ON, where an x-crossing Toggle (or the
// level start) switched that group OFF before the coin. Counters (Count, Item Compare, Tap) are the
// DP's own gates and are not listed here.
// (Included inside namespace p1 by solver_bridge.hpp, like the other solver/ headers: no standard
// headers here.)

namespace route {

struct Prereq {
    int coin = -1;                 // index into solver::g_coins
    int boxUid = -1, boxId = 0;    // the root the player has to enter
    float bx = 0.f, by = 0.f, bhw = 0.f, bhh = 0.f;   // its rect: centre and half sizes
    int watchUid = -1;             // an object the chain switches on (the coin, or the gate object)
    GameObject* watch = nullptr;
    bool gate = false;             // false: switches the coin itself on; true: a platform/orb near it
};
inline std::vector<Prereq> g_prereq;
// This attempt's first tick each prerequisite's object came ON after being seen off (-1 = not yet),
// and the same for the attempt the loop keeps as its deepest (repair.hpp keepAsDeepest).
inline std::vector<long long> g_onTick, g_onTickDeepest;
inline std::vector<uint8_t> g_seenOff;
inline bool g_built = false;

// What the route stands on or uses: solids and platforms (triggers.hpp collidableType, less the
// hazard, type 2), orbs and rings (activatorTargetSafe) and pads. A hazard switched ON is a trap,
// not a way: SubZero 4002's touch Toggle uid 18126 brings the saw in beside the third coin.
inline bool usedType(int t) {
    switch (t) {
        case 0: case 21: case 47:
        case 11: case 12: case 13: case 29: case 32: case 35: case 37: case 38: case 43:
        case 8: case 9: case 10: case 44:
            return true;
        default:
            return false;
    }
}

inline void clear() {
    g_prereq.clear();
    g_onTick.clear();
    g_onTickDeepest.clear();
    g_seenOff.clear();
    g_built = false;
}

inline void onAttemptStart() {
    g_onTick.assign(g_prereq.size(), -1);
    g_seenOff.assign(g_prereq.size(), 0);
}

// Once per tick of an attempt.
inline void sample(long long tick) {
    for (size_t k = 0; k < g_prereq.size() && k < g_onTick.size(); ++k) {
        GameObject* w = g_prereq[k].watch;
        if (!w) continue;
        if (w->m_isGroupDisabled) g_seenOff[k] = 1;
        else if (g_seenOff[k] && g_onTick[k] < 0) g_onTick[k] = tick;
    }
}

// The prerequisites of `coin` the attempt with `onTick` had not met by tick `by` (by < 0: at all).
inline std::vector<size_t> unmet(int coin, const std::vector<long long>& onTick, long long by) {
    std::vector<size_t> out;
    for (size_t k = 0; k < g_prereq.size(); ++k) {
        if (g_prereq[k].coin != coin) continue;
        const long long t = k < onTick.size() ? onTick[k] : -1;
        if (t < 0 || (by >= 0 && t > by)) out.push_back(k);
    }
    return out;
}

template <class Log>
inline void build(GJBaseGameLayer* l, const std::vector<GameObject*>& coinObjs, Log&& log) {
    clear();
    if (!l || !l->m_objects) return;
    std::unordered_map<int, GameObject*> byUid;
    std::unordered_map<int, std::vector<int>> members;   // group -> uids
    auto groupsOf = [](GameObject* o) {
        std::vector<int> g;
        if (o && o->m_groupCount > 0 && o->m_groups)
            for (int k = 0; k < std::min((int)o->m_groupCount, 10); ++k)
                g.push_back((int)(*o->m_groups)[k]);
        return g;
    };
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!o) continue;
        byUid[o->m_uniqueID] = o;
        for (const int g : groupsOf(o)) members[g].push_back(o->m_uniqueID);
    }
    auto eff = [&](int uid) -> EffectGameObject* {
        const auto it = byUid.find(uid);
        return it == byUid.end() ? nullptr : geode::cast::typeinfo_cast<EffectGameObject*>(it->second);
    };
    // A pickup (collectible, type 30) or a toggle block (1594) with "activate group" switches its
    // target group ON when entered (triggers.hpp --activators). Touch and spawn rows are not.
    auto isActivator = [](EffectGameObject* e) {
        return e && e->m_targetGroupID > 0 && e->m_activateGroup && !e->m_isSpawnTriggered
               && (((int)e->m_objectType == 30 && !e->m_isTouchTriggered) || e->m_objectID == 1594);
    };
    std::unordered_map<int, std::vector<int>> firers;    // group -> triggers that SPAWN it
    std::unordered_map<int, std::vector<int>> onEff;     // group -> what switches it ON
    std::unordered_map<int, float> offX;                 // group -> first x-crossing OFF (x)
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
        auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(o);
        if (!e || e->m_targetGroupID <= 0) continue;
        const int g = e->m_targetGroupID, id = e->m_objectID;
        if (id == 1049) {
            if (e->m_activateGroup) onEff[g].push_back(e->m_uniqueID);
            else if (!e->m_isTouchTriggered && !e->m_isSpawnTriggered) {
                const float x = e->getPositionX();
                const auto it = offX.find(g);
                if (it == offX.end() || x < it->second) offX[g] = x;
            }
        } else if (isActivator(e)) {
            onEff[g].push_back(e->m_uniqueID);
        } else if (id != 901 && id != 1616 && id != 1007 && id != 1006) {
            firers[g].push_back(e->m_uniqueID);
        }
    }
    // The roots that make trigger `uid` fire: boxes, keys, toggle blocks. An x-crossing root is not a
    // prerequisite (every route passes it), and a counter root is the DP's gate, so both end the walk
    // empty.
    std::vector<int> roots;
    std::unordered_set<int> seen;
    auto walk = [&](auto&& self, int uid, int depth) -> void {
        if (depth > 8 || !seen.insert(uid).second) return;
        EffectGameObject* e = eff(uid);
        if (!e) return;
        if (isActivator(e) || e->m_isTouchTriggered) {
            if (e->m_objectID != 1595) roots.push_back(uid);
            return;
        }
        const int id = e->m_objectID;
        if (id == 1611 || id == 1811 || id == 3620 || id == 1595) return;
        if (!e->m_isSpawnTriggered) return;
        for (const int g : groupsOf(e))
            if (const auto f = firers.find(g); f != firers.end())
                for (const int fu : f->second) self(self, fu, depth + 1);
    };
    auto rootsOf = [&](int uid) {
        roots.clear();
        seen.clear();
        walk(walk, uid, 0);
        return roots;
    };
    // A gate object this close to the coin (or to a prerequisite's box), on both axes. Derived, not
    // picked: the census was re-run off the dumps at radii 60..3,000 on the six official levels
    // with switched coins (lv20, lv21, lv22, SubZero 4001-4003) and the two route rigs. Every
    // prerequisite that is real needs at most 200 (rig route2's step under its second key; 176 for
    // SubZero 4003's first coin, 166 for lv20's second), and the first box not shown to be needed
    // comes in at 392 (SubZero 4002, third coin). Anything in [200, 392) gives the same census on
    // all eight; below it the chain in route2 and 4003's first coin are lost, above it the extra box
    // is ranked.
    constexpr float kNear = 300.f;
    std::unordered_set<long long> have;   // (coin, root)
    auto add = [&](int ci, int root, GameObject* w, bool gate) {
        if (!have.insert(((long long)ci << 32) | (unsigned)root).second) return;
        GameObject* r = byUid[root];
        if (!r) return;
        const auto rr = r->getObjectRect();
        Prereq p;
        p.coin = ci;
        p.boxUid = root;
        p.boxId = r->m_objectID;
        p.bx = rr.origin.x + rr.size.width * 0.5f;
        p.by = rr.origin.y + rr.size.height * 0.5f;
        p.bhw = rr.size.width * 0.5f;
        p.bhh = rr.size.height * 0.5f;
        p.watchUid = w ? w->m_uniqueID : -1;
        p.watch = w;
        p.gate = gate;
        g_prereq.push_back(p);
    };
    // A group switched off by an x-crossing before the coin and on by a chain: its roots.
    auto consider = [&](int ci, GameObject* w, float coinX, bool gate) {
        for (const int g : groupsOf(w)) {
            const auto off = offX.find(g);
            const auto on = onEff.find(g);
            if (off == offX.end() || off->second > coinX || on == onEff.end()) continue;
            for (const int e : on->second)
                for (const int root : rootsOf(e)) add(ci, root, w, gate);
        }
    };
    auto gatesNear = [&](int ci, GameObject* self, float px, float py) {
        for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
            if (!o || o == self || !usedType((int)o->m_objectType)) continue;
            if (std::fabs(o->getPositionX() - px) > kNear || std::fabs(o->getPositionY() - py) > kNear)
                continue;
            consider(ci, o, px, true);
        }
    };
    for (size_t ci = 0; ci < coinObjs.size(); ++ci) {
        GameObject* c = coinObjs[ci];
        if (!c) continue;
        consider((int)ci, c, c->getPositionX(), false);
        gatesNear((int)ci, c, c->getPositionX(), c->getPositionY());
    }
    // ...and what a prerequisite's own box needs: a key on a ledge that only another key's step
    // leads up to. Its prerequisites are the coin's too, and the earliest one unmet is what the
    // loop ranks at (repair.hpp coinApproachRoute). Two more levels at most: at the radius above,
    // one level is all the eight levels and rigs need (route2), and one, two and three levels give
    // the same census on every one of them -- the bound is not binding anywhere measured, it only
    // keeps a pathological level from walking its whole trigger map.
    for (int depth = 0; depth < 2; ++depth) {
        const size_t n = g_prereq.size();
        for (size_t k = 0; k < n; ++k) {
            const Prereq p = g_prereq[k];
            GameObject* box = byUid[p.boxUid];
            if (box) gatesNear(p.coin, box, p.bx, p.by);
        }
        if (g_prereq.size() == n) break;
    }
    for (const Prereq& p : g_prereq)
        log(p);
    g_onTickDeepest.assign(g_prereq.size(), -1);
    onAttemptStart();
    g_built = true;
}

}  // namespace route
