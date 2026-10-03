#pragma once
// Solving a heavy level on a slice of itself (solver/slice.hpp), and verifying the plan on the
// level itself before anything is filed.
//
// THE FLOW. The session's first solve (dpsolve::start) asks maybeSlice first. When the cut
// removes at least `slicemin` objects, the level is swapped for a copy without them and the solve
// starts there, as on any level. The plan that clears the copy is not filed: the level itself is
// swapped back in and the plan is flown on it (onSliceCleared, then the next start). A clear
// there is the ordinary clear of a solve session -- filed and shown under the level's own id. A
// death there means the cut was wrong somewhere (onVerifyDeath): the objects dropped around the
// tick where the two runs parted are put back and the solve goes on on the new copy; after
// `sliceaddbacks` of those, or when the parting did not move on, it goes on on the level itself.
// A copy that is wrong only at a wall would never clear, so a wall the copy holds is checked on
// the level itself too (maybeCheckWall), and giving up on the copy moves the solve to the level
// itself (onGiveUp) rather than ending it.
//
// WHAT CARRIES ACROSS A SWAP. A swap is one run, not a new one -- choosing a level, solving it and
// playing the solution back are one frame, so a plan and what the run has learnt are not thrown
// away because some objects came back. The plan, the fixups (keyed by the player's state, not by
// any object), the phantom vetoes and the round count carry over. What is keyed by an object's
// uid does not -- the recordings of the moving geometry, the anchor rows, the pads and rings an
// attempt spent -- because the copy numbers its objects differently; those are recorded again on
// the new level (dpsolve::start with a carried plan).
//
// The copy is an editor-type level with the original's id, song and version. The game turns a
// modified main level away as soon as it starts, and an editor-type copy of one plays the same
// (the player's state on every tick of known solutions of the four heaviest official levels).
#include "mod/level_entry.hpp"
#include "solver/slice.hpp"

namespace p1 {
namespace levelslice {

// THE THRESHOLD, IN OBJECTS REMOVED, is where the copy pays for itself.
//
// What an object costs, per round of the loop: measured on a heavy custom level solved twice from
// the same build, once on the level (109,663 objects) and once on its cut (4,827), whose 44 rounds
// made identical decisions ([fp] equal line for line) -- so the two runs did the same work and
// only its price differs. The 39 solver calls took 175 s against 75 s (the level the model reads
// is built from every object), and the 221,738 attempt ticks 17.1 s against 10.2 s. Per object
// removed that is about 24 us a round. (The section search's restores, which also follow the
// object count, come on top and are not counted here.)
//
// What the copy costs, once: a second load of the level and one flight of the plan on it, plus the
// cut itself (tens of ms, 162 ms on the level above). On level 22 (18,329 objects) everything
// after the last solve returned -- the copy's clearing flight, the swap back, the flight on the
// level and the session's end -- took under 10 s.
//
// So a cut of 10,000 objects saves about 0.24 s a round and has paid for itself within some twenty
// rounds. A solve shorter than that is a short solve, and the most it loses is those few seconds.
inline constexpr int kSliceMinDefault = 10000;
// Half the width of the stretch of level put back around where the plan and the level parted.
inline constexpr double kAddBackHalfWidth = 1500.0;
// Ticks after the plan's first press within which a parting from the copy says the two runs did
// not start alike rather than anything about the cut (onVerifyDeath). Measured on a level that
// starts as a ship: a press at t=0 first shows in the attempt's record at t=2.
inline constexpr long long kSyncTicks = 2;

inline GJGameLevel* g_orig = nullptr;   // the level the session was started on (retained)
inline std::string g_raw;                // its level string, decompressed
inline slice::Cut g_cut;                 // what the rule keeps (views into g_raw)
inline std::vector<std::pair<double, double>> g_keepX;   // stretches put back
inline int g_addBacks = 0;
inline double g_lastPartX = -1e18;       // where the last verification parted from the copy
inline std::vector<InputCmd> g_carry;    // the plan the next level flies first
inline bool g_carryPending = false;
// ...and flown WITHOUT touching the loop: a wall check's flight on the level, and the return to
// the same copy after one. The copy is rebuilt from the same string, so its objects get the same
// uids and every recording, anchor and plan the loop holds is still the copy's.
inline bool g_lightPending = false;
inline std::vector<AnchorRow> g_clearRows;   // the copy's attempt the level is compared with
inline long long g_refEnd = -1;          // ...the tick it ended on
inline bool g_refCleared = false;        // ...and whether that end was a clear or a death
inline long long g_checkedWall = -1;     // the copy's wall last checked on the level itself
inline long long g_ownWall = -1;         // ...when that check found it the level's own
inline long long g_giveUpCopyAt = -1;    // the wall a give-up last went on on a fresh copy from
inline int g_wallChecks = 0;
inline bool g_swapInit = false;          // the PlayLayer being built is one of our swaps
inline size_t g_keptNow = 0;             // objects in the level being played
inline int g_verifies = 0;               // plans flown on the level itself
inline std::chrono::steady_clock::time_point g_t0;
// The uids (slice.hpp "HOLDING THE UIDS"): each string object's uid in the level itself, and for
// the copy being played, which of the level's string objects each of its objects is. The copy is
// checked against the level once it has loaded (checkCopyUids); g_uidsHeld is that verdict.
inline std::vector<int> g_levelUid;
inline std::vector<std::vector<int>> g_levelExtra;
inline std::vector<int> g_copyOrd, g_copyIds;
inline size_t g_placeholders = 0;
inline bool g_uidCheckPending = false;
inline bool g_uidsHeld = false;
// cfg slicefirstref (onVerifyDeath / onRefFlightEnd): the wall check's plan and the level's flight
// of it, kept while the same plan is flown on a fresh copy as that copy's first attempt.
inline std::vector<InputCmd> g_verifyPlan;
inline std::vector<AnchorRow> g_levelRows;
inline long long g_levelDeath = -1;
inline double g_levelDeathX = 0.0;
inline long long g_firstParting = -1;
inline int g_refFlights = 0;

inline int sliceMin() { return g_cfg.sliceMin >= 0 ? g_cfg.sliceMin : kSliceMinDefault; }

inline void holdOrig(GJGameLevel* l) {
    if (l) l->retain();
    if (g_orig) g_orig->release();
    g_orig = l;
}

inline void reset() {
    g_phase = Off;
    holdOrig(nullptr);
    g_cut = slice::Cut{};   // before the string its views point into
    g_raw.clear();
    g_keepX.clear();
    g_addBacks = 0;
    g_lastPartX = -1e18;
    g_carry.clear();
    g_carryPending = false;
    g_lightPending = false;
    g_clearRows.clear();
    g_refEnd = -1;
    g_refCleared = false;
    g_checkedWall = -1;
    g_ownWall = -1;
    g_giveUpCopyAt = -1;
    g_wallChecks = 0;
    g_swapInit = false;
    g_keptNow = 0;
    g_verifies = 0;
    g_levelUid.clear();
    g_levelExtra.clear();
    g_copyOrd.clear();
    g_copyIds.clear();
    g_placeholders = 0;
    g_uidCheckPending = false;
    g_uidsHeld = false;
    g_verifyPlan.clear();
    g_levelRows.clear();
    g_levelDeath = -1;
    g_levelDeathX = 0.0;
    g_firstParting = -1;
    g_refFlights = 0;
}

// The level string the layer was built from. The main levels' strings begin with whitespace
// before the data (hooks_playmenu.cpp readLevel), and a stored level is compressed.
inline std::string levelString(GJGameLevel* level) {
    if (!level) return {};
    std::string s(level->m_levelString);
    const size_t b = s.find_first_not_of(" \t\r\n");
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
    if (s.rfind("H4sI", 0) == 0) s = std::string(cocos2d::ZipUtils::decompressString(s, false, 0));
    return s;
}

inline std::vector<slice::LiveObj> liveObjects(GJBaseGameLayer* l) {
    std::vector<slice::LiveObj> out;
    if (!l || !l->m_objects) return out;
    out.reserve(l->m_objects->count());
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!o) continue;
        slice::LiveObj r;
        r.id = o->m_objectID;
        r.type = (int)o->m_objectType;
        if (o->m_groupCount > 0 && o->m_groups)
            for (int i = 0; i < std::min((int)o->m_groupCount, 10); ++i)
                if ((*o->m_groups)[i] > 0) r.groups.push_back((int)(*o->m_groups)[i]);
        if (auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(o)) {
            r.effect = true;
            r.target = e->m_targetGroupID;
            r.center = e->m_centerGroupID;
            r.tmodCenter = e->m_targetModCenterID;
            r.moveTarget = e->m_useMoveTarget;
            r.dynamic = e->m_isDynamicMode;
        }
        out.push_back(std::move(r));
    }
    return out;
}

