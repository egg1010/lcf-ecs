// test_multi_view_perf.cpp - multi_view<T...> 多组件查询性能测试
// 1M 实体, 1~10 组件 for_each 遍历性能 scaling
#include "perf_common.hpp"
#include "include/component.hpp"
#include <bit>

using namespace std;
using ecs::manager;
using ecs::entity;

// === 测试组件 (10 种, 覆盖 4B/8B/12B/16B/32B 多种尺寸) ===
struct Pos  { float x, y, z; };           // 12B
struct Vel  { float vx, vy, vz; };        // 12B
struct Hp   { int current, max; };        // 8B
struct Dmg  { int amount; };              // 4B
struct Arm  { int defense; };             // 4B
struct Spd  { float value; };             // 4B
struct Rot  { float x, y, z, w; };        // 16B
struct Scl  { float x, y, z; };           // 12B
struct Name { char data[32]; };           // 32B
struct Mass { float value; };             // 4B

// === 防死码消除: 组件字段读取器 ===
// float 字段直接返回 (vmovss 单指令, 2 loads/cyc)
// int 字段返回 uint32_t (mov 单指令, 避免 vcvtsi2ss 1/cyc 瓶颈)
inline float    comp_touch(const Pos& v)   noexcept { return v.x; }
inline float    comp_touch(const Vel& v)   noexcept { return v.vx; }
inline uint32_t comp_touch(const Hp& v)    noexcept { return static_cast<uint32_t>(v.current); }
inline uint32_t comp_touch(const Dmg& v)   noexcept { return static_cast<uint32_t>(v.amount); }
inline uint32_t comp_touch(const Arm& v)   noexcept { return static_cast<uint32_t>(v.defense); }
inline float    comp_touch(const Spd& v)   noexcept { return v.value; }
inline float    comp_touch(const Rot& v)   noexcept { return v.x; }
inline float    comp_touch(const Scl& v)   noexcept { return v.x; }
inline uint32_t comp_touch(const Name& v)  noexcept { return static_cast<uint32_t>(v.data[0]); }
inline float    comp_touch(const Mass& v)  noexcept { return v.value; }

// touch_barrier: 标记值已使用, 防止死码消除, 不创建串行依赖
#if defined(_MSC_VER)
    inline void touch_barrier(float v) noexcept    { volatile float sink = v; _ReadWriteBarrier(); (void)sink; }
    inline void touch_barrier(uint32_t v) noexcept { volatile uint32_t sink = v; _ReadWriteBarrier(); (void)sink; }
#else
    inline void touch_barrier(float v) noexcept    { asm volatile("" : : "x"(v) :); }
    inline void touch_barrier(uint32_t v) noexcept { asm volatile("" : : "r"(v) :); }
#endif

// 批量屏障: 所有 load 作为实参先求值, 再依次施加 barrier
// 使编译器可自由调度所有 load 指令 (2 loads/cyc 并行发射)
template <typename... Ts>
inline void touch_all(Ts... vs) noexcept
{
    (touch_barrier(vs), ...);
}

// === 通用 for_each 基准 ===
// 构造 view 一次复用, warmup 预建 mappings + 预热 cache
// 多次取最小值, 减少大数据集 L3 越界导致的测量噪声
template <typename... Comps>
static void bench_for_each(manager& mgr, const char* label) noexcept
{
    auto v = mgr.view<Comps...>();
    size_t cnt = v.size();

    // warmup: 预建 mappings + 预热 L3 cache
    v.for_each([&](Comps&... comps) {
        touch_all(comp_touch(comps)...);
    });
    v.for_each([&](Comps&... comps) {
        touch_all(comp_touch(comps)...);
    });
    v.for_each([&](Comps&... comps) {
        touch_all(comp_touch(comps)...);
    });

    // timed: 多次取最小值
    constexpr int REPEAT = 5;
    double best_ns = 1e18;
    for (int r = 0; r < REPEAT; ++r)
    {
        timer t;
        t.reset();
        v.for_each([&](Comps&... comps) {
            touch_all(comp_touch(comps)...);
        });
        double ns = t.elapsed_nanoseconds();
        if (ns < best_ns) best_ns = ns;
    }

    double per_entity = best_ns / static_cast<double>(cnt);
    double throughput = (best_ns > 0) ? static_cast<double>(cnt) / best_ns : 0;
    cout << "  " << left << setw(28) << label
         << " | " << right << setw(10) << cnt << " 实体"
         << " | " << fixed << setprecision(3) << setw(10) << per_entity << " ns/实体"
         << " | " << setprecision(2) << setw(10) << throughput << " 亿/s"
         << " | 总计 " << setprecision(3) << setw(10) << best_ns / 1e6 << " ms\n";
}

