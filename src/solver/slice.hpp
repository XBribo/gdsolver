#pragma once
// The level slice: which of a level's objects the player's run can depend on, and the level
// string without the rest.
//
// A heavy level is solved on a copy that keeps only those objects, and the plan that clears the
// copy is then verified on the level itself (mod/level_slice.hpp). The slice is one more
// approximation in front of the game, the way the model is: a wrong cut costs rounds, never a
// wrong answer, because nothing is filed as a solution until the original level has cleared.
//
// THE RULE. Every trigger (type 20), area trigger (type 45), keyframe point (3032) and object the
// game does not class as a decoration (type 7) is kept. A decoration is kept only when something
// reads its GROUP AS A POSITION:
//   - a trigger's centre (m_centerGroupID) or its move-to-target reference (m_targetModCenterID),
//     and for a move-to-target or dynamic move the moved group itself, whose reference point may
//     be any member -- counted only for a trigger that acts on a relevant group (below);
//   - an area trigger's target group, which it times its effect off as a whole (dropping
//     decorations from one such group delayed a heavy level's area move by 8 ticks for all 158
//     members);
//   - a teleport's destination (747, 2902, 3022, 3027);
//   - a camera trigger's target (edge 2062, static 1914, guide 2016), read from the level string
//     (keys 51 and 71) as well as from the object, since the static camera keeps its centre group
//     in key 71; in free mode the camera sets the flight band;
//   - the level's spawn group (settings kA36): the player appears at its object's y.
// ...or when it is a group parent (key 34), or when it sets the level's extent (the leftmost,
// rightmost, lowest and highest object in the string). Sharing a group with a gameplay object is
// NOT a reason: a Move, Rotate, Scale, Alpha, Toggle or Area effect applies to each member on its
// own, so a decoration beside a moving block changes nothing about the block.
//
// "Relevant" groups start as every group a trigger or a gameplay object belongs to and grow by the
// positions each trigger acting on a relevant group reads, to a fixed point.
//
// The object types, groups and trigger fields come from the objects the game built when it loaded
// the level; the cut is made in the level STRING by index, so every kept object keeps its bytes and
// its order. A string object whose id the loaded level has no object for (an id the game rewrites
// on load), or whose id the game built with more than one type, is kept.
//
// Validated before the port, on the original level against the cut one: the player's state is
// identical on every tick of known solutions of the four heaviest official levels and along a
// 24,733-tick route of a heavy custom level, and a plan solved on the cut copy of level 22 cleared
// the level itself.
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slice {

// One object of the level string, by its index among the ';'-separated parts after the header.
struct StrObj {
    int idx = -1;
    int id = -1;
    double x = 0.0, y = 0.0;
    std::vector<int> groups;   // key 57
    bool parent = false;       // key 34
    int camTarget51 = 0, camTarget71 = 0;
};

struct Cut {
    bool ok = false;
    std::string refused;           // why no cut was made
    std::string head;              // the settings part, unchanged
    std::vector<std::string_view> body;   // views into `raw` (kept alive by the caller)
    std::vector<StrObj> objs;      // the non-empty body parts
    std::vector<char> drop;        // per body index: 1 = not needed by the rule
    size_t objects = 0, dropped = 0;
    std::map<std::string, size_t> why;   // per reason, kept and dropped
    size_t positionalGroups = 0, relevantGroups = 0;
};

inline std::string_view kvGet(std::string_view kv, std::string_view key) {
    size_t p = 0;
    while (p < kv.size()) {
        const size_t c1 = kv.find(',', p);
        if (c1 == std::string_view::npos) break;
        const size_t c2 = kv.find(',', c1 + 1);
        const size_t e = (c2 == std::string_view::npos) ? kv.size() : c2;
        if (kv.substr(p, c1 - p) == key) return kv.substr(c1 + 1, e - c1 - 1);
        if (c2 == std::string_view::npos) break;
        p = c2 + 1;
    }
    return {};
}

inline double numOr(std::string_view s, double fallback) {
    if (s.empty()) return fallback;
    std::string t(s);
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    return (end && end != t.c_str()) ? v : fallback;
}

inline constexpr int kTypeDecoration = 7, kTypeTrigger = 20, kTypeArea = 45;
inline constexpr int kKeyframePoint = 3032;
inline bool isTeleport(int id) { return id == 747 || id == 2902 || id == 3022 || id == 3027; }
inline bool isCameraPos(int id) { return id == 2062 || id == 1914 || id == 2016; }

// What the rule reads off the loaded level, one row per object the game built.
struct LiveObj {
    int id = 0, type = -1;
    std::vector<int> groups;
    bool effect = false;       // an EffectGameObject with a target group
    int target = 0, center = 0, tmodCenter = 0;
    bool moveTarget = false, dynamic = false;
};

// `raw` is the decompressed level string; it must outlive the Cut (body holds views into it).
// `positions` = false drops the decorations kept for being read as a position too: a cut that is
// wrong on purpose, to exercise the verification and the add-back (cfg slicenoposition).
inline Cut cut(const std::string& raw, const std::vector<LiveObj>& live, bool positions = true) {
    Cut c;
    const std::string_view all(raw);
    const size_t h = all.find(';');
    if (h == std::string_view::npos) { c.refused = "no objects in the level string"; return c; }
    c.head = std::string(all.substr(0, h));
    for (size_t p = h + 1;;) {
        const size_t e = all.find(';', p);
        c.body.push_back(all.substr(p, e == std::string_view::npos ? std::string_view::npos
                                                                   : e - p));
        if (e == std::string_view::npos) break;
        p = e + 1;
    }
    c.drop.assign(c.body.size(), 0);
    for (size_t i = 0; i < c.body.size(); ++i) {
        const std::string_view p = c.body[i];
        if (p.empty()) continue;
        StrObj o;
        o.idx = (int)i;
        o.id = (int)numOr(kvGet(p, "1"), -1);
        o.x = numOr(kvGet(p, "2"), 0.0);
        o.y = numOr(kvGet(p, "3"), 0.0);
        const std::string_view g = kvGet(p, "57");
        for (size_t q = 0; q < g.size();) {
            const size_t d = g.find('.', q);
            const std::string_view one = g.substr(q, d == std::string_view::npos ? std::string_view::npos
                                                                                : d - q);
            const int v = (int)numOr(one, 0);
            if (v > 0) o.groups.push_back(v);
            if (d == std::string_view::npos) break;
            q = d + 1;
        }
        o.parent = kvGet(p, "34") == "1";
        if (isCameraPos(o.id)) {
            o.camTarget51 = (int)numOr(kvGet(p, "51"), 0);
            o.camTarget71 = (int)numOr(kvGet(p, "71"), 0);
        }
        c.objs.push_back(std::move(o));
    }
    c.objects = c.objs.size();
    if (c.objs.empty()) { c.refused = "no objects in the level string"; return c; }

    // The game's class for each id, from the objects it built. An id built with two classes is
    // not trusted either way.
    std::unordered_map<int, int> typeOf;
    std::unordered_set<int> mixed;
    for (const auto& o : live) {
        auto [it, fresh] = typeOf.emplace(o.id, o.type);
        if (!fresh && it->second != o.type) mixed.insert(o.id);
    }
    auto typeOfId = [&](int id) {
        if (mixed.count(id)) return -1;
        auto it = typeOf.find(id);
        return it == typeOf.end() ? -1 : it->second;
    };

    std::unordered_set<int> relevant, refG;
    for (const auto& o : live) {
        // Both a trigger's groups and a gameplay object's groups are relevant: moving a trigger
        // changes when it is crossed, so what places it counts too.
        if (o.type != kTypeDecoration || o.id == kKeyframePoint)
            relevant.insert(o.groups.begin(), o.groups.end());
    }
    struct Reads { int target; std::vector<int> pos; };
    std::vector<Reads> reads;
    for (const auto& o : live) {
        if (!o.effect || o.target == 0) continue;
        Reads r{o.target, {}};
        if (o.center > 0) r.pos.push_back(o.center);
        if (o.tmodCenter > 0) r.pos.push_back(o.tmodCenter);
        if (o.moveTarget || o.dynamic) r.pos.push_back(o.target);
        if (o.type == kTypeArea) r.pos.push_back(o.target);
        reads.push_back(std::move(r));
        if (isTeleport(o.id) || isCameraPos(o.id)) refG.insert(o.target);
    }
    for (const auto& o : c.objs) {
        if (o.camTarget51 > 0) refG.insert(o.camTarget51);
        if (o.camTarget71 > 0) refG.insert(o.camTarget71);
    }
    const int spawnGroup = (int)numOr(kvGet(c.head, "kA36"), 0);
    if (spawnGroup > 0) refG.insert(spawnGroup);
    // Both sets only grow, so equal sizes mean nothing changed.
    for (;;) {
        const size_t r0 = relevant.size(), p0 = refG.size();
        relevant.insert(refG.begin(), refG.end());
        for (const auto& r : reads)
            if (relevant.count(r.target)) refG.insert(r.pos.begin(), r.pos.end());
        if (relevant.size() == r0 && refG.size() == p0) break;
    }
    c.positionalGroups = refG.size();
    c.relevantGroups = relevant.size();

    // The extent: the first object with the smallest x in string order, the last with the
    // largest, and the same for y.
    std::set<int> extent;
    {
        const StrObj *xmin = &c.objs[0], *xmax = &c.objs[0], *ymin = &c.objs[0], *ymax = &c.objs[0];
        for (const auto& o : c.objs) {
            if (o.x < xmin->x) xmin = &o;
            if (o.x >= xmax->x) xmax = &o;
            if (o.y < ymin->y) ymin = &o;
            if (o.y >= ymax->y) ymax = &o;
        }
        extent = {xmin->idx, xmax->idx, ymin->idx, ymax->idx};
    }

    for (const auto& o : c.objs) {
        const int t = typeOfId(o.id);
        const char* why = nullptr;
        if (t == kTypeTrigger) why = "keep:trigger";
        else if (t == kTypeArea) why = "keep:area-trigger";
        else if (t < 0) why = "keep:unmapped";
        else if (t != kTypeDecoration) why = "keep:gameplay";
        else if (o.id == kKeyframePoint) why = "keep:keyframe-point";
        else if (extent.count(o.idx)) why = "keep:deco-extent";
        else if (positions && std::any_of(o.groups.begin(), o.groups.end(),
                                          [&](int g) { return refG.count(g) > 0; }))
            why = "keep:deco-referenced-as-position";
        else if (o.parent) why = "keep:deco-group-parent";
        if (why) { ++c.why[why]; continue; }
        c.drop[(size_t)o.idx] = 1;
        ++c.dropped;
        ++c.why[o.groups.empty() ? "drop:deco-ungrouped" : "drop:deco-grouped"];
    }
    c.ok = true;
    return c;
}

// The level string without the dropped objects, except those whose string x lies in one of the
// windows (objects added back after a verification on the level itself). Empty parts and the order
// of what is kept are left exactly as they were.
inline std::string build(const Cut& c, const std::vector<std::pair<double, double>>& keepX,
                         size_t* keptOut = nullptr) {
    std::vector<char> addBack(c.body.size(), 0);
    for (const auto& o : c.objs) {
        if (!c.drop[(size_t)o.idx]) continue;
        for (const auto& w : keepX)
            if (o.x >= w.first && o.x <= w.second) { addBack[(size_t)o.idx] = 1; break; }
    }
    std::string out = c.head;
    size_t kept = 0;
    for (size_t i = 0; i < c.body.size(); ++i) {
        if (!c.body[i].empty() && c.drop[i] && !addBack[i]) continue;
        out += ';';
        out.append(c.body[i]);
        if (!c.body[i].empty()) ++kept;
    }
    if (keptOut) *keptOut = kept;
    return out;
}

}  // namespace slice