// The built objects as (uid, id), in uid order.
inline std::vector<std::pair<int, int>> byUid(GJBaseGameLayer* l) {
    std::vector<std::pair<int, int>> out;
    if (!l || !l->m_objects) return out;
    out.reserve(l->m_objects->count());
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects))
        if (o) out.emplace_back(o->m_uniqueID, o->m_objectID);
    std::sort(out.begin(), out.end());
    return out;
}

// The level's own manifest, taken before the first copy is built: each string object's uid and
// the uids of what the game built right behind it (slice_uids_level_lv<id>.tsv).
inline void noteLevelUids(GJBaseGameLayer* l) {
    const std::vector<int> ids = slice::stringIds(g_raw);
    slice::OrdinalUids o = slice::ordinalUids(ids, byUid(l));
    g_levelUid = std::move(o.uid);
    g_levelExtra = std::move(o.extra);
    std::ofstream f(std::string(DATA_DIR) + "/slice_uids_level_lv" + std::to_string(g_cfg.levelId)
                    + ".tsv", std::ios::trunc);
    f << "ordinal\tid\tuid\tbehind\n";
    for (size_t k = 0; k < ids.size(); ++k) {
        f << k << '\t' << ids[k] << '\t' << g_levelUid[k] << '\t';
        for (size_t e = 0; e < g_levelExtra[k].size(); ++e) f << (e ? " " : "") << g_levelExtra[k][e];
        f << '\n';
    }
    if (o.matched != ids.size())
        writeResult("slice: uids - only " + std::to_string(o.matched) + " of the level's "
                    + std::to_string(ids.size()) + " string objects were found among its objects "
                      "in uid order");
}

// The copy's string from the cut and what has been put back, noting which of the level's string
// objects each of its objects is, for the check once it has loaded.
inline std::string copyString(size_t* kept) {
    size_t ph = 0;
    std::string s = slice::build(g_cut, g_keepX, kept, g_cfg.sliceUids, &ph, &g_copyOrd);
    g_copyIds = slice::stringIds(s);
    g_placeholders = ph;
    g_uidCheckPending = true;
    return s;
}

