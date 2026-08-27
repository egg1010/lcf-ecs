// test_perf.cpp - lcf-ecs 性能测试 (Performance Benchmark)
// 支持: MinGW GCC / Linux GCC / Clang / MSVC, 启用 LTO
//   - MinGW GCC: 不启用 AVX2 (vmovdqa 32 字节栈对齐 vs MinGW ABI 16 字节对齐
//     → 随机崩溃 0xC0000005). MinGW ABI 遵循 Windows x64 ABI 规范, 仅保证 16 字节
//     栈对齐 (设计规范). 用 SSE2/BMI2 路径, 16 字节对齐安全.
//   - MSVC / Linux / macOS GCC / Clang: 启用 AVX2 (ABI 保证 32 字节栈对齐).
//   - LTO: GCC -flto=N / Clang -flto=thin / MSVC /GL+/LTCG
// 注意: 禁止 std::sort + lambda (历史 MinGW+AVX2 崩溃), 改用 pdqsort
#include "test_common.hpp"
#include "include/part/analysis.hpp"
#include "include/part/dense.hpp"
#include "include/part/class_pool_views.hpp"
#include <bit>
#include <numeric>

// 数量级格式化
inline std::string mag(size_t n) noexcept
{
    if (n >= 1000000)
    {
        return std::to_string(n / 1000000) + "M/百万";
    }
    if (n >= 10000)
    {
        return std::to_string(n / 10000) + "0K/十万";
    }
    if (n >= 1000)
    {
        return std::to_string(n / 1000) + "K/千";
    }
    return std::to_string(n);
}

// 周期级精确基准 (替代旧 time.hpp 的 benchmark_precise_cycles, 仅本文件内部使用)
template<typename F>
stats benchmark_precise_cycles(size_t iterations, size_t warmup, F&& fn) noexcept
{
    for (size_t i = 0; i < warmup; ++i) { fn(); }
    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i)
    {
        uint64_t c0 = rdtscp();
        fn();
        uint64_t c1 = rdtscp();
        samples.push_back(static_cast<double>(c1 - c0));
    }
    if (samples.empty()) { return {}; }
    std::sort(samples.begin(), samples.end());
    stats s;
    s.count = samples.size();
    s.min = samples.front();
    s.max = samples.back();
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    s.mean = sum / static_cast<double>(s.count);
    auto pct = [&](double q) -> double
    {
        if (s.count == 1) { return samples[0]; }
        size_t idx = static_cast<size_t>(q * static_cast<double>(s.count - 1));
        return samples[idx];
    };
    s.median = s.p50 = pct(0.50);
    s.p90 = pct(0.90);
    s.p95 = pct(0.95);
    s.p99 = pct(0.99);
    return s;
}

// === multi_view 多组件查询基准助手 (1~10 组件) ===
// 组件字段读取器: 返回 float 或 uint32_t 触发真实数据访问, 防止死码消除.
// float 字段: 直接返回 (vmovss 单指令 load, 2 loads/cyc throughput)
// int 字段: 返回 uint32_t (mov 单指令 load, 2 loads/cyc throughput)
//   避免 static_cast<float>(int): vcvtsi2ss 转换端口仅 1/cyc, 成为瓶颈.
// Name 的 size() 取低 32bit 作为 uint32_t, 避免 64→32 显式转换.
inline float comp_touch(const Position& p) noexcept { return p.x; }
inline float comp_touch(const Velocity& v) noexcept { return v.vx; }
inline uint32_t comp_touch(const Health& h) noexcept { return static_cast<uint32_t>(h.current); }
inline uint32_t comp_touch(const Damage& d) noexcept { return static_cast<uint32_t>(d.amount); }
inline uint32_t comp_touch(const Armor& a) noexcept { return static_cast<uint32_t>(a.defense); }
inline float comp_touch(const Speed& s) noexcept { return s.value; }
inline uint32_t comp_touch(const Name& n) noexcept { return static_cast<uint32_t>(n.value.size()); }
inline float comp_touch(const Rotation& r) noexcept { return r.x; }
inline float comp_touch(const Scale& s) noexcept { return s.x; }
inline float comp_touch(const Mass& m) noexcept { return m.value; }

// 统一 uint32_t 读取器: 用于 add-chain 模式 (无 asm barrier, 编译器可自由调度 load 跨实体)
// float 字段: bit_cast<uint32_t> (vmovd XMM→GPR, 2/cyc, 无 vcvtsi2ss 转换瓶颈)
// int 字段: 直接返回 (mov, 2/cyc)
// Name: size() 返回 size_t, 取低 32bit
inline uint32_t comp_touch_u32(const Position& p) noexcept { return std::bit_cast<uint32_t>(p.x); }
inline uint32_t comp_touch_u32(const Velocity& v) noexcept { return std::bit_cast<uint32_t>(v.vx); }
inline uint32_t comp_touch_u32(const Health& h) noexcept { return static_cast<uint32_t>(h.current); }
inline uint32_t comp_touch_u32(const Damage& d) noexcept { return static_cast<uint32_t>(d.amount); }
inline uint32_t comp_touch_u32(const Armor& a) noexcept { return static_cast<uint32_t>(a.defense); }
inline uint32_t comp_touch_u32(const Speed& s) noexcept { return std::bit_cast<uint32_t>(s.value); }
inline uint32_t comp_touch_u32(const Name& n) noexcept { return static_cast<uint32_t>(n.value.size()); }
inline uint32_t comp_touch_u32(const Rotation& r) noexcept { return std::bit_cast<uint32_t>(r.x); }
inline uint32_t comp_touch_u32(const Scale& s) noexcept { return std::bit_cast<uint32_t>(s.x); }
inline uint32_t comp_touch_u32(const Mass& m) noexcept { return std::bit_cast<uint32_t>(m.value); }

// touch_barrier: 标记值已使用, 防止死代码消除, 不创建串行依赖
#if defined(_MSC_VER)
    // MSVC x64 无 inline asm, 用 volatile + 屏障
    inline void touch_barrier(float v) noexcept
    {
        volatile float sink = v;
        _ReadWriteBarrier();
        (void)sink;
    }
    inline void touch_barrier(uint32_t v) noexcept
    {
        volatile uint32_t sink = v;
        _ReadWriteBarrier();
        (void)sink;
    }
#else
    // GCC/Clang: "x" 入 XMM, "r" 入 GPR
    inline void touch_barrier(float v) noexcept { asm volatile("" : : "x"(v) :); }
    inline void touch_barrier(uint32_t v) noexcept { asm volatile("" : : "r"(v) :); }
#endif

// 批量屏障: 所有值作为函数参数先求值 (所有 load 先于任何 barrier),
// 再依次施加 barrier. 消除 touch_barrier 之间的串行依赖,
// 使编译器可自由调度所有 load 指令 (2 loads/cyc 并行发射).
// 对比旧方案 (touch_barrier(comp_touch(comps)), ...):
//   - 旧方案 comma fold 把 load-barrier 交错, asm volatile 是编译器屏障,
//     编译器无法把 load_i+1 调度到 load_i 之前, load 被串行化.
//   - 新方案 所有 load 作为实参先求值, 编译器可自由重排 load 顺序.
template <typename... Ts>
inline void touch_all(Ts... vs) noexcept
{
    (touch_barrier(vs), ...);
}

// 通用 multi_view for_each 基准: 对任意 1~10 组件组合统一测量
// 设计要点:
//   1. 构造 view 一次复用, warmup 预建 mappings (ensure_mappings 的 rebuild_mappings
//      做 N-1 次 2MB memcmp, 10 组件 ~0.6ms, 必须排除出计时区).
//   2. touch_all(comp_touch(comps)...) — load-all-then-barrier:
//      - 所有 comp_touch 作为 touch_all 实参先求值 (N 个独立 load 并行发射).
//      - touch_all 内 (touch_barrier(vs), ...) 仅施加 asm 屏障, 不产生指令.
//      - 编译器自由调度所有 load, 瓶颈回到 load throughput (2 loads/cyc).
//   3. int 字段返回 uint32_t 走 "r" 约束 (GPR load), 避免 vcvtsi2ss 1/cyc 瓶颈.
//   4. 多次重复取最小值 (REPEAT=3): 9+ comps 数据量 52MB+ 远超 L3(32MB), 测量
//      噪声大 (TLB/L3 驱逐/调度抖动), 取最小值反映真实循环吞吐能力.
//   注: add-chain (sink += fold) 经测试更慢 — fold 内部 N 级串行 add 被编译器
//      跨实体串行化, 且 bit_cast 的 vmovd (XMM→GPR) 引入额外依赖. touch_all
//      的 asm barrier 不创建数据依赖, 仅约束编译器重排, 负载仍可并行发射.
template <typename... Comps>
size_t bench_multi_view(ecs::manager& mgr, const char* label) noexcept
{
    auto v = mgr.view<Comps...>();
    size_t cnt = v.size();
    // warmup: 预建 mappings + 预热 cache (不计入计时)
    //   3 次 warmup 确保 L3 充分预热, 减少 TLB/L3 驱逐导致的测量噪声
    v.for_each([&](Comps&... comps) {
        touch_all(comp_touch(comps)...);
    });
    v.for_each([&](Comps&... comps) {
        touch_all(comp_touch(comps)...);
    });
    v.for_each([&](Comps&... comps) {
        touch_all(comp_touch(comps)...);
    });
    // timed run: 多次取最小值 (减少大数据集 L3 越界导致的测量噪声)
    //   REPEAT=10 平衡采样数与测量时间, 取最小值反映真实循环吞吐能力
    constexpr int REPEAT = 10;
    double best_ms = 1e9;
    for (int r = 0; r < REPEAT; ++r)
    {
        timer t;
        t.reset();
        v.for_each([&](Comps&... comps) {
            touch_all(comp_touch(comps)...);
        });
        double ms = t.elapsed_milliseconds();
        if (ms < best_ms) best_ms = ms;
    }
    print_perf(label, cnt, best_ms);
    return cnt;
}

