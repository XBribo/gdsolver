#pragma once
// What the play menu warns about before a bot run, read from the level string alone: the menu opens
// before the level is loaded, so nothing here may need a GameObject.
//
// Two kinds, two colours in the menu:
//   * UNMODELLED (red): the level holds something the model cannot express at all, so it plans
//     blind there and the section solver is the only way through -- a solve may well fail. Each
//     bit is a shape the dp loader reports as NOT modelled or refuses outright, a feature no part
//     of dp reads, or a level that does not behave the same on every attempt.
//   * NEWER (yellow, custom levels only): an object added in 2.0 or later that is not a recolour of
//     an older one. The model is measured against those one mechanic at a time and much of it is
//     not done, so such a level takes longer and may not solve.
// Neither refuses anything; a warning that shows a little too often costs nothing.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace levelwarn {

enum : uint32_t {
    // A teleport portal (2902) whose exit is away from it in x, with ignoreX off: x is the model's
    // clock, so an exit elsewhere is a jump it cannot take (dp level_loader: "moves x to ... - NOT
    // modelled"). A 747 puts its exit above or below itself and is modelled.
    kSidewaysTeleport = 1u << 0,
    // ignoreY (key 353, TeleportPortalObject::m_ignoreY): keep the player's height -- "uses ignoreY -
    // NOT modelled" in the same loader.
    kTeleportKeepsHeight = 1u << 1,
    // A static force (key 345) or a redirected one (key 347): the portal sets the player's
    // velocity. Neither the level export nor dp reads either field.
    kTeleportPush = 1u << 2,
    // A 2902 whose target group holds more than one object: teleportPlayer (0x20fdb0) draws one of
    // them from the seed at 0x6c2ef8. The export takes the first; the model has no draw.
    kTeleportSeveralExits = 1u << 3,
    // The teleport orb (3027, GameObjectType::TeleportOrb) and the teleport trigger (3022): the
    // export lists the orb as a point of interest and dp reads neither.
    kTeleportOrb = 1u << 4,
    kTeleportTrigger = 1u << 5,
    // The 2.2 gravity trigger (2066): dp has no gravity multiplier.
    kGravityTrigger = 1u << 6,
    // A level the dp loader refuses for its gravity portals ("gravity portals: N (limit 128, ...)"
    // in level_loader.hpp). The portal latch holds kGravPortalBits (128) of them; past that the
    // loader shares bits -- portal j takes the bit of portal j-128 in x order -- unless an object
    // carries a reversal or some such pair is closer than kGpShareGap (120 px). Most levels over
    // 128 share fine, so this is set only when the sharing would fail: see gravityPortalsRefused.
    kGravityPortals = 1u << 7,
    // The level itself runs differently from one attempt to the next: an Item Compare or Item Edit
    // (3620, 3619) that reads the attempt count (item type 5, keys 476/477; getItemValue 0x2341c0
    // reads it from the layer), or Item Persistence (3641), which keeps items across attempts. The
    // loop records the moving geometry on one attempt and flies its plans on later ones, and a
    // solution replayed as a session's first attempt meets a different level.
    kAttemptDependent = 1u << 8,
    kPlatformer = 1u << 9,
};

// The phrase each bit is shown as, in bit order.
inline const char* const kUnmodelledNames[] = {
    "teleports that move you sideways",
    "teleports that keep your height",
    "teleports that push you",
    "teleports with several exits",
    "teleport orbs",
    "teleport triggers",
    "gravity triggers",
    "too many gravity portals",
    "triggers that count attempts",
    "platformer mode",
};