// Once a copy has loaded: does each of its objects have the uid the same string object has in the
// level itself, and does what the game built behind it too (slice_uids_copy_lv<id>.tsv)? The last
// object's tail is left out of the verdict: it also holds what the game makes after the level
// (the anti-cheat spike, the collision block) and what a run makes (checkpoints).
inline void checkCopyUids(GJBaseGameLayer* l) {
    g_uidCheckPending = false;
    const slice::OrdinalUids o = slice::ordinalUids(g_copyIds, byUid(l));
    size_t same = 0, differ = 0, behindDiffer = 0;
    long long firstBad = -1;
    std::ofstream f(std::string(DATA_DIR) + "/slice_uids_copy_lv" + std::to_string(g_cfg.levelId)
                    + ".tsv", std::ios::trunc);
    f << "index\tordinal\tid\tuid\tlevel_uid\tbehind\tlevel_behind\tok\n";
    for (size_t k = 0; k < g_copyIds.size() && k < g_copyOrd.size(); ++k) {
        const int ord = g_copyOrd[k];
        const bool known = ord >= 0 && (size_t)ord < g_levelUid.size();
        const int want = known ? g_levelUid[(size_t)ord] : -1;
        static const std::vector<int> kNone;
        const std::vector<int>& wantBehind = known ? g_levelExtra[(size_t)ord] : kNone;
        const bool last = k + 1 == g_copyIds.size();
        const bool uidOk = known && o.uid[k] == want;
        const bool behindOk = last || o.extra[k] == wantBehind;
        if (uidOk) ++same; else ++differ;
        if (!behindOk) ++behindDiffer;
        if ((!uidOk || !behindOk) && firstBad < 0) firstBad = (long long)k;
        f << k << '\t' << ord << '\t' << g_copyIds[k] << '\t' << o.uid[k] << '\t' << want << '\t';
        for (size_t e = 0; e < o.extra[k].size(); ++e) f << (e ? " " : "") << o.extra[k][e];
        f << '\t';
        for (size_t e = 0; e < wantBehind.size(); ++e) f << (e ? " " : "") << wantBehind[e];
        f << '\t' << ((uidOk && behindOk) ? 1 : 0) << '\n';
    }
    g_uidsHeld = differ == 0 && behindDiffer == 0 && o.matched == g_copyIds.size()
                 && g_copyIds.size() == g_copyOrd.size();
    char b[360];
    snprintf(b, sizeof(b), "slice: uids - the copy's %zu objects (%zu placeholders): %zu have the "
             "level's uid, %zu do not, %zu differ in what the game built behind them; %s",
             g_copyIds.size(), g_placeholders, same, differ, behindDiffer,
             g_uidsHeld ? "the copy numbers its objects as the level does"
                        : ("NOT the level's numbering (first at copy object "
                           + std::to_string(firstBad) + ") - not for fidelity").c_str());
    writeResult(b);
}

inline GJGameLevel* makeCopy(const std::string& raw) {
    auto* c = GJGameLevel::create();
    c->m_levelName = g_orig->m_levelName;
    c->m_levelID = g_orig->m_levelID.value();
    c->m_levelType = GJLevelType::Editor;
    c->m_audioTrack = g_orig->m_audioTrack;
    c->m_songID = g_orig->m_songID;
    c->m_songIDs = g_orig->m_songIDs;
    c->m_sfxIDs = g_orig->m_sfxIDs;
    c->m_gameVersion = g_orig->m_gameVersion;
    c->m_levelVersion = g_orig->m_levelVersion;
    c->m_twoPlayerMode = g_orig->m_twoPlayerMode;
    // GD's own compression: doing base64+gzip by hand fails to load (level_entry.hpp).
    c->m_levelString = cocos2d::ZipUtils::compressString(raw, false, 0);
    return c;
}

// What the outgoing layer leaves behind that points into it. The session goes on, so this is
// the level-specific part of resetSessionState and nothing else.
inline void forgetLayer() {
    speedgate::reset();
    orbtrace::reset();
    padtrace::reset();
    touchseed::reset();
    portalseed::reset();
    padseed::reset();
    ringseed::reset();
    grouptrace::reset();     // g_on stays: the new level records as this one did
    areaenv::clear();        // keyed by GameObject*, which the new layer reallocates
    cpflight::reset();       // checkpoints of a layer that is going (cfg cpflight)
    secsolve::reset();
    hitbox::clearHidden();
    g_ckpt = nullptr; g_ckptTick = -1; g_headHeld = 0;
    solver::g_levelMaxX = 0;
    solver::g_prevTickX = -1e9f;
    solver::g_pois.clear(); solver::g_poisBuilt = false;
    solver::g_coins.clear();
    solver::g_coinPickupTick.clear();
    solver::g_coinGdTick.clear();
    solver::g_coinGdUnmatched = 0;
    solver::g_coinMissFired = false;
    solver::g_coinMissIdx = -1;
    solver::g_endTriggers = 0;
    solver::g_endTriggerFiredTick = -1;
    solver::g_hasRotGameplay = false;
    solver::g_itemCounts.clear();
    solver::g_coinGates.clear();
    solver::g_coinNoMiss.clear();
}

// Replace the level being played, once nothing else is replacing the scene. Never where the
// callers stand -- inside the game's update or its completion path, where replacing the scene
// deletes the layer they are in (level_entry.hpp swapLevelNow) -- and never inside a scene
// transition: a level entered with a fade (enterConfiguredLevel) is built and starts its first
// solve while the fade still runs, and the fade's end puts ITS scene back on top of one replaced
// in the meantime. Measured on the first cut of this (a heavy custom level, entered by the cold
// harness): the copy was built and its solve started, then the original went on running under a
// PlayLayer::get() that named the copy, and every hook took it for a layer on its way out. The
// suite waits for the same thing (SuiteKeeper). The level is held still until the new one takes
// over. The wait is capped in time, not frames: cfg fps sets the frame rate, and at fps=1000 a
// cap of 1,200 frames would give up after 1.2 s, well inside an ordinary fade.
inline constexpr std::chrono::seconds kSwapWait{20};