// 带 entity 版本
template <typename... Comps>
static void bench_for_each_ent(manager& mgr, const char* label) noexcept
{
    auto v = mgr.view<Comps...>();
    size_t cnt = v.size();

    v.for_each([&](entity, Comps&... comps) {
        touch_all(comp_touch(comps)...);
    });
    v.for_each([&](entity, Comps&... comps) {
        touch_all(comp_touch(comps)...);
    });

    constexpr int REPEAT = 5;
    double best_ns = 1e18;
    for (int r = 0; r < REPEAT; ++r)
    {
        timer t;
        t.reset();
        v.for_each([&](entity e, Comps&... comps) {
            touch_all(static_cast<uint32_t>(e.parts_.index_), comp_touch(comps)...);
        });
        double ns = t.elapsed_nanoseconds();
        if (ns < best_ns) best_ns = ns;
    }

    double per_entity = best_ns / static_cast<double>(cnt);
    double throughput = (best_ns > 0) ? static_cast<double>(cnt) / best_ns : 0;
    cout << "  " << left << setw(28) << label
         << " | " << right << setw(10) << cnt << " 实体"
         << " | " << fixed << setprecision(3) << setw(10) << per_entity << " ns/实体"
         << " | " << setprecision(2) << setw(10) << throughput << " 亿/s"
         << " | 总计 " << setprecision(3) << setw(10) << best_ns / 1e6 << " ms\n";
}

int main()
{
    cout << "============================================================\n";
    cout << "  multi_view 多组件查询性能测试 (1~10 组件, 1M/百万)\n";
    cout << "============================================================\n";

    constexpr size_t N = 1000000;

    // 构建 1M 实体, 每个实体拥有全部 10 个组件 (pools_aligned 快路径)
    manager mgr;
    mgr.disable_track_changes();
    mgr.disable_comp_signals();
    mgr.append_preallocated_entities(N);

    mt19937 rng(42);
    uniform_real_distribution<float> rf(-1000.0f, 1000.0f);
    uniform_int_distribution<int> ri(0, 100);

    dense<entity> ents; ents.increase_capacity(N);
    dense<Pos> pos;     pos.increase_capacity(N);
    dense<Vel> vel;     vel.increase_capacity(N);
    dense<Hp>  hp;      hp.increase_capacity(N);
    dense<Dmg> dmg;     dmg.increase_capacity(N);
    dense<Arm> arm;     arm.increase_capacity(N);
    dense<Spd> spd;     spd.increase_capacity(N);
    dense<Rot> rot;     rot.increase_capacity(N);
    dense<Scl> scl;     scl.increase_capacity(N);
    dense<Name> name;   name.increase_capacity(N);
    dense<Mass> mass;   mass.increase_capacity(N);

    for (size_t i = 0; i < N; ++i)
    {
        ents.emplace_back(mgr.create_entity());
        pos.emplace_back(rf(rng), rf(rng), rf(rng));
        vel.emplace_back(rf(rng), rf(rng), rf(rng));
        hp.emplace_back(ri(rng), 100);
        dmg.emplace_back(ri(rng));
        arm.emplace_back(ri(rng));
        spd.emplace_back(rf(rng));
        rot.emplace_back(rf(rng), rf(rng), rf(rng), 1.0f);
        scl.emplace_back(rf(rng), rf(rng), rf(rng));
        Name nm; memset(nm.data, static_cast<int>(i & 0xFF), 32);
        name.emplace_back(nm);
        mass.emplace_back(rf(rng) * 10.0f);
    }

    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Pos>(pos.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Vel>(vel.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Hp>(hp.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Dmg>(dmg.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Arm>(arm.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Spd>(spd.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Rot>(rot.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Scl>(scl.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Name>(name.data(), N));
    mgr.add_batch(std::span<const entity>(ents.data(), N), std::span<const Mass>(mass.data(), N));

    cout << "\n  数据: " << N << " 实体, 每个实体拥有全部 10 个组件 (pools_aligned)\n";
    cout << "  组件: Pos(12B) Vel(12B) Hp(8B) Dmg(4B) Arm(4B) Spd(4B) Rot(16B) Scl(12B) Name(32B) Mass(4B)\n";
    cout << "  总计: 108B/实体 × " << N << " = " << (108 * N) / (1024 * 1024) << " MB\n";

    // === for_each 1~10 组件 scaling ===
    print_header("for_each 1~10 组件 (1M/百万, pools_aligned)");
    cout << "\n";

    bench_for_each<Pos>(mgr, "1 comp");
    bench_for_each<Pos, Vel>(mgr, "2 comps");
    bench_for_each<Pos, Vel, Hp>(mgr, "3 comps");
    bench_for_each<Pos, Vel, Hp, Dmg>(mgr, "4 comps");
    bench_for_each<Pos, Vel, Hp, Dmg, Arm>(mgr, "5 comps");
    bench_for_each<Pos, Vel, Hp, Dmg, Arm, Spd>(mgr, "6 comps");
    bench_for_each<Pos, Vel, Hp, Dmg, Arm, Spd, Rot>(mgr, "7 comps");
    bench_for_each<Pos, Vel, Hp, Dmg, Arm, Spd, Rot, Scl>(mgr, "8 comps");
    bench_for_each<Pos, Vel, Hp, Dmg, Arm, Spd, Rot, Scl, Name>(mgr, "9 comps");
    bench_for_each<Pos, Vel, Hp, Dmg, Arm, Spd, Rot, Scl, Name, Mass>(mgr, "10 comps");

    cout << "\n";
    print_footer();

    // === 带 entity 版本 (2 组件对照) ===
    print_header("for_each +entity (1M/百万, pools_aligned)");
    cout << "\n";

    bench_for_each_ent<Pos, Vel>(mgr, "2 comps +entity");
    bench_for_each_ent<Pos, Vel, Hp, Dmg, Arm>(mgr, "5 comps +entity");
    bench_for_each_ent<Pos, Vel, Hp, Dmg, Arm, Spd, Rot, Scl, Name, Mass>(mgr, "10 comps +entity");

    cout << "\n";
    print_footer();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