// Objects added in 2.0 or later that are not a recolour of an older one. Derived, not listed by
// hand, in two steps.
// Which objects are new mechanics: an ID from 286 on (the IDs of pre-1.8 levels, game version 7
// and 10 on the servers, stop at 285) counts when the game gives it a type no pre-1.8 object has
// (dual, slope, wave, robot, teleport, the green, black, red, custom, dash, teleport and spider
// rings and pads, collision and force blocks, swing and gravity-toggle portals, the area effects),
// read off the objects of a few hundred levels as loaded by the game, or when it is a trigger
// whose editor icon (ObjectToolbox::init) names a gameplay effect -- move, rotate, toggle, spawn,
// follow, the item and count triggers, collision, camera, time, gravity, teleport, end. Colour,
// pulse, alpha, shake, animation, particles, shaders, background, sound, UI and BPM triggers do not
// count, nor do solids, hazards (the animated monsters included), decoration and coins, nor a new
// ID of an old type such as the 4x speed portal.
// Which of those came in 2.0 or later: an object's version is the lowest game version of a server
// level that holds it (game version 18 = 1.8, 19 = 1.9, 20 = 2.0, ...), over 525 levels downloaded
// from the servers. 43 came out as 1.8 or 1.9 -- the dual and slope objects and the rings and pads
// of their kind, IDs 286-367 and 483-493, and 1.9's wave and its companions, IDs 651-674 and 728 --
// and are left out. In a survey of 300 custom levels solved to the end, 206 of the 215 levels with
// nothing newer than 1.9 were solved, and they diverged from the game about a fifth as often per
// tick as the levels with 2.0 objects. 371 and 372 sit among 1.8's IDs but were found only in 2.1
// levels, and are kept. The 15 found in none of the 525 are kept too: all are numbered among
// 2.2's. Sorted.
inline constexpr int kNewer[] = {
    371, 372, 709, 745, 747, 901, 1022, 1049, 1268, 1330, 1331, 1332,
    1333, 1338, 1339, 1341, 1342, 1344, 1345, 1346, 1347, 1594, 1595, 1611, 1616, 1704, 1717, 1718,
    1723, 1724, 1743, 1744, 1745, 1746, 1747, 1748, 1749, 1750, 1751, 1755, 1811, 1812, 1813, 1814,
    1815, 1816, 1817, 1829, 1859, 1906, 1907, 1912, 1913, 1914, 1916, 1917, 1931, 1932, 1933, 1935,
    2015, 2062, 2066, 2067, 2068, 2069, 2866, 2899, 2900, 2901, 2902, 2925, 2926, 3004, 3005, 3006,
    3007, 3008, 3011, 3012, 3013, 3016, 3017, 3018, 3019, 3022, 3023, 3024, 3027, 3033, 3600, 3604,
    3607, 3609, 3614, 3615, 3617, 3618, 3619, 3620, 3640, 3641, 3643, 3645, 3655, 3660, 3661};

inline bool isNewer(int id) {
    return std::binary_search(std::begin(kNewer), std::end(kNewer), id);
}

struct Findings {
    uint32_t unmodelled = 0;   // the bits above
    bool newer = false;        // an object in kNewer
    std::unordered_map<int, int> newerCount;   // how many of each kNewer object, for the log
};