// Why the scene cannot be replaced yet, or nullptr when it can. A running transition is not the
// only way to be early: the play menu's level page builds the PlayLayer in playStep3 and starts the
// fade to it only in playStep4, frames later (LevelInfoLayer 0x2fd190 / 0x2fd310 in 2.2081, the
// page of an online level). The session's first solve runs in between,
// while the running scene is still the level page and no transition is in sight, and a swap there
// put the copy on screen only for the fade to replace it with the level itself: the original ran
// undriven, PlayLayer::get() named a copy that had no scene, the loop stood still, and Escape
// crashed in pauseGame, which adds the pause menu to that copy's missing parent (reported on the
// play menu, 2026-09-27, on two heavy online levels). So the layer the session is on has to be the
// one on screen, with no scene change queued behind it.
inline const char* sceneBusy() {
    auto* dir = cocos2d::CCDirector::sharedDirector();
    auto* running = dir->getRunningScene();
    if (!running) return "no running scene";
    if (geode::cast::typeinfo_cast<cocos2d::CCTransitionScene*>(running)) return "a transition runs";
    if (dir->getNextScene()) return "a scene change is queued";
    auto* pl = PlayLayer::get();
    if (!pl || pl->getParent() != running) return "the level is not on screen yet";
    return nullptr;
}

inline void swapWhenClear(geode::Ref<GJGameLevel> keep, int frames, std::string waitedFor = {},
                          std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now()) {
    geode::Loader::get()->queueInMainThread([keep, frames, waitedFor, since] {
        if (!g_started || g_sessionOver) return;   // the session ended in the meantime
        const char* busy = sceneBusy();
        std::string seen = waitedFor;
        if (busy && seen.find(busy) == std::string::npos) seen += std::string(seen.empty() ? "" : ", ") + busy;
        if (busy && std::chrono::steady_clock::now() - since < kSwapWait) {
            swapWhenClear(keep, frames + 1, seen, since);
            return;
        }
        writeResult(busy ? "slice: the scene was still changing after " + std::to_string(frames)
                               + " frames (" + busy + ") - swapping anyway"
                         : "slice: swapping the level in (waited " + std::to_string(frames)
                               + " frame(s) for the scene to settle"
                               + (seen.empty() ? "" : ": " + seen) + ")");
        g_forceCleanStart = true;
        g_swapInit = true;
        cocos2d::CCDirector::sharedDirector()->replaceScene(PlayLayer::scene(keep.data(), false, false));
    });
}

// The census (cfg slicecount) ends the session the same way, once the scene has settled: leaving
// a level inside its entry fade leaves the layer up, and a suite waiting for it gives up.
inline void endWhenClear(std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now()) {
    geode::Loader::get()->queueInMainThread([since] {
        if (!g_started || g_sessionOver) return;
        if (sceneBusy() && std::chrono::steady_clock::now() - since < kSwapWait) {
            endWhenClear(since);
            return;
        }
        endSession("slice_count");
    });
}

inline void swapTo(GJGameLevel* level, size_t objects) {
    forgetLayer();
    g_paused = true;
    g_keptNow = objects;
    swapWhenClear(geode::Ref<GJGameLevel>(level), 0);
}

inline std::string whyLine(const slice::Cut& c) {
    std::string s;
    for (const auto& [k, v] : c.why) s += " " + k + "=" + std::to_string(v);
    return s;
}

// Asked by the session's first solve. True = the level is being swapped for its slice, and the
// solve starts there instead (dpsolve::start runs again on the new layer).
inline bool maybeSlice(GJBaseGameLayer* l) {
    // On a copy this is also where it is first seen loaded: its uids are checked against the level.
    if (g_phase != Off) {
        if (g_uidCheckPending && (g_phase == Sliced || g_phase == RefFlight) && l == PlayLayer::get())
            checkCopyUids(l);
        return false;
    }
    if (!g_cfg.slice || !g_cfg.dpSolve) return false;
    auto* pl = PlayLayer::get();
    if (!pl || pl != l || !pl->m_level) return false;
    g_t0 = std::chrono::steady_clock::now();
    std::string raw = levelString(pl->m_level);
    if (raw.empty()) {
        writeResult("slice: not used - the level string could not be read");
        return false;
    }
    g_raw = std::move(raw);
    const std::vector<slice::LiveObj> live = liveObjects(l);
    g_cut = slice::cut(g_raw, live, slice::Options{!g_cfg.sliceNoPosition, g_cfg.sliceNoTouch,
                                                   g_cfg.sliceTriggers});
    if (g_cfg.sliceNoPosition)
        writeResult("slice: cfg slicenoposition - decorations read as positions are dropped too "
                    "(a cut that is wrong on purpose)");
    if (!g_cut.ok) {
        writeResult("slice: not used - " + g_cut.refused);
        g_cut = slice::Cut{};
        g_raw.clear();
        return false;
    }
    char b[320];
    snprintf(b, sizeof(b), "slice: %zu objects, %zu the run cannot depend on (threshold %d); "
             "%zu positional / %zu relevant groups;", g_cut.objects, g_cut.dropped, sliceMin(),
             g_cut.positionalGroups, g_cut.relevantGroups);
    writeResult(b + whyLine(g_cut));
    // cfg `slicecount=1`: the census of what the rule would remove, one line per level, and no
    // solve (a suite steps straight on to its next level).
    if (g_cfg.sliceCount) {
        writeResult("slice: count only (cfg slicecount) - nothing is solved");
        g_cut = slice::Cut{};
        g_raw.clear();
        g_paused = true;
        endWhenClear();
        return true;
    }
    if ((long long)g_cut.dropped < (long long)sliceMin()) {
        writeResult("slice: not used - below the threshold; solving on the level itself");
        g_cut = slice::Cut{};
        g_raw.clear();
        return false;
    }
    // A run that has to take the level's coins is not solved on a copy that may not credit them.
    // The copy is an editor-type level, and the game credits no SECRET coin (id 142) in one.
    // Measured on level 22 with coins on: the copy was cleared 103 times, each with 0 of 3 coins by
    // the game's own count (coingd:), where the level itself credits all three -- and the game
    // builds no secret coin at all in an editor-type level (the six other official levels that
    // have three, 2026-09-22). Whether a USER coin (1329) is credited in the copy has not been
    // measured, so it is treated the same way until it is.
    const long long coins = std::count_if(live.begin(), live.end(), [](const slice::LiveObj& o) {
        return o.id == 142 || o.id == 1329;
    });
    if (g_cfg.coinRoute && coins > 0) {
        writeResult("slice: not used - this run takes the level's " + std::to_string(coins)
                    + " coin(s), which the editor-type copy is not known to credit (a secret coin "
                      "is not); solving on the level itself");
        g_cut = slice::Cut{};
        g_raw.clear();
        return false;
    }
    holdOrig(pl->m_level);
    noteLevelUids(l);
    size_t kept = 0;
    const std::string cutStr = copyString(&kept);
    {
        std::ofstream f(std::string(DATA_DIR) + "/slice_lv" + std::to_string(g_cfg.levelId)
                            + ".lvl",
                        std::ios::binary | std::ios::trunc);
        f << cutStr;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_t0).count();
    snprintf(b, sizeof(b), "slice: solving on a copy with %zu of %zu objects (%.1f%%), cut in "
             "%lld ms - its plan is verified on the level itself before it is filed", kept,
             g_cut.objects, 100.0 * (double)kept / (double)g_cut.objects, (long long)ms);
    writeResult(b);
    writeResult(g_cfg.sliceUids
                    ? "slice: the " + std::to_string(g_placeholders) + " objects left out are each "
                      "replaced by a placeholder that takes their uids (slice.hpp)"
                    : std::string("slice: cfg sliceuids=0 - the objects left out are removed, which "
                                  "renumbers every object after them (not for fidelity)"));
    g_phase = Sliced;
    swapTo(makeCopy(cutStr), kept + g_placeholders);
    return true;
}

