#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "thread/error.hpp"
#include "thread/lane.hpp"
#include "thread/manager.hpp"
#include "thread/profile.hpp"
#include "thread/worker.hpp"

namespace thread
{

struct worker_metrics
{
    std::uint64_t executed{0};
    std::uint64_t local{0};
    std::uint64_t injected{0};
    std::uint64_t stolen{0};
    std::uint64_t steal_attempts{0};
    std::uint64_t parks{0};
    std::uint64_t drops{0};
};

struct manager_metrics
{
    std::uint64_t submitted{0};
    std::uint64_t rejected{0};
    std::uint64_t completed{0};
    std::uint64_t wakes{0};
};

struct lane_metrics
{
    std::uint32_t lane_id{lane_no_id};
    lane_mode mode{lane_mode::exclusive};
    std::uint64_t submitted{0};
    std::uint64_t executed{0};
    std::uint64_t owned_batches{0};
    std::uint64_t reclaims{0};
    std::uint64_t seq_breaks{0};
};

inline constexpr std::size_t metrics_max_workers = 256;
inline constexpr std::size_t metrics_max_lanes = manager_default_lane_capacity;

struct task_metrics_snapshot
{
    std::uint64_t declared{0};
    std::uint64_t activations{0};
    std::uint64_t completions{0};
    std::uint64_t coalesced{0};
    std::uint64_t stale_tickets{0};
    std::uint64_t migrations{0};
    std::uint64_t forwarded{0};
    std::uint64_t forward_failures{0};
    std::uint64_t dependency_waits{0};
    std::uint64_t reorders{0};
    std::uint64_t reparents{0};
    std::uint64_t cancellations{0};
    std::uint64_t faults{0};
    std::uint64_t overruns{0};
    std::uint64_t stolen{0};
    std::uint64_t steal_denied{0};
};

struct watchdog_metrics_snapshot
{
    std::uint64_t scans{0};
    std::uint64_t deadline_misses{0};
    std::uint64_t hung_tasks{0};
    std::uint64_t heartbeat_misses{0};
    std::uint64_t queue_timeouts{0};
    std::uint64_t timed_wakes{0};
    std::uint64_t max_wait_ns{0};
    std::uint64_t max_run_ns{0};
};

struct admission_metrics_snapshot
{
    std::uint64_t admitted{0};
    std::uint64_t rejected{0};
    std::uint64_t degraded{0};
    std::uint64_t dropped{0};
    std::uint64_t coalesced{0};
    std::uint64_t completed{0};
    std::uint32_t inflight{0};
};

struct metrics_snapshot
{
    manager_metrics manager{};
    task_metrics_snapshot task{};
    watchdog_metrics_snapshot watchdog{};
    admission_metrics_snapshot admission{};
    std::uint64_t blocking_queue_depth{0};
    std::uint64_t blocking_completed{0};
    std::uint64_t trace_dropped{0};
    std::size_t spawned_workers{0};
    std::size_t thread_budget{0};
    worker_metrics workers[metrics_max_workers] = {};
    std::size_t worker_count{0};
    lane_metrics lanes[metrics_max_lanes] = {};
    std::size_t lane_count{0};

    std::size_t workers_truncated{0};
    std::size_t lanes_truncated{0};

    std::uint64_t frame_pool_hits{0};
    std::uint64_t frame_pool_misses{0};
    std::uint64_t frame_pool_returns{0};
    std::uint64_t frame_heap_allocs{0};
    std::uint64_t frame_high_water[frame_tier_count] = {};

    std::uint64_t total_executed{0};
    std::uint64_t total_stolen{0};
    std::uint64_t total_parks{0};

    std::uint64_t errors_by_tag[tag_count_max] = {};
    std::uint64_t errors_total{0};
    std::uint64_t ignored_fatal{0};
    std::uint64_t handler_faults{0};

