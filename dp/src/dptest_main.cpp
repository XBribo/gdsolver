// dptest -- the header core's rules that can be decided without a level.
//
// The suite (py/quick_regress.py) compares whole sections against a corpus, which is the right
// instrument for a change that moves the model -- and the wrong one for a boundary that the
// corpus happens not to contain. The recording overlay is such a boundary: the corpus'
// recordings predate the `end` trailer, so quick_regress cannot see the rule at all.
//
//   dptest            runs every case, prints one line each, exit 1 on the first failure
//   dptest --groups-corpus <file>...
//                     reads each --groups recording with parseGroupTimeline and with the sscanf
//                     reader it replaced, and says whether they agree bit for bit
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "dp/cli.hpp"
#include "../../src/mod/level_warnings.hpp"

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) g_fail = 1;
}

std::string writeTmp(const std::string& name, const std::string& body) {
    const std::string p =
        (std::filesystem::temp_directory_path() / name).string();
    std::ofstream f(p, std::ios::trunc);
    f << body;
    return p;
}

// Does the merged timeline hold a row at this tick with this cy?
bool has(const dp::GroupTimeline& g, int uid, int t, float cy) {
    const auto it = g.find(uid);
    if (it == g.end()) return false;
    for (const dp::DynSample& s : it->second)
        if (s.t == t && s.cy == cy) return true;
    return false;
}

// THE OVERLAY'S REACH. `over` is the newer recording: it wins for every tick its RUN covered,
// which is its `end` trailer when it has one and its last row when it does not. The rows here
// are far enough apart that the bootstrap re-timing finds no fit, so the base keeps its own
// ticks and the only thing under test is the reach.
void overlayReach() {
    const std::string over = writeTmp("dptest_over.txt",
        "tick,uid,cx,cy,w,h,on,rot\n"
        "1,7,0,100,30,3,1,0\n"
        "10,7,0,200,30,3,1,0\n"
        "end,50\n");
    const std::string base = writeTmp("dptest_base.txt",
        "tick,uid,cx,cy,w,h,on,rot\n"
        "5,7,0,300,30,3,1,0\n"
        "20,7,0,400,30,3,1,0\n"
        "50,7,0,500,30,3,1,0\n"
        "60,7,0,600,30,3,1,0\n");

    long long end = -1;
    dp::GroupTimeline o = dp::loadGroupTimeline(over, &end);
    check(end == 50, "the `end` trailer is read back as the run's end");
    check(o[7].size() == 2, "the trailer is not taken for a row");

    dp::GroupTimeline g = dp::loadGroupTimeline(base);
    dp::overlayGroupTimeline(g, o, false, end);
    check(has(g, 7, 10, 200.f), "the newer recording's own rows are kept");
    check(!has(g, 7, 20, 400.f),
          "a base row after the newer file's last row but inside its run is dropped");
    check(!has(g, 7, 50, 500.f), "a base row at t == end is dropped (the run covers it)");
    check(has(g, 7, 60, 600.f), "a base row past the end is kept");

    long long none = -1;
    dp::GroupTimeline o2 = dp::loadGroupTimeline(writeTmp("dptest_over2.txt",
        "tick,uid,cx,cy,w,h,on,rot\n"
        "1,7,0,100,30,3,1,0\n"
        "10,7,0,200,30,3,1,0\n"), &none);
    check(none == -1, "a recording without the trailer says so");
    dp::GroupTimeline g2 = dp::loadGroupTimeline(base);
    dp::overlayGroupTimeline(g2, o2, false, none);
    check(has(g2, 7, 20, 400.f) && has(g2, 7, 50, 500.f) && has(g2, 7, 60, 600.f),
          "without a trailer the reach is the last row, as before");
}

// THE t=0 SNAPSHOT. The mod writes `init,<uid>,<on>` right after the game's reset; the reader
// hands them out on request and otherwise skips them, and they are never taken for a row.
void initLines() {
    const std::string p = writeTmp("dptest_init.txt",
        "tick,uid,cx,cy,w,h,on,rot\n"
        "init,7,1\n"
        "init,8,0\n"
        "1,7,0,100,30,3,0,0\n"
        "5,8,0,200,30,3,1,0\n"
        "end,9\n");
    std::unordered_map<int, uint8_t> init;
    long long end = -1;
    const dp::GroupTimeline g = dp::loadGroupTimeline(p, &end, &init);
    check(init.size() == 2 && init.at(7) == 1 && init.at(8) == 0,
          "each init line gives its object's on at t=0");
    check(g.at(7).size() == 1 && g.at(8).size() == 1 && end == 9,
          "the init lines are not rows, and the trailer still reads");
    const dp::GroupTimeline g0 = dp::loadGroupTimeline(p);
    check(g0.at(7).size() == 1 && g0.at(8).size() == 1,
          "a caller that does not ask gets the same rows as before");
}

// The objrects header as the mod writes it (src/solver/solver.hpp). `nocol` is the 50th column,
// so a fixture that put it anywhere else would test a layout no dump has.
const char* const kObjHeader =
    "id,type,cx,cy,w,h,groups,uid,radius,rot,sy0,sy1,shz,sdir,sup,w0,h0,"
    "tpy,tpg,tpix,tpiy,tw,zoom,zdur,zease,zrate,mvdir,gnddir,"
    "optp1,optp2,flipx,flipy,nofx,notouch,tpex,tpey,dis,"
    "editvel,vmodx,vmody,ovrvel,force,free,touch,spawn,chan,axis,exstat,rev,"
    "nocol";

// One 30x30 static solid. Every column past `rot` is empty (what an older dump leaves there)
// except `nocol`, which is written when `withNocol` says the dump has that column at all.
std::string solidRow(int id, int uid, double cx, bool withNocol, const char* nocol) {
    std::string r = std::to_string(id) + ",0," + std::to_string(cx) + ",100,30,30,0,"
                    + std::to_string(uid) + ",0,0";
    if (withNocol) {
        for (int i = 10; i < 49; ++i) r += ",";
        r += ",";
        r += nocol;
    }
    return r + "\n";
}

bool keeps(const dp::Level& L, int uid) {
    for (const dp::Obj& o : L.objs)
        if (o.uid == uid) return true;
    return false;
}

// WHICH OBJECTS THE MODEL DOES NOT COLLIDE WITH. GD skips an object whose byte [obj+0x515] is
// set, whatever its id; the model used to stand in for that byte with id 1910. With the `nocol`
// column the byte decides, and without it the id rule stands. The corpus cannot test the new
// branch: every stored dump predates the column, so all 1,116 sections take the fallback.
void noCollideColumn() {
    {
        std::istringstream in(std::string(kObjHeader) + "\n"
                              + solidRow(1910, 11, 100, true, "0")
                              + solidRow(1, 12, 200, true, "1")
                              + solidRow(1, 13, 300, true, "0")
                              + solidRow(1910, 14, 400, true, ""));
        const dp::Level L = dp::loadLevelFrom(in);
        check(keeps(L, 11), "with the column, an id-1910 object whose byte is 0 stays solid");
        check(!keeps(L, 12), "with the column, an id-1 object whose byte is 1 is dropped");
        check(keeps(L, 13), "with the column, an id-1 object whose byte is 0 stays solid");
        check(!keeps(L, 14), "an empty cell falls back to the id rule for that row");
    }
    {
        std::istringstream in(std::string(kObjHeader).substr(0, std::string(kObjHeader).size()
                                                                    - std::string(",nocol").size())
                              + "\n"
                              + solidRow(1910, 21, 100, false, "")
                              + solidRow(1, 22, 200, false, ""));
        const dp::Level L = dp::loadLevelFrom(in);
        check(!keeps(L, 21), "without the column, id 1910 is dropped as before");
        check(keeps(L, 22), "without the column, any other id stays solid");
    }
}