// Asked by dpsolve::start on each new layer: the plan to fly first, if one was handed over.
inline bool takeCarry(std::vector<InputCmd>& out) {
    if (!g_carryPending) return false;
    g_carryPending = false;
    out = std::move(g_carry);
    g_carry.clear();
    return true;
}

// ...and whether it is to be flown without touching the loop (g_lightPending).
inline bool takeLight(std::vector<InputCmd>& out) {
    if (!g_lightPending) return false;
    g_lightPending = false;
    out = std::move(g_carry);
    g_carry.clear();
    return true;
}

// The copy was cleared (levelComplete, past its false-clear and coin gates). True = taken over:
// the plan goes to the level itself instead of being filed.
inline bool onRefFlightEnd(long long end, double endX, bool cleared);
inline bool onSliceCleared(const std::vector<InputCmd>& plan, long long tick) {
    // cfg slicefirstref: a first-attempt flight on a copy that clears is never a clear to file.
    if (g_phase == RefFlight) {
        double x = 0.0;
        for (auto it = anchors::g_live.rbegin(); it != anchors::g_live.rend(); ++it)
            if (it->valid) { x = it->x; break; }
        onRefFlightEnd(tick, x, true);
        return true;
    }
    if (g_phase != Sliced || !g_orig) return false;
    g_clearRows = anchors::g_live;
    g_refEnd = tick;
    g_refCleared = true;
    g_carry = plan;
    g_carryPending = true;
    g_phase = Verifying;
    ++g_verifies;
    char b[256];
    snprintf(b, sizeof(b), "slice: the copy cleared at t=%lld after %d rounds - flying the plan "
             "on the level itself (%zu inputs)", tick, dpsolve::g_iter, plan.size());
    writeResult(b);
    g_hudPhase = "verifying the plan on the level itself";
    swapTo(g_orig, g_cut.objects);
    return true;
}

// The level itself was cleared.
inline void onLevelCleared(long long tick) {
    if (g_phase == Off) return;
    char b[200];
    snprintf(b, sizeof(b), "slice: cleared on the level itself at t=%lld (%s, %d add-back%s)",
             tick, g_phase == Verifying ? "the copy's plan held" : "after solving on it",
             g_addBacks, g_addBacks == 1 ? "" : "s");
    writeResult(b);
}

// Where the attempt on the level itself first parted from the copy's clearing attempt: the first
// tick whose player state differs. -1 when none does before `until`.
inline long long partingTick(const std::vector<AnchorRow>& a, const std::vector<AnchorRow>& b,
                             long long until, const char** field) {
    const long long n = std::min<long long>({(long long)a.size(), (long long)b.size(), until});
    for (long long t = 0; t < n; ++t) {
        const AnchorRow& p = a[(size_t)t];
        const AnchorRow& q = b[(size_t)t];
        if (!p.valid || !q.valid) continue;
        // Not the flight band: the game carries it from one attempt to the next, so an attempt on
        // a layer just swapped in starts with another band than the copy's hundredth attempt did,
        // whatever the cut. Compared, it parted every check at tick 1 (a heavy custom level whose
        // copy's plan cleared the level itself). Where the band matters it moves the player.
        const char* f = nullptr;
        if (p.x != q.x) f = "x";
        else if (p.y != q.y) f = "y";
        else if (p.vy != q.vy) f = "vy";
        else if (p.mode != q.mode) f = "mode";
        if (f) { *field = f; return t; }
    }
    return -1;
}

