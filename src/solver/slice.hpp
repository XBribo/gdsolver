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
//
// TWO FURTHER CUTS, each behind its own switch (Options):
//
// noTouchDeco -- an id the game built both as a decoration and as one other type is decided per
// object: the decorations are exactly the string objects carrying No Touch (key 121). On a heavy
// custom level all 2,717 decoration instances of its 21 such ids had it and none of the 3,889
// others did. Used only when the string's count of key-121 objects of the id equals the game's
// count of its decorations; otherwise the id stays unmapped (kept).
//
// dropTriggers -- a trigger that can only act on what the run cannot depend on is dropped. The rule
// is the lab's clone reducer: a mover (Move, Rotate, Follow, Follow-Y, Scale, Alpha, Pulse, Toggle,
// Animate, Stop) whose target is outside the relevant groups R, and a cosmetic trigger with no
// target (colour, gradient, background, song, sound, shader). R starts as the groups of every
// object that is neither a decoration nor a trigger, every trigger's centre / move-to-target
// reference / spawn remap, every target that is not a mover's, and the camera triggers' targets
// (keys 51 and 71: a Move on a static camera's target moved a heavy level's flight band by up to
// 1,950 px); a kept trigger's own groups and target join R, to a fixed point. It is also kept
//   - when it is in a group an ORDERED Spawn (1268, key 441) fires: such a spawn times its members
//     off their x, so removing one moves the others (level 22: dropping one Pulse moved a zoom
//     trigger of the same group by a tick, and the flight band by up to 22 px);
//   - when its id is not one the game built only as a trigger.
// Validated on known solutions of levels 19-22 and a heavy custom route: every column of the dump
// matches the uncut level except the camera shake (which differs between two runs of the same
// level), the trigger queue cursor and the snapped object's uid.
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
    int target51 = 0;          // key 51 (a trigger's target group, or a pulse's channel)
    bool noTouch = false;      // key 121
    bool orderedSpawn = false; // a Spawn (1268) with key 441
    std::vector<int> remap;    // key 442: a Spawn's remap pairs, both sides
};

struct Options {
    bool positions = true;     // false: cfg slicenoposition
    bool noTouchDeco = false;  // cfg slicenotouch
    bool dropTriggers = false; // cfg slicetriggers
};