// AN OBJECT'S ON/OFF AS A SEQUENCE OF SWITCHES (five boundaries). A touch mask
// cannot carry any of these -- it only ever gains bits -- so each case is one the mask form gets
// wrong.
void onOffEvents() {
    uint16_t fireB[dp::kTouchBits] = {};
    auto on = [&](bool init, const std::vector<dp::OnEvent>& ev, dp::TouchMask mask, int t) {
        return dp::resolveOn(init, ev.data(), ev.size(), mask, fireB, t);
    };
    auto boxEv = [](int box, bool isOn, double delay) {
        dp::OnEvent e;
        e.box = box;
        e.on = isOn ? 1 : 0;
        e.delay = delay;
        return e;
    };
    const dp::TouchMask b0 = dp::touchBit(0), b1 = dp::touchBit(1);

    // (1) a spawn delay: the switch lands `delay` ticks after the box, not with it
    fireB[0] = 100;
    const std::vector<dp::OnEvent> delayed{boxEv(0, true, 10.0)};
    check(!on(false, delayed, b0, 109), "(1) one tick before the delay runs out it is still off");
    check(on(false, delayed, b0, 110), "(1) on the tick the delay runs out it is on");
    check(!on(false, delayed, dp::TouchMask{}, 500), "(1) a state that never entered the box: off");

    // (2) a Toggle that switches OFF (which a mask cannot say at all)
    const std::vector<dp::OnEvent> off{boxEv(0, false, 0.0)};
    check(on(true, off, b0, 99), "(2) before the box it is on");
    check(!on(true, off, b0, 100), "(2) from the box on it is off");

    // (3) two chains on one object: the later switch wins, whichever box it came from
    fireB[0] = 100;
    fireB[1] = 150;
    const std::vector<dp::OnEvent> onThenOff{boxEv(0, true, 0.0), boxEv(1, false, 0.0)};
    check(on(false, onThenOff, b0 | b1, 120), "(3) on at 100, off at 150: on at 120");
    check(!on(false, onThenOff, b0 | b1, 160), "(3) on at 100, off at 150: off at 160");
    const std::vector<dp::OnEvent> offThenOn{boxEv(0, false, 0.0), boxEv(1, true, 0.0)};
    check(on(true, offThenOn, b0 | b1, 160), "(3) off at 100, on at 150: on at 160");
    // ...and it is the EFFECTIVE tick that orders them: the box entered first can land last
    const std::vector<dp::OnEvent> slowFirst{boxEv(0, true, 100.0), boxEv(1, false, 0.0)};
    check(!on(false, slowFirst, b0 | b1, 190), "(3) off lands at 150, on at 200: off at 190");
    check(on(false, slowFirst, b0 | b1, 210), "(3) off lands at 150, on at 200: on at 210");
    // ...and a box the state did not enter takes no part
    check(on(false, onThenOff, b0, 160), "(3) without the second box it stays on");

    // (4) and (5): which roots give switches at all. A Tap and a Count set their bit on the
    // first press or item, so their chain's Toggle cannot be read off the bit; an Item Compare
    // (a Count root with cmode 3) has its own gate for coins in cli.hpp.
    std::vector<dp::TouchTrig> boxes(4);
    for (dp::TouchTrig& T : boxes) T.togEv.push_back({42, 1, 0.0});
    boxes[1].tap = true;
    boxes[2].count = 3;
    boxes[3].count = 6;
    boxes[3].cmode = 3;
    const auto ev = dp::boxOnEvents(boxes);
    const auto it = ev.find(42);
    check(it != ev.end() && it->second.size() == 1 && it->second[0].box == 0,
          "(4)(5) only the plain box gives a switch -- not the Tap, the Count or the Item Compare");
}

// A pickup row as the mod dumps it (SubZero 4002's key uid 6166, with uid / x / target / actgrp
// swapped in). actgrp is the 41st column.
std::string pickupRow(int uid, double cx, int target, int actgrp, int id = 1275,
                      int touch = 0) {
    return std::to_string(uid) + "," + std::to_string(id) + "," + std::to_string(cx)
           + ",985,25,20," + std::to_string(target) + ",0," + std::to_string(touch)
           + ",0,0.5,0,0,0,2,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,-,0,0,-1,0,"
           + std::to_string(actgrp) + ",0,0,0,-1,-1,-1,-1,0,0,-1,-1,-1,0,-1,-1,-1,-1\n";
}

const dp::TouchTrig* boxOf(const std::vector<dp::TouchTrig>& v, int uid) {
    for (const dp::TouchTrig& t : v)
        if (t.uid == uid) return &t;
    return nullptr;
}

// WHICH PICKUPS BECOME BOXES (--activators). Only one that switches its group ON, and only when
// everything in the group is something to stand on, collect or press: custom levels have pickups
// that bring hazards in or take coins away, and those are left out rather than guessed at.
void activatorRoots() {
    const std::string trig = writeTmp("dptest_act_trig.txt",
        "uid,id,cx,cy,w,h,target,center,touch,spawn,dur,ox,oy,ease,erate,lockx,locky,grav,"
        "gravmod,deg,ord,chan,sord,sordd,t360,lockrot,sdelay,mvtgt,mvaxis,tmodctr,dirsnap,"
        "dirdist,dynmode,silent,togon,remap,item,item2,count,subcount,actgrp,thold,ttog,tdual,"
        "cmode,i1mode,i2mode,tgtmode,mod1,mod2,res1,res2,res3,tol,rnd1,rnd2,sgn1,sgn2\n"
        + pickupRow(101, 1000, 10, 1)    // two solids
        + pickupRow(102, 2000, 20, 1)    // a solid and a hazard
        + pickupRow(103, 3000, 30, 1)    // an orb
        + pickupRow(104, 4000, 40, 0)    // switches OFF
        + pickupRow(105, 5000, 50, 1, 1594, 1));   // a toggle block (the dump says touch=1)
    const std::string grp = writeTmp("dptest_act_grp.txt",
        "uid,groups\n1001 10\n1002 10\n2001 20\n2002 20\n3001 30\n4001 40\n5001 50\n");
    const std::unordered_map<int, int> types{
        {101, 30}, {102, 30}, {103, 30}, {104, 30}, {105, 36},
        {1001, 0}, {1002, 0}, {2001, 0}, {2002, 2}, {3001, 35}, {4001, 0}, {5001, 35}};

    const auto on = dp::loadTouchTriggers(trig, grp, -1e18, &types);
    const dp::TouchTrig* k = boxOf(on, 101);
    bool both = k && k->activator && k->togEv.size() == 2;
    for (size_t i = 0; both && i < k->togEv.size(); ++i)
        both = k->togEv[i].on == 1 && k->togEv[i].delay == 0.0
               && (k->togEv[i].uid == 1001 || k->togEv[i].uid == 1002);
    check(both, "a pickup that switches two solids on is a box that switches both on");
    check(k && k->cx == 1000 && k->hw == 12.5 && k->hh == 10.0,
          "its box is the pickup's own rect");
    check(!boxOf(on, 102), "a pickup whose group holds a hazard is left out");
    check(boxOf(on, 103) != nullptr, "a pickup that switches an orb on is kept");
    check(!boxOf(on, 104), "a pickup that switches its group OFF is left out");
    const dp::TouchTrig* tb = boxOf(on, 105);
    check(tb && tb->activator && tb->press && !k->press,
          "a toggle block is an activator that needs the button; a pickup does not");
}

// A trigger row for the chain walks: the columns up to togon matter here (sdelay is the 27th,
// togon the 35th).
std::string trigRow(int uid, int id, double cx, int target, int spawn, int togon, double sdelay,
                    double ox) {
    std::string r = std::to_string(uid) + "," + std::to_string(id) + "," + std::to_string(cx)
                    + ",315,30,30," + std::to_string(target) + ",0,0," + std::to_string(spawn)
                    + ",0.5," + std::to_string(ox) + ",0,0,2,0,0,1,0,0,0,0,0,0,0,0,"
                    + std::to_string(sdelay) + ",0,0,0,0,0,0,0," + std::to_string(togon)
                    + ",-,0,0,-1,0,-1,0,0,0,-1,-1,-1,-1,0,0,-1,-1,-1,0,-1,-1,-1,-1\n";
    return r;
}