    double steal_hit_ratio() const noexcept
    {
        std::uint64_t attempts = 0;
        for (std::size_t i = 0; i < worker_count; ++i)
        {
            attempts += workers[i].steal_attempts;
        }
        return attempts == 0 ? 0.0
                             : static_cast<double>(total_stolen) / static_cast<double>(attempts);
    }
};

inline metrics_snapshot collect_metrics(manager& mgr) noexcept
{
    metrics_snapshot snapshot{};

    const task_metrics& tm = mgr.task_stats();
    snapshot.task.declared = tm.declared_.load(std::memory_order_relaxed);
    snapshot.task.activations = tm.activations_.load(std::memory_order_relaxed);
    snapshot.task.completions = tm.completions_.load(std::memory_order_relaxed);
    snapshot.task.coalesced = tm.coalesced_.load(std::memory_order_relaxed);
    snapshot.task.stale_tickets = tm.stale_.load(std::memory_order_relaxed);
    snapshot.task.migrations = tm.migrations_.load(std::memory_order_relaxed);
    snapshot.task.forwarded = tm.forwarded_.load(std::memory_order_relaxed);
    snapshot.task.forward_failures = tm.forward_failures_.load(std::memory_order_relaxed);
    snapshot.task.dependency_waits = tm.dependency_waits_.load(std::memory_order_relaxed);
    snapshot.task.reorders = tm.reorders_.load(std::memory_order_relaxed);
    snapshot.task.reparents = tm.reparents_.load(std::memory_order_relaxed);
    snapshot.task.cancellations = tm.cancellations_.load(std::memory_order_relaxed);
    snapshot.task.faults = tm.faults_.load(std::memory_order_relaxed);
    snapshot.task.overruns = tm.overruns_.load(std::memory_order_relaxed);
    snapshot.task.stolen = tm.stolen_.load(std::memory_order_relaxed);
    snapshot.task.steal_denied = tm.steal_denied_.load(std::memory_order_relaxed);

    const watchdog_metrics& wm = mgr.watchdog_stats();
    snapshot.watchdog.scans = wm.scans_.load(std::memory_order_relaxed);
    snapshot.watchdog.deadline_misses = wm.deadline_misses_.load(std::memory_order_relaxed);
    snapshot.watchdog.hung_tasks = wm.hung_tasks_.load(std::memory_order_relaxed);
    snapshot.watchdog.heartbeat_misses = wm.heartbeat_misses_.load(std::memory_order_relaxed);
    snapshot.watchdog.queue_timeouts = wm.queue_timeouts_.load(std::memory_order_relaxed);
    snapshot.watchdog.timed_wakes = wm.timed_wakes_.load(std::memory_order_relaxed);
    snapshot.watchdog.max_wait_ns = wm.max_wait_ns_.load(std::memory_order_relaxed);
    snapshot.watchdog.max_run_ns = wm.max_run_ns_.load(std::memory_order_relaxed);

    const admission_metrics& am = mgr.admission().metrics_;
    snapshot.admission.admitted = am.admitted_.load(std::memory_order_relaxed);
    snapshot.admission.rejected = am.rejected_.load(std::memory_order_relaxed);
    snapshot.admission.degraded = am.degraded_.load(std::memory_order_relaxed);
    snapshot.admission.dropped = am.dropped_.load(std::memory_order_relaxed);
    snapshot.admission.coalesced = am.coalesced_.load(std::memory_order_relaxed);
    snapshot.admission.completed = am.completed_.load(std::memory_order_relaxed);
    snapshot.admission.inflight = mgr.admission().total_inflight();
    snapshot.blocking_queue_depth = mgr.blocking().queue_depth();
    snapshot.blocking_completed = mgr.blocking().completed_.load(std::memory_order_relaxed);
    snapshot.trace_dropped = mgr.trace().dropped_.load(std::memory_order_relaxed);
    snapshot.spawned_workers = mgr.spawned_worker_count();
    snapshot.thread_budget = profile::thread_budget();

    const manager_stats& ms = mgr.stats();
    snapshot.manager.submitted = ms.submitted_.load(std::memory_order_relaxed);
    snapshot.manager.rejected = ms.rejected_.load(std::memory_order_relaxed);
    snapshot.manager.completed = mgr.completed_count();
    snapshot.manager.wakes = ms.wakes_.load(std::memory_order_relaxed);

    const std::size_t wc = mgr.worker_count();
    const std::size_t wc_clamped = wc < metrics_max_workers ? wc : metrics_max_workers;
    for (std::size_t i = 0; i < wc_clamped; ++i)
    {
        worker* w = mgr.worker_at(i);
        if (w == nullptr)
        {
            continue;
        }
        const worker_stats& ws = w->stats();
        worker_metrics& slot = snapshot.workers[i];
        slot.executed = ws.executed_.load(std::memory_order_relaxed);
        slot.local = ws.local_.load(std::memory_order_relaxed);
        slot.injected = ws.injected_.load(std::memory_order_relaxed);
        slot.stolen = ws.stolen_.load(std::memory_order_relaxed);
        slot.steal_attempts = ws.steal_attempts_.load(std::memory_order_relaxed);
        slot.parks = ws.parks_.load(std::memory_order_relaxed);
        slot.drops = w->stack().drops();
        snapshot.total_executed += slot.executed;
        snapshot.total_stolen += slot.stolen;
        snapshot.total_parks += slot.parks;
    }
    snapshot.worker_count = wc_clamped;
    if (wc > wc_clamped)
    {
        snapshot.workers_truncated = wc - wc_clamped;
        FRAMEWORK_REPORT(error_severity::warning, errc::invalid_argument);
    }

    const std::size_t lc = mgr.lane_count();
    const std::size_t lc_clamped = lc < metrics_max_lanes ? lc : metrics_max_lanes;
    for (std::size_t i = 0; i < lc_clamped; ++i)
    {
        lane* l = mgr.lane_at(i);
        if (l == nullptr)
        {
            continue;
        }
        const lane_stats& ls = l->stats();
        lane_metrics& slot = snapshot.lanes[i];
        slot.lane_id = l->id();
        slot.mode = l->mode();
        slot.submitted = ls.submitted_.load(std::memory_order_relaxed);
        slot.executed = ls.executed_.load(std::memory_order_relaxed);
        slot.owned_batches = ls.owned_batches_.load(std::memory_order_relaxed);
        slot.reclaims = ls.reclaims_.load(std::memory_order_relaxed);
        slot.seq_breaks = ls.seq_breaks_.load(std::memory_order_relaxed);
    }
    snapshot.lane_count = lc_clamped;
    if (lc > lc_clamped)
    {
        snapshot.lanes_truncated = lc - lc_clamped;
        FRAMEWORK_REPORT(error_severity::warning, errc::invalid_argument);
    }

    const frame_pool& frames = mgr.runtime()->frames_;
    snapshot.frame_pool_hits = frames.hits_.load(std::memory_order_relaxed);
    snapshot.frame_pool_misses = frames.misses_.load(std::memory_order_relaxed);
    snapshot.frame_pool_returns = frames.returns_.load(std::memory_order_relaxed);
    snapshot.frame_heap_allocs = frames.heap_allocs_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < frame_tier_count; ++i)
    {
        snapshot.frame_high_water[i] = frames.high_water(i);
    }

    const error_counters& ec = global_error_counters();
    for (std::size_t i = 0; i < tag_count_max; ++i)
    {
        snapshot.errors_by_tag[i] = ec.by_tag_[i].load(std::memory_order_relaxed);
    }
    snapshot.errors_total = ec.total_.load(std::memory_order_relaxed);
    snapshot.ignored_fatal = ec.ignored_fatal_.load(std::memory_order_relaxed);
    snapshot.handler_faults = ec.handler_faults_.load(std::memory_order_relaxed);

    return snapshot;
}
} // namespace thread