inline size_t buildCopy(GJGameLevel** out) {
    size_t kept = 0;
    *out = makeCopy(copyString(&kept));
    return kept + g_placeholders;
}

// After a verification has read a death as the cut's: the objects dropped around partX go back and
// the solve goes on on that copy -- or, with the add-backs spent or the parting not moving on, on
// the level itself.
inline bool addBackOrWhole(double partX) {
    char b[360];
    if (g_addBacks < g_cfg.sliceAddBacks && partX > g_lastPartX) {
        g_lastPartX = partX;
        ++g_addBacks;
        g_keepX.push_back({partX - kAddBackHalfWidth, partX + kAddBackHalfWidth});
        size_t kept = 0;
        const std::string cutStr = copyString(&kept);
        snprintf(b, sizeof(b), "slice: add-back %d - the objects dropped within %.0f px of x=%.0f "
                 "go back; solving on a copy with %zu of %zu objects, from the same plan",
                 g_addBacks, kAddBackHalfWidth, partX, kept, g_cut.objects);
        writeResult(b);
        g_carry = dpsolve::g_plan;
        g_carryPending = true;
        g_phase = Sliced;
        g_hudPhase = "solving on the copy with objects put back";
        swapTo(makeCopy(cutStr), kept + g_placeholders);
        return true;
    }
    writeResult(std::string("slice: solving on the level itself from here, with the plan and "
                            "what the run has learnt (")
                + (g_addBacks >= g_cfg.sliceAddBacks ? "add-backs spent" : "the parting did not "
                                                                             "move on")
                + ")");
    g_phase = Whole;
    g_keptNow = g_cut.objects;
    if (g_refCleared) return false;   // the loop is already the level's (a full carry)
    // A wall check's flight left the loop as the copy had it, keyed by the copy's uids: the level
    // is loaded again for a full carry.
    g_carry = dpsolve::g_plan;
    g_carryPending = true;
    swapTo(g_orig, g_cut.objects);
    return true;
}

// cfg slicefirstref: the end of the plan's flight on a fresh copy (onVerifyDeath started it), the
// copy's first attempt of the plan, compared with the level's flight of it -- also a first attempt.
// A wall check compares the level with the copy's DEEPEST attempt, which can be its hundredth, and
// what the game carries from one attempt to the next (the flight band) can move the player there:
// on a heavy custom level a check parted from the copy at t=10,460 and put objects back, while the
// same plan flown as a first attempt on the level and on the copy matched on every column for
// 25,588 ticks. So when the two first attempts agree and die on the same tick, the wall is the
// level's own and nothing goes back; when they part, the parting is the cut's. True = taken over;
// false = this death is the loop's round on the copy, as after any return to it.
inline bool onRefFlightEnd(long long end, double endX, bool cleared) {
    const char* field = "";
    const long long upTo = std::min(g_levelDeath, end);
    const long long pt = partingTick(anchors::g_live, g_levelRows, upTo, &field);
    char b[360];
    g_phase = Sliced;
    g_hudPhase = "solving on the copy";
    if (pt < 0 && !cleared && end == g_levelDeath) {
        snprintf(b, sizeof(b), "slice: first attempts agree - the copy's first attempt of the plan "
                 "dies at t=%lld as the level's does, the same way; the check's %s the copy's "
                 "deepest attempt, not the cut. The wall is the level's own; nothing is put back",
                 end, g_firstParting >= 0 ? "parting was" : "difference was");
        writeResult(b);
        g_levelRows.clear();
        g_checkedWall = end;
        g_ownWall = end;
        return false;
    }
    double partX;
    if (pt >= 0) {
        partX = (size_t)pt < g_levelRows.size() ? g_levelRows[(size_t)pt].x : endX;
        snprintf(b, sizeof(b), "slice: first attempts part at t=%lld x=%.1f (%s) - the level's "
                 "(died t=%lld) and the copy's (%s t=%lld): the parting is the cut's", pt, partX,
                 field, g_levelDeath, cleared ? "cleared" : "died", end);
    } else if (cleared || end > g_levelDeath) {
        partX = g_levelDeathX;
        snprintf(b, sizeof(b), "slice: the copy's first attempt %s t=%lld, past the level's death "
                 "at t=%lld x=%.0f, the same way until then: that death is the cut's",
                 cleared ? "cleared at" : "died at", end, g_levelDeath, partX);
    } else {
        partX = endX;
        snprintf(b, sizeof(b), "slice: the copy's first attempt died at t=%lld x=%.0f, before the "
                 "level's (t=%lld), the same way until then: the level went past the copy's wall",
                 end, partX, g_levelDeath);
    }
    writeResult(b);
    g_levelRows.clear();
    return addBackOrWhole(partX);
}