// THE x-CROSSING SWITCHES, and the whole of SubZero 4003's hidden ring through the resolver: on
// at the start, off when the player crosses x=6,225 (Toggle uid 3027), on again when the box
// (id 1594) fires. The crossing's tick is a level property the search resolves as it goes, so the
// event must read it then, not when it was built.
void autoSwitches() {
    const std::string trig = writeTmp("dptest_auto_trig.txt",
        "uid,id,cx,cy,w,h,target,center,touch,spawn,dur,ox,oy,ease,erate,lockx,locky,grav,"
        "gravmod,deg,ord,chan,sord,sordd,t360,lockrot,sdelay,mvtgt,mvaxis,tmodctr,dirsnap,"
        "dirdist,dynmode,silent,togon,remap,item,item2,count,subcount,actgrp,thold,ttog,tdual,"
        "cmode,i1mode,i2mode,tgtmode,mod1,mod2,res1,res2,res3,tol,rnd1,rnd2,sgn1,sgn2\n"
        + trigRow(3027, 1049, 6225, 145, 0, 0, 0.0, 0.0)     // x-crossing Toggle, off
        + trigRow(900, 901, 9000, 50, 0, -1, 0.0, 30.0)      // x-crossing Move ...
        + trigRow(910, 1049, 9001, 60, 1, 1, 0.5, 0.0));     // ... spawning a Toggle, on, 0.5 s
    const std::string grp = writeTmp("dptest_auto_grp.txt",
        "uid,groups\n4256 145\n500 50\n910 50\n600 60\n");
    std::vector<dp::AutoTrig> autos = dp::loadAutoTriggers(trig, grp);
    std::sort(autos.begin(), autos.end(),
              [](const dp::AutoTrig& a, const dp::AutoTrig& b) { return a.cx < b.cx; });
    const auto ev = dp::autoOnEvents(autos);
    const auto ring = ev.find(4256);
    check(ring != ev.end() && ring->second.size() == 1 && ring->second[0].on == 0
              && ring->second[0].autoIdx == 0 && ring->second[0].delay == 0.0,
          "an x-crossing Toggle switches its group off, on its own crossing");
    const auto nested = ev.find(600);
    check(nested != ev.end() && nested->second.size() == 1 && nested->second[0].on == 1
              && nested->second[0].delay == 120.0,
          "a Toggle a crossing spawns switches after the spawn's delay (0.5 s = 120 ticks)");

    // The ring: the crossing's switch plus the box's, resolved the way the model will.
    std::vector<dp::OnEvent> r = ring->second;
    dp::OnEvent box;
    box.box = 0;
    box.on = 1;
    r.push_back(box);
    uint16_t fireB[dp::kTouchBits] = {};
    fireB[0] = 7750;
    const dp::TouchMask b0 = dp::touchBit(0);
    dp::g_autoTrig = autos;
    auto on = [&](dp::TouchMask m, int t) {
        return dp::resolveOn(true, r.data(), r.size(), m, fireB, t);
    };
    check(on(b0, 7000), "before the crossing is resolved the ring is as the reset left it (on)");
    dp::g_autoTrig[0].fireT = 6995;
    check(!on(b0, 7000), "once the crossing is at 6,995 the ring is off after it");
    check(on(b0, 6994), "...and still on the tick before");
    check(!on(dp::TouchMask{}, 8000), "a state that never pressed in the box: off for good");
    check(on(b0, 7760) && !on(b0, 7749), "a state whose box fired at 7,750: on from then");
    dp::g_autoTrig.clear();
}

// THE KEY HOLDS A DELAYED SWITCH. Two states with the same mask that fired a
// box at different ticks are in different worlds until the later one's switch has landed, so
// the dedupe key must keep them apart for that long -- and may merge them afterwards.
void keyHoldsDelayedSwitch() {
    const std::vector<dp::TouchTrig> saved = dp::g_touch;
    dp::g_touch.assign(1, dp::TouchTrig{});
    dp::g_touch[0].togEv.push_back({42, 1, 100.0});   // on, 100 ticks after the box
    dp::State a{};
    a.trig = dp::touchBit(0);
    a.fireB[0] = 100;
    dp::State b = a;
    b.fireB[0] = 150;
    // a's switch lands at 200, b's at 250: at 220 one world has it and the other does not.
    dp::buildTouchMoveTicks();
    check(dp::keyOf(a, 220) != dp::keyOf(b, 220),
          "while the later switch has not landed, the two states keep different keys");
    check(dp::keyOf(a, 400) == dp::keyOf(b, 400),
          "once both have landed they merge again");
    dp::g_touch = saved;
    dp::buildTouchMoveTicks();
}

}  // namespace

// splitCommaFields against the stream loop it replaced in loadLevelFrom, field by field, on the
// shapes a row can take: empty, leading/trailing/repeated commas, a CR left by the line read, and
// rows at and past the loader's column bound.
static void commaFields() {
    std::vector<std::string> rows{"", ",", "a", "a,", ",a", "a,,b", "a,b,", ",,,", "a\r",
                                  "a,b\r", " a , b ", "1,2,3"};
    std::string wide;
    for (int i = 0; i < dp::kObjFields + 6; ++i) wide += std::to_string(i) + ",";
    rows.push_back(wide);
    rows.push_back(wide.substr(0, wide.size() - 1));
    std::string exact;
    for (int i = 0; i < dp::kObjFields; ++i) exact += (i ? "," : "") + std::to_string(i);
    rows.push_back(exact);
    rows.push_back(exact + ",");
    bool same = true;
    for (const std::string& line : rows) {
        std::string a[dp::kObjFields], b[dp::kObjFields];
        std::stringstream ss(line);
        for (int i = 0; i < dp::kObjFields && std::getline(ss, a[i], ','); ++i) {}
        dp::splitCommaFields(line, b, dp::kObjFields);
        for (int i = 0; i < dp::kObjFields; ++i) same = same && a[i] == b[i];
    }
    check(same, "splitCommaFields fills the same fields as the getline loop it replaced");
}

// The reader loadGroupTimeline was until 2026-09-30 -- getline on a text-mode stream, sscanf per
// line -- kept as the reference parseGroupTimeline is held to.
static dp::GroupTimeline groupsByScanf(const std::string& path, long long* endOut,
                                       std::unordered_map<int, uint8_t>* initOut) {
    *endOut = -1;
    initOut->clear();
    dp::GroupTimeline g;
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        int t = 0, uid = 0, on = 1, env = 0;
        float cx = 0, cy = 0, w = 0, h = 0, rot = 0;
        if (line.rfind("end,", 0) == 0) {
            *endOut = std::atoll(line.c_str() + 4);
            continue;
        }
        if (line.rfind("init,", 0) == 0) {
            int u = 0, o = 1;
            if (std::sscanf(line.c_str() + 5, "%d,%d", &u, &o) == 2) (*initOut)[u] = o ? 1 : 0;
            continue;
        }
        const int n = std::sscanf(line.c_str(), "%d,%d,%f,%f,%f,%f,%d,%f,%d", &t, &uid, &cx, &cy,
                                  &w, &h, &on, &rot, &env);
        if (n < 6) continue;
        g[uid].push_back({t, cx, cy, w * 0.5f, h * 0.5f, (uint8_t)(on ? 1 : 0), rot,
                          (uint8_t)(env ? 1 : 0)});
    }
    for (auto& kv : g)
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const dp::DynSample& a, const dp::DynSample& b) { return a.t < b.t; });
    return g;
}

static bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// Empty when the two hold the same samples, bit for bit, AND iterate in the same order: the loader
// walks the timeline, so the order is part of what a reader hands on.
static std::string timelineDiff(const dp::GroupTimeline& a, const dp::GroupTimeline& b) {
    if (a.size() != b.size())
        return "objects " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    auto ia = a.begin();
    auto ib = b.begin();
    for (; ia != a.end(); ++ia, ++ib) {
        if (ia->first != ib->first) return "order differs at uid " + std::to_string(ia->first);
        const auto& va = ia->second;
        const auto& vb = ib->second;
        if (va.size() != vb.size()) return "uid " + std::to_string(ia->first) + " row count";
        for (size_t k = 0; k < va.size(); ++k) {
            const dp::DynSample& x = va[k];
            const dp::DynSample& y = vb[k];
            if (x.t != y.t || x.on != y.on || x.env != y.env || !sameBits(x.cx, y.cx)
                || !sameBits(x.cy, y.cy) || !sameBits(x.hw, y.hw) || !sameBits(x.hh, y.hh)
                || !sameBits(x.rot, y.rot))
                return "uid " + std::to_string(ia->first) + " t=" + std::to_string(x.t);
        }
    }
    return "";
}

// One file through both readers: the rows, the trailer and the init lines. Empty when they agree.
static std::string groupsReadersDiff(const std::string& path, long long* rows = nullptr) {
    long long endA = -1, endB = -1;
    std::unordered_map<int, uint8_t> initA, initB;
    const dp::GroupTimeline a = groupsByScanf(path, &endA, &initA);
    const dp::GroupTimeline b = dp::loadGroupTimeline(path, &endB, &initB);
    if (rows) {
        *rows = 0;
        for (const auto& kv : a) *rows += (long long)kv.second.size();
    }
    const std::string d = timelineDiff(a, b);
    if (!d.empty()) return d;
    if (endA != endB) return "end " + std::to_string(endA) + " vs " + std::to_string(endB);
    if (initA != initB) return "init lines";
    return "";
}

