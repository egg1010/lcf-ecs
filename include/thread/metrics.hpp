#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "thread/error.hpp"
#include "thread/lane.hpp"
#include "thread/manager.hpp"
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

struct metrics_snapshot
{
    manager_metrics manager{};
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