// A death while a plan from the copy is flown on the level itself -- the plan that cleared the
// copy, or the copy's deepest plan when its wall is being checked (maybeCheckWall). True = taken
// over (the solve goes back to a copy: with the objects around the parting put back, or as it was
// when the wall is the level's own); false = the loop takes this death on the level itself, as
// any other.
inline bool onVerifyDeath(long long dt, float deathX) {
    if (g_phase == RefFlight) return onRefFlightEnd(dt, deathX, false);
    if (g_phase != Verifying) return false;
    const char* field = "";
    const long long upTo = std::min(dt, g_refEnd >= 0 ? g_refEnd : dt);
    long long pt = partingTick(g_clearRows, anchors::g_live, upTo, &field);
    double partX = deathX;
    if (pt >= 0 && (size_t)pt < anchors::g_live.size()) partX = anchors::g_live[(size_t)pt].x;
    char b[360];
    // FAIL-SAFE: a parting is read as the cut's only when the two runs were in step. They are not
    // when the flight's inputs were taken before its first tick (the layer a swap replaced once
    // fed on them: 28c44ad), and they are not proven to be before the plan's first press has had
    // its effect -- a parting there says the runs did not start alike, which no object put back
    // around it can mend. Either way nothing is put back: a wall check goes back to the copy with
    // the wall marked checked, and a clear's check goes on on the level itself. Both sides around
    // the first press are logged, so a mismatch of this kind can be looked at.
    long long firstPress = -1;
    for (const auto& in : g_cfg.inputs)
        if (in.down) { firstPress = in.step; break; }
    const long long syncTick = std::max<long long>(firstPress, 0) + kSyncTicks;
    const bool tookEarly = g_nextInputAtStart != 0;
    const bool beforeSync = pt >= 0 && pt <= syncTick;
    {
        std::string s = "slice: sync check - the flight's first tick found "
                        + std::to_string(g_nextInputAtStart) + " input(s) already taken; the "
                        "plan's first press at t=" + std::to_string(firstPress) + "; copy|level";
        for (long long t = std::max<long long>(firstPress, 1); t <= syncTick + 1; ++t) {
            const bool ha = (size_t)t < g_clearRows.size() && g_clearRows[(size_t)t].valid;
            const bool hb = (size_t)t < anchors::g_live.size() && anchors::g_live[(size_t)t].valid;
            const AnchorRow& ra = ha ? g_clearRows[(size_t)t] : AnchorRow{};
            const AnchorRow& rb = hb ? anchors::g_live[(size_t)t] : AnchorRow{};
            char r[160];
            snprintf(r, sizeof(r), " t%lld %.3f/%.3f/%.3f/m%d|%.3f/%.3f/%.3f/m%d", t, ra.x, ra.y,
                     ra.vy, ra.mode, rb.x, rb.y, rb.vy, rb.mode);
            s += r;
        }
        writeResult(s);
    }
    if (tookEarly || beforeSync) {
        snprintf(b, sizeof(b), "slice: the flight on the level itself died at t=%lld x=%.0f, %s - "
                 "read as the two runs not being in step, not as the cut: nothing is put back",
                 dt, (double)deathX,
                 tookEarly ? "with inputs taken before its first tick"
                           : (std::string("parting from the copy at t=") + std::to_string(pt)
                              + " (" + field + "), before the plan's first press had had its "
                                "effect").c_str());
        writeResult(b);
        g_clearRows.clear();
        if (!g_refCleared) {
            // Back to the same copy, as when the wall is the level's own; g_checkedWall keeps
            // this wall from being checked again.
            g_carry = dpsolve::g_plan;
            g_lightPending = true;
            g_phase = Sliced;
            GJGameLevel* copy = nullptr;
            const size_t kept = buildCopy(&copy);
            swapTo(copy, kept);
            return true;
        }
        writeResult("slice: solving on the level itself from here, with the plan and what the run "
                    "has learnt (the check flight was not in step with the copy)");
        g_phase = Whole;
        g_keptNow = g_cut.objects;
        return false;   // the loop is already the level's (a full carry)
    }
    // cfg slicefirstref: before a wall check's death is read as the cut's, the plan is flown on a
    // fresh copy as its first attempt, and that is what the level's flight is compared with
    // (onRefFlightEnd). A check that finds the wall the level's own needs no second flight.
    const bool ownWall = pt < 0 && !g_refCleared && dt == g_refEnd;
    if (g_cfg.sliceFirstRef && !g_refCleared && !ownWall) {
        g_levelRows = anchors::g_live;
        g_levelDeath = dt;
        g_levelDeathX = deathX;
        g_firstParting = pt;
        ++g_refFlights;
        snprintf(b, sizeof(b), "slice: the check flight died at t=%lld x=%.0f, %s the copy's deepest "
                 "attempt (which is not a first attempt) - flying the plan on a fresh copy to "
                 "compare the two first attempts (cfg slicefirstref)", dt, (double)deathX,
                 pt >= 0 ? ("parting at t=" + std::to_string(pt) + " (" + field + ") from").c_str()
                         : "not ending like");
        writeResult(b);
        g_clearRows.clear();
        g_carry = g_verifyPlan;
        g_lightPending = true;
        g_phase = RefFlight;
        g_hudPhase = "flying the plan on a fresh copy";
        GJGameLevel* copy = nullptr;
        const size_t kept = buildCopy(&copy);
        swapTo(copy, kept);
        return true;
    }
    if (pt >= 0) {
        snprintf(b, sizeof(b), "slice: the plan died on the level itself at t=%lld x=%.0f; it "
                 "parted from the copy at t=%lld x=%.1f (%s)", dt, (double)deathX, pt, partX,
                 field);
    } else if (!g_refCleared && dt == g_refEnd) {
        // The copy's wall is the level's own: the same plan dies on the same tick, and the
        // player's state agreed with the copy's all the way there. Back to the same copy.
        snprintf(b, sizeof(b), "slice: the copy's wall at t=%lld is the level's own - the plan "
                 "dies there on the level too, the same way; back to the copy", dt);
        writeResult(b);
        g_clearRows.clear();
        g_checkedWall = dt;
        g_ownWall = dt;
        // The same copy, from the same string: the loop carries on as it was (g_lightPending).
        g_carry = dpsolve::g_plan;
        g_lightPending = true;
        g_phase = Sliced;
        GJGameLevel* copy = nullptr;
        const size_t kept = buildCopy(&copy);
        swapTo(copy, kept);
        return true;
    } else if (!g_refCleared && dt > g_refEnd) {
        // The level went past the copy's wall: that death was the cut's.
        pt = g_refEnd;
        if ((size_t)pt < g_clearRows.size() && g_clearRows[(size_t)pt].valid)
            partX = g_clearRows[(size_t)pt].x;
        snprintf(b, sizeof(b), "slice: the plan went on past the copy's wall on the level itself "
                 "(the copy died at t=%lld x=%.1f, the level at t=%lld x=%.0f)", g_refEnd, partX,
                 dt, (double)deathX);
    } else {
        snprintf(b, sizeof(b), "slice: the plan died on the level itself at t=%lld x=%.0f; the "
                 "player's state matched the copy's until then", dt, (double)deathX);
    }
    writeResult(b);
    g_clearRows.clear();
    return addBackOrWhole(partX);
}