// parseGroupTimeline against the sscanf reader it replaced, on the shapes a line can take: a
// line cut off mid-write, blanks, signs, a CR, an exponent, inf/nan, hex, a trailing comma, more
// columns than the format, more digits than the plain path takes, float rounding midpoints -- and
// 20,000 rows of %.3f the way the recorder writes them, from small to 1e7. Once through a text
// stream (CRLF, as the mod writes) and once written as bytes (LF only).
static void groupsParser() {
    std::string body =
        "tick,uid,cx,cy,w,h,on,rot\n"
        "1,7,0,100,30,3,1,0\n"
        "2,7,0.125,100.5,30.000,3.000,1,45.500\n"
        "3,8,-0.000,-12.345,1.5,2.5,0,-90.000,1\n"
        "4,8,1e3,2E-2,3,4\n"
        "5,9, 7.5, 8.25,9,10,1,0\n"
        "6,9,.5,1.,+2.5,-.25,0,0\n"
        "7,10,1.5,2.5,3.5\n"
        "8,10,1.5,2.5,3.5,4.5,\n"
        "9,10,1.5,2.5,3.5,4.\n"
        "10,11,nan,inf,-inf,1,1,0\n"
        "11,11,123456.789,0.001,99999.999,0.0005,1,359.999\n"
        "12,11,1.2.3,4,5,6\n"
        "\n"
        "13,12,1,2,3,4,5,6,7,8\n"
        "14,12,0x1p3,2,3,4\n"
        "init,12,0\n"
        "init,13,1\n"
        "15,13,3.14159265358979,2.718281828,1,1\n"
        "16,13,16777217.000,33554433.5,1,1\n"
        "17,13,2097152.125,4194303.875,1,1\n"
        "18,13,0.1,0.2,0.3,0.7,1,0.9\n"
        "19,14,-5,6,7,8,1,0\r\n"
        "20,14,  -7.5  ,1,1,1\n"
        "21 ,15,1,1,1,1\n"
        "-22,15,1,1,1,1\n"
        "23,16,1,1,1,1,1,1,1\n"
        "24,16,1,1,1,1,0,0,0\n"
        "25,16,-,1,1,1\n"
        "26,16,1,.,1,1\n"
        "end,77\n";
    unsigned long long s = 0x9E3779B97F4A7C15ULL;
    auto next = [&s]() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return (double)(s >> 11) / 9007199254740992.0;
    };
    const double scales[] = {1.0, 30.0, 360.0, 5000.0, 40000.0, 2.1e6, 1.0e7};
    for (int i = 0; i < 20000; ++i) {
        const double sc = scales[i % 7];
        char b[256];
        std::snprintf(b, sizeof b, "%d,%d,%.3f,%.3f,%.3f,%.3f,%d,%.3f%s\n", 30 + i, 100 + i % 97,
                      (float)((next() - 0.5) * sc), (float)(next() * sc), (float)(next() * 60.0),
                      (float)(next() * 60.0), i % 2, (float)((next() - 0.5) * 720.0),
                      i % 5 == 0 ? ",1" : "");
        body += b;
    }
    body += "99999,17,1,2,3,4";   // the last line with no newline
    const std::string text = writeTmp("dptest_groups_text.txt", body);
    const std::string bin = (std::filesystem::temp_directory_path() / "dptest_groups_lf.txt").string();
    {
        std::ofstream f(bin, std::ios::binary | std::ios::trunc);
        f << body;
    }
    const std::string d1 = groupsReadersDiff(text);
    const std::string d2 = groupsReadersDiff(bin);
    if (!d1.empty()) std::printf("  text stream: %s\n", d1.c_str());
    if (!d2.empty()) std::printf("  bytes: %s\n", d2.c_str());
    check(d1.empty() && d2.empty(),
          "parseGroupTimeline reads every line the way the sscanf reader it replaced did");

    // groupLayersFor's first layer is a COPY of a file's parse (the parse stays cached for the
    // next call), where it used to be the parse itself: the copy has to iterate the same way, and
    // an overlay laid over it has to come out as it did over the original.
    long long end = -1;
    std::unordered_map<int, uint8_t> init;
    dp::GroupTimeline base = dp::loadGroupTimeline(bin, &end, &init);
    const dp::GroupTimeline over = dp::loadGroupTimeline(writeTmp("dptest_groups_over.txt",
        "tick,uid,cx,cy,w,h,on,rot\n"
        "5,7,1,2,3,4,1,0\n"
        "6,50000,1,2,3,4,1,0\n"
        "7,50001,1,2,3,4,1,0\n"
        "8,150,1,2,3,4,1,0\n"
        "end,20\n"), &end, &init);
    dp::GroupTimeline copy = base;
    const std::string dc = timelineDiff(base, copy);
    dp::overlayGroupTimeline(copy, over, false, end);
    dp::overlayGroupTimeline(base, over, false, end);
    const std::string doverlay = timelineDiff(base, copy);
    if (!dc.empty()) std::printf("  copy: %s\n", dc.c_str());
    if (!doverlay.empty()) std::printf("  overlay: %s\n", doverlay.c_str());
    check(dc.empty() && doverlay.empty(),
          "a copied parse iterates as the parse does, and so does an overlay laid over it");
}

// World-axis teleport settings must survive all four gameplay frames.
void teleportTargets() {
    dp::Obj entry{};
    entry.id = 2902; entry.type = 28;
    entry.cx = 100; entry.cy = 200;
    entry.tpEntryDx = -3; entry.tpEntryDy = 7;
    entry.tpEx = 1000; entry.tpEy = 400; entry.tpExitCount = 1;
    for (int frame = 0; frame < 4; ++frame) {
        bool ok = true;
        for (int save = 0; save < 2; ++save)
            for (int axes = 0; axes < 4; ++axes) {
                dp::Obj p = entry;
                p.tpSaveOffset = (uint8_t)save;
                p.tpIgnoreX = (uint8_t)(axes & 1); p.tpIgnoreY = (uint8_t)(axes & 2);
                dp::turnObj(p, frame);
                double x, y, tx, ty, wx, wy;
                dp::toFrame(frame, 105, 210, x, y);
                dp::teleportTarget(p, frame, x, y, tx, ty);
                dp::fromFrame(frame, tx, ty, wx, wy);
                const double ex = (axes & 1) ? 105 : 1000 + (save ? 8 : 0);
                const double ey = (axes & 2) ? 210 : 400 + (save ? 3 : 0);
                ok = ok && wx == ex && wy == ey;
            }
        check(ok, "teleport offset and ignored axes use real entry/world coordinates in every frame");
    }
    double x, y;
    entry.tpEx = 0; entry.tpEy = 0;
    dp::teleportTarget(entry, 0, 105, 210, x, y);
    check(x == 0 && y == 0, "an explicitly known exit at the origin is not a missing destination");
    entry.tpExitCount = 0;
    dp::teleportTarget(entry, 0, 105, 210, x, y);
    check(x == 105 && y == 210, "a teleport with no exit does not move the body");
    entry.id = 747; entry.tpExitCount = 1; entry.tpEx = 1000; entry.tpEy = 400;
    dp::teleportTarget(entry, 0, 105, 210, x, y);
    check(x == 105 && y == 400, "747 retains player X and resolves its linked exit Y");
    entry.tpSaveOffset = 1;
    dp::teleportTarget(entry, 0, 105, 210, x, y);
    check(x == 113 && y == 403, "747 applies saveOffset after constructing its player-X target");
    entry.tpSaveOffset = 0;
    entry.tpExitCount = -1; entry.tpEx = 0; entry.tpEy = 0; entry.tpY = 275;
    dp::teleportTarget(entry, 0, 105, 210, x, y);
    check(x == 105 && y == 275, "old dumps retain their unlinked vertical target");
}

// Force arithmetic keeps GD's redirect precedence and boost semantics.
void teleportForces() {
    dp::Obj p{};
    p.tpStaticForce = 1; p.tpForce = 20; p.tpForceAngle = 90;
    float vy = -4;
    uint8_t boost = 0;
    dp::teleportForce(p, 0, vy, boost);
    check(std::fabs(vy - 20) < 1e-5 && boost == 1, "static force overwrites velocity and raises boost");
    p.tpForceAdditive = 1; vy = -4;
    dp::teleportForce(p, 0, vy, boost);
    check(std::fabs(vy - 16) < 1e-5, "additive force adds to incoming native velocity");
    p.tpForce = 0; p.tpForceAdditive = 0; vy = 100; boost = 1;
    dp::teleportForce(p, 0, vy, boost);
    check(vy == 0 && boost == 0, "zero non-additive force clears velocity and boost");
    p.tpForce = 20; p.tpForceAngle = 0; vy = 4;
    dp::teleportForce(p, 1, vy, boost);
    check(std::fabs(vy - 20) < 1e-5, "sideways gameplay swaps force components");
    vy = 4;
    dp::teleportForce(p, 3, vy, boost);
    check(std::fabs(vy + 20) < 1e-5, "frame 3 mirrors native force into the solver axis");
    p.tpRedirectForce = 1; p.tpForceAngle = 90; p.tpRedirectMod = 2;
    p.tpRedirectMax = 6; vy = -4;
    dp::teleportForce(p, 0, vy, boost);
    check(std::fabs(vy - 6) < 1e-5, "redirect wins over static force and caps scaled magnitude");
    p.tpRedirectMax = 0; p.tpRedirectMin = 3; vy = 0;
    dp::teleportForce(p, 0, vy, boost);
    check(std::fabs(vy - 3) < 1e-5, "zero-speed redirect uses its bearing and minimum");
    p.tpRedirectMin = 10; p.tpRedirectMax = 6; vy = -4;
    dp::teleportForce(p, 0, vy, boost);
    check(std::fabs(vy - 6) < 1e-5, "redirect maximum takes precedence over minimum");
}

