#pragma once
#include <iomanip>
#include <sstream>

namespace solver {
#ifdef GEODE_IS_WINDOWS
static_assert(offsetof(GJBaseGameLayer, m_timePlayed) == 0x3560);
static_assert(offsetof(GJBaseGameLayer, m_attempts) == 0x3084);
static_assert(offsetof(GJBaseGameLayer, m_gameState) + offsetof(GJGameState, m_points) == 0x864);
static_assert(offsetof(ItemTriggerGameObject, m_item1Mode) == 0x740);
static_assert(offsetof(ItemTriggerGameObject, m_tolerance) == 0x760);
static_assert(offsetof(ItemTriggerGameObject, m_signType2) == 0x770);
static_assert(offsetof(TimerTriggerGameObject, m_startTime) == 0x740);
static_assert(offsetof(TimerTriggerGameObject, m_timeMod) == 0x754);
static_assert(offsetof(TimerTriggerGameObject, m_controlType) == 0x75c);
#endif
inline bool g_itemMechanism = false;

// Preserve the native numeric store and outstanding callbacks at an anchor, not a replay estimate.
inline std::string itemStatePayload(GJBaseGameLayer* layer,
                                   const std::map<std::pair<int, int>, int>& fired) {
    if (!g_itemMechanism || !layer || !layer->m_effectManager) return {};
#ifdef GEODE_IS_WINDOWS
    const auto* base = reinterpret_cast<const char*>(layer);
    std::ostringstream out;
    out << std::setprecision(17) << "1|" << *reinterpret_cast<const double*>(base + 0x3560)
        << '|' << *reinterpret_cast<const int*>(base + 0x864) << '|' << layer->m_attempts << '|';
    auto* em = layer->m_effectManager;
    std::map<int, int> counts(em->m_itemCountMap.begin(), em->m_itemCountMap.end());
    for (const auto& p : counts) out << "1:" << p.first << ':' << p.second << ":0:0:0:1:0:-1:1,";
    std::map<int, TimerItem> timers(em->m_timerItemMap.begin(), em->m_timerItemMap.end());
    for (const auto& p : timers) {
        const auto& t = p.second;
        out << "2:" << p.first << ':' << t.m_time << ':' << t.m_active << ':' << t.m_ignoreTimeWarp
            << ':' << t.m_stopTimeEnabled << ':' << t.m_timeMod << ':' << t.m_targetTime
            << ':' << t.m_triggerUniqueID << ":1:" << (t.m_disabled || !t.m_remapKeys.empty()) << ',';
    }
    out << '|';
    for (const auto& p : em->m_spawnTriggerActions) {
        if (p.m_finished || p.m_disabled) continue;
        out << p.m_triggerUniqueID << ':' << std::max(0.0, p.m_duration - p.m_deltaTime)
            << ':' << (p.m_spawnOrdered || p.m_gameObject || !p.m_remapKeys.empty()) << ',';
    }
    out << '|';
    for (const auto& p : em->m_unkMap3f8) for (const auto& w : p.second) {
        if (w.m_disabled) continue;
        out << w.m_triggerUniqueID << ':' << w.m_time << ':' << !w.m_remapKeys.empty() << ',';
    }
    out << '|';
    bool first = true;
    for (const auto& p : fired) {
        out << (first ? "" : ",") << p.first.first << '/' << p.first.second << ':' << p.second;
        first = false;
    }
    if (first) out << '-';
    return out.str();
#else
    return "unsupported";
#endif
}
} // namespace solver