// The key's value in one object's "k,v,k,v" run, or empty.
inline std::string_view field(std::string_view kv, std::string_view key) {
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

inline bool isOn(std::string_view v) { return !v.empty() && v != "0"; }
inline int toInt(std::string_view v) { return v.empty() ? 0 : std::atoi(std::string(v).c_str()); }
inline double toNum(std::string_view v) { return v.empty() ? 0.0 : std::atof(std::string(v).c_str()); }

// Calls f(object) for every object of a decompressed level string (the header before the first
// ';' is skipped).
template <class F>
inline void forEachObject(std::string_view all, F&& f) {
    size_t pos = all.find(';');
    while (pos != std::string_view::npos) {
        const size_t end = all.find(';', pos + 1);
        const auto obj = all.substr(pos + 1, end == std::string_view::npos
                                                 ? std::string_view::npos : end - pos - 1);
        if (!obj.empty()) f(obj);
        pos = end;
    }
}

// A gravity portal as the dp loader sees it: its x and the half width of its box.
struct GravPortal {
    double x;
    double hw;
};

// The half width of a gravity portal's box as getObjectRect gives it to the export the loader
// reads: the 25x75 hitbox (m_width, m_height), scaled by key 32 (both axes) or 128 / 129 (x / y)
// and turned by key 6, which makes it the bound of the turned box, |w cos| + |h sin|. Keys
// 131 / 132 turn the two axes apart; then the larger |cos| and the larger |sin| of the angles
// given are taken, which is never narrower than the box. Two keys for one axis (never seen) take
// the larger scale. Checked against the export on 6,595 portals of a few hundred levels: x as
// exported, and the half width within 0.01 px except on one portal whose axes are turned 29.75
// and 29.76 degrees, where it comes out 0.013 px wider.
inline double gravPortalHalfWidth(std::string_view obj) {
    auto scale = [&](std::string_view axisKey) {
        double s = 0.0;
        bool given = false;
        for (std::string_view k : {std::string_view("32"), axisKey}) {
            const auto v = field(obj, k);
            if (v.empty()) continue;
            s = std::max(s, std::fabs(toNum(v)));
            given = true;
        }
        return given ? s : 1.0;
    };
    const double w = 25.0 * scale("128"), h = 75.0 * scale("129");
    double c = 0.0, s = 0.0;
    bool turned = false;
    for (std::string_view k : {"6", "131", "132"}) {
        const auto v = field(obj, k);
        if (v.empty()) continue;
        const double a = toNum(v) * 3.14159265358979 / 180.0;
        c = std::max(c, std::fabs(std::cos(a)));
        s = std::max(s, std::fabs(std::sin(a)));
        turned = true;
    }
    if (!turned) c = 1.0;
    return 0.5 * (w * c + h * s);
}

// Would the dp loader refuse these gravity portals? level_loader.hpp numbers the portals it holds
// in cx order, and past kGravPortalBits (128) it refuses the level when any object carries a
// reversal, or else when for some j the box of portal j starts less than kGpShareGap (120 px)
// after the box of portal j - 128 ends. `reversal`: an object with key 117 (m_isReverse -- a ring
// or pad that turns the player round).
//
// From the string the answer may come out too eager, never too shy:
//   * The loader may hold fewer portals than the string. The load that refuses a session is the
//     one at its start, which has no recording and holds them all, but the solver's later loads
//     route a portal that moving geometry carries to the dynamic set, which the numbering does
//     not walk. Fewer portals only spread the pairs, so a test on all of them is the strict one.
//   * The export writes x to six significant digits, and cx order leaves ties to the sort. So a
//     pair is not "128 apart in the string's order" but any two portals with at least 129
//     portals, both included, between their x widened by kSlack -- every pair the loader can form
//     is one, whatever it holds and however it orders ties -- and a pair is too close within kSlack
//     of kGpShareGap. Two px covers the rounding for any x below 1,000,000. Where portals stand
//     under kSlack apart, the window also admits a partner a portal or two short of the 128th.
//   * Any object with key 117 counts as a reversal, where the loader reads it only off what it
//     collides with (rings, pads, portals, solids, hazards). It has only been seen on rings and
//     pads.
inline bool gravityPortalsRefused(std::vector<GravPortal> g, bool reversal) {
    constexpr size_t kBits = 128;       // dp: kGravPortalBits
    constexpr double kGap = 120.0;      // dp: kGpShareGap
    constexpr double kSlack = 2.0;
    if (g.size() <= kBits) return false;
    if (reversal) return true;
    std::sort(g.begin(), g.end(), [](const GravPortal& a, const GravPortal& b) { return a.x < b.x; });
    const size_t n = g.size();
    auto firstAt = [&](double x) {   // the first portal whose x is at least `x`
        return (size_t)(std::lower_bound(g.begin(), g.end(), x,
                                         [](const GravPortal& p, double v) { return p.x < v; })
                        - g.begin());
    };
    // hiMin[k]: the leftmost box start among portals k.. in x order.
    std::vector<double> hiMin(n + 1, std::numeric_limits<double>::infinity());
    for (size_t k = n; k-- > 0;) hiMin[k] = std::min(hiMin[k + 1], g[k].x - g[k].hw);
    for (const GravPortal& a : g) {
        // The window starts at the first portal within kSlack of a, and a partner must lie at or
        // beyond the 129th portal from there. Both only move right as a does.
        const size_t lo = firstAt(a.x - kSlack);
        if (lo + kBits >= n) break;
        const size_t from = firstAt(g[lo + kBits].x - kSlack);
        if (hiMin[from] - (a.x + a.hw) < kGap + kSlack) return true;
    }
    return false;
}

inline Findings scan(std::string_view all, bool platformer) {
    Findings r;
    if (platformer) r.unmodelled |= kPlatformer;
    struct Portal { double x; int group; bool keepX; };
    std::vector<Portal> portals;
    std::vector<GravPortal> gravity;
    bool reversal = false;
    forEachObject(all, [&](std::string_view obj) {
        const int id = toInt(field(obj, "1"));
        if (isNewer(id)) {
            r.newer = true;
            ++r.newerCount[id];
        }
        // A No Touch object (key 121) is a decoration to the game (GameObjectType 7 on every one
        // in the export): a No Touch portal is not a portal and a No Touch ring reverses nothing.
        if (!reversal && isOn(field(obj, "117")) && !isOn(field(obj, "121"))) reversal = true;
        switch (id) {
            case 10: case 11:
                if (!isOn(field(obj, "121")))
                    gravity.push_back({toNum(field(obj, "2")), gravPortalHalfWidth(obj)});
                break;
            case 747: case 2902:
                if (isOn(field(obj, "345")) || isOn(field(obj, "347"))) r.unmodelled |= kTeleportPush;
                if (isOn(field(obj, "353"))) r.unmodelled |= kTeleportKeepsHeight;
                if (id == 2902) {
                    const int g = toInt(field(obj, "51"));
                    if (g > 0) portals.push_back({toNum(field(obj, "2")), g, isOn(field(obj, "352"))});
                }
                break;
            case 3027: r.unmodelled |= kTeleportOrb; break;
            case 3022: r.unmodelled |= kTeleportTrigger; break;
            case 2066: r.unmodelled |= kGravityTrigger; break;
            case 3641: r.unmodelled |= kAttemptDependent; break;
            case 3619: case 3620:
                if (field(obj, "476") == "5" || field(obj, "477") == "5")
                    r.unmodelled |= kAttemptDependent;
                break;
            default: break;
        }
    });
    if (gravityPortalsRefused(std::move(gravity), reversal)) r.unmodelled |= kGravityPortals;
    if (portals.empty()) return r;
    // The exits: every object in a portal's target group (key 57, '.'-separated).
    struct Exits { int n = 0; double x = 0.0; };
    std::unordered_map<int, Exits> exits;
    for (const auto& p : portals) exits.emplace(p.group, Exits{});
    forEachObject(all, [&](std::string_view obj) {
        const auto groups = field(obj, "57");
        size_t p = 0;
        while (!groups.empty() && p <= groups.size()) {
            const size_t d = groups.find('.', p);
            const auto one = groups.substr(p, d == std::string_view::npos ? std::string_view::npos
                                                                          : d - p);
            if (auto it = exits.find(toInt(one)); it != exits.end() && it->second.n++ == 0)
                it->second.x = toNum(field(obj, "2"));
            if (d == std::string_view::npos) break;
            p = d + 1;
        }
    });
    for (const auto& p : portals) {
        const Exits& e = exits[p.group];
        if (e.n > 1) r.unmodelled |= kTeleportSeveralExits;
        else if (e.n == 1 && !p.keepX && std::fabs(e.x - p.x) > 0.5) r.unmodelled |= kSidewaysTeleport;
    }
    return r;
}

// Every bit set, "; "-separated, or "none" -- for the log line the menu writes.
inline std::string describeAll(uint32_t bits) {
    std::string s;
    for (size_t i = 0; i < sizeof(kUnmodelledNames) / sizeof(kUnmodelledNames[0]); ++i)
        if (bits & (1u << i)) s += (s.empty() ? "" : "; ") + std::string(kUnmodelledNames[i]);
    return s.empty() ? "none" : s;
}

// "901 x12, 1346 x3, ..." -- the kNewer objects found, most numerous first, at most `most` of
// them -- or "none". For the log line the menu writes.
inline std::string newerSummary(const Findings& f, size_t most = 12) {
    std::vector<std::pair<int, int>> v(f.newerCount.begin(), f.newerCount.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::string s;
    for (size_t i = 0; i < v.size() && i < most; ++i)
        s += (i ? ", " : "") + std::to_string(v[i].first) + " x" + std::to_string(v[i].second);
    if (v.size() > most) s += ", +" + std::to_string(v.size() - most) + " more kinds";
    return s.empty() ? "none" : s;
}

// "a", "a and b", or "a, b and N more" for the bits set, in bit order: the menu gives it one line.
inline std::string describe(uint32_t bits) {
    std::vector<const char*> names;
    for (size_t i = 0; i < sizeof(kUnmodelledNames) / sizeof(kUnmodelledNames[0]); ++i)
        if (bits & (1u << i)) names.push_back(kUnmodelledNames[i]);
    std::string s;
    const size_t shown = names.size() > 2 ? 2 : names.size();
    for (size_t i = 0; i < shown; ++i) {
        if (i) s += (i + 1 == names.size()) ? " and " : ", ";
        s += names[i];
    }
    if (names.size() > shown) s += " and " + std::to_string(names.size() - shown) + " more";
    return s;
}

}  // namespace levelwarn