// New columns remain below the field bound and are resolved by name.
void teleportColumns() {
    dp::resetInvocationState();
    const std::string tail = ",tpentryx,tpentryy,tpsave,tpexits,tpf,tpfv,tpfadd,tpangle,"
                             "tpr,tprmod,tprmin,tprmax,tprdash";
    std::vector<std::string> values(63, "0");
    values[0] = "2902"; values[1] = "28";
    values[2] = "100"; values[3] = "200"; values[4] = "10"; values[5] = "60";
    values[7] = "42"; values[17] = "200"; values[34] = "1000"; values[35] = "400";
    values[50] = "97"; values[51] = "207"; values[52] = "1"; values[53] = "1";
    values[54] = "1"; values[55] = "20"; values[56] = "1"; values[57] = "90";
    values[58] = "1"; values[59] = "2"; values[60] = "3"; values[61] = "6";
    values[62] = "1";
    // Serialize exactly the CSV field boundaries used by objrects.
    auto row = [&]() {
        std::string out;
        for (const auto& value : values) out += (out.empty() ? "" : ",") + value;
        return out + "\n";
    };
    std::istringstream in(std::string(kObjHeader) + tail + "\n" + row());
    const dp::Level level = dp::loadLevelFrom(in);
    check(level.portals.size() == 1, "new export columns retain the teleport portal");
    if (!level.portals.empty()) {
        const dp::Obj& p = level.portals[0];
        check(p.tpEntryDx == -3 && p.tpEntryDy == 7 && p.tpSaveOffset && p.tpExitCount == 1
            && p.tpStaticForce && p.tpForce == 20 && p.tpForceAdditive && p.tpForceAngle == 90
            && p.tpRedirectForce && p.tpRedirectMod == 2 && p.tpRedirectMin == 3
            && p.tpRedirectMax == 6 && p.tpRedirectDash && level.spatialTeleport,
            "all geometry and force settings survive the complete export tail");
    }
    check(level.unsupported.find("redirects a dash") != std::string::npos,
          "unmodelled dash redirection is reported rather than silently dropped");
    values[62] = "0";
    values[53] = "2";
    std::istringstream random(std::string(kObjHeader) + tail + "\n" + row());
    check(dp::loadLevelFrom(random).unsupported.find("random destinations") != std::string::npos,
          "random multi-exit teleport is refused rather than choosing the first exit");
    values[53] = "1";
    std::string crowded = std::string(kObjHeader) + tail + "\n";
    for (int i = 0; i <= dp::kGravPortalBits; ++i) {
        values[7] = std::to_string(i + 1);
        crowded += row();
    }
    std::istringstream over(crowded);
    check(dp::loadLevelFrom(over).unsupported.find("independent activation bits") != std::string::npos,
          "spatial levels cannot silently exceed their per-body portal activation capacity");
    dp::resetInvocationState();
}

// A remote body's read-only query must neither move a group cursor nor lose wide rectangles.
void independentWindows() {
    std::vector<dp::Obj> objects(3);
    objects[0].cx = 10; objects[0].hw = 1; objects[0].uid = 1;
    objects[1].cx = 100; objects[1].hw = 1; objects[1].uid = 2;
    objects[2].cx = 1000; objects[2].hw = 1000; objects[2].uid = 3;
    dp::XSlice slice(objects);
    slice.seekTo(2500);
    const size_t cursor = slice.lo;
    std::vector<int> found;
    slice.forRangeAt(10, 10, [&](const dp::Obj& o) { found.push_back(o.uid); });
    check(found == std::vector<int>({1, 3}) && slice.lo == cursor,
          "separate windows find overlapping wide objects without changing the shared cursor");
}

// Exercise the same dual transition used by search, replay and witness reconstruction.
void teleportDualStep() {
    dp::resetInvocationState();
    dp::Level level;
    dp::Obj a{};
    a.id = 2902; a.type = 28; a.uid = 10; a.gpBit = 0;
    a.cx = 100; a.cy = 200; a.hw = 1; a.hh = 30;
    a.tpEx = 1000; a.tpEy = 300; a.tpExitCount = 1;
    dp::Obj b = a;
    b.uid = 20; b.gpBit = 1; b.cx = 3017; b.cy = 400;
    b.tpEx = 4000; b.tpEy = 500;
    level.portals = {a, b};
    dp::XSlice near(level.objs), ports(level.portals), pads(level.pads), orbs(level.orbs),
               slopes(level.slopes), speeds(level.speeds);
    std::vector<const dp::Obj*> empty, primary{&level.portals[0]};
    const auto ship = gdapprox::ShipParams::normal(), shipMini = gdapprox::ShipParams::mini();
    const auto ufo = gdapprox::UfoParams::normal(), ufoMini = gdapprox::UfoParams::mini();
    dp::State s{};
    s.xAbs = 83.5f; s.xAbs2 = 3000; s.y = 200; s.y2 = 400;
    s.dual = 1; s.mode2 = 0; s.flip2 = 1; s.dx = 1.6f;
    dp::StepCtx k{85.1, 83.5, 1.6f, 1, &empty, &primary, &empty, &empty, &empty, &empty,
                  &ship, &shipMini, &ufo, &ufoMini};
    k.slices = {&near, &ports, &pads, &orbs, &slopes, &speeds}; k.dyn = &level.dyn;
    bool dead = false;
    const dp::State c = dp::stepBoth(s, 0, k, dead);
    check(!dead && c.xAbs == 1000 && c.xAbs2 == 4000 && c.y == 300 && c.y2 == 500,
          "each separated dual body reaches its own teleport and retains its own exit X");
    check(c.portalLatch.test(0) && c.portalLatch2.test(1),
          "each body spends only its own teleport activation");
    check(c.tpSkip == 1 && c.tpSkip2 == 1,
          "each body's teleport arrival survives the dual merge");
    dp::State swapped = c;
    dp::swapHalves(swapped); dp::swapHalves(swapped);
    check(swapped.xAbs == c.xAbs && swapped.xAbs2 == c.xAbs2,
          "swapping dual bodies twice preserves both float positions exactly");
    dp::State other = c; other.xAbs2 += 10;
    const auto key = dp::keyOf(c, 1);
    dp::SearchKey parsed;
    check(key != dp::keyOf(other, 1) && dp::parseKeyText(dp::keyText(key), parsed) && parsed == key,
          "full search keys distinguish independent secondary X and round-trip the new schema");
    check(!dp::parseKeyText("v1:0", parsed), "legacy full keys cannot certify a new spatial state");
    other = c; other.tpSkip2 = 0;
    check(key != dp::keyOf(other, 1), "arrival history is part of physical search identity");
    other = c; other.xAbs2 += 10;
    dp::Fixup fix{};
    fix.x = c.xAbs; fix.x2 = c.xAbs2; fix.y = c.y; fix.vy = c.vy;
    fix.y2 = c.y2; fix.vy2 = c.vy2; fix.dual = 1;
    fix.g = c.grounded; fix.flip = c.flip; fix.mode = c.mode; fix.mini = c.mini;
    check(dp::fixupMatches(fix, c, 0) && !dp::fixupMatches(fix, other, 0),
          "a learned transition cannot match another secondary position");
    dp::Obj destination = a;
    destination.uid = 30; destination.gpBit = 2;
    destination.cx = 1000; destination.cy = 300;
    destination.tpEx = 5000; destination.tpEy = 600;
    level.portals.push_back(destination);
    dp::XSlice destinationPorts(level.portals);
    k.slices[1] = &destinationPorts;
    primary = {&level.portals.back()};
    k.t = 2; k.x = c.xAbs + c.dx; k.xPrev = c.xAbs;
    const dp::State arrived = dp::stepBoth(c, 0, k, dead);
    check(!dead && arrived.xAbs == 5000 && arrived.portalLatch.test(2),
          "an unspent destination portal fires on the step after a spatial arrival");
    dp::State spent = c;
    spent.portalLatch.set(2);
    const dp::State held = dp::stepBoth(spent, 0, k, dead);
    check(held.xAbs != 5000 && held.tpSkip == 0 && held.tpSkip2 == 0,
          "arrival cannot re-fire a spent portal and both one-tick flags expire");
    level.portals.back().tpExitCount = 0;
    const dp::State noExit = dp::stepBoth(c, 0, k, dead);
    check(noExit.xAbs != 5000 && noExit.tpSkip == 1 && noExit.portalLatch.test(2),
          "a portal without an exit still spends its activation and sets the native arrival byte");
    const auto names = dp::histNamesFor(5);
    check(std::string(names[9]) == "teleported" && std::string(names[10]) == "teleported2"
          && dp::histNamesFor(4)[9] == nullptr,
          "versioned history carries both arrival flags without changing older payloads");
    dp::resetInvocationState();
}