template <typename... Comps>
size_t bench_multi_view_ent(ecs::manager& mgr, const char* label) noexcept
{
    auto v = mgr.view<Comps...>();
    size_t cnt = v.size();
    v.for_each([&](entity e, Comps&... comps) {
        touch_all(e.parts_.index_, comp_touch(comps)...);
    });
    timer t;
    t.reset();
    v.for_each([&](entity e, Comps&... comps) {
        touch_all(e.parts_.index_, comp_touch(comps)...);
    });
    print_perf(label, cnt, t.elapsed_milliseconds());
    return cnt;
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "========================================================\n"
              << "  lcf-ecs 性能测试 / Performance Benchmark\n"
              << "========================================================\n";

    constexpr size_t N = 1000000;  // 1M / 百万实体
    ecs::manager ecss;
    timer t;

    dense<entity> entities;
    entities.increase_capacity(N);

    // === Section 0: Data Setup / 数据准备 (1M/百万) ===
    print_section(0, "Data Setup / 数据准备 (1M/百万)");
    {
        print_perf_sub("0.1 实体预分配与创建 (1M/百万)");

        t.reset();
        ecss.append_preallocated_entities(N);
        print_perf("append_preallocated_entities", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            entities.emplace_back(ecss.create_entity());
        }
        print_perf("create_entity", N, t.elapsed_milliseconds());

        // 预留组件容量, 避免后续 add 触发重分配
        ecss.reserve_component_capacity<Position>(N);
        ecss.reserve_component_capacity<Health>(N);
        ecss.reserve_component_capacity<Velocity>(N / 2);
        ecss.reserve_component_capacity<Damage>(N / 2);
        ecss.reserve_component_capacity<Armor>(N / 2);
        ecss.reserve_component_capacity<Rotation>(N / 2);
        ecss.reserve_component_capacity<Speed>(N / 4);
        ecss.reserve_component_capacity<Scale>(N / 4);
        ecss.reserve_component_capacity<Name>(N / 10);
        ecss.reserve_component_capacity<Mass>(N / 10);

        print_perf_sub("0.2 组件数据生成 (随机分布)");
        std::mt19937 gen(42);
        std::uniform_real_distribution<float> pos_dist(-1000.0f, 1000.0f);
        std::uniform_real_distribution<float> vel_dist(-10.0f, 10.0f);
        std::uniform_int_distribution<int> hp_dist(1, 100);
        std::uniform_int_distribution<int> dmg_dist(1, 50);
        std::uniform_int_distribution<int> armor_dist(1, 200);

        const size_t vel_count = N / 2;       // 500K
        const size_t speed_count = N / 4;     // 250K
        const size_t name_count = N / 10;     // 100K

        dense<Position> positions;
        dense<Velocity> velocities;
        dense<Health> healths;
        dense<Name> names;
        dense<Damage> damages;
        dense<Armor> armors;
        dense<Speed> speeds;
        dense<Rotation> rotations;
        dense<Scale> scales;
        dense<Mass> masses;

        positions.increase_capacity(N);
        velocities.increase_capacity(vel_count);
        healths.increase_capacity(N);
        names.increase_capacity(name_count);
        damages.increase_capacity(vel_count);
        armors.increase_capacity(vel_count);
        speeds.increase_capacity(speed_count);
        rotations.increase_capacity(vel_count);
        scales.increase_capacity(speed_count);
        masses.increase_capacity(name_count);

        for (size_t i = 0; i < N; ++i)
        {
            positions.emplace_back(pos_dist(gen), pos_dist(gen), pos_dist(gen));
            healths.emplace_back(hp_dist(gen), 100);
        }
        for (size_t i = 0; i < vel_count; ++i)
        {
            velocities.emplace_back(vel_dist(gen), vel_dist(gen), vel_dist(gen));
            damages.emplace_back(dmg_dist(gen));
            armors.emplace_back(armor_dist(gen));
            rotations.emplace_back(vel_dist(gen), vel_dist(gen), vel_dist(gen), 1.0f);
        }
        for (size_t i = 0; i < name_count; ++i)
        {
            names.emplace_back("Entity_" + std::to_string(i));
            masses.emplace_back(vel_dist(gen) * 10.0f);
        }
        for (size_t i = 0; i < speed_count; ++i)
        {
            speeds.emplace_back(vel_dist(gen) * 0.5f);
            scales.emplace_back(vel_dist(gen), vel_dist(gen), vel_dist(gen));
        }

        print_perf_sub("0.3 批量挂载组件 (add_batch)");
        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), N),
                       std::span<const Position>(positions.data(), N));
        print_perf("add_batch Position", N, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), N),
                       std::span<const Health>(healths.data(), N));
        print_perf("add_batch Health", N, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count),
                       std::span<const Velocity>(velocities.data(), vel_count));
        print_perf("add_batch Velocity", vel_count, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count),
                       std::span<const Damage>(damages.data(), vel_count));
        print_perf("add_batch Damage", vel_count, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count),
                       std::span<const Armor>(armors.data(), vel_count));
        print_perf("add_batch Armor", vel_count, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count),
                       std::span<const Rotation>(rotations.data(), vel_count));
        print_perf("add_batch Rotation", vel_count, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), speed_count),
                       std::span<const Speed>(speeds.data(), speed_count));
        print_perf("add_batch Speed", speed_count, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), speed_count),
                       std::span<const Scale>(scales.data(), speed_count));
        print_perf("add_batch Scale", speed_count, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), name_count),
                       std::span<const Name>(names.data(), name_count));
        print_perf("add_batch Name", name_count, t.elapsed_milliseconds());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), name_count),
                       std::span<const Mass>(masses.data(), name_count));
        print_perf("add_batch Mass", name_count, t.elapsed_milliseconds());

        std::cout << "\n  ┌─ 数据分布 / Data Distribution (10 组件)\n";
        std::cout << "  │ Position: " << N << " (100%)\n";
        std::cout << "  │ Health:   " << N << " (100%)\n";
        std::cout << "  │ Velocity: " << vel_count << " (50%)\n";
        std::cout << "  │ Damage:   " << vel_count << " (50%)\n";
        std::cout << "  │ Armor:    " << vel_count << " (50%)\n";
        std::cout << "  │ Rotation: " << vel_count << " (50%)\n";
        std::cout << "  │ Speed:    " << speed_count << " (25%)\n";
        std::cout << "  │ Scale:    " << speed_count << " (25%)\n";
        std::cout << "  │ Name:     " << name_count << " (10%)\n";
        std::cout << "  │ Mass:     " << name_count << " (10%)\n";
    }

    // === Section 1: Basic Types / 基础类型 (entity / type_id) (1M/百万) ===
    print_section(1, "Basic Types / 基础类型 (entity / type_id) (1M/百万)");
    {
        print_perf_sub("1.1 entity 构造与比较 (1M/百万)");
        t.reset();
        entity e_test;
        for (size_t i = 0; i < N; ++i)
        {
            e_test = entity(static_cast<uint32_t>(i), static_cast<uint32_t>(i));
            lcf_sink_all(e_test.parts_.index_, e_test.parts_.version_);
        }
        print_perf("entity construct", N, t.elapsed_milliseconds());

        t.reset();
        entity e1(1, 1), e2(1, 1), e3(2, 1);
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink_all(e1.is_valid(), (e1 == e2), (e1 != e3));
        }
        print_perf("entity is_valid/==/!=", N * 3, t.elapsed_milliseconds());

        t.reset();
        std::hash<entity> eh;
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(eh(entity(static_cast<uint32_t>(i), 1)));
        }
        print_perf("std::hash<entity>", N, t.elapsed_milliseconds());

        print_perf_sub("1.2 type_id 与 entity_mask (1M/百万)");
        t.reset();
        for (int i = 0; i < static_cast<int>(N); ++i)
        {
            lcf_sink(type_id::get_type_id<Position>());
        }
        print_perf("type_id::get_type_id", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(ecss.get_entity_mask(entities[i]));
        }
        print_perf("get_entity_mask", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(ecss.get_entity_block(entities[i], 0));
        }
        print_perf("get_entity_block", N, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(ecss.num_mask_blocks());
        }
        print_perf("num_mask_blocks", 1000000, t.elapsed_milliseconds());
    }

    // === Section 2: class_pool / 组件池 (1M/百万) ===
    print_section(2, "class_pool / 组件池 (1M/百万)");
    {
        print_perf_sub("2.1 写入接口 (1M/百万)");
        t.reset();
        dense<int> cp_em;
        cp_em.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_em.emplace_back(static_cast<int>(i));
        }
        print_perf("emplace_back", N, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_pb;
        cp_pb.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_pb.push_back_unchecked(static_cast<int>(i));
        }
        print_perf("push_back_unchecked", N, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_ub;
        cp_ub.emplace_back(0);
        cp_ub.increase_capacity(N + 1);
        for (size_t i = 0; i < N; ++i)
        {
            cp_ub.emplace_back_unchecked(static_cast<int>(i));
        }
        print_perf("emplace_back_unchecked", N, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_db;
        cp_db.emplace_back(0);
        cp_db.increase_capacity(N + 1);
        for (size_t i = 0; i < N; ++i)
        {
            cp_db.emplace_back_dense_unchecked(static_cast<int>(i));
        }
        print_perf("emplace_back_dense_unchecked", N, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_an;
        cp_an.increase_capacity(N + 1);
        cp_an.append_n(N, 42);
        print_perf("append_n", N, t.elapsed_milliseconds());

        t.reset();
        class_pool<int> cp_ea;
        cp_ea.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_ea.emplace_at(i, static_cast<int>(i));
        }
        print_perf("emplace_at", N, t.elapsed_milliseconds());

        t.reset();
        class_pool<int> cp_sea;
        cp_sea.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_sea.sparse_emplace_at(i, static_cast<int>(i));
        }
        print_perf("sparse_emplace_at", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; i += 2)
        {
            cp_sea.sparse_erase_at(i);
        }
        print_perf("sparse_erase_at", N / 2, t.elapsed_milliseconds());

        print_perf_sub("2.2 容量与拷贝 (1M/百万)");
        t.reset();
        dense<int> cp_rz;
        cp_rz.reserve_exact(N);
        print_perf("reserve_exact", N, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_rzv;
        cp_rzv.increase_capacity(N, 77);
        print_perf("increase_capacity(cap,val) [empty]", N, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_ic;
        cp_ic.emplace_back(1);
        cp_ic.increase_capacity(N, 99);
        print_perf("increase_capacity(cap,val)", N, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_copy(cp_em);
        print_perf("copy ctor", 1, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_assign;
        cp_assign = cp_em;
        print_perf("copy assign", 1, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_move(std::move(cp_assign));
        print_perf("move ctor", 1, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_sf;
        cp_sf.increase_capacity(N);
        for (size_t i = 0; i < N / 2; ++i)
        {
            cp_sf.emplace_back(static_cast<int>(i));
        }
        cp_sf.shrink_to_fit();
        print_perf("shrink_to_fit", N / 2, t.elapsed_milliseconds());

        t.reset();
        dense<int> cp_cl;
        cp_cl.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_cl.emplace_back(static_cast<int>(i));
        }
        cp_cl.clear();
        print_perf("clear", N, t.elapsed_milliseconds());

        print_perf_sub("2.3 访问与遍历 (1M/百万)");
        class_pool<int> cp_acc;
        cp_acc.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_acc.emplace_back(static_cast<int>(i));
        }

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(cp_acc[i]);
        }
        print_perf("operator[]", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(cp_acc.get(i));
        }
        print_perf("get(i) 等价 operator[]", N, t.elapsed_milliseconds());

        // get(index, error_index): 越界保护访问, 一半合法一半越界
        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(cp_acc.get(i + N, 0));
        }
        print_perf("get(i+N, 0) 全越界", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink_all(cp_acc.front(), cp_acc.back());
        }
        print_perf("front/back", N * 2, t.elapsed_milliseconds());

        t.reset();
        for (int iter = 0; iter < 10; ++iter)
        {
            for (auto it = cp_acc.cbegin(); it != cp_acc.cend(); ++it)
            {
                lcf_sink(*it);
            }
        }
        print_perf("cbegin/cend iter (10x)", N * 10, t.elapsed_milliseconds());

        // range-for (begin/end 迭代器): 基准对照
        t.reset();
        for (int iter = 0; iter < 10; ++iter)
        {
            for (auto& v : cp_acc)
            {
                lcf_sink(v);
            }
        }
        print_perf("range-for iter (10x)", N * 10, t.elapsed_milliseconds());

        print_perf_sub("2.4 状态查询 (1M/百万)");
        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(cp_acc.is_dense());
        }
        print_perf("is_dense", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(cp_acc.size(), cp_acc.capacity(), cp_acc.sparse_capacity(), cp_acc.size_bytes(), cp_acc.capacity_bytes(), cp_acc.empty());
        }
        print_perf("capacity queries", 1000000 * 6, t.elapsed_milliseconds());
    }

    // === Section 2.5: class_pool 视图 (cpv 命名空间) (1M/百万) ===
    print_section(2, "class_pool 视图 / cpv namespace (1M/百万)");
    {
        print_perf_sub("2.5.1 子范围与反向视图 (1M/百万)");
        class_pool<int> cpv_p;
        cpv_p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cpv_p.emplace_back(static_cast<int>(i));
        }

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            auto sp = subspan(cpv_p, N / 4, N / 2);
            lcf_sink(sp.size());
        }
        print_perf("subspan(off, cnt)", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            auto sp = subspan(cpv_p, N / 2);
            lcf_sink(sp.size());
        }
        print_perf("subspan(off)", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            auto f = first(cpv_p, N / 4);
            auto l = last(cpv_p, N / 4);
            lcf_sink(f.size() + l.size());
        }
        print_perf("first(n)/last(n)", 1000000 * 2, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            auto f = first_fixed<8>(cpv_p);
            auto l = last_fixed<8>(cpv_p);
            lcf_sink(f.size() + l.size());
        }
        print_perf("first_fixed<N>/last_fixed<N>", 1000000 * 2, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            reverse_for_each(cpv_p, [&](int& v) { lcf_sink(v); });
        }
        print_perf("reverse_for_each", N * 100, t.elapsed_milliseconds());

        print_perf_sub("2.5.2 步进与变换视图 (1M/百万)");
        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            strided_for_each(cpv_p, 0, 4, [&](int& v) { lcf_sink(v); });
        }
        print_perf("strided_for_each (rt step=4)", (N / 4) * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            strided_for_each<4>(cpv_p, [&](int& v) { lcf_sink(v); });
        }
        print_perf("strided_for_each<4> (ct step)", (N / 4) * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            strided_for_each<1>(cpv_p, [&](int& v) { lcf_sink(v); });
        }
        print_perf("strided_for_each<1> (fast path)", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            transform_for_each(
                cpv_p,
                [](int& v) -> int { return v; },
                [&](int v) { lcf_sink(v); });
        }
        print_perf("transform_for_each", N * 100, t.elapsed_milliseconds());

        t.reset();
        class_pool<int> cpv_dst;
        cpv_dst.increase_capacity(N);
        for (int i = 0; i < 100; ++i)
        {
            transform_to(cpv_p, cpv_dst.data(), N, [](const int& v) -> int { return v; });
        }
        print_perf("transform_to", N * 100, t.elapsed_milliseconds());

        print_perf_sub("2.5.3 过滤与查找 (1M/百万)");
        t.reset();
        int target = static_cast<int>(N / 2);
        for (int i = 0; i < 100; ++i)
        {
            int* r = find(cpv_p, target);
            lcf_sink(r ? *r : 0);
        }
        print_perf("find (mid hit)", 100, t.elapsed_milliseconds());

        t.reset();
        bool found = false;
        for (int i = 0; i < 100; ++i)
        {
            found = contains(cpv_p, target);
        }
        print_perf("contains (mid hit)", 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            int* r = find_if(cpv_p, [&](const int& v) { return v == target; });
            lcf_sink(r ? *r : 0);
        }
        print_perf("find_if (mid hit)", 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 10; ++i)
        {
            lcf_sink(count_if(cpv_p, [](const int&) { return true; }));
        }
        print_perf("count_if", N * 10, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            filter_for_each(cpv_p, [](const int&) { return true; }, [&](int& v) { lcf_sink(v); });
        }
        print_perf("filter_for_each (all)", N * 100, t.elapsed_milliseconds());

        t.reset();
        class_pool<size_t> idx_dst;
        idx_dst.increase_capacity(N);
        for (int i = 0; i < 10; ++i)
        {
            idx_dst.clear();
            filter_indices_to(cpv_p, idx_dst, [](const int&) { return true; });
        }
        print_perf("filter_indices_to", N * 10, t.elapsed_milliseconds());

        print_perf_sub("2.5.4 规约与极值 (1M/百万)");
        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            int r = reduce(cpv_p, [](int acc, const int& v) -> int { return acc + v; }, 0);
            lcf_sink(r);
        }
        print_perf("reduce", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            int r = reduce_pairwise(cpv_p, [](int acc, const int& v) -> int { return acc + v; }, 0);
            lcf_sink(r);
        }
        print_perf("reduce_pairwise", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            int* r = min_element(cpv_p);
            lcf_sink(r ? *r : 0);
        }
        print_perf("min_element", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            int* r = max_element(cpv_p);
            lcf_sink(r ? *r : 0);
        }
        print_perf("max_element", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            int s = sum(cpv_p);
            lcf_sink(s);
        }
        print_perf("sum (ivdep)", N * 100, t.elapsed_milliseconds());

        t.reset();
        class_pool<int> cpv_other;
        cpv_other.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cpv_other.emplace_back(static_cast<int>(i * 2));
        }
        for (int i = 0; i < 100; ++i)
        {
            int r = dot_product(cpv_p, cpv_other.data(), N);
            lcf_sink(r);
        }
        print_perf("dot_product", N * 100, t.elapsed_milliseconds());

        print_perf_sub("2.5.5 窗口/分块/枚举/zip (1M/百万)");
        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            for_each_window<4>(cpv_p, [&](std::span<int, 4> w) { lcf_sink(w[0]); });
        }
        print_perf("for_each_window<4>", (N - 3) * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            for_each_chunk<4>(cpv_p, [&](std::span<int, 4> c) { lcf_sink(c[0]); });
        }
        print_perf("for_each_chunk<4>", (N / 4) * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            for_each_enumerated(cpv_p, [&](size_t, int& v) { lcf_sink(v); });
        }
        print_perf("for_each_enumerated", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            for_each_zip(cpv_p, cpv_other, [&](int& a, int& b) { lcf_sink(a + b); });
        }
        print_perf("for_each_zip (pool)", N * 100, t.elapsed_milliseconds());

        t.reset();
        int* other_ptr = cpv_other.data();
        for (int i = 0; i < 100; ++i)
        {
            for_each_zip(cpv_p, other_ptr, N, [&](int& a, int& b) { lcf_sink(a + b); });
        }
        print_perf("for_each_zip (ptr)", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            zip_with_to(cpv_p, cpv_other.data(), cpv_dst.data(), N,
                [](const int& a, const int& b) -> int { return a + b; });
        }
        print_perf("zip_with_to", N * 100, t.elapsed_milliseconds());

        t.reset();
        class_pool<int> cpv_cpy = cpv_p;
        for (int i = 0; i < 100; ++i)
        {
            bool r = equal(cpv_p, cpv_cpy);
            lcf_sink(r ? 1 : 0);
        }
        print_perf("equal (pool)", N * 100, t.elapsed_milliseconds());

        t.reset();
        const int* eq_ptr = cpv_cpy.data();
        for (int i = 0; i < 100; ++i)
        {
            bool r = equal(cpv_p, eq_ptr, N);
            lcf_sink(r ? 1 : 0);
        }
        print_perf("equal (ptr, count)", N * 100, t.elapsed_milliseconds());

        print_perf_sub("2.5.6 SIMD/对齐与拷贝视图 (1M/百万)");
        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            simd_for_each(cpv_p, [&](int& v) { lcf_sink(v); });
        }
        print_perf("simd_for_each", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            copy_to(cpv_p, cpv_dst.data(), N);
        }
        print_perf("copy_to", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            move_to(cpv_p, cpv_dst.data(), N);
        }
        print_perf("move_to", N * 100, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 100; ++i)
        {
            reverse_copy_to(cpv_p, cpv_dst.data(), N);
        }
        print_perf("reverse_copy_to", N * 100, t.elapsed_milliseconds());

        print_perf_sub("2.5.7 稀疏模式视图 (1M/百万)");
        class_pool<int> cpv_sparse = cpv_p;
        for (size_t i = 0; i < N; i += 4)
        {
            cpv_sparse.sparse_erase_at(i);
        }
        const size_t live = cpv_sparse.count();

        t.reset();
        for (int i = 0; i < 10; ++i)
        {
            simd_for_each(cpv_sparse, [&](int& v) { lcf_sink(v); });
        }
        print_perf("sparse simd_for_each (degraded)", live * 10, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 10; ++i)
        {
            filter_for_each(cpv_sparse, [](const int&) { return true; }, [&](int& v) { lcf_sink(v); });
        }
        print_perf("sparse filter_for_each", live * 10, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 10; ++i)
        {
            size_t c = compact_to(cpv_sparse, cpv_dst.data(), N);
            lcf_sink(c);
        }
        print_perf("sparse compact_to", live * 10, t.elapsed_milliseconds());

        (void)found;
    }

    // === Section 3: void_any / 类型擦除 (1M/百万) ===
    print_section(3, "void_any / 类型擦除 (1M/百万)");
    {
        print_perf_sub("3.1 构造与赋值 (1M/百万)");
        t.reset();
        dense<void_any> va_pool;
        va_pool.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            va_pool.emplace_back(static_cast<int>(i));
        }
        print_perf("ctor(T&&)", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            va_pool[i].set(static_cast<double>(i));
        }
        print_perf("set<T>", N, t.elapsed_milliseconds());

        print_perf_sub("3.2 查询与访问 (1M/百万)");
        t.reset();
        size_t hv = 0;
        for (size_t i = 0; i < N; ++i)
        {
            if (va_pool[i].has_value()) ++hv;
        }
        lcf_sink(hv);
        print_perf("has_value", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(va_pool[i].type_id());
        }
        print_perf("type_id", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(va_pool[i].get_ptr<double>());
        }
        print_perf("get_ptr<T>", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(va_pool[i].fast_get_ptr<double>());
        }
        print_perf("fast_get_ptr<T>", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            lcf_sink(va_pool[i].get_ptr_unchecked<double>());
        }
        print_perf("get_ptr_unchecked<T>", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            double v = va_pool[i].get<double>();
            (void)v;
        }
        print_perf("get<T>", N, t.elapsed_milliseconds());

        print_perf_sub("3.3 重置与拷贝 (1M/百万)");
        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            va_pool[i].reset();
        }
        print_perf("reset", N, t.elapsed_milliseconds());

        t.reset();
        void_any va_src(42);
        for (size_t i = 0; i < N; ++i)
        {
            void_any va_copy_obj(va_src);
        }
        print_perf("copy ctor", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            void_any va_tmp(42);
            void_any va_move(std::move(va_tmp));
        }
        print_perf("move ctor", N, t.elapsed_milliseconds());
    }

    // === Section 4: memory_pool / 内存池 (1M/百万) ===
    print_section(4, "memory_pool / 内存池 (1M/百万)");
    {
        print_perf_sub("4.1 分配与释放 (1M/百万)");
        t.reset();
        memory_pool mp(1024 * 1024);
        dense<void*> ptrs;
        ptrs.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            ptrs.emplace_back(mp.allocate(64));
        }
        print_perf("allocate(64)", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            mp.deallocate(ptrs[i]);
        }
        print_perf("deallocate", N, t.elapsed_milliseconds());

        t.reset();
        dense<int*> iptrs;
        iptrs.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            iptrs.emplace_back(mp.construct<int>(static_cast<int>(i)));
        }
        print_perf("construct<int>", N, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            mp.destroy(iptrs[i]);
        }
        print_perf("destroy<int>", N, t.elapsed_milliseconds());

        print_perf_sub("4.2 容量与状态 (1M/百万)");
        t.reset();
        memory_pool mp2(4096);
        mp2.increase_capacity(8 * 1024 * 1024);
        print_perf("increase_capacity", 1, t.elapsed_milliseconds());

        t.reset();
        mp2.reduce_capacity(0);
        print_perf("reduce_capacity", 1, t.elapsed_milliseconds());

        t.reset();
        memory_pool mp3(4096);
        for (size_t i = 0; i < 10000; ++i)
        {
            void* p = mp3.allocate(64);
            (void)p;
        }
        mp3.reset();
        print_perf("reset", 10000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(mp.owns(ptrs[i % 10000]));
        }
        print_perf("owns", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(mp.total_allocated(), mp.total_used(), mp.empty(), mp.chunk_size());
        }
        print_perf("state queries", 1000000 * 4, t.elapsed_milliseconds());

        t.reset();
        pool_stats st;
        for (int i = 0; i < 1000000; ++i)
        {
            st = mp.stats();
        }
        print_perf("stats", 1000000, t.elapsed_milliseconds());
    }

    // === Section 5: Allocators / 分配器 (arena / slab / layered) (1M/百万) ===
    print_section(5, "Allocators / 分配器 (arena/slab/layered) (1M/百万)");
    {
        const size_t alloc_count = 1000000;

        print_perf_sub("5.1 arena_allocator (1M/百万)");
        t.reset();
        arena_allocator ar(16 * 1024 * 1024);
        for (size_t i = 0; i < alloc_count; ++i)
        {
            void* p = ar.allocate(16);
            if (!p)
            {
                ar.reset();
                p = ar.allocate(16);
            }
            lcf_sink(p);
        }
        print_perf("arena allocate(16)", alloc_count, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            ar.reset();
        }
        print_perf("arena reset", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(ar.used(), ar.capacity(), ar.remaining(), ar.empty());
        }
        print_perf("arena state queries", 1000000 * 4, t.elapsed_milliseconds());

        void* q = ar.allocate(32);
        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(ar.owns(q));
        }
        print_perf("arena owns", 1000000, t.elapsed_milliseconds());

        print_perf_sub("5.2 slab_allocator (1M/百万)");
        t.reset();
        slab_allocator sl(64);
        dense<void*> sptrs;
        sptrs.increase_capacity(alloc_count);
        for (size_t i = 0; i < alloc_count; ++i)
        {
            void* sp = sl.allocate();
            if (!sp)
            {
                for (void* sq : sptrs)
                {
                    sl.deallocate(sq);
                }
                sptrs.clear();
                sp = sl.allocate();
            }
            sptrs.emplace_back(sp);
        }
        print_perf("slab allocate(64)", alloc_count, t.elapsed_milliseconds());

        t.reset();
        for (void* sp : sptrs)
        {
            sl.deallocate(sp);
        }
        print_perf("slab deallocate", alloc_count, t.elapsed_milliseconds());

        void* test_p = sl.allocate();
        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(sl.owns(test_p));
        }
        print_perf("slab owns", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(sl.block_size(), sl.total_blocks(), sl.free_blocks(), sl.empty());
        }
        print_perf("slab state queries", 1000000 * 4, t.elapsed_milliseconds());

        print_perf_sub("5.3 layered_allocator (1M/百万)");
        t.reset();
        layered_allocator la;
        dense<void*> small_ptrs;
        small_ptrs.increase_capacity(alloc_count);
        for (size_t i = 0; i < alloc_count; ++i)
        {
            void* lp = la.allocate(64);
            if (!lp)
            {
                for (void* lq : small_ptrs)
                {
                    la.deallocate(lq);
                }
                small_ptrs.clear();
                lp = la.allocate(64);
            }
            small_ptrs.emplace_back(lp);
        }
        print_perf("layered allocate(64) slab", alloc_count, t.elapsed_milliseconds());

        t.reset();
        for (void* lp : small_ptrs)
        {
            la.deallocate(lp);
        }
        print_perf("layered deallocate(slab)", alloc_count, t.elapsed_milliseconds());

        t.reset();
        dense<int*> liptrs;
        liptrs.increase_capacity(alloc_count);
        for (size_t i = 0; i < alloc_count; ++i)
        {
            liptrs.emplace_back(la.construct<int>(static_cast<int>(i)));
        }
        print_perf("layered construct<int>", alloc_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < alloc_count; ++i)
        {
            la.destroy(liptrs[i]);
        }
        print_perf("layered destroy<int>", alloc_count, t.elapsed_milliseconds());

        void* lo_p = la.allocate(48);
        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(la.owns(lo_p));
        }
        print_perf("layered owns", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(la.slab_max());
        }
        print_perf("layered slab_max", 1000000, t.elapsed_milliseconds());
    }

    // === Section 6: operating_message / 操作消息 (1M/百万) ===
    print_section(6, "operating_message / 操作消息 (1M/百万)");
    {
        const size_t om_count = 1000000;

        print_perf_sub("6.1 write_message 系列 (1M/百万)");
        t.reset();
        operating_message om1;
        for (size_t i = 0; i < om_count; ++i)
        {
            om1.reset();
            om1.write_message(true, "msg", i);
        }
        lcf_sink(om1.message_size());
        print_perf("write_message", om_count, t.elapsed_milliseconds());

        t.reset();
        operating_message om2;
        for (size_t i = 0; i < om_count; ++i)
        {
            om2.reset();
            om2.write_message_fmt(true, "fmt: {} + {}", i, i + 1);
        }
        lcf_sink(om2.message_size());
        print_perf("write_message_fmt", om_count, t.elapsed_milliseconds());

        t.reset();
        operating_message om_lv;
        om_lv.set_min_level(msg_level::debug);
        for (size_t i = 0; i < om_count; ++i)
        {
            om_lv.reset();
            om_lv.write_message_level(msg_level::info, true, "msg", i);
        }
        lcf_sink(om_lv.message_size());
        print_perf("write_message_level", om_count, t.elapsed_milliseconds());

        t.reset();
        operating_message om_lv2;
        om_lv2.set_min_level(msg_level::debug);
        for (size_t i = 0; i < om_count; ++i)
        {
            om_lv2.reset();
            om_lv2.write_message_fmt_level(msg_level::warn, true, "v={}", i);
        }
        lcf_sink(om_lv2.message_size());
        print_perf("write_message_fmt_level", om_count, t.elapsed_milliseconds());

        // 等级过滤快速路径
        t.reset();
        operating_message om_f;
        om_f.set_min_level(msg_level::error);
        for (size_t i = 0; i < om_count; ++i)
        {
            om_f.reset();
            om_f.write_message_level(msg_level::debug, true, "filtered", i);
        }
        lcf_sink(om_f.message_size());
        print_perf("level filter fast path", om_count, t.elapsed_milliseconds());

        t.reset();
        operating_message om_mix;
        for (size_t i = 0; i < om_count; ++i)
        {
            om_mix.reset();
            om_mix.write_message(true, "i=", i, " d=", 3.14, " s=", std::string_view("x"));
        }
        lcf_sink(om_mix.message_size());
        print_perf("write_message mixed", om_count, t.elapsed_milliseconds());

        print_perf_sub("6.2 operator+= 与状态查询 (1M/百万)");
        t.reset();
        for (size_t i = 0; i < om_count; ++i)
        {
            operating_message om3;
            om3 += "hello";
            om3 += " world";
            lcf_sink(om3.message_size());
        }
        print_perf("operator+=(str)", om_count * 2, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < om_count; ++i)
        {
            operating_message om4, om5;
            om5.write_message(true, "src");
            om4 += std::move(om5);
            lcf_sink(om4.message_size());
        }
        print_perf("operator+=(om&&)", om_count, t.elapsed_milliseconds());

        t.reset();
        operating_message om6;
        om6.reserve(4096);
        for (size_t i = 0; i < om_count; ++i)
        {
            om6.reset();
            om6.set_switch_bool(false);
            lcf_sink(om6.get_switch_bool());
            om6.clear_message();
        }
        print_perf("reset/clear/switch", om_count, t.elapsed_milliseconds());

        t.reset();
        om6.reset();
        om6.write_message(true, "test");
        for (size_t i = 0; i < om_count; ++i)
        {
            lcf_sink((bool)om6);
            auto sv = om6.read_message();
            (void)sv;
        }
        print_perf("read/bool", om_count, t.elapsed_milliseconds());
    }

    // === Section 7: id_allocation / ID 分配 (1M/百万) ===
    print_section(7, "id_allocation / ID 分配 (1M/百万)");
    {
        const size_t id_count = 1000000;

        print_perf_sub("7.1 分配与回收 (1M/百万)");
        t.reset();
        id_allocation<int> ida;
        for (size_t i = 0; i < id_count; ++i)
        {
            lcf_sink(ida.get_id());
        }
        print_perf("get_id", id_count, t.elapsed_milliseconds());

        t.reset();
        dense<int> ids;
        ids.increase_capacity(id_count);
        for (size_t i = 0; i < id_count; ++i)
        {
            ids.emplace_back(ida.get_id());
        }
        for (size_t i = 0; i < id_count; ++i)
        {
            ida.free_id(ids[i]);
        }
        print_perf("free_id", id_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < id_count; ++i)
        {
            lcf_sink(ida.get_id());
        }
        print_perf("recycle get_id", id_count, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(ida.total_number_of_ids(), ida.maximum_id());
        }
        print_perf("total/maximum", 1000000, t.elapsed_milliseconds());
    }

    // === Section 8: single_class_set / 组件集合 (1M/百万) ===
    print_section(8, "single_class_set / 组件集合 (1M/百万)");
    {
        const size_t scs_count = 1000000;

        print_perf_sub("8.1 add / add_batch (1M/百万)");
        single_class_set scs;
        scs.increase_capacity(scs_count);
        dense<entity> ents;
        ents.increase_capacity(scs_count);
        for (size_t i = 0; i < scs_count; ++i)
        {
            ents.emplace_back(entity(static_cast<uint32_t>(i), 1));
            scs.add(ents[i], Position{static_cast<float>(i), 0, 0});
        }

        t.reset();
        single_class_set scs_b;
        scs_b.increase_capacity(scs_count);
        dense<entity> e_arr;
        dense<Position> p_arr;
        e_arr.increase_capacity(scs_count / 10);
        p_arr.increase_capacity(scs_count / 10);
        for (size_t i = 0; i < scs_count / 10; ++i)
        {
            e_arr.emplace_back(entity(static_cast<uint32_t>(i), 1));
            p_arr.emplace_back(Position{static_cast<float>(i), 0, 0});
        }
        scs_b.add_batch(std::span<const entity>(e_arr.data(), e_arr.size()),
                        std::span<const Position>(p_arr.data(), p_arr.size()));
        print_perf("add_batch(span)", scs_count / 10, t.elapsed_milliseconds());

        t.reset();
        single_class_set scs_b2;
        scs_b2.increase_capacity(scs_count);
        dense<entity> e_pool;
        dense<Position> p_pool;
        e_pool.increase_capacity(scs_count / 10);
        p_pool.increase_capacity(scs_count / 10);
        for (size_t i = 0; i < scs_count / 10; ++i)
        {
            e_pool.emplace_back(entity(static_cast<uint32_t>(i), 1));
            p_pool.emplace_back(Position{static_cast<float>(i), 0, 0});
        }
        scs_b2.add_batch(std::span<const entity>(e_pool.data(), e_pool.size()),
                         std::span<const Position>(p_pool.data(), p_pool.size()));
        print_perf("add_batch(rvalue)", scs_count / 10, t.elapsed_milliseconds());

        print_perf_sub("8.2 查询接口 (1M/百万)");
        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            lcf_sink(scs.get_version(static_cast<uint32_t>(i)));
        }
        print_perf("get_version", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            lcf_sink(scs.get_version_unchecked(static_cast<uint32_t>(i)));
        }
        print_perf("get_version_unchecked", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            lcf_sink(scs.sparse_dense_at(static_cast<uint32_t>(i)));
        }
        print_perf("get_dense_at", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            lcf_sink(scs.sparse_dense_at(static_cast<uint32_t>(i)));
        }
        print_perf("sparse_dense_at", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            lcf_sink(scs.sparse_version_at(static_cast<uint32_t>(i)));
        }
        print_perf("sparse_version_at", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            lcf_sink(scs.contains_entity(ents[i]));
        }
        print_perf("contains_entity", scs_count, t.elapsed_milliseconds());

        print_perf_sub("8.3 get_ptr 与预取 (1M/百万)");
        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            lcf_sink(scs.get_ptr<Position>(ents[i]));
        }
        print_perf("get_ptr<T>", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            lcf_sink(scs.get_ptr_fast<Position>(ents[i]));
        }
        print_perf("get_ptr_fast<T>", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            scs.prefetch_sparse_entry(ents[i].parts_.index_);
        }
        print_perf("prefetch_sparse_entry", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            scs.prefetch_ptr_data<Position>(ents[i]);
        }
        print_perf("prefetch_ptr_data", scs_count, t.elapsed_milliseconds());

        t.reset();
        for (int iter = 0; iter < 100; ++iter)
        {
            scs.prefetch_ptr_batch(ents.data(), 64);
        }
        print_perf("prefetch_ptr_batch", 100 * 64, t.elapsed_milliseconds());

        {
            dense<Position*> results;
            results.increase_capacity(64);
            t.reset();
            for (int iter = 0; iter < 10000; ++iter)
            {
                scs.get_ptr_batch<Position>(ents.data(), results.data(), 64);
            }
            print_perf("get_ptr_batch", 10000 * 64, t.elapsed_milliseconds());
        }

        print_perf_sub("8.4 元数据与状态 (1M/百万)");
        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(scs.get_sparse_size());
        }
        print_perf("sparse_size", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            scs.clear_hot_set();
        }
        print_perf("clear_hot_set", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(scs.get_typed_pool_ptr<Position>(), &scs.get_entity_indices(), scs.get_pool_version());
        }
        print_perf("metadata queries", 1000000 * 3, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(scs.size(), scs.empty(), scs.get_type_id_value());
        }
        print_perf("size/empty/type_id", 1000000 * 3, t.elapsed_milliseconds());

        t.reset();
        single_class_set scs_sr;
        scs_sr.increase_capacity(scs_count);
        dense<entity> ents_sr;
        ents_sr.increase_capacity(scs_count);
        for (size_t i = 0; i < scs_count; ++i)
        {
            ents_sr.emplace_back(entity(static_cast<uint32_t>(i), 1));
            scs_sr.add(ents_sr[i], Position{0, 0, 0});
        }
        for (size_t i = 0; i < scs_count; ++i)
        {
            scs_sr.soft_remove(ents_sr[i]);
        }
        print_perf("soft_remove", scs_count, t.elapsed_milliseconds());
    }

    // === Section 9: radix_sort / 基数排序 (1M/百万) ===
    print_section(9, "radix_sort & tiered_sort / 排序 (1M/百万)");
    {
        const size_t rdx_count = 1000000;
        std::mt19937 rng(42);

        print_perf_sub("9.1 radix_sort_entries (1M/百万)");
        {
            struct entry { int key; size_t index; };
            dense<entry> entries;
            entries.increase_capacity(rdx_count);
            for (size_t i = 0; i < rdx_count; ++i)
            {
                entries.emplace_back(static_cast<int>(rng()), i);
            }
            t.reset();
            radix_sort_entries<int>(entries.data(), rdx_count);
            print_perf("radix_sort_entries<int>", rdx_count, t.elapsed_milliseconds());
        }

        {
            struct entry { float key; size_t index; };
            dense<entry> entries;
            entries.increase_capacity(rdx_count);
            for (size_t i = 0; i < rdx_count; ++i)
            {
                entries.emplace_back(static_cast<float>(rng()), i);
            }
            t.reset();
            radix_sort_entries<float>(entries.data(), rdx_count);
            print_perf("radix_sort_entries<float>", rdx_count, t.elapsed_milliseconds());
        }

        {
            struct entry { uint64_t key; size_t index; };
            dense<entry> entries;
            entries.increase_capacity(rdx_count);
            std::mt19937_64 rng64(456);
            for (size_t i = 0; i < rdx_count; ++i)
            {
                entries.emplace_back(rng64(), i);
            }
            t.reset();
            radix_sort_entries<uint64_t>(entries.data(), rdx_count);
            print_perf("radix_sort_entries<uint64_t>", rdx_count, t.elapsed_milliseconds());
        }

        print_perf_sub("9.2 radix_sort_indices (1M/百万)");
        {
            dense<size_t> indices, temp;
            dense<int> keys;
            indices.increase_capacity(rdx_count);
            temp.increase_capacity(rdx_count);
            keys.increase_capacity(rdx_count);
            for (size_t i = 0; i < rdx_count; ++i)
            {
                indices.emplace_back(i);
                keys.emplace_back(static_cast<int>(rng()));
            }
            t.reset();
            radix_sort_indices<int>(indices.data(), keys.data(), rdx_count, temp.data());
            print_perf("radix_sort_indices<int>", rdx_count, t.elapsed_milliseconds());
        }

        print_perf_sub("9.3 pdqsort / tiered_sort / sort_n (1M/百万)");
        // pdqsort (避免 std::sort + lambda 在 MinGW+AVX2 下崩溃)
        {
            dense<int> data;
            data.increase_capacity(rdx_count);
            for (size_t i = 0; i < rdx_count; ++i)
            {
                data.emplace_back(static_cast<int>(rng()));
            }
            t.reset();
            pdqsort<int>(data.data(), rdx_count, [](const int& a, const int& b) noexcept { return a < b; });
            print_perf("pdqsort<int>", rdx_count, t.elapsed_milliseconds());

            t.reset();
            tiered_sort<int>(data.data(), rdx_count, [](const int& a, const int& b) noexcept { return a < b; });
            print_perf("tiered_sort<int>", rdx_count, t.elapsed_milliseconds());
        }

        // sort_n 排序网络 (小规模批量)
        {
            constexpr size_t small_n = 1000000;
            int* v5 = static_cast<int*>(::operator new(small_n * 5 * sizeof(int)));
            int* v16 = static_cast<int*>(::operator new(small_n * 16 * sizeof(int)));
            for (size_t i = 0; i < small_n * 5; ++i)
            {
                v5[i] = static_cast<int>(rng());
            }
            for (size_t i = 0; i < small_n * 16; ++i)
            {
                v16[i] = static_cast<int>(rng());
            }

            t.reset();
            for (size_t i = 0; i < small_n; ++i)
            {
                ::sort_n<5>(&v5[i * 5]);
            }
            print_perf("sort_n<5> network", small_n * 5, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < small_n; ++i)
            {
                ::sort_n<16>(&v16[i * 16]);
            }
            print_perf("sort_n<16> network", small_n * 16, t.elapsed_milliseconds());

            ::operator delete(v5);
            ::operator delete(v16);
        }

        t.reset();
        for (size_t i = 0; i < rdx_count; ++i)
        {
            lcf_sink(radix_key(static_cast<int>(i)));
        }
        print_perf("radix_key<int>", rdx_count, t.elapsed_milliseconds());
    }

    // === Section 10: Manager / 管理器核心接口 (1M/百万) ===
    print_section(10, "Manager Core / 管理器核心接口 (1M/百万)");
    {
        const size_t op_count = 1000000;

        print_perf_sub("10.1 实体与组件操作 (1M/百万)");
        {
            ecs::manager mgr3;
            mgr3.disable_track_changes();
            mgr3.disable_comp_signals();
            mgr3.append_preallocated_entities(op_count * 2);
            dense<entity> op_ents;
            op_ents.increase_capacity(op_count);

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                op_ents.emplace_back(mgr3.create_entity());
            }
            print_perf("create_entity", op_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr3.add(op_ents[i], Position{1.0f, 0.0f, 0.0f});
            }
            print_perf("add<T>", op_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr3.hard_remove<Position>(op_ents[i]);
            }
            print_perf("hard_remove<T>", op_count, t.elapsed_milliseconds());

            for (size_t i = 0; i < op_count; ++i)
            {
                mgr3.add(op_ents[i], Velocity{1.0f, 0.0f, 0.0f});
            }
            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr3.soft_remove<Velocity>(op_ents[i]);
            }
            print_perf("soft_remove<T>", op_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < op_count / 2; ++i)
            {
                mgr3.delete_entity(op_ents[i]);
            }
            print_perf("delete_entity", op_count / 2, t.elapsed_milliseconds());

            // 链式 addc / hard_removec / soft_removec
            ecs::manager mgr_ch;
            mgr_ch.append_preallocated_entities(op_count);
            dense<entity> ents_ch;
            ents_ch.increase_capacity(op_count);
            for (size_t i = 0; i < op_count; ++i)
            {
                ents_ch.emplace_back(mgr_ch.create_entity());
            }

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr_ch.addc(ents_ch[i], Position{1.0f, 0, 0});
            }
            print_perf("addc chain", op_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr_ch.hard_removec<Position>(ents_ch[i]);
            }
            print_perf("hard_removec chain", op_count, t.elapsed_milliseconds());

            // add(T, e) 反向参数
            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr_ch.add(Velocity{1.0f, 0, 0}, ents_ch[i]);
            }
            print_perf("add(T,e) reverse", op_count, t.elapsed_milliseconds());
        }

        print_perf_sub("10.2 单点查询 (1M/百万)");
        {
            std::uniform_int_distribution<size_t> idx_dist(0, N - 1);
            std::mt19937 qgen(7);

            dense<entity> query_ents;
            query_ents.reserve_exact(op_count);
            for (size_t i = 0; i < op_count; ++i)
            {
                query_ents[i] = entities[idx_dist(qgen)];
            }

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                auto* p = ecss.get_ptr<Position>(query_ents[i]);
                lcf_sink(p != nullptr);
            }
            print_perf("get_ptr<T>", op_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                auto* p = ecss.get_ptr_fast<Position>(query_ents[i]);
                lcf_sink(p != nullptr);
            }
            print_perf("get_ptr_fast<T>", op_count, t.elapsed_milliseconds());

            // 双级预取查询
            dense<entity> pf_ents;
            pf_ents.reserve_exact(op_count);
            for (size_t i = 0; i < op_count; ++i)
            {
                pf_ents[i] = entities[idx_dist(qgen)];
            }
            constexpr size_t sparse_dist = 16;
            constexpr size_t data_dist = 8;
            for (size_t i = 0; i < sparse_dist; ++i)
            {
                ecss.prefetch_ptr<Position>(pf_ents[i]);
            }
            for (size_t i = 0; i < data_dist; ++i)
            {
                ecss.prefetch_ptr_data<Position>(pf_ents[i]);
            }

            t.reset();
            size_t pf_hit = 0;
            for (size_t i = 0; i < op_count; ++i)
            {
                if (i + sparse_dist < op_count)
                {
                    ecss.prefetch_ptr<Position>(pf_ents[i + sparse_dist]);
                }
                if (i + data_dist < op_count)
                {
                    ecss.prefetch_ptr_data<Position>(pf_ents[i + data_dist]);
                }
                auto* p = ecss.get_ptr<Position>(pf_ents[i]);
                if (p)
                {
                    ++pf_hit;
                    lcf_sink(p->x);
                }
            }
            print_perf("prefetch+get_ptr", pf_hit, t.elapsed_milliseconds());

            // cached 双级预取
            dense<entity> cache_ents;
            cache_ents.reserve_exact(op_count);
            for (size_t i = 0; i < op_count; ++i)
            {
                cache_ents[i] = entities[idx_dist(qgen)];
            }
            auto* set = ecss.get_single_class_set<Position>();
            t.reset();
            size_t cached_hit = 0;
            for (size_t i = 0; i < op_count; ++i)
            {
                if (i + sparse_dist < op_count)
                {
                    ecss.prefetch_ptr_cached<Position>(set, cache_ents[i + sparse_dist]);
                }
                if (i + data_dist < op_count)
                {
                    ecss.prefetch_ptr_data_cached<Position>(set, cache_ents[i + data_dist]);
                }
                auto* p = ecss.get_ptr_fast_cached<Position>(set, cache_ents[i]);
                if (p)
                {
                    ++cached_hit;
                    lcf_sink(p->x);
                }
            }
            print_perf("cached+prefetch", cached_hit, t.elapsed_milliseconds());

            // query_context 双级预取
            dense<entity> ctx_ents;
            ctx_ents.reserve_exact(op_count);
            for (size_t i = 0; i < op_count; ++i)
            {
                ctx_ents[i] = entities[idx_dist(qgen)];
            }
            t.reset();
            size_t ctx_hit = 0;
            {
                ecs::query_context<Position> ctx(ecss);
                for (size_t i = 0; i < op_count; ++i)
                {
                    if (i + sparse_dist < op_count)
                    {
                        ctx.prefetch_sparse(ctx_ents[i + sparse_dist]);
                    }
                    if (i + data_dist < op_count)
                    {
                        ctx.prefetch_data(ctx_ents[i + data_dist]);
                    }
                    auto* p = ctx.get_ptr(ctx_ents[i]);
                    if (p)
                    {
                        ++ctx_hit;
                        lcf_sink(p->x);
                    }
                }
            }
            print_perf("query_context+prefetch", ctx_hit, t.elapsed_milliseconds());

            {
                dense<Position*> results;
                results.reserve_exact(op_count);
                t.reset();
                ecss.get_ptr_batch<Position>(query_ents.data(), results.data(), op_count);
                print_perf("get_ptr_batch", op_count, t.elapsed_milliseconds());
            }

            t.reset();
            for (int iter = 0; iter < 100; ++iter)
            {
                ecss.prefetch_ptr_batch<Position>(query_ents.data(), 64);
            }
            print_perf("prefetch_ptr_batch", 100 * 64, t.elapsed_milliseconds());
        }

        print_perf_sub("10.3 元数据与状态查询 (1M/百万)");
        {
            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                lcf_sink(ecss.is_entity_valid(entities[i]));
            }
            print_perf("is_entity_valid", N, t.elapsed_milliseconds());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                lcf_sink(ecss.get_component_bit<Position>());
            }
            print_perf("get_component_bit", 1000000, t.elapsed_milliseconds());

            t.reset();
            int pid = type_id::get_type_id<Position>();
            for (int i = 0; i < 1000000; ++i)
            {
                lcf_sink(ecss.get_component_meta(pid));
            }
            print_perf("get_component_meta", 1000000, t.elapsed_milliseconds());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                lcf_sink(ecss.get_single_class_set<Position>());
            }
            print_perf("get_single_class_set", 1000000, t.elapsed_milliseconds());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                lcf_sink(ecss.get_single_class_set_by_id(pid));
            }
            print_perf("get_single_class_set_by_id", 1000000, t.elapsed_milliseconds());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                lcf_sink(ecss.get_component_container<Position>());
            }
            print_perf("get_component_container", 1000000, t.elapsed_milliseconds());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                auto& emr = ecss.get_entity_manager();
                (void)emr;
            }
            print_perf("get_entity_manager", 1000000, t.elapsed_milliseconds());
        }

        print_perf_sub("10.4 排序接口 (1M/百万)");
        {
            constexpr size_t sort_n_ = 1000000;
            ecs::manager sort_mgr;
            sort_mgr.disable_track_changes();
            sort_mgr.disable_comp_signals();
            sort_mgr.append_preallocated_entities(sort_n_);
            dense<entity> sort_ents;
            sort_ents.increase_capacity(sort_n_);
            std::mt19937 srng(99);
            std::uniform_real_distribution<float> sdist(0, 10000);
            for (size_t i = 0; i < sort_n_; ++i)
            {
                sort_ents.emplace_back(sort_mgr.create_entity());
                sort_mgr.add(sort_ents[i], Position{sdist(srng), 0, 0});
                sort_mgr.add(sort_ents[i], Velocity{sdist(srng) * 0.1f, 0, 0});
            }

            t.reset();
            sort_mgr.sort_entities_by_component<Position>(
                [](const Position& a, const Position& b) noexcept { return a.x < b.x; });
            print_perf("sort_entities_by_component", sort_n_, t.elapsed_milliseconds());

            t.reset();
            sort_mgr.reorder_by_component<Position, Velocity>(
                [](const Velocity& a, const Velocity& b) noexcept { return a.vx < b.vx; });
            print_perf("reorder_by_component", sort_n_, t.elapsed_milliseconds());

            t.reset();
            sort_mgr.delete_type_container<Velocity>();
            print_perf("delete_type_container", sort_n_, t.elapsed_milliseconds());
        }

        print_perf_sub("10.5 生命周期信号 (1M/百万)");
        {
            const size_t sig_count = 1000000;

            // 即时信号 entity_created / destroyed
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(sig_count * 2);
                size_t created = 0, destroyed = 0;
                mgr.set_on_entity_created([](entity, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &created);
                mgr.set_on_entity_destroyed([](entity, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &destroyed);

                dense<entity> ents;
                ents.increase_capacity(sig_count);
                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                {
                    ents.emplace_back(mgr.create_entity());
                }
                print_perf("signal entity_created", sig_count, t.elapsed_milliseconds());

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                {
                    mgr.delete_entity(ents[i]);
                }
                print_perf("signal entity_destroyed", sig_count, t.elapsed_milliseconds());
            }

            // 即时信号 on_add / on_remove
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(sig_count);
                size_t added = 0, removed = 0;
                mgr.set_on_add<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &added);
                mgr.set_on_remove<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &removed);

                dense<entity> ents;
                ents.increase_capacity(sig_count);
                for (size_t i = 0; i < sig_count; ++i)
                {
                    ents.emplace_back(mgr.create_entity());
                }

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                {
                    mgr.add(ents[i], Position{1.0f, 0, 0});
                }
                print_perf("signal on_add<Position>", sig_count, t.elapsed_milliseconds());

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                {
                    mgr.hard_remove<Position>(ents[i]);
                }
                print_perf("signal on_remove<Position>", sig_count, t.elapsed_milliseconds());
            }

            // on_modify 覆盖写
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(sig_count);
                size_t modify_cnt = 0;
                mgr.set_on_modify<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &modify_cnt);

                dense<entity> ents;
                ents.increase_capacity(sig_count);
                for (size_t i = 0; i < sig_count; ++i)
                {
                    ents.emplace_back(mgr.create_entity());
                }

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                {
                    mgr.add(ents[i], Position{1.0f, 0, 0});
                    mgr.add(ents[i], Position{2.0f, 0, 0});
                }
                print_perf("signal on_modify overwrite", sig_count, t.elapsed_milliseconds());
            }

            // 延迟信号 flush
            {
                ecs::manager mgr;
                mgr.disable_comp_signals();
                mgr.append_preallocated_entities(sig_count);
                dense<entity> ents;
                ents.increase_capacity(sig_count);
                for (size_t i = 0; i < sig_count; ++i)
                {
                    ents.emplace_back(mgr.create_entity());
                }
                for (size_t i = 0; i < sig_count / 2; ++i)
                {
                    mgr.delete_entity(ents[i]);
                }

                t.reset();
                mgr.flush_entity_signals([&](uint32_t, uint32_t) noexcept {});
                print_perf("flush_entity_signals", sig_count, t.elapsed_milliseconds());
            }

            // 信号开关 / 容量
            {
                ecs::manager mgr_chk;
                t.reset();
                for (int i = 0; i < 1000000; ++i)
                {
                    lcf_sink_all(mgr_chk.has_pending_entity_signals(),
                                 mgr_chk.has_pending_component_signals());
                }
                print_perf("has_pending_signals", 1000000 * 2, t.elapsed_milliseconds());

                t.reset();
                mgr_chk.reserve_entity_signal_capacity(2048);
                mgr_chk.reserve_comp_signal_capacity(2048);
                print_perf("reserve_signal_capacity", 2, t.elapsed_milliseconds());

                t.reset();
                for (int i = 0; i < 1000000; ++i)
                {
                    lcf_sink_all(mgr_chk.entity_signal_overflow_count(),
                                 mgr_chk.comp_signal_overflow_count());
                }
                print_perf("overflow_count query", 1000000 * 2, t.elapsed_milliseconds());

                t.reset();
                for (int i = 0; i < 1000000; ++i)
                {
                    mgr_chk.disable_comp_signals();
                    mgr_chk.enable_comp_signals();
                    mgr_chk.disable_track_changes();
                    mgr_chk.enable_track_changes();
                }
                print_perf("enable/disable switches", 1000000 * 4, t.elapsed_milliseconds());
            }
        }
    }

    // === Section 11: Views / 视图查询 (1M/百万) ===
    print_section(11, "Views / 视图查询 (1M/百万)");
    {
        const size_t view_count = N;

        print_perf_sub("11.1 single_view 遍历 (1M/百万)");
        t.reset();
        size_t cnt_sv = 0;
        ecss.view<Position>().for_each([&](Position& p) {
            ++cnt_sv;
            lcf_sink(p.x);
        });
        print_perf("single_view for_each", cnt_sv, t.elapsed_milliseconds());

        {
            auto v = ecss.view<Position>();
            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = v.get_first_entity();
            }
            print_perf("single_view get_first_entity", 1000000, t.elapsed_milliseconds());

            t.reset();
            entity le{};
            for (int i = 0; i < 1000000; ++i)
            {
                le = v.get_last_entity();
            }
            print_perf("single_view get_last_entity", 1000000, t.elapsed_milliseconds());

            t.reset();
            entity ee{};
            for (size_t i = 0; i < view_count; ++i)
            {
                ee = v.get_entity_at_index(i);
            }
            print_perf("single_view get_entity_at_index", view_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < view_count; ++i)
            {
                lcf_sink(v.get_component_at_index(i));
            }
            print_perf("single_view get_component_at_index", view_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < view_count; ++i)
            {
                lcf_sink(v.get_component_for_entity(entities[i]));
            }
            print_perf("single_view get_component_for_entity", view_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < view_count; ++i)
            {
                lcf_sink(v.contains(entities[i]));
            }
            print_perf("single_view contains", view_count, t.elapsed_milliseconds());

            t.reset();
            for (auto it = v.component_begin(); it != v.component_end(); ++it)
            {
                lcf_sink(it->x);
            }
            print_perf("single_view component_begin/end", view_count, t.elapsed_milliseconds());
        }

        print_perf_sub("11.2 multi_view 多组件查询 (1~10 组件, 1M/百万)");
        // 组件按分布从大到小递增加入, 匹配数随组件数递减:
        //   Pos(1M) Hp(1M) Vel(500K) Dmg(500K) Arm(500K) Rot(500K)
        //   Spd(250K) Scl(250K) Name(100K) Mass(100K)
        size_t cnt_1  = bench_multi_view<Position>(ecss, "multi_view 1 comp");
        size_t cnt_2  = bench_multi_view<Position, Health>(ecss, "multi_view 2 comps");
        size_t cnt_3  = bench_multi_view<Position, Health, Velocity>(ecss, "multi_view 3 comps");
        size_t cnt_4  = bench_multi_view<Position, Health, Velocity, Damage>(ecss, "multi_view 4 comps");
        size_t cnt_5  = bench_multi_view<Position, Health, Velocity, Damage, Armor>(ecss, "multi_view 5 comps");
        size_t cnt_6  = bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation>(ecss, "multi_view 6 comps");
        size_t cnt_7  = bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation, Speed>(ecss, "multi_view 7 comps");
        size_t cnt_8  = bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation, Speed, Scale>(ecss, "multi_view 8 comps");
        size_t cnt_9  = bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation, Speed, Scale, Name>(ecss, "multi_view 9 comps");
        size_t cnt_10 = bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation, Speed, Scale, Name, Mass>(ecss, "multi_view 10 comps");

        // 带 entity (2 组件对照)
        size_t cnt_ent = bench_multi_view_ent<Position, Velocity>(ecss, "multi_view 2 comps +entity");

        // 11.2b: 相同实体数下 1-10 组件单次查询延迟对比 / Single-query latency scaling (equal entity count)
        // 每个实体都拥有全部 10 个组件, 消除匹配数差异, 纯测组件数对延迟的影响
        // eq_n=250K: 10 comps × 108B × 250K = 27MB < L3(32MB), 全部装入 L3
        //   消除 DRAM 带宽瓶颈, 纯测架构查询延迟
        print_perf_sub("11.2b multi_view 1~10 comps equal-count (250K/二十五万, L3-resident)");
        {
            constexpr size_t eq_n = 250000;
            ecs::manager eq_mgr;
            eq_mgr.disable_track_changes();
            eq_mgr.disable_comp_signals();
            eq_mgr.append_preallocated_entities(eq_n);

            std::mt19937 eq_gen(77);
            std::uniform_real_distribution<float> eq_dist(-100.0f, 100.0f);
            std::uniform_int_distribution<int> eq_idist(1, 100);

            dense<Position> eq_pos;     eq_pos.increase_capacity(eq_n);
            dense<Health>   eq_hp;      eq_hp.increase_capacity(eq_n);
            dense<Velocity> eq_vel;     eq_vel.increase_capacity(eq_n);
            dense<Damage>   eq_dmg;     eq_dmg.increase_capacity(eq_n);
            dense<Armor>    eq_arm;     eq_arm.increase_capacity(eq_n);
            dense<Rotation> eq_rot;     eq_rot.increase_capacity(eq_n);
            dense<Speed>    eq_spd;     eq_spd.increase_capacity(eq_n);
            dense<Scale>    eq_scl;     eq_scl.increase_capacity(eq_n);
            dense<Name>     eq_name;    eq_name.increase_capacity(eq_n);
            dense<Mass>     eq_mass;    eq_mass.increase_capacity(eq_n);
            dense<entity>   eq_ents;    eq_ents.increase_capacity(eq_n);

            for (size_t i = 0; i < eq_n; ++i)
            {
                eq_ents.emplace_back(eq_mgr.create_entity());
                eq_pos.emplace_back(eq_dist(eq_gen), eq_dist(eq_gen), eq_dist(eq_gen));
                eq_hp.emplace_back(eq_idist(eq_gen), 100);
                eq_vel.emplace_back(eq_dist(eq_gen), eq_dist(eq_gen), eq_dist(eq_gen));
                eq_dmg.emplace_back(eq_idist(eq_gen));
                eq_arm.emplace_back(eq_idist(eq_gen));
                eq_rot.emplace_back(eq_dist(eq_gen), eq_dist(eq_gen), eq_dist(eq_gen), 1.0f);
                eq_spd.emplace_back(eq_dist(eq_gen));
                eq_scl.emplace_back(eq_dist(eq_gen), eq_dist(eq_gen), eq_dist(eq_gen));
                eq_name.emplace_back("E" + std::to_string(i));
                eq_mass.emplace_back(eq_dist(eq_gen) * 10.0f);
            }

            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Position>(eq_pos.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Health>(eq_hp.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Velocity>(eq_vel.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Damage>(eq_dmg.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Armor>(eq_arm.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Rotation>(eq_rot.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Speed>(eq_spd.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Scale>(eq_scl.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Name>(eq_name.data(), eq_n));
            eq_mgr.add_batch(std::span<const entity>(eq_ents.data(), eq_n),
                             std::span<const Mass>(eq_mass.data(), eq_n));

            bench_multi_view<Position>(eq_mgr, "eq 1 comp");
            bench_multi_view<Position, Health>(eq_mgr, "eq 2 comps");
            bench_multi_view<Position, Health, Velocity>(eq_mgr, "eq 3 comps");
            bench_multi_view<Position, Health, Velocity, Damage>(eq_mgr, "eq 4 comps");
            bench_multi_view<Position, Health, Velocity, Damage, Armor>(eq_mgr, "eq 5 comps");
            bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation>(eq_mgr, "eq 6 comps");
            bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation, Speed>(eq_mgr, "eq 7 comps");
            bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation, Speed, Scale>(eq_mgr, "eq 8 comps");
            bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation, Speed, Scale, Name>(eq_mgr, "eq 9 comps");
            bench_multi_view<Position, Health, Velocity, Damage, Armor, Rotation, Speed, Scale, Name, Mass>(eq_mgr, "eq 10 comps");
        }

        {
            auto v_opt = ecss.view<Position, Velocity>().include_optional_component<Health>();
            t.reset();
            size_t cnt = 0;
            v_opt.for_each([&](entity, Position&, Velocity&, Health* h) {
                if (h)
                {
                    cnt = cnt + 1;
                }
            });
            lcf_sink(cnt);
            print_perf("include_optional_component", view_count, t.elapsed_milliseconds());
        }

        {
            auto v2 = ecss.view<Position, Velocity>();
            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = v2.get_first_entity();
            }
            print_perf("multi_view get_first_entity", 1000000, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < view_count; ++i)
            {
                lcf_sink(v2.get_component_for_entity<Position>(entities[i]));
            }
            print_perf("multi_view get_component_for_entity", view_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < view_count; ++i)
            {
                lcf_sink(v2.contains(entities[i]));
            }
            print_perf("multi_view contains", view_count, t.elapsed_milliseconds());
        }

        print_perf_sub("11.3 page / track_changes / sorted (500K/五十万)");
        t.reset();
        size_t cnt_page = 0;
        {
            auto mv = ecss.view<Position, Velocity>();
            mv.page(0, mv.size()).for_each([&](Position&, Velocity&) { ++cnt_page; });
        }
        print_perf("multi_view page", cnt_page, t.elapsed_milliseconds());

        t.reset();
        size_t cnt_changed = 0;
        {
            auto mv = ecss.view<Position, Velocity>();
            auto cv = mv.track_changes();
            cv.for_each([&](Position&, Velocity&) { ++cnt_changed; });
        }
        print_perf("track_changes", cnt_changed, t.elapsed_milliseconds());

        {
            constexpr size_t sort_n = 1000000;
            ecs::manager sort_mgr;
            sort_mgr.disable_track_changes();
            sort_mgr.disable_comp_signals();
            sort_mgr.append_preallocated_entities(sort_n);
            std::mt19937 srng(123);
            std::uniform_real_distribution<float> sdist(0, 1000);
            for (size_t i = 0; i < sort_n; ++i)
            {
                auto e = sort_mgr.create_entity();
                sort_mgr.add(e, Position{sdist(srng), sdist(srng), 0});
                sort_mgr.add(e, Velocity{sdist(srng) * 0.1f, 0, 0});
            }

            t.reset();
            size_t cnt_sorted = 0;
            {
                auto mv = sort_mgr.view<Position, Velocity>();
                auto sv = mv.sorted_by_component<Position>(
                    [](const Position& a, const Position& b) noexcept { return a.x < b.x; });
                sv.for_each([&](Position&, Velocity&) { ++cnt_sorted; });
            }
            print_perf("multi_view sorted_by_component", cnt_sorted, t.elapsed_milliseconds());

            t.reset();
            size_t cnt_grp = 0;
            {
                auto sv = sort_mgr.view<Position>();
                auto gv = sv.sorted_by_component_value(
                    [](Position& p) -> int { return static_cast<int>(p.x) / 10; });
                gv.for_each([&](Position&) { ++cnt_grp; });
            }
            print_perf("sorted_by_component_value", cnt_grp, t.elapsed_milliseconds());
        }

        print_perf_sub("11.4 filter_changed / filter_added / exactly_one / find_one");
        {
            auto cv = ecss.view<Position>().filter_changed();
            t.reset();
            size_t cnt = 0;
            cv.for_each([&](Position&) { cnt = cnt + 1; });
            lcf_sink(cnt);
            print_perf("single_view filter_changed first", view_count, t.elapsed_milliseconds());

            // 修改部分组件后增量
            for (size_t i = 0; i < view_count; i += 10)
            {
                ecss.add(entities[i], Position{999.0f, 0, 0});
            }
            t.reset();
            cnt = 0;
            cv.for_each([&](Position&) { cnt = cnt + 1; });
            lcf_sink(cnt);
            print_perf("single_view filter_changed delta", view_count / 10, t.elapsed_milliseconds());
            cv.reset_tracking();

            auto mcv = ecss.view<Position, Velocity>().filter_changed<Position>();
            t.reset();
            size_t mcnt = 0;
            mcv.for_each([&](Position&, Velocity&) { mcnt = mcnt + 1; });
            lcf_sink(mcnt);
            print_perf("multi_view filter_changed", view_count, t.elapsed_milliseconds());

            ecs::manager mgr_fa;
            mgr_fa.disable_track_changes();
            mgr_fa.disable_comp_signals();
            mgr_fa.append_preallocated_entities(view_count);
            dense<entity> ents_fa;
            ents_fa.increase_capacity(view_count);
            auto av = mgr_fa.view<Position>().filter_added();
            for (size_t i = 0; i < view_count; ++i)
            {
                ents_fa.emplace_back(mgr_fa.create_entity());
                mgr_fa.add(ents_fa[i], Position{static_cast<float>(i), 0, 0});
            }
            t.reset();
            size_t acnt = 0;
            av.for_each([&](Position&) { acnt = acnt + 1; });
            lcf_sink(acnt);
            print_perf("single_view filter_added", view_count, t.elapsed_milliseconds());
        }

        {
            ecs::manager mgr_eo;
            mgr_eo.append_preallocated_entities(10);
            entity e1 = mgr_eo.create_entity();
            mgr_eo.add(e1, Position{42.0f, 0, 0});
            mgr_eo.add(e1, Velocity{1.0f, 0, 0});

            auto v_eo = mgr_eo.view<Position, Velocity>();
            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                auto [p, v] = v_eo.exactly_one();
                lcf_sink(p.x);
            }
            print_perf("multi_view exactly_one", 1000000, t.elapsed_milliseconds());

            auto v_eo2 = mgr_eo.view<Position>();
            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                auto& p = v_eo2.exactly_one();
                lcf_sink(p.x);
            }
            print_perf("single_view exactly_one", 1000000, t.elapsed_milliseconds());

            auto v_fo = ecss.view<Position, Velocity>();
            t.reset();
            for (size_t i = 0; i < view_count; ++i)
            {
                auto [p, v] = v_fo.find_one(entities[i]);
                lcf_sink(p);
            }
            print_perf("multi_view find_one", view_count, t.elapsed_milliseconds());

            dense<entity> targets;
            targets.increase_capacity(view_count / 10);
            for (size_t i = 0; i < view_count; i += 10)
            {
                targets.emplace_back(entities[i]);
            }
            auto v_ie = ecss.view<Position, Velocity>().iter_over_entities(targets);
            t.reset();
            size_t cnt = 0;
            v_ie.for_each([&](Position&, Velocity&) { cnt = cnt + 1; });
            lcf_sink(cnt);
            print_perf("iter_over_entities", targets.size(), t.elapsed_milliseconds());
        }

        print_perf_sub("11.5 复合视图 without/with/or/any_of (1M/百万)");
        t.reset();
        size_t cnt_excl = 0;
        ecss.view<Position>(ecs::without<Velocity>).for_each([&](Position& p) {
            ++cnt_excl;
            (void)p;
        });
        print_perf("without<Velocity>", cnt_excl, t.elapsed_milliseconds());

        t.reset();
        size_t cnt_with = 0;
        ecss.view<Position>(ecs::with<Health>).for_each([&](Position& p, Health* hp) {
            ++cnt_with;
            (void)p;
            (void)hp;
        });
        print_perf("with<Health>", cnt_with, t.elapsed_milliseconds());

        t.reset();
        size_t cnt_or = 0;
        ecss.view_or<Position, Velocity>().for_each([&](entity, Position* p, Velocity* v) {
            ++cnt_or;
            (void)p;
            (void)v;
        });
        print_perf("view_or<Pos,Vel>", cnt_or, t.elapsed_milliseconds());

        t.reset();
        size_t cnt_any = 0;
        ecss.view_any_of<Position, Velocity, Health>().for_each([&](Position* p, Velocity* v, Health* h) {
            ++cnt_any;
            (void)p;
            (void)v;
            (void)h;
        });
        print_perf("view_any_of<3 types>", cnt_any, t.elapsed_milliseconds());

        print_perf_sub("11.6 filter_view / filter_and / filter_or");
        t.reset();
        size_t cnt_filt = 0;
        {
            auto fv = ecss.view_filtered<Position>([](Position& p) noexcept { return p.x > 0.0f; });
            fv.for_each([&](Position&) { ++cnt_filt; });
        }
        print_perf("filter_view", cnt_filt, t.elapsed_milliseconds());

        size_t cnt_fand = 0;
        {
            auto fa = ecss.view_filtered<Position>([](Position& p) noexcept { return p.x > 0.0f; }).and_<Velocity>();
            t.reset();
            constexpr int warmup = 50;
            for (int iter = 0; iter < warmup; ++iter)
            {
                cnt_fand = 0;
                fa.for_each([&](Position&, Velocity&) { ++cnt_fand; });
            }
            double elapsed = t.elapsed_milliseconds() / warmup;
            print_perf("filter_and_view", cnt_fand, elapsed);
        }

        size_t cnt_for = 0;
        {
            auto fo = ecss.view_filtered<Position>([](Position& p) noexcept { return p.x > 0.0f; }).or_<Velocity>();
            t.reset();
            constexpr int warmup = 50;
            for (int iter = 0; iter < warmup; ++iter)
            {
                cnt_for = 0;
                fo.for_each([&](entity, Position* p, Velocity* v) {
                    ++cnt_for;
                    (void)p;
                    (void)v;
                });
            }
            double elapsed = t.elapsed_milliseconds() / warmup;
            print_perf("filter_or_view", cnt_for, elapsed);
        }

        std::cout << "\n  ┌─ 匹配数汇总 / Match Counts\n";
        std::cout << "  │ single_view Position:        " << cnt_sv << "\n";
        std::cout << "  │ multi_view 1 comp:           " << cnt_1 << "\n";
        std::cout << "  │ multi_view 2 comps:          " << cnt_2 << "\n";
        std::cout << "  │ multi_view 3 comps:          " << cnt_3 << "\n";
        std::cout << "  │ multi_view 4 comps:          " << cnt_4 << "\n";
        std::cout << "  │ multi_view 5 comps:          " << cnt_5 << "\n";
        std::cout << "  │ multi_view 6 comps:          " << cnt_6 << "\n";
        std::cout << "  │ multi_view 7 comps:          " << cnt_7 << "\n";
        std::cout << "  │ multi_view 8 comps:          " << cnt_8 << "\n";
        std::cout << "  │ multi_view 9 comps:          " << cnt_9 << "\n";
        std::cout << "  │ multi_view 10 comps:         " << cnt_10 << "\n";
        std::cout << "  │ multi_view 2 comps +entity:  " << cnt_ent << "\n";
        std::cout << "  │ without<Velocity>:           " << cnt_excl << "\n";
        std::cout << "  │ with<Health>:                " << cnt_with << "\n";
        std::cout << "  │ view_or<Pos,Vel>:            " << cnt_or << "\n";
        std::cout << "  │ view_any_of<3 types>:        " << cnt_any << "\n";
        std::cout << "  │ filter_view:                 " << cnt_filt << "\n";
        std::cout << "  │ filter_and_view:             " << cnt_fand << "\n";
        std::cout << "  │ filter_or_view:              " << cnt_for << "\n";
    }

    // === Section 12: Groups / Group 系统 (500K/五十万) ===
    print_section(12, "Groups / Group 系统 (500K/五十万)");
    {
        const size_t grp_count = 500000;
        ecs::manager mgr;
        mgr.disable_track_changes();
        mgr.disable_comp_signals();
        mgr.append_preallocated_entities(grp_count);
        dense<entity> ents;
        ents.increase_capacity(grp_count);
        for (size_t i = 0; i < grp_count; ++i)
        {
            ents.emplace_back(mgr.create_entity());
            mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
            mgr.add(ents[i], Velocity{1.0f, 0, 0});
        }

        print_perf_sub("12.1 Non-Owning Group (500K/五十万)");
        {
            auto g = mgr.group<Position, Velocity>();

            t.reset();
            g.rebuild();
            print_perf("group rebuild", 1, t.elapsed_milliseconds());

            t.reset();
            size_t cnt = 0;
            g.for_each([&](Position& p, Velocity& v) {
                ++cnt;
                lcf_sink(p.x * v.vx);
            });
            print_perf("group for_each", cnt, t.elapsed_milliseconds());

            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = g.front();
            }
            print_perf("group front", 1000000, t.elapsed_milliseconds());

            t.reset();
            entity le{};
            for (int i = 0; i < 1000000; ++i)
            {
                le = g.back();
            }
            print_perf("group back", 1000000, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < grp_count; ++i)
            {
                lcf_sink(g.get<Position>(ents[i]));
            }
            print_perf("group get<T>", grp_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < grp_count; ++i)
            {
                lcf_sink(g.contains(ents[i]));
            }
            print_perf("group contains", grp_count, t.elapsed_milliseconds());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                lcf_sink_all(g.size(), g.empty());
            }
            print_perf("group size/empty", 1000000 * 2, t.elapsed_milliseconds());
        }

        print_perf_sub("12.2 Owning Group (500K/五十万)");
        {
            auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);

            t.reset();
            og.rebuild();
            print_perf("owning_group rebuild", 1, t.elapsed_milliseconds());

            t.reset();
            size_t cnt = 0;
            og.for_each([&](Position& p, Velocity& v) {
                ++cnt;
                lcf_sink(p.x * v.vx);
            });
            print_perf("owning_group for_each", cnt, t.elapsed_milliseconds());

            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = og.front();
            }
            print_perf("owning_group front", 1000000, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < grp_count; ++i)
            {
                lcf_sink(og.get<Position>(ents[i]));
            }
            print_perf("owning_group get<T>", grp_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < grp_count; ++i)
            {
                lcf_sink(og.contains(ents[i]));
            }
            print_perf("owning_group contains", grp_count, t.elapsed_milliseconds());
        }

        print_perf_sub("12.3 Reorder Group (500K/五十万)");
        {
            auto rg = mgr.group<Position, Velocity>(ecs::reorder<Position>);

            t.reset();
            rg.rebuild();
            print_perf("reorder_group rebuild", 1, t.elapsed_milliseconds());

            t.reset();
            size_t cnt = 0;
            rg.for_each([&](Position& p, Velocity& v) {
                ++cnt;
                lcf_sink(p.x * v.vx);
            });
            print_perf("reorder_group for_each", cnt, t.elapsed_milliseconds());

            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = rg.front();
            }
            print_perf("reorder_group front", 1000000, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < grp_count; ++i)
            {
                lcf_sink(rg.get<Position>(ents[i]));
            }
            print_perf("reorder_group get<T>", grp_count, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < grp_count; ++i)
            {
                lcf_sink(rg.contains(ents[i]));
            }
            print_perf("reorder_group contains", grp_count, t.elapsed_milliseconds());

            auto rg2 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
            t.reset();
            rg2.share_with(rg);
            print_perf("reorder_group share_with", 1, t.elapsed_milliseconds());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                lcf_sink(rg2.size());
            }
            print_perf("reorder_group shared size", 1000000, t.elapsed_milliseconds());
        }
    }

    // === Section 13: runtime_view / 运行时视图 (500K/五十万) ===
    print_section(13, "runtime_view / 运行时视图 (500K/五十万)");
    {
        const size_t rv_count = 500000;
        ecs::manager mgr;
        mgr.disable_track_changes();
        mgr.disable_comp_signals();
        mgr.append_preallocated_entities(rv_count);
        dense<entity> ents;
        ents.increase_capacity(rv_count);
        for (size_t i = 0; i < rv_count; ++i)
        {
            ents.emplace_back(mgr.create_entity());
            mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
            mgr.add(ents[i], Velocity{1.0f, 0, 0});
        }

        int pos_id = type_id::get_type_id<Position>();
        int vel_id = type_id::get_type_id<Velocity>();

        print_perf_sub("13.1 创建与基础查询 (500K/五十万)");
        t.reset();
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, vel_id});
        print_perf("runtime_view_create", 1, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(rv.size(), rv.empty());
        }
        print_perf("size/empty", 1000000 * 2, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < rv_count; ++i)
        {
            lcf_sink(rv.contains(ents[i]));
        }
        print_perf("contains", rv_count, t.elapsed_milliseconds());

        t.reset();
        for (size_t i = 0; i < rv_count; ++i)
        {
            lcf_sink(rv.get_ptr<Position>(ents[i]));
        }
        print_perf("get_ptr<T>", rv_count, t.elapsed_milliseconds());

        t.reset();
        entity fe{};
        for (int i = 0; i < 1000000; ++i)
        {
            fe = rv.get_first_entity();
        }
        print_perf("get_first_entity", 1000000, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 10; ++i)
        {
            lcf_sink(rv.count());
        }
        print_perf("count()", 10, t.elapsed_milliseconds());

        print_perf_sub("13.2 for_each 系列 (500K/五十万)");
        t.reset();
        size_t cnt = 0;
        rv.for_each([&](entity) { cnt = cnt + 1; });
        lcf_sink(cnt);
        print_perf("for_each", rv_count, t.elapsed_milliseconds());

        t.reset();
        cnt = 0;
        rv.for_each_typed<Position, Velocity>([&](entity, Position&, Velocity&) { cnt = cnt + 1; });
        lcf_sink(cnt);
        print_perf("for_each_typed", rv_count, t.elapsed_milliseconds());

        t.reset();
        cnt = 0;
        rv.for_each_parallel(0, 2, [&](entity, size_t) { cnt = cnt + 1; });
        rv.for_each_parallel(1, 2, [&](entity) { cnt = cnt + 1; });
        lcf_sink(cnt);
        print_perf("for_each_parallel(2w)", rv_count, t.elapsed_milliseconds());

        t.reset();
        cnt = 0;
        rv.for_each_paged(0, rv_count / 2, [&](entity) { cnt = cnt + 1; });
        lcf_sink(cnt);
        print_perf("for_each_paged", rv_count / 2, t.elapsed_milliseconds());

        t.reset();
        cnt = 0;
        for (auto it = rv.begin(); it != rv.end(); ++it)
        {
            cnt = cnt + 1;
        }
        lcf_sink(cnt);
        print_perf("begin/end iter", rv_count, t.elapsed_milliseconds());

        print_perf_sub("13.3 变更追踪 (500K/五十万)");
        rv.reset_change_tracking();
        for (size_t i = 0; i < rv_count; i += 100)
        {
            mgr.add(ents[i], Position{999.0f, 0, 0});
        }

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink(rv.changed());
        }
        print_perf("changed()", 1000000, t.elapsed_milliseconds());

        t.reset();
        cnt = 0;
        if (rv.changed())
        {
            rv.for_each_changed([&](entity) { cnt = cnt + 1; });
        }
        lcf_sink(cnt);
        print_perf("for_each_changed", rv_count, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            rv.reset_change_tracking();
        }
        print_perf("reset_change_tracking", 1000000, t.elapsed_milliseconds());

        print_perf_sub("13.4 runtime_term (OR/NOT/OPTIONAL)");
        int hp_id = type_id::get_type_id<Health>();
        dense<ecs::runtime_term> terms;
        terms.emplace_back(ecs::runtime_term{pos_id, 0, ecs::access_mode::read_write});  // AND
        terms.emplace_back(ecs::runtime_term{vel_id, 1, ecs::access_mode::read_only});   // OR
        terms.emplace_back(ecs::runtime_term{hp_id, 2, ecs::access_mode::read_only});    // NOT
        terms.emplace_back(ecs::runtime_term{hp_id, 3, ecs::access_mode::read_only});    // OPTIONAL

        t.reset();
        auto rv_term = mgr.runtime_view_create_from_terms(
            std::span<const ecs::runtime_term>(terms.data(), terms.size()));
        print_perf("create_from_terms", 1, t.elapsed_milliseconds());

        t.reset();
        cnt = 0;
        rv_term.for_each([&](entity) { cnt = cnt + 1; });
        lcf_sink(cnt);
        print_perf("term OR/NOT/OPTIONAL", rv_count, t.elapsed_milliseconds());

        t.reset();
        rv.rebuild();
        print_perf("rebuild", 1, t.elapsed_milliseconds());
    }

    // === Section 14: command_buffer / 命令缓冲 (500K/五十万) ===
    print_section(14, "command_buffer / 命令缓冲 (500K/五十万)");
    {
        const size_t cb_count = 500000;
        ecs::manager mgr;
        mgr.disable_track_changes();
        mgr.disable_comp_signals();
        mgr.append_preallocated_entities(cb_count);
        dense<entity> ents;
        ents.increase_capacity(cb_count);
        for (size_t i = 0; i < cb_count; ++i)
        {
            ents.emplace_back(mgr.create_entity());
        }

        print_perf_sub("14.1 录制 add_component (500K/五十万)");
        auto cb = mgr.create_command_buffer();
        t.reset();
        for (size_t i = 0; i < cb_count; ++i)
        {
            cb.add_component<Position>(ents[i], Position{static_cast<float>(i), 0, 0});
        }
        print_perf("add_component record", cb_count, t.elapsed_milliseconds());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            lcf_sink_all(cb.size(), cb.empty());
        }
        print_perf("size/empty", 1000000 * 2, t.elapsed_milliseconds());

        t.reset();
        cb.flush();
        print_perf("flush(add)", cb_count, t.elapsed_milliseconds());

        print_perf_sub("14.2 录制 remove + destroy 并 flush (500K/五十万)");
        auto cb2 = mgr.create_command_buffer();
        for (size_t i = 0; i < cb_count; ++i)
        {
            cb2.remove_component<Position>(ents[i]);
        }
        for (size_t i = 0; i < cb_count / 2; ++i)
        {
            cb2.destroy_entity(ents[i]);
        }

        t.reset();
        cb2.flush();
        print_perf("flush(remove+destroy)", cb_count + cb_count / 2, t.elapsed_milliseconds());

        print_perf_sub("14.3 clear (500K/五十万)");
        auto cb3 = mgr.create_command_buffer();
        for (size_t i = 0; i < cb_count; ++i)
        {
            cb3.add_component<Velocity>(ents[i], Velocity{1.0f, 0, 0});
        }
        t.reset();
        cb3.clear();
        print_perf("clear", cb_count, t.elapsed_milliseconds());
    }

    // === Section 15: Cache / 缓存命中率测试 (100K/十万 + 1M/百万) ===
    print_section(15, "Cache Analysis / 缓存命中率测试");
    {
#if 1 // 缓存测量功能已迁移至 part/analysis.hpp (15.1 ~ 15.3)
        analyzer a;  // 默认 3 级配置, 复用于 15.1~15.5
        print_perf_sub("15.1 class_pool 顺序 vs 随机 (Position 12B × 100K = 1.2MB)");
        {
            const size_t cache_n = 100000;
            dense<Position> cp;
            cp.increase_capacity(cache_n);
            for (size_t i = 0; i < cache_n; ++i)
            {
                cp.emplace_back(static_cast<float>(i), 0.0f, 0.0f);
            }

            const Position* base = cp.data();
            const size_t stride = sizeof(Position);

            auto seq_addrs = analyzer::make_sequential_addresses(base, cache_n, stride);
            analyzer::address_view seq_av{seq_addrs.data(), seq_addrs.size()};
            auto seq_report = a.measure_hits(seq_av);
            analyzer::print_report("顺序访问 (逐次)", seq_report);
            auto seq_batch = a.measure_batch(seq_av, 10);
            analyzer::print_batch("顺序访问 (批量×10)", seq_batch);

            auto rnd_addrs = analyzer::make_random_addresses(base, cache_n, stride, 42);
            analyzer::address_view rnd_av{rnd_addrs.data(), rnd_addrs.size()};
            auto rnd_report = a.measure_hits(rnd_av);
            analyzer::print_report("随机访问 (逐次)", rnd_report);
            auto rnd_batch = a.measure_batch(rnd_av, 10);
            analyzer::print_batch("随机访问 (批量×10)", rnd_batch);

            double ratio = (seq_batch.net_cycles_per_access > 0)
                ? rnd_batch.net_cycles_per_access / seq_batch.net_cycles_per_access : 0;
            std::cout << "  >> 随机/顺序 延迟比: " << std::fixed << std::setprecision(2)
                      << ratio << " 倍\n";
        }

        print_perf_sub("15.2 大组件顺序 vs 随机 (Cache128B 128B × 100K = 12.8MB)");
        {
            struct Cache128B
            {
                char data[128];
                Cache128B(int v = 0) { data[0] = static_cast<char>(v); }
            };

            const size_t big_n = 100000;
            dense<Cache128B> cp_big;
            cp_big.increase_capacity(big_n);
            for (size_t i = 0; i < big_n; ++i)
            {
                cp_big.emplace_back(static_cast<int>(i));
            }

            const Cache128B* base_big = cp_big.data();
            const size_t stride_big = sizeof(Cache128B);

            auto seq_big = analyzer::make_sequential_addresses(base_big, big_n, stride_big);
            analyzer::address_view seq_big_av{seq_big.data(), seq_big.size()};
            auto seq_big_r = a.measure_hits(seq_big_av);
            analyzer::print_report("大组件顺序 (逐次)", seq_big_r);
            auto seq_big_b = a.measure_batch(seq_big_av, 5);
            analyzer::print_batch("大组件顺序 (批量×5)", seq_big_b);

            auto rnd_big = analyzer::make_random_addresses(base_big, big_n, stride_big, 99);
            analyzer::address_view rnd_big_av{rnd_big.data(), rnd_big.size()};
            auto rnd_big_r = a.measure_hits(rnd_big_av);
            analyzer::print_report("大组件随机 (逐次)", rnd_big_r);
            auto rnd_big_b = a.measure_batch(rnd_big_av, 5);
            analyzer::print_batch("大组件随机 (批量×5)", rnd_big_b);

            double ratio_big = (seq_big_b.net_cycles_per_access > 0)
                ? rnd_big_b.net_cycles_per_access / seq_big_b.net_cycles_per_access : 0;
            std::cout << "  >> 随机/顺序 延迟比: " << std::fixed << std::setprecision(2)
                      << ratio_big << " 倍\n";
        }

        print_perf_sub("15.3 ECS 组件数据 顺序 vs 随机 (Position × 1M = 12MB)");
        {
            dense<Position>* pos_pool = ecss.get_component_container<Position>();
            if (pos_pool && pos_pool->size() > 0)
            {
                const Position* ecs_base = pos_pool->data();
                size_t ecs_n = pos_pool->size();
                size_t ecs_stride = sizeof(Position);

                auto ecs_seq = analyzer::make_sequential_addresses(ecs_base, ecs_n, ecs_stride);
                analyzer::address_view ecs_seq_av{ecs_seq.data(), ecs_seq.size()};
                auto ecs_seq_r = a.measure_hits(ecs_seq_av);
                analyzer::print_report("ECS顺序 (逐次)", ecs_seq_r);
                auto ecs_seq_b = a.measure_batch(ecs_seq_av, 3);
                analyzer::print_batch("ECS顺序 (批量×3)", ecs_seq_b);

                auto ecs_rnd = analyzer::make_random_addresses(ecs_base, ecs_n, ecs_stride, 77);
                analyzer::address_view ecs_rnd_av{ecs_rnd.data(), ecs_rnd.size()};
                auto ecs_rnd_r = a.measure_hits(ecs_rnd_av);
                analyzer::print_report("ECS随机 (逐次)", ecs_rnd_r);
                auto ecs_rnd_b = a.measure_batch(ecs_rnd_av, 3);
                analyzer::print_batch("ECS随机 (批量×3)", ecs_rnd_b);

                double ecs_ratio = (ecs_seq_b.net_cycles_per_access > 0)
                    ? ecs_rnd_b.net_cycles_per_access / ecs_seq_b.net_cycles_per_access : 0;
                std::cout << "  >> 随机/顺序 延迟比: " << std::fixed << std::setprecision(2)
                          << ecs_ratio << " 倍\n";
            }
            else
            {
                std::cout << "  (ECS Position 数据不可用, 跳过)\n";
            }
        }
#endif // 缓存测量功能已从 time.hpp 移除 (15.1 ~ 15.3)

        print_perf_sub("15.4 ECS get_ptr 延迟 (含稀疏表间接寻址)");
        {
            // benchmark_precise_cycles 测量 get_ptr 延迟 (含稀疏表查找 + 数据访问)
            size_t gi = 0;
            auto getptr_stats = benchmark_precise_cycles(2000, 200, [&]() {
                entity e = entities[gi % N];
                Position* p = ecss.get_ptr<Position>(e);
                lcf_sink(p ? p->x : 0.0f);
                ++gi;
            });
            print_stats("get_ptr 随机延迟", getptr_stats, "周期");

            // 对比: 直接数组访问 (无稀疏表开销)
            dense<Position>* pos_pool = ecss.get_component_container<Position>();
            if (pos_pool && pos_pool->size() > 0)
            {
                size_t di = 0;
                size_t pn = pos_pool->size();
                auto direct_stats = benchmark_precise_cycles(2000, 200, [&]() {
                    Position& p = (*pos_pool)[di % pn];
                    lcf_sink(p.x);
                    ++di;
                });
                print_stats("直接数组访问", direct_stats, "周期");

                double overhead = getptr_stats.mean - direct_stats.mean;
                std::cout << "  >> 稀疏表间接开销: " << std::fixed << std::setprecision(3)
                          << overhead << " 周期/次\n";
            }
        }

#if 1 // 缓存测量功能已迁移至 part/analysis.hpp (15.5 ~ 15.6)
        print_perf_sub("15.5 数据规模 vs 缓存行为 (Position 12B, batch×20)");
        {
            struct SizeCase { const char* name; size_t count; };
            SizeCase cases[] = {
                {"4K  (48KB  <L1)",    4096},
                {"32K (384KB >L1<L2)", 32768},
                {"256K(3MB   >L2<L3)", 262144},
                {"1M  (12MB  >L3)",    1048576},
            };

            for (const auto& tc : cases)
            {
                dense<Position> cp_sz;
                cp_sz.increase_capacity(tc.count);
                for (size_t i = 0; i < tc.count; ++i)
                {
                    cp_sz.emplace_back(static_cast<float>(i), 0.0f, 0.0f);
                }

                const Position* base = cp_sz.data();
                size_t stride = sizeof(Position);

                auto seq = analyzer::make_sequential_addresses(base, tc.count, stride);
                auto rnd = analyzer::make_random_addresses(base, tc.count, stride, 314);

                analyzer::address_view seq_av{seq.data(), seq.size()};
                analyzer::address_view rnd_av{rnd.data(), rnd.size()};
                auto seq_b = a.measure_batch(seq_av, 20);
                auto rnd_b = a.measure_batch(rnd_av, 20);

                double ratio = (seq_b.net_cycles_per_access > 0)
                    ? rnd_b.net_cycles_per_access / seq_b.net_cycles_per_access : 0;
                std::cout << std::fixed << std::setprecision(3);
                std::cout << "  " << std::left << std::setw(30) << tc.name
                          << " | 顺序 " << std::setw(8) << seq_b.net_cycles_per_access << " 周期"
                          << " | 随机 " << std::setw(8) << rnd_b.net_cycles_per_access << " 周期"
                          << " | 比值 " << std::setw(6) << ratio << " 倍\n";
            }
        }

        print_perf_sub("15.6 缓存层级自适应检测");
        {
            std::cout << "  检测本地 CPU 缓存层级...\n";
            analyzer::config auto_th = analyzer::detect_config();
            std::cout << "  >> 检测到 " << auto_th.cache_levels << " 级缓存\n";
            std::cout << "  >> L1 阈值: " << std::fixed << std::setprecision(1)
                      << auto_th.l1_max << " 周期\n";
            if (auto_th.cache_levels >= 2)
            {
                std::cout << "  >> L2 阈值: " << auto_th.l2_max << " 周期\n";
            }
            if (auto_th.cache_levels >= 3)
            {
                std::cout << "  >> L3 阈值: " << auto_th.l3_max << " 周期\n";
            }
            if (auto_th.cache_levels >= 4)
            {
                std::cout << "  >> L4 阈值: " << auto_th.l4_max << " 周期\n";
            }

            int buf[4096];
            auto addrs = analyzer::make_sequential_addresses(buf, 4096, sizeof(int));
            analyzer::address_view addrs_av{addrs.data(), addrs.size()};
            std::cout << "  >> 默认三级阈值 → 测量结果:\n";
            analyzer a_default;
            analyzer::cache_report r_default = a_default.measure_hits(addrs_av);
            std::cout << "     L1: " << std::setprecision(1) << r_default.l1_hit_rate * 100 << "%"
                      << "  L2: " << r_default.l2_hit_rate * 100 << "%"
                      << "  L3: " << r_default.l3_hit_rate * 100 << "%"
                      << "  Miss: " << r_default.miss_rate * 100 << "%"
                      << "  levels=" << r_default.active_levels << "\n";

            std::cout << "  >> 自适应阈值 → 测量结果:\n";
            analyzer a_auto(auto_th);
            analyzer::cache_report r_auto = a_auto.measure_hits(addrs_av);
            std::cout << "     L1: " << r_auto.l1_hit_rate * 100 << "%"
                      << "  L2: " << r_auto.l2_hit_rate * 100 << "%"
                      << "  L3: " << r_auto.l3_hit_rate * 100 << "%"
                      << "  Miss: " << r_auto.miss_rate * 100 << "%"
                      << "  levels=" << r_auto.active_levels << "\n";
        }
#endif // 缓存测量功能已从 time.hpp 移除 (15.5 ~ 15.6)
    }

    // === Section 16: multi_block_bitmask 扩容/缩容/状态查询 (1M/百万) ===
    print_section(16, "multi_block_bitmask 掩码管理 (1M/百万)");
    {
        print_perf_sub("16.1 单块扩容与写入 (1M/百万)");
        {
            multi_block_bitmask m1;

            t.reset();
            m1.increase_capacity(N);
            print_perf("increase_capacity(N)", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                m1.ensure_entity(static_cast<uint32_t>(i));
            }
            print_perf("ensure_entity ×N", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                m1.set_bit_no_check(static_cast<uint32_t>(i), 0,
                                    static_cast<uint32_t>(i & 63));
            }
            print_perf("set_bit_no_check ×N", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                lcf_sink(m1.get_block(static_cast<uint32_t>(i), 0));
            }
            print_perf("get_block ×N", N, t.elapsed_milliseconds());
        }

        print_perf_sub("16.2 状态查询开销 (1M/百万次)");
        {
            multi_block_bitmask m1;
            m1.increase_capacity(N);
            for (size_t i = 0; i < N; ++i)
            {
                m1.ensure_entity(static_cast<uint32_t>(i));
            }

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                lcf_sink(m1.size());
            }
            print_perf("size() ×N", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                lcf_sink(m1.capacity());
            }
            print_perf("capacity() ×N", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                lcf_sink(m1.empty());
            }
            print_perf("empty() ×N", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                lcf_sink(m1.size_bytes());
            }
            print_perf("size_bytes() ×N", N, t.elapsed_milliseconds());
        }

        print_perf_sub("16.3 缩容/清空/预留 (单次, N=1M)");
        {
            multi_block_bitmask m1;
            m1.increase_capacity(N);
            for (size_t i = 0; i < N; ++i)
            {
                m1.ensure_entity(static_cast<uint32_t>(i));
            }

            t.reset();
            m1.reserve_exact(N * 2);
            print_perf("reserve_exact(2N)", N, t.elapsed_milliseconds());

            t.reset();
            m1.shrink_to_fit();
            print_perf("shrink_to_fit", N, t.elapsed_milliseconds());

            t.reset();
            m1.reduce_capacity(N / 2);
            print_perf("reduce_capacity(N/2)", N / 2, t.elapsed_milliseconds());

            t.reset();
            m1.clear();
            print_perf("clear", 1, t.elapsed_milliseconds());
        }

        print_perf_sub("16.4 多块写入与查询 (2块, 1M/百万)");
        {
            multi_block_bitmask m2;
            m2.reserve_blocks(2);
            m2.increase_capacity(N);

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                m2.ensure_entity(static_cast<uint32_t>(i));
            }
            print_perf("多块 ensure_entity ×N", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                m2.set_bit_no_check(static_cast<uint32_t>(i), 0,
                                    static_cast<uint32_t>(i & 63));
                m2.set_bit_no_check(static_cast<uint32_t>(i), 1,
                                    static_cast<uint32_t>(i & 63));
            }
            print_perf("多块 set_bit_no_check ×2N", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                lcf_sink_all(m2.get_block(static_cast<uint32_t>(i), 0),
                             m2.get_block(static_cast<uint32_t>(i), 1));
            }
            print_perf("多块 get_block ×2N", N, t.elapsed_milliseconds());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                lcf_sink(m2.capacity_bytes());
            }
            print_perf("多块 capacity_bytes() ×N", N, t.elapsed_milliseconds());
        }

        // 证明 multi_block_bitmask 在多组件查询时的性能优势
        // 测试矩阵: 2/3/5/8 组件 × mask_path/sparse_path
        // 关键: group 的 use_mask_path_ = (N>=3) || (块数<=5)
        //   N=2, 块数<=5 → mask_path (但优势小)
        //   N=3, 块数<=5 → mask_path (优势开始显现)
        //   N=5, 块数<=5 → mask_path (优势显著)
        //   N=8, 块数<=5 → mask_path (优势最大)
        print_perf_sub("16.5 mask_path vs sparse_path 实战对比 (500K/五十万)");
        {
            constexpr size_t mask_n = 500000;
            ecs::manager mgr;
            mgr.disable_track_changes();
            mgr.disable_comp_signals();
            mgr.append_preallocated_entities(mask_n);
            dense<entity> ents;
            ents.increase_capacity(mask_n);
            for (size_t i = 0; i < mask_n; ++i)
            {
                ents.emplace_back(mgr.create_entity());
                // 全员全组件: 确保所有 group 查询都命中全部实体
                mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
                mgr.add(ents[i], Velocity{1.0f, 0, 0});
                mgr.add(ents[i], Health{static_cast<int>(i), 100});
                mgr.add(ents[i], Damage{static_cast<int>(i % 50)});
                mgr.add(ents[i], Armor{static_cast<int>(i % 200)});
                mgr.add(ents[i], Speed{static_cast<float>(i % 100)});
                mgr.add(ents[i], Rotation{1.0f, 0, 0, 1.0f});
                mgr.add(ents[i], Scale{1.0f, 1.0f, 1.0f});
            }

            // 通用基准函数: 计时 rebuild + for_each, 多次取最小值
            auto bench_group = [](auto& g, const char* label, size_t cnt) noexcept
            {
                g.rebuild();
                g.for_each([&](auto&... comps) {
                    float tmp = 0;
                    ((tmp += comp_touch(comps)), ...);
                    lcf_sink(tmp);
                });

                // timed rebuild (取最小值)
                constexpr int REPEAT = 5;
                double best_rebuild = 1e9;
                double best_iter = 1e9;
                for (int r = 0; r < REPEAT; ++r)
                {
                    timer tr;
                    tr.reset();
                    g.rebuild();
                    double ms_r = tr.elapsed_milliseconds();
                    if (ms_r < best_rebuild) best_rebuild = ms_r;

                    timer ti;
                    ti.reset();
                    g.for_each([&](auto&... comps) {
                        float tmp = 0;
                        ((tmp += comp_touch(comps)), ...);
                        lcf_sink(tmp);
                    });
                    double ms_i = ti.elapsed_milliseconds();
                    if (ms_i < best_iter) best_iter = ms_i;
                }
                print_perf(label, cnt, best_iter);
                std::cout << "      rebuild: " << std::fixed << std::setprecision(3)
                          << best_rebuild << " ms\n";
            };

            std::cout << "\n  ── 2 组件 (N=2, mask_path 启用因块数<=5) ──\n";
            {
                auto g2 = mgr.group<Position, Velocity>();
                bench_group(g2, "2-comp group for_each", mask_n);
            }

            std::cout << "\n  ── 3 组件 (N=3, mask_path 强制启用) ──\n";
            {
                auto g3 = mgr.group<Position, Velocity, Health>();
                bench_group(g3, "3-comp group for_each", mask_n);
            }

            std::cout << "\n  ── 5 组件 (N=5, mask_path 优势显著) ──\n";
            {
                auto g5 = mgr.group<Position, Velocity, Health, Damage, Armor>();
                bench_group(g5, "5-comp group for_each", mask_n);
            }

            std::cout << "\n  ── 8 组件 (N=8, mask_path 优势最大) ──\n";
            {
                auto g8 = mgr.group<Position, Velocity, Health, Damage, Armor, Speed, Rotation, Scale>();
                bench_group(g8, "8-comp group for_each", mask_n);
            }

            // 对比: multi_view (无 mask_path, 用 pools_aligned_ 快路径)
            // 证明 mask_path 不是唯一快路径, 但在非对齐场景下 mask 是关键
            std::cout << "\n  ── 对比 multi_view (pools_aligned_ 快路径, 对齐数据) ──\n";
            {
                auto mv3 = mgr.view<Position, Velocity, Health>();
                size_t cnt = mv3.size();
                mv3.for_each([&](Position& p, Velocity& v, Health& h) {
                    lcf_sink(comp_touch(p) + comp_touch(v) + comp_touch(h));
                });

                constexpr int REPEAT = 5;
                double best = 1e9;
                for (int r = 0; r < REPEAT; ++r)
                {
                    timer ti;
                    ti.reset();
                    mv3.for_each([&](Position& p, Velocity& v, Health& h) {
                        lcf_sink(comp_touch(p) + comp_touch(v) + comp_touch(h));
                    });
                    double ms = ti.elapsed_milliseconds();
                    if (ms < best) best = ms;
                }
                print_perf("3-comp multi_view for_each (aligned)", cnt, best);
            }

            // 16.6 非对齐数据测试: 证明 mask_path 在 pools_aligned_ 失效时的价值
            // 场景: 50% 实体缺失部分组件 → pools_aligned_ = false
            // 此时 multi_view 走 SoA mapping 慢路径, group 走 mask_path + mapping
            print_perf_sub("16.6 非对齐数据: mask_path 真正价值 (500K/五十万)");
            {
                constexpr size_t unaligned_n = 500000;
                ecs::manager mgr2;
                mgr2.disable_track_changes();
                mgr2.disable_comp_signals();
                mgr2.append_preallocated_entities(unaligned_n);
                dense<entity> ents2;
                ents2.increase_capacity(unaligned_n);
                for (size_t i = 0; i < unaligned_n; ++i)
                {
                    ents2.emplace_back(mgr2.create_entity());
                    mgr2.add(ents2[i], Position{static_cast<float>(i), 0, 0});
                    mgr2.add(ents2[i], Velocity{1.0f, 0, 0});
                    // 仅 50% 实体有 Health/Damage/Armor → pools_aligned_ 失效
                    if ((i & 1) == 0)
                    {
                        mgr2.add(ents2[i], Health{static_cast<int>(i), 100});
                        mgr2.add(ents2[i], Damage{static_cast<int>(i % 50)});
                        mgr2.add(ents2[i], Armor{static_cast<int>(i % 200)});
                    }
                }
                // 命中数: 250K (50% 实体拥有全部 5 组件)

                std::cout << "\n  ── 5 组件非对齐 (50% 命中, mask_path 过滤) ──\n";
                {
                    auto g5 = mgr2.group<Position, Velocity, Health, Damage, Armor>();
                    size_t cnt = 0;
                    g5.rebuild();
                    cnt = g5.size();

                    g5.for_each([&](Position& p, Velocity& v, Health& h, Damage& d, Armor& a) {
                        lcf_sink(comp_touch(p) + comp_touch(v) + comp_touch(h)
                              + comp_touch(d) + comp_touch(a));
                    });

                    constexpr int REPEAT = 5;
                    double best_rebuild = 1e9;
                    double best_iter = 1e9;
                    for (int r = 0; r < REPEAT; ++r)
                    {
                        timer tr;
                        tr.reset();
                        g5.rebuild();
                        double ms_r = tr.elapsed_milliseconds();
                        if (ms_r < best_rebuild) best_rebuild = ms_r;

                        timer ti;
                        ti.reset();
                        g5.for_each([&](Position& p, Velocity& v, Health& h, Damage& d, Armor& a) {
                            lcf_sink(comp_touch(p) + comp_touch(v) + comp_touch(h)
                               + comp_touch(d) + comp_touch(a));
                        });
                        double ms_i = ti.elapsed_milliseconds();
                        if (ms_i < best_iter) best_iter = ms_i;
                    }
                    print_perf("5-comp group for_each (unaligned)", cnt, best_iter);
                    std::cout << "      rebuild: " << std::fixed << std::setprecision(3)
                              << best_rebuild << " ms\n";
                }

                std::cout << "\n  ── 5 组件非对齐 multi_view (SoA mapping, 无 mask_path) ──\n";
                {
                    auto mv5 = mgr2.view<Position, Velocity, Health, Damage, Armor>();
                    size_t cnt = mv5.size();

                    mv5.for_each([&](Position& p, Velocity& v, Health& h, Damage& d, Armor& a) {
                        lcf_sink(comp_touch(p) + comp_touch(v) + comp_touch(h)
                              + comp_touch(d) + comp_touch(a));
                    });

                    constexpr int REPEAT = 5;
                    double best = 1e9;
                    for (int r = 0; r < REPEAT; ++r)
                    {
                        timer ti;
                        ti.reset();
                        mv5.for_each([&](Position& p, Velocity& v, Health& h, Damage& d, Armor& a) {
                            lcf_sink(comp_touch(p) + comp_touch(v) + comp_touch(h)
                               + comp_touch(d) + comp_touch(a));
                        });
                        double ms = ti.elapsed_milliseconds();
                        if (ms < best) best = ms;
                    }
                    print_perf("5-comp multi_view for_each (unaligned)", cnt, best);
                }
            }
        }
    }

    print_summary("性能测试");
    return 0;
}