// A wall the copy cannot get past may be the cut's rather than the level's, and a copy that is
// wrong only there would never clear to be verified. So after kWallCheckRounds rounds without
// getting deeper, the copy's deepest plan is flown on the level itself, once per wall, and
// compared with the copy's attempt of the same plan (onVerifyDeath): where the two part, the
// objects go back; if the level dies on the same tick the same way, the wall is real and the solve
// goes back to the copy. Called from the loop's death handler before the death is scored; the
// death that fires it is left unscored (the plan is flown again on the level).
inline constexpr int kWallCheckRounds = 8;
inline bool maybeCheckWall() {
    if (g_phase != Sliced || !g_orig) return false;
    if (dpsolve::g_stallRuns < kWallCheckRounds) return false;
    if (dpsolve::g_best.empty() || dpsolve::g_bestDeath < 0) return false;
    if (dpsolve::g_bestDeath == g_checkedWall) return false;
    if (anchors::g_deepest.empty()) return false;
    g_checkedWall = dpsolve::g_bestDeath;
    ++g_wallChecks;
    g_clearRows = anchors::g_deepest;
    g_refEnd = dpsolve::g_bestDeath;
    g_refCleared = false;
    g_carry = dpsolve::g_best;
    g_verifyPlan = dpsolve::g_best;   // cfg slicefirstref flies it again on a fresh copy
    g_lightPending = true;   // a flight, not a new run: the loop stays the copy's
    g_phase = Verifying;
    char b[256];
    snprintf(b, sizeof(b), "slice: %d rounds at the copy's wall t=%lld - flying its deepest plan "
             "on the level itself, to see whether the wall is the level's or the cut's",
             dpsolve::g_stallRuns, dpsolve::g_bestDeath);
    writeResult(b);
    g_hudPhase = "checking the copy's wall on the level itself";
    swapTo(g_orig, g_cut.objects);
    return true;
}

// The loop gave up on the copy. That is the copy's verdict, not the level's: the solve goes on on
// the level itself, from the copy's deepest plan, with what the run has learnt.
//
// cfg `slicegiveupcopy` (on by default since 2026-10): ...unless the wall it gave up at is one the level was flown
// to and shares -- the same plan dying on the same tick the same way (onVerifyDeath). The copy is
// right there, so the level would only be the same wall at the level's price, and what the level
// brought was the full carry: the recordings, anchors and seeds taken again. A fresh copy gets
// the same carry. On a heavy custom level (147,423 objects, 14,482 in the copy) the section
// search's step cost 858-2,059 us on the level against 91-183 us on the copy, and the searches
// after the move to the level took 91% of that run's search time. Once per wall: a second give-up
// at the same wall goes to the level as before.
inline bool onGiveUp(const char* reason) {
    if (g_phase != Sliced || !g_orig) return false;
    if (g_cfg.sliceGiveUpCopy && g_ownWall >= 0 && dpsolve::g_bestDeath == g_ownWall
        && g_giveUpCopyAt != g_ownWall) {
        g_giveUpCopyAt = g_ownWall;
        char b[256];
        snprintf(b, sizeof(b), "slice: the solve gave up on the copy (%s) at t=%lld, a wall the "
                 "level shares - going on on a fresh copy from the copy's deepest plan", reason,
                 g_ownWall);
        writeResult(b);
        g_carry = dpsolve::g_best.empty() ? dpsolve::g_plan : dpsolve::g_best;
        g_carryPending = true;
        g_phase = Sliced;
        g_hudPhase = "solving on the copy again";
        GJGameLevel* copy = nullptr;
        const size_t kept = buildCopy(&copy);
        swapTo(copy, kept);
        return true;
    }
    writeResult(std::string("slice: the solve gave up on the copy (") + reason
                + ") - going on on the level itself from the copy's deepest plan");
    g_carry = dpsolve::g_best.empty() ? dpsolve::g_plan : dpsolve::g_best;
    g_carryPending = true;
    g_phase = Whole;
    g_hudPhase = "solving on the level itself";
    swapTo(g_orig, g_cut.objects);
    return true;
}

inline std::string summary() {
    if (g_phase == Off) return {};
    static const char* names[] = {"off", "sliced", "verifying", "whole", "ref-flight"};
    char b[280];
    snprintf(b, sizeof(b), "slice: ended %s - %zu of %zu objects in play, %d verification%s and "
             "%d wall check%s on the level itself, %d add-back%s", names[g_phase], g_keptNow,
             g_cut.objects, g_verifies, g_verifies == 1 ? "" : "s", g_wallChecks,
             g_wallChecks == 1 ? "" : "s", g_addBacks, g_addBacks == 1 ? "" : "s");
    std::string s = b;
    if (g_refFlights > 0)   // cfg slicefirstref
        s += "; " + std::to_string(g_refFlights) + " first-attempt flight"
             + (g_refFlights == 1 ? "" : "s") + " on a fresh copy";
    return s;
}

}  // namespace levelslice
}  // namespace p1