// Native role selection includes the inactive partner and never inherits a Spawn's toucher.
void playerEffectRoles() {
    dp::PlayerEffect e{};
    e.teleport.id = 2066; e.gravity = 2.f;
    for (int flags = 0; flags < 4; ++flags) {
        e.player1 = (uint8_t)(flags & 1); e.player2 = (uint8_t)((flags >> 1) & 1);
        dp::State s{};
        dp::applyPlayerEffect(s, e, 0);
        check(s.gravityMod == (e.player2 ? 1.f : 2.f)
            && s.gravityMod2 == (e.player1 ? 1.f : 2.f),
            "gravity target flags match native writes, including an inactive P2");
    }
    e.triggeringPlayer = 1;
    for (int who = 0; who < 3; ++who) {
        dp::State s{};
        dp::applyPlayerEffect(s, e, who);
        check(s.gravityMod == (who == 1 ? 2.f : 1.f)
            && s.gravityMod2 == (who == 2 ? 2.f : 1.f),
            "triggering-player gravity writes only a real collision toucher");
    }
    e = {};
    e.teleport.id = 3022; e.teleport.tpExitCount = 1;
    e.teleport.tpEx = 1000; e.teleport.tpEy = 300;
    dp::State s{};
    s.xAbs = 100; s.y = 200; s.xAbs2 = 2000; s.y2 = 400; s.dual = 1;
    dp::applyPlayerEffect(s, e, 2);
    check(s.xAbs == 1000 && s.y == 300 && s.xAbs2 == 2000 && s.y2 == 400,
          "a teleport trigger always teleports P1, even when P2 touches it");
    check(s.tpSkip == 1 && !s.portalLatch.any(),
          "teleport triggers set the arrival byte without spending a collision portal");
    e.teleport.tpExitCount = 0; e.teleport.tpStaticForce = 1;
    e.teleport.tpForce = 12; e.teleport.tpForceAngle = 90;
    dp::applyPlayerEffect(s, e, 0);
    check(s.xAbs == 1000 && s.y == 300 && std::fabs(s.vy - 12) < 1e-5 && s.boost,
          "a trigger without an exit still applies its native force");
    s.gravityMod = 0; s.gravityMod2 = -2; s.spinMod = .5f; s.spinMod2 = 3;
    dp::swapHalves(s);
    check(s.gravityMod == -2 && s.gravityMod2 == 0 && s.spinMod == 3 && s.spinMod2 == .5f,
          "per-body gravity and retained spin swap with their body");
}

// Exercise the public tail, not a hand-constructed effect, including the actual Spawn delay.
void playerEffectColumns() {
    dp::resetInvocationState();
    const std::string tail = ",tpentryx,tpentryy,tpsave,tpexits,tpf,tpfv,tpfadd,tpangle,"
        "tpr,tprmod,tprmin,tprmax,tprdash,pgrav,ptarget1,ptarget2,ptrigger,multi,"
        "trigexit,ord,silent,psdelay,psrange,psordered,pexituid,psingle";
    std::vector<std::string> v(76, "0");
    v[0] = "2066"; v[1] = "20"; v[2] = "100"; v[3] = "300";
    v[4] = "30"; v[5] = "30"; v[7] = "42"; v[63] = "-0.5";
    v[64] = "1"; v[66] = "1"; v[69] = "7";
    // Build the same column boundaries the exporter uses.
    auto text = [&]() {
        std::string row;
        for (size_t i = 0; i < v.size(); ++i) row += (i ? "," : "") + v[i];
        return std::string(kObjHeader) + tail + "\n" + row + "\n";
    };
    std::istringstream g(text());
    auto level = dp::loadLevelFrom(g);
    check(level.unsupported.empty() && level.playerEffects.size() == 1
        && level.playerEffects[0].gravity == -.5f && level.playerEffects[0].player1
        && level.playerEffects[0].triggeringPlayer && level.playerSources.at(42).order == 7,
        "gravity value, roles and ordering survive the complete objrects export");
    v[0] = "3022"; v[34] = "1000"; v[35] = "400"; v[53] = "1";
    std::istringstream tp(text());
    level = dp::loadLevelFrom(tp);
    check(level.unsupported.empty() && level.playerEffects.size() == 1 && level.portals.empty()
        && level.playerEffects[0].teleport.tpEx == 1000 && level.spatialTeleport,
        "ID 3022 resolves an exit but is never routed as an ordinary portal");
    v[0] = "1268"; v[71] = ".125"; v[72] = ".25"; v[73] = "1";
    std::istringstream sp(text());
    level = dp::loadLevelFrom(sp);
    const auto& meta = level.playerSources.at(42);
    check(meta.delay == .125 && meta.delayRange == .25 && meta.ordered,
          "Spawn scheduling reads its own delay, range and ordered flag, not dur");
    std::istringstream old(std::string(kObjHeader) + "\n"
        "2066,20,100,300,30,30,0,42\n");
    check(dp::loadLevelFrom(old).unsupported.find("refreshed") != std::string::npos,
          "old exports cannot silently drop player effect metadata");
    dp::resetInvocationState();
}

// Delayed effects share source clocks with geometry, without widening every search state.
void playerTriggerClocks() {
    dp::resetInvocationState();
    dp::TouchTrig t{};
    t.uid = 10; t.id = 1268; t.cx = 100; t.cy = 300; t.hw = t.hh = 15;
    t.playerOnly = true; t.playerAuto = true;
    dp::PlayerEffect e{};
    e.teleport.id = 2066; e.gravity = 2;
    t.playerActions.push_back({e, 10});
    dp::g_touch = {t}; dp::g_playerRoots = {0};
    std::vector<const dp::Obj*> empty;
    dp::StepCtx k{}; k.t = 100;
    dp::State s{}; s.xAbs = 99; s.y = 300;
    dp::playerTriggerTick(s, k, dp::PlayerPhase::Automatic);
    check(!s.trig.any(), "an autonomous player source does not fire before its crossing");
    s.xAbs = 100;
    dp::playerTriggerTick(s, k, dp::PlayerPhase::Automatic);
    check(s.trig.test(0) && s.fireB[0] == 100 && s.gravityMod == 1,
          "an autonomous source latches its own crossing without applying a delayed leaf early");
    dp::State later = s; later.fireB[0] = 101;
    check(dp::keyOf(s, 105) != dp::keyOf(later, 105),
          "pending player events retain exact fire ticks within the old four-tick key bucket");
    dp::SearchKey parsed;
    check(dp::parseKeyText(dp::keyText(dp::keyOf(s, 105)), parsed)
          && parsed == dp::keyOf(s, 105) && !dp::parseKeyText("v2:0", parsed),
          "player-state full keys round-trip and reject a pre-gravity schema");
    k.t = 109; dp::applyPendingPlayerEffects(s, k.t);
    check(s.gravityMod == 1, "a delayed player event remains pending through the tick before due");
    k.t = 110; dp::applyPendingPlayerEffects(s, k.t);
    check(s.gravityMod == 2 && s.gravityMod2 == 2,
          "the exact due tick updates both native gravity fields");
    k.t = 111; dp::applyPendingPlayerEffects(later, k.t);
    check(s.gravityMod == later.gravityMod && dp::keyOf(s, 200) == dp::keyOf(later, 200),
          "once delayed writes settle, identical worlds can merge again");
    dp::TouchTrig earlier = t;
    earlier.uid = 20;
    earlier.playerActions[0] = {e, 60, {{0, 1}}};
    earlier.playerActions[0].effect.gravity = 3;
    dp::g_touch[0].playerActions[0].queuePath = {{0, 1}};
    dp::g_touch.push_back(earlier); dp::g_playerRoots = {0, 1};
    dp::State simultaneous{};
    simultaneous.trig.set(0); simultaneous.trig.set(1);
    simultaneous.fireB[0] = 100; simultaneous.fireB[1] = 50;
    k.t = 110;
    dp::applyPendingPlayerEffects(simultaneous, k.t);
    check(simultaneous.gravityMod == 2,
          "simultaneous delayed writes follow enqueue time, not source UID order");
    dp::g_touch[0].playerActions[0] = {e, 10, {{0, 1}, {5, 2}}};
    dp::g_touch[1].playerActions[0].delay = 7;
    simultaneous.fireB[1] = 103; simultaneous.gravityMod = 1;
    dp::applyPendingPlayerEffects(simultaneous, k.t);
    check(simultaneous.gravityMod == 2,
          "nested delayed writes compare the final Spawn enqueue, not the root date");
    dp::g_touch[1].playerActions[0].delay = 5;
    simultaneous.fireB[1] = 105; simultaneous.gravityMod = 1;
    dp::applyPendingPlayerEffects(simultaneous, k.t);
    check(simultaneous.gravityMod == 3,
          "a pending event enqueues before an autonomous root on the same tick");
    dp::g_touch.resize(1); dp::g_playerRoots = {0};
    k.t = 111;
    later = s; later.gravityMod2 = 3;
    check(dp::keyOf(s, 200) != dp::keyOf(later, 200),
          "an inactive P2 multiplier is still part of future search identity");
    later = s; later.spinMod = .5f;
    check(dp::keyOf(s, 200) != dp::keyOf(later, 200),
          "a cube's retained spin stake cannot merge into another magnitude");
    dp::g_touch[0].playerAuto = false; dp::g_touch[0].id = 2066;
    dp::g_touch[0].playerActions[0] = {e, 0};
    dp::g_touch[0].playerActions[0].effect.triggeringPlayer = 1;
    s = {}; s.xAbs = 100; s.y = 300;
    dp::playerTriggerTick(s, k, dp::PlayerPhase::TouchP1, 300);
    check(s.gravityMod == 2 && s.gravityMod2 == 1 && s.fireB[0] == 111,
          "a direct touch supplies P1's ID and the collision tick");
    dp::resetInvocationState();
    check(dp::g_playerRoots.empty() && dp::g_touch.empty(),
          "a second solver invocation inherits no player sources or pending actions");
}

