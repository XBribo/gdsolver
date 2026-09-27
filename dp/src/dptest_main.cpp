// dptest -- the header core's rules that can be decided without a level.
//
// The suite (py/quick_regress.py) compares whole sections against a corpus, which is the right
// instrument for a change that moves the model -- and the wrong one for a boundary that the
// corpus happens not to contain. The recording overlay is such a boundary: the corpus'
// recordings predate the `end` trailer, so quick_regress cannot see the rule at all.
//
//   dptest            runs every case, prints one line each, exit 1 on the first failure
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "dp/cli.hpp"

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

    dp::g_activators = false;
    const auto off = dp::loadTouchTriggers(trig, grp, -1e18, &types);
    check(!boxOf(off, 101) && !boxOf(off, 103), "without --activators no pickup is a box");

    dp::g_activators = true;
    const auto on = dp::loadTouchTriggers(trig, grp, -1e18, &types);
    dp::g_activators = false;
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
    const dp::TouchTrig* tbOff = boxOf(off, 105);
    check(!tbOff || !tbOff->press, "without --activators a toggle block is never a press box");
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
    dp::g_activators = true;
    dp::buildTouchMoveTicks();
    check(dp::keyOf(a, 220) != dp::keyOf(b, 220),
          "while the later switch has not landed, the two states keep different keys");
    check(dp::keyOf(a, 400) == dp::keyOf(b, 400),
          "once both have landed they merge again");
    dp::g_activators = false;
    dp::buildTouchMoveTicks();
    check(dp::g_touchMoveTicks[0] == 0,
          "without --activators the switch does not hold the key (nothing reads it)");
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

int main() {
    commaFields();
    overlayReach();
    initLines();
    noCollideColumn();
    onOffEvents();
    activatorRoots();
    autoSwitches();
    keyHoldsDelayedSwitch();
    std::printf(g_fail ? "FAILED\n" : "all ok\n");
    return g_fail;
}