struct Cut {
    bool ok = false;
    std::string refused;           // why no cut was made
    std::string head;              // the settings part, unchanged
    std::vector<std::string_view> body;   // views into `raw` (kept alive by the caller)
    std::vector<StrObj> objs;      // the non-empty body parts
    // per body index: not needed by the rule -- 1 a decoration, 2 a trigger (0 = kept)
    std::vector<char> drop;
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
// Options::dropTriggers: the triggers that address a group only to move it or change its look,
// and the cosmetic ones that address no group.
inline bool isMover(int id) {
    return id == 901 || id == 1346 || id == 1347 || id == 1814 || id == 2067 || id == 1007
           || id == 1006 || id == 1049 || id == 1585 || id == 1616;
}
inline bool isCosmetic(int id) {
    return id == 899 || id == 1006 || id == 2903 || id == 1818 || id == 1819 || id == 3029
           || id == 3030 || id == 3031 || id == 1934 || id == 3602 || id == 3603
           || (id >= 2904 && id <= 2924);
}
// A '.'-separated list of positive ints (groups, remap pairs).
inline std::vector<int> intList(std::string_view s) {
    std::vector<int> out;
    for (size_t q = 0; q < s.size();) {
        const size_t d = s.find('.', q);
        const std::string_view one = s.substr(q, d == std::string_view::npos ? std::string_view::npos
                                                                            : d - q);
        const int v = (int)numOr(one, 0);
        if (v > 0) out.push_back(v);
        if (d == std::string_view::npos) break;
        q = d + 1;
    }
    return out;
}

// What the rule reads off the loaded level, one row per object the game built.
struct LiveObj {
    int id = 0, type = -1;
    std::vector<int> groups;
    bool effect = false;       // an EffectGameObject with a target group
    int target = 0, center = 0, tmodCenter = 0;
    bool moveTarget = false, dynamic = false;
};

// `raw` is the decompressed level string; it must outlive the Cut (body holds views into it).
// Options::positions = false drops the decorations kept for being read as a position too: a cut
// that is wrong on purpose, to exercise the verification and the add-back (cfg slicenoposition).
inline Cut cut(const std::string& raw, const std::vector<LiveObj>& live, const Options& opt = {}) {
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
        o.groups = intList(kvGet(p, "57"));
        o.parent = kvGet(p, "34") == "1";
        o.target51 = (int)numOr(kvGet(p, "51"), 0);
        if (isCameraPos(o.id)) {
            o.camTarget51 = o.target51;
            o.camTarget71 = (int)numOr(kvGet(p, "71"), 0);
        }
        o.noTouch = kvGet(p, "121") == "1";
        if (o.id == 1268) {
            o.orderedSpawn = kvGet(p, "441") == "1";
            o.remap = intList(kvGet(p, "442"));
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
    // Options::noTouchDeco: an id built as a decoration and exactly one other type is decided per
    // object by No Touch, when the counts agree (see the note at the top).
    std::unordered_map<int, int> otherType;   // id -> its type when not a decoration
    if (opt.noTouchDeco) {
        std::unordered_map<int, std::set<int>> kinds;
        std::unordered_map<int, size_t> liveDeco, strNoTouch;
        for (const auto& o : live) {
            if (!mixed.count(o.id)) continue;
            kinds[o.id].insert(o.type);
            if (o.type == kTypeDecoration) ++liveDeco[o.id];
        }
        for (const auto& o : c.objs)
            if (mixed.count(o.id) && o.noTouch) ++strNoTouch[o.id];
        for (const auto& [id, ks] : kinds) {
            if (ks.size() != 2 || !ks.count(kTypeDecoration) || liveDeco[id] != strNoTouch[id])
                continue;
            for (int k : ks)
                if (k != kTypeDecoration) otherType[id] = k;
        }
    }
    auto typeOfObj = [&](const StrObj& o) {
        const auto it = otherType.find(o.id);
        if (it != otherType.end()) return o.noTouch ? kTypeDecoration : it->second;
        return typeOfId(o.id);
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

    // Options::dropTriggers: the relevant groups R and which of the string's triggers act only
    // outside them (see the note at the top).
    std::vector<char> inert(c.objs.size(), 0);
    size_t orderedKept = 0;
    if (opt.dropTriggers) {
        std::unordered_set<int> R, ordered;
        for (const auto& o : live) {
            if (o.type != kTypeDecoration && o.type != kTypeTrigger)
                R.insert(o.groups.begin(), o.groups.end());
            if (!o.effect) continue;
            if (o.center > 0) R.insert(o.center);
            if (o.tmodCenter > 0) R.insert(o.tmodCenter);
            if (o.target > 0 && !(o.type == kTypeTrigger && isMover(o.id))) R.insert(o.target);
        }
        for (const auto& o : c.objs) {
            if (o.camTarget51 > 0) R.insert(o.camTarget51);
            if (o.camTarget71 > 0) R.insert(o.camTarget71);
            R.insert(o.remap.begin(), o.remap.end());
            if (o.orderedSpawn && o.target51 > 0) ordered.insert(o.target51);
        }
        if (spawnGroup > 0) R.insert(spawnGroup);
        // A mover with no key 51 is kept unless it is cosmetic anyway: a Stop can name what it stops
        // by control ID rather than by group, so no group in the string is not "no target".
        auto actsOutside = [&](const StrObj& o) {
            if (isMover(o.id)) return o.target51 > 0 ? !R.count(o.target51) : isCosmetic(o.id);
            return o.target51 <= 0 && isCosmetic(o.id);
        };
        // A kept trigger's own groups and its target are relevant: Spawn, Toggle and Stop address
        // triggers by group. R only grows, so an unchanged size means a fixed point.
        for (;;) {
            const size_t r0 = R.size();
            for (const auto& o : c.objs) {
                if (typeOfObj(o) != kTypeTrigger || actsOutside(o)) continue;
                R.insert(o.groups.begin(), o.groups.end());
                if (o.target51 > 0) R.insert(o.target51);
            }
            if (R.size() == r0) break;
        }
        for (size_t i = 0; i < c.objs.size(); ++i) {
            const StrObj& o = c.objs[i];
            if (typeOfObj(o) != kTypeTrigger || extent.count(o.idx) || !actsOutside(o)) continue;
            if (std::any_of(o.groups.begin(), o.groups.end(),
                            [&](int g) { return ordered.count(g) > 0; })) {
                ++orderedKept;
                continue;
            }
            inert[i] = 1;
        }
    }

    for (size_t i = 0; i < c.objs.size(); ++i) {
        const StrObj& o = c.objs[i];
        const int t = typeOfObj(o);
        const char* why = nullptr;
        if (t == kTypeTrigger && inert[i]) {
            c.drop[(size_t)o.idx] = 2;
            ++c.dropped;
            ++c.why["drop:trigger-inert"];
            continue;
        }
        if (t == kTypeTrigger) why = "keep:trigger";
        else if (t == kTypeArea) why = "keep:area-trigger";
        else if (t < 0) why = "keep:unmapped";
        else if (t != kTypeDecoration) why = "keep:gameplay";
        else if (o.id == kKeyframePoint) why = "keep:keyframe-point";
        else if (extent.count(o.idx)) why = "keep:deco-extent";
        else if (opt.positions && std::any_of(o.groups.begin(), o.groups.end(),
                                              [&](int g) { return refG.count(g) > 0; }))
            why = "keep:deco-referenced-as-position";
        else if (o.parent) why = "keep:deco-group-parent";
        if (why) { ++c.why[why]; continue; }
        c.drop[(size_t)o.idx] = 1;
        ++c.dropped;
        const bool byNoTouch = otherType.count(o.id) > 0;
        ++c.why[byNoTouch ? "drop:deco-no-touch"
                          : o.groups.empty() ? "drop:deco-ungrouped" : "drop:deco-grouped"];
    }
    if (orderedKept) c.why["keep:trigger-ordered-spawn"] = orderedKept;
    c.ok = true;
    return c;
}

// HOLDING THE UIDS. The game numbers a level's objects in string order as it builds them, and some
// objects take a second number for a companion it builds right behind them (a portal's back, id 38;
// the rod ball of decorations 15-17, id 37). A string with objects left out therefore numbers every
// later object differently, and the game's play depends on those numbers: on a heavy custom level
// with an area move, removing any one object before the area trigger -- or putting one more at the
// head of the string -- moved the player's landing on the platform the area moves (the same plan,
// every other object where it was), while removing one behind it changed nothing. So a dropped
// object is not left out but replaced, in its place, by an object that takes the same numbers and
// does nothing:
//   - a decoration by itself, stripped to id and position -- its own id keeps its companion, and it
//     keeps No Touch (key 121: without it a No Touch portal is a portal again) and high detail
//     (key 103: with low detail on the game skips such an object without numbering it);
//   - a trigger by a decoration with no companion (id 1292) at its position, keeping key 103.
// Measured on that level: every one of its 117,272 string objects and 83 companions got the
// level's own uid in both cuts (with and without the two switches), and a 25,588-tick flight
// matched the level itself on every column except the camera shake (which two runs of the level
// itself do not share) and, with the triggers cut, the trigger queue cursor.
inline constexpr int kPlaceholderId = 1292;
inline void appendPlaceholder(std::string& out, std::string_view p, char kind) {
    const std::string_view x = kvGet(p, "2"), y = kvGet(p, "3"), hd = kvGet(p, "103");
    out += "1,";
    if (kind == 1) out.append(kvGet(p, "1"));
    else out += std::to_string(kPlaceholderId);
    out += ",2,";
    out.append(x.empty() ? std::string_view("0") : x);
    out += ",3,";
    out.append(y.empty() ? std::string_view("0") : y);
    if (kind == 1 && kvGet(p, "121") == "1") out += ",121,1";
    if (!hd.empty()) { out += ",103,"; out.append(hd); }
}

// The level string without the dropped objects, except those whose string x lies in one of the
// windows (objects added back after a verification on the level itself). Empty parts and the order
// of what is kept are left exactly as they were. `holdUids`: a dropped object is replaced by a
// placeholder instead of left out (see above), so every object keeps the level's uid.
// `ordinalsOut`: for each object of the result, its index among the level's string objects.
inline std::string build(const Cut& c, const std::vector<std::pair<double, double>>& keepX,
                         size_t* keptOut = nullptr, bool holdUids = false,
                         size_t* placeholdersOut = nullptr, std::vector<int>* ordinalsOut = nullptr) {
    std::vector<char> addBack(c.body.size(), 0);
    for (const auto& o : c.objs) {
        if (!c.drop[(size_t)o.idx]) continue;
        for (const auto& w : keepX)
            if (o.x >= w.first && o.x <= w.second) { addBack[(size_t)o.idx] = 1; break; }
    }
    std::string out = c.head;
    size_t kept = 0, placeholders = 0;
    int ordinal = -1;
    if (ordinalsOut) ordinalsOut->clear();
    for (size_t i = 0; i < c.body.size(); ++i) {
        if (!c.body[i].empty()) ++ordinal;
        if (!c.body[i].empty() && c.drop[i] && !addBack[i]) {
            if (!holdUids) continue;
            out += ';';
            appendPlaceholder(out, c.body[i], c.drop[i]);
            ++placeholders;
            if (ordinalsOut) ordinalsOut->push_back(ordinal);
            continue;
        }
        out += ';';
        out.append(c.body[i]);
        if (!c.body[i].empty()) {
            ++kept;
            if (ordinalsOut) ordinalsOut->push_back(ordinal);
        }
    }
    if (keptOut) *keptOut = kept;
    if (placeholdersOut) *placeholdersOut = placeholders;
    return out;
}

// Each string object's uid in a built level, read off the objects the game built: in uid order,
// each object whose id is the next string object's id is that object, and anything else (a
// companion, or an object the game makes for itself) belongs to the string object before it.
// `ids` are the ids of the string's non-empty parts in order; `byUid` the built objects' (uid, id)
// sorted by uid.
struct OrdinalUids {
    std::vector<int> uid;                  // per string object; -1 when not matched
    std::vector<std::vector<int>> extra;   // per string object: the uids of what follows it
    size_t matched = 0;
};
// A coin (secret 142, user 1329) may be numbered and then not added to the layer -- an editor-type
// level adds no secret coin -- so a string coin with no built object is passed over (uid -2).
inline bool mayBeUnbuilt(int id) { return id == 142 || id == 1329; }
inline OrdinalUids ordinalUids(const std::vector<int>& ids,
                               const std::vector<std::pair<int, int>>& byUid) {
    OrdinalUids r;
    r.uid.assign(ids.size(), -1);
    r.extra.resize(ids.size());
    size_t j = 0;
    for (const auto& [uid, id] : byUid) {
        while (j < ids.size() && id != ids[j] && mayBeUnbuilt(ids[j])) r.uid[j++] = -2;
        if (j < ids.size() && id == ids[j]) {
            r.uid[j++] = uid;
        } else if (j > 0) {
            r.extra[j - 1].push_back(uid);
        }
    }
    r.matched = j;
    return r;
}
inline std::vector<int> stringIds(std::string_view levelString) {
    std::vector<int> ids;
    const size_t h = levelString.find(';');
    if (h == std::string_view::npos) return ids;
    for (size_t p = h + 1; p < levelString.size();) {
        const size_t e = levelString.find(';', p);
        const std::string_view part = levelString.substr(p, e == std::string_view::npos
                                                                 ? std::string_view::npos : e - p);
        if (!part.empty()) ids.push_back((int)numOr(kvGet(part, "1"), -1));
        if (e == std::string_view::npos) break;
        p = e + 1;
    }
    return ids;
}

}  // namespace slice