// The graph uses measured Spawn metadata, and refuses schedules not represented by a clock.
void playerSpawnGraph() {
    dp::resetInvocationState();
    const std::string tr = writeTmp("dptest_player_trig.txt", "header\n"
        + trigRow(1, 1268, 100, 10, 0, -1, 99, 0)
        + trigRow(2, 1268, 200, 20, 1, -1, 99, 0));
    const std::string gr = writeTmp("dptest_player_group.txt", "uid,groups\n2 10\n3 20\n");
    dp::Level l;
    dp::PlayerTriggerMeta a{}, b{}, c{};
    a.uid = 1; a.id = 1268; a.cx = 100; a.cy = 300; a.delay = .125;
    b = a; b.uid = 2; b.spawn = true; b.delay = .001;
    c = a; c.uid = 3; c.id = 2066; c.spawn = true;
    l.playerSources = {{1, a}, {2, b}, {3, c}};
    dp::PlayerEffect e{}; e.teleport.id = 2066; e.teleport.uid = 3; e.gravity = 0;
    l.playerEffects = {e};
    dp::loadPlayerTriggers(l, tr, gr);
    check(l.unsupported.empty() && dp::g_playerRoots.size() == 1
          && dp::g_touch[0].playerActions.size() == 1
          && dp::g_touch[0].playerActions[0].delay == 31
          && dp::g_touch[0].playerActions[0].queuePath
              == std::vector<std::pair<int, int>>{{0, 1}, {30, 2}},
          "nested Spawn delays use the native float-dt clock per hop, ignoring dur/sdelay");
    dp::g_touch[0].playerOnly = false;
    dp::loadPlayerTriggers(l, tr, gr);
    check(dp::g_touch.size() == 1 && !dp::g_touch[0].playerOnly,
          "a Spawn that also controls geometry reuses its existing source bit");
    l.playerSources[1].delay = (double)0.05f;
    l.playerSources[2].delay = 0;
    dp::loadPlayerTriggers(l, tr, gr);
    check(l.unsupported.empty() && dp::g_touch[0].playerActions[0].delay == 12,
          "a float 0.05-second delay fires at 12 ticks, not ceil(float delay * 240) = 13");
    l.playerSources[2].delayRange = .1;
    dp::loadPlayerTriggers(l, tr, gr);
    check(l.unsupported.find("random") != std::string::npos,
          "random Spawn delays are refused instead of being treated as deterministic");
    l.unsupported.clear(); l.playerSources[2].delayRange = 0;
    l.playerSources[1].multi = true;
    dp::loadPlayerTriggers(l, tr, gr);
    check(l.unsupported.find("repeating") != std::string::npos,
          "a repeated source cannot overwrite its only event clock silently");
    l.unsupported.clear(); l.playerSources[1].multi = false;
    dp::loadPlayerTriggers(l, "", "");
    check(l.unsupported.find("no modelled Spawn source") != std::string::npos,
          "a spawned player effect without the graph export is explicitly unsupported");
    dp::resetInvocationState(); l.unsupported.clear();
    dp::g_timeWarps.push_back({0, .5});
    dp::loadPlayerTriggers(l, tr, gr);
    check(l.unsupported.find("game-time event clock") != std::string::npos,
          "TimeWarp cannot silently turn a tick clock into a real-time Spawn timer");
    dp::resetInvocationState(); l.unsupported.clear();
    dp::g_rotTrig.push_back({});
    dp::loadPlayerTriggers(l, tr, gr);
    check(l.unsupported.find("rotation queue") != std::string::npos,
          "unmodelled autonomous queue interleaving is explicitly refused");
    dp::resetInvocationState();
    l.unsupported.clear(); l.playerSources[1].disabled = true;
    dp::loadPlayerTriggers(l, tr, gr);
    check(l.unsupported.find("disabled or NoTouch") != std::string::npos,
          "a disabled source cannot silently fire its player effects");
    dp::resetInvocationState();
}

// Gravity changes acceleration, not jump/flap targets, ship thrust or terminal velocities.
void gravityMultiplierPhysics() {
    for (int mode : {0, 2, 5, 6, 7}) {
        check(dp::playerGravityStep(mode, 1.6, false, 0, 1) == 0,
              "zero gravity removes each gravity-mode acceleration");
        check(dp::playerGravityStep(mode, 1.6, false, -2, 1)
              == -dp::playerGravityStep(mode, 1.6, false, 2, 1),
              "negative gravity reverses acceleration rather than flipping body polarity");
    }
    check(dp::playerGravityStep(5, 1, false, 2, 1)
          != dp::playerGravityStep(5, 2, false, 2, 1),
          "robot gravity retains its speed-dependent native base");
    const auto ship = gdapprox::ShipParams::normal();
    const auto ufo = gdapprox::UfoParams::normal();
    check(gdapprox::ShipModel::stepVy(0, true, ship, false, false, 0)
          == gdapprox::ShipModel::stepVy(0, true, ship),
          "zero gravity does not remove a ship's upward thrust");
    check(gdapprox::ShipModel::stepVy(0, false, ship, false, false, 0) == 0,
          "zero gravity does remove a ship's downward acceleration");
    check(gdapprox::UfoModel::stepVy(0, true, ufo, false, false, 0, 0) == ufo.flapTargetVy,
          "zero gravity preserves the UFO's native flap target");
    check(gdapprox::UfoModel::stepVy(-100, false, ufo, false, false, 0, 10)
          == ufo.vyMinPlayerFrame,
          "the UFO terminal is not multiplied by gravity");
    dp::resetInvocationState();
    dp::Level level;
    dp::XSlice near(level.objs), ports(level.portals), pads(level.pads), orbs(level.orbs),
        slopes(level.slopes), speeds(level.speeds);
    std::vector<const dp::Obj*> empty;
    const auto shipMini = gdapprox::ShipParams::mini();
    const auto ufoMini = gdapprox::UfoParams::mini();
    dp::StepCtx k{101.6, 100, 1.6f, 1, &empty, &empty, &empty, &empty, &empty, &empty,
        &ship, &shipMini, &ufo, &ufoMini};
    k.slices = {&near, &ports, &pads, &orbs, &slopes, &speeds}; k.dyn = &level.dyn;
    dp::State s{}; s.xAbs = 100; s.y = 300; s.dx = 1.6f;
    bool dead = false;
    s.gravityMod = 0;
    const auto zero = dp::stepBoth(s, 0, k, dead);
    check(!dead && zero.y == 300 && zero.vy == 0,
          "the real transition preserves an airborne zero-gravity cube");
    s.gravityMod = 4; s.vy = -14.9f;
    const auto capped = dp::stepBoth(s, 0, k, dead);
    check(!dead && capped.vy == -15,
          "scaled cube acceleration still uses the native -15 terminal");
    s = {}; s.xAbs = 100; s.y = 300; s.dx = 1.6f; s.mode = 4;
    const auto wave = dp::stepBoth(s, 0, k, dead);
    s.gravityMod = 0;
    const auto waveZero = dp::stepBoth(s, 0, k, dead);
    check(wave.y == waveZero.y && wave.vy == waveZero.vy,
          "wave motion ignores the stored gravity multiplier");
    s = {}; s.xAbs = 100; s.y = 300; s.dx = 1.6f;
    s.dual = 1; s.xAbs2 = 3000; s.y2 = 400; s.flip2 = 1;
    dp::TouchTrig t{}; t.uid = 42; t.id = 2066; t.cx = 100; t.cy = 300;
    t.hw = t.hh = 15; t.playerOnly = true;
    dp::PlayerEffect e{}; e.teleport.id = 2066; e.gravity = 0;
    t.playerActions.push_back({e, 0}); dp::g_touch = {t}; dp::g_playerRoots = {0};
    const auto pair = dp::stepBoth(s, 0, k, dead);
    check(!dead && pair.vy < 0 && pair.vy2 == 0 && pair.gravityMod == 0 && pair.gravityMod2 == 0,
          "P1's collision changes P2 gravity before its same-tick physics, not P1's finished move");
    dp::g_touch[0].playerAuto = true;
    const auto automatic = dp::stepBoth(s, 0, k, dead);
    check(!dead && automatic.vy < 0 && automatic.vy2 > 0 && automatic.gravityMod == 0
        && automatic.gravityMod2 == 0,
        "an autonomous gravity write follows both moves, unlike a collision touch");
    dp::g_touch[0].playerAuto = false;
    dp::g_touch[0].cx = 3000; dp::g_touch[0].cy = 400;
    dp::g_touch[0].playerActions[0].effect.player1 = 1;
    const auto fromP2 = dp::stepBoth(s, 0, k, dead);
    check(!dead && fromP2.vy < 0 && fromP2.vy2 > 0 && fromP2.gravityMod == 0
        && fromP2.gravityMod2 == 1,
        "P2's collision updates P1 only after both bodies have integrated");
    dp::resetInvocationState();
}

