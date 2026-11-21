#ifndef FELSORIA_DYNAMIC_BOT_METRICS_H
#define FELSORIA_DYNAMIC_BOT_METRICS_H
// FELSORIA_DYNAMIC_V2_BASELINE: observation only; no world-object operations.
#include "Common.h"
#include "Log.h"
#include <atomic>
#include <chrono>

namespace FelsoriaDynamicMetrics
{
enum Metric : unsigned
{
    WorldWork, WorldCycle, ManagerWork, ManagerLockWait, SnapshotWork,
    SnapshotLockWait, SpawnPositionWork, CreateDispatch, RetireCleanup,
    SettingsStep, FieldAIWork, MapWork, MapSessionWork, Count
};
struct Counter
{
    std::atomic<uint64> count{0}, total{0}, maximum{0};
};
struct State
{
    std::atomic<bool> enabled{false}, events{false};
    Counter counters[Count];
    std::atomic<uint64> createFailures{0};
    std::atomic<uint32> activeWorld{0}, allocated{0}, creating{0}, pendingLogin{0},
        pendingCreate{0}, retiring{0}, queueOldestMs{0}, unready{0};
};
inline State& Get() { static State state; return state; }
inline bool Enabled() { return Get().enabled.load(std::memory_order_relaxed); }
inline void Record(Metric metric, uint64 microseconds)
{
    Counter& counter = Get().counters[metric];
    counter.total.fetch_add(microseconds, std::memory_order_relaxed);
    counter.count.fetch_add(1, std::memory_order_relaxed);
    uint64 previous = counter.maximum.load(std::memory_order_relaxed);
    while (previous < microseconds && !counter.maximum.compare_exchange_weak(
        previous, microseconds, std::memory_order_relaxed)) { }
}
class Scope
{
    Metric metric;
    bool running;
    std::chrono::steady_clock::time_point start;
public:
    explicit Scope(Metric value) : metric(value), running(Enabled())
    { if (running) start = std::chrono::steady_clock::now(); }
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;
    void Stop()
    {
        if (!running) return;
        running = false;
        Record(metric, uint64(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count()));
    }
    ~Scope() { Stop(); }
};
inline char const* Name(unsigned metric)
{
    static char const* const names[] = {"world_work", "world_cycle", "manager_work",
        "manager_lock_wait", "snapshot_work", "snapshot_lock_wait", "spawn_position_work",
        "create_dispatch", "retire_cleanup", "settings_step", "field_ai_work", "map_base_work", "map_session_work"};
    return names[metric];
}
// Called only by the world loop. Map workers only increment numeric counters.
inline void Report(uint32 windowMs)
{
    if (!Enabled()) return;
    State& state = Get();
    for (unsigned i = 0; i < Count; ++i)
    {
        Counter& counter = state.counters[i];
        uint64 const count = counter.count.exchange(0, std::memory_order_relaxed);
        uint64 const total = counter.total.exchange(0, std::memory_order_relaxed);
        uint64 const maximum = counter.maximum.exchange(0, std::memory_order_relaxed);
        TC_LOG_INFO(LOG_FILTER_PLAYER,
            "DYNAMIC_V2_METRIC name=%s window_ms=%u samples=%llu total_us=%llu avg_us=%.2f max_us=%llu",
            Name(i), windowMs, static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(total), count ? double(total) / double(count) : 0.0,
            static_cast<unsigned long long>(maximum));
    }
    TC_LOG_INFO(LOG_FILTER_PLAYER,
        "DYNAMIC_V2_STATE active_world=%u allocated=%u creating=%u pending_create=%u pending_login=%u unready=%u retiring=%u queue_oldest_ms=%u create_failures_total=%llu reuse=world_cache",
        state.activeWorld.load(), state.allocated.load(), state.creating.load(),
        state.pendingCreate.load(), state.pendingLogin.load(), state.unready.load(),
        state.retiring.load(), state.queueOldestMs.load(),
        static_cast<unsigned long long>(state.createFailures.load()));
}
}
#endif