// Re-anchoring must reject malformed clocks and preserve exact finite multipliers.
void playerAnchorParsing() {
    std::vector<dp::PlayerFire> events;
    check(dp::parsePlayerHistory("-", 10, events) && events.empty(),
          "an explicitly empty player history is complete");
    check(dp::parsePlayerHistory("42:0,50:10", 10, events) && events.size() == 2,
          "player history accepts reset-tick and anchor-tick sources");
    check(dp::parsePlayerHistory("42/1:2,42/2:5", 10, events) && events.size() == 2
          && events[0].body == 1 && events[1].body == 2,
          "the same source UID retains independent P1/P2 native clocks");
    for (const char* bad : {"", "42", "42:x", "-1:2", "42:-1", "42:11", "42:2,",
                            "42:2,42:3", "42:2junk", "42:2:3", "42:99999999999"})
        check(!dp::parsePlayerHistory(bad, 10, events), "malformed/future player history is refused");
    double a, b;
    check(dp::parseFinitePair("0,-0.5", a, b) && a == 0 && b == -.5,
          "zero and negative gravity multipliers are valid anchor values");
    for (const char* bad : {"nan,1", "inf,1", "1", "1,2junk", "1,2,3"})
        check(!dp::parseFinitePair(bad, a, b), "non-finite or truncated multiplier pairs are refused");
}

// Touch-triggered effects latch per player; m_isSinglePTouch intentionally shares one slot.
void playerTouchLatches() {
    dp::resetInvocationState();
    dp::Level l;
    dp::PlayerTriggerMeta m{};
    m.uid = 42; m.id = 2066; m.touch = true;
    m.cx = 100; m.cy = 300; m.hw = m.hh = 15;
    l.playerSources.emplace(42, m);
    dp::PlayerEffect e{}; e.teleport.uid = 42; e.teleport.id = 2066;
    e.gravity = 0; e.triggeringPlayer = 1;
    l.playerEffects.push_back(e);
    dp::loadPlayerTriggers(l, "", "");
    check(l.unsupported.empty() && dp::g_playerRoots.size() == 2,
          "ordinary touch effects allocate independent body slots without widening State");
    dp::StepCtx k{}; k.t = 5;
    dp::State s{}; s.xAbs = s.xAbs2 = 100; s.y = s.y2 = 300; s.dual = 1;
    dp::playerTriggerTick(s, k, dp::PlayerPhase::TouchP1, 300);
    check(s.gravityMod == 0 && s.gravityMod2 == 1 && s.trig.test(0) && !s.trig.test(1),
          "P1 spends only its own touch effect latch");
    k.t = 8; dp::playerTriggerTick(s, k, dp::PlayerPhase::TouchP2, 300);
    check(s.gravityMod2 == 0 && s.fireB[0] == 5 && s.fireB[1] == 8,
          "P2 can fire the same effect later with its own source date");
    dp::resetInvocationState();
    l.playerSources[42].singleTouch = true;
    dp::loadPlayerTriggers(l, "", "");
    check(dp::g_playerRoots.size() == 1,
          "single-player-touch deliberately shares one source between both bodies");
    s = {}; s.xAbs = s.xAbs2 = 100; s.y = s.y2 = 300; s.dual = 1;
    dp::playerTriggerTick(s, k, dp::PlayerPhase::TouchP1, 300);
    dp::playerTriggerTick(s, k, dp::PlayerPhase::TouchP2, 300);
    check(s.gravityMod == 0 && s.gravityMod2 == 1,
          "a shared touch latch prevents the second player's independent re-fire");
    dp::resetInvocationState();
}

// Menu diagnostics must match supported teleports without hiding real restrictions.
void levelWarnings() {
    const auto supported = levelwarn::scan("header;"
        "1,747,2,100,345,1,353,1;"
        "1,2902,2,100,51,9,352,0,353,1,345,1,347,1,351,1;"
        "1,1,2,900,57,9;1,3022,2,200;1,2066,2,300;", false);
    check(supported.unmodelled == 0 && levelwarn::describe(supported.unmodelled) == "",
          "supported sideways, height-preserving and force teleports emit no red warning");
    check(supported.newer && supported.newerCount.at(3022) == 1
          && supported.newerCount.at(2066) == 1,
          "newer-mechanic diagnostics remain separate from unsupported warnings");
    for (int id : {2902, 3022}) {
        const auto random = levelwarn::scan("header;1," + std::to_string(id)
            + ",51,9;1,1,57,9;1,1,57,9;", false);
        check(random.unmodelled == levelwarn::kTeleportSeveralExits,
              "random exit groups still warn for portals and teleport triggers");
    }
    for (int id : {747, 2902, 3022}) {
        const auto dash = levelwarn::scan("header;1," + std::to_string(id) + ",591,1;", false);
        check(dash.unmodelled == levelwarn::kTeleportDash
              && levelwarn::describe(dash.unmodelled) == "teleports that redirect a dash",
              "dash redirection retains a specific unsupported warning");
    }
    const auto combined = levelwarn::scan("header;1,2902,51,9,591,1;"
        "1,1,57,9;1,1,57,9;1,3027;", false);
    check(combined.unmodelled == (levelwarn::kTeleportSeveralExits | levelwarn::kTeleportOrb
          | levelwarn::kTeleportDash)
          && levelwarn::describe(combined.unmodelled)
              == "teleports with several exits, teleport orbs and 1 more",
          "remaining unsupported teleport features produce the correct combined menu text");
    check(levelwarn::scan("header;1,3641;1,3619,476,5;", true).unmodelled
          == (levelwarn::kAttemptDependent | levelwarn::kPlatformer),
          "attempt-dependent and platformer warnings are unaffected");
    std::string crowded = "header;";
    for (int n = 0; n < 129; ++n)
        crowded += "1,10,2," + std::to_string(n * 1000) + ";";
    check(levelwarn::scan(crowded, false).unmodelled == 0
          && levelwarn::scan(crowded + "1,2902;", false).unmodelled == levelwarn::kGravityPortals,
          "spatial portals retain the real gravity-latch overflow warning, not a sideways warning");
    check(levelwarn::describeAll(levelwarn::kSidewaysTeleport | levelwarn::kTeleportPush)
          == "teleports that move you sideways; teleports that push you",
          "legacy warning bit numbers and descriptions remain readable");
}

// dptest --groups-corpus <file>...: the same comparison on real recordings.
static int groupsCorpus(int argc, char** argv) {
    int bad = 0;
    for (int i = 2; i < argc; ++i) {
        long long rows = 0;
        const std::string d = groupsReadersDiff(argv[i], &rows);
        std::printf("%s: %lld rows %s%s\n", d.empty() ? "ok" : "FAIL", rows, argv[i],
                    d.empty() ? "" : (" -- " + d).c_str());
        bad |= !d.empty();
    }
    return bad;
}

int main(int argc, char** argv) {
    if (argc > 2 && !std::strcmp(argv[1], "--groups-corpus")) return groupsCorpus(argc, argv);
    groupsParser();
    commaFields();
    overlayReach();
    initLines();
    noCollideColumn();
    onOffEvents();
    activatorRoots();
    autoSwitches();
    keyHoldsDelayedSwitch();
    teleportTargets();
    teleportForces();
    teleportColumns();
    independentWindows();
    teleportDualStep();
    playerEffectRoles();
    playerEffectColumns();
    playerTriggerClocks();
    playerSpawnGraph();
    gravityMultiplierPhysics();
    playerAnchorParsing();
    playerTouchLatches();
    levelWarnings();
    std::printf(g_fail ? "FAILED\n" : "all ok\n");
    return g_fail;
}
