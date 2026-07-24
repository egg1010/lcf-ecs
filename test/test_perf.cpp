// ============================================================
// test_perf.cpp - lcf-ecs 性能测试 (Performance Benchmark)
// 编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2
// 注意: 禁止 std::sort + lambda (MinGW+AVX2 崩溃), 改用 pdqsort
// ============================================================
#include "test_common.hpp"
#include "include/part/dense.hpp"

// 数量级格式化 / magnitude formatter
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

// ============================================================
// multi_view 多组件查询基准助手 (1~10 组件)
// ============================================================
// 组件字段读取器: 返回 float 触发真实数据访问, 防止死码消除
inline float comp_touch(const Position& p) noexcept { return p.x; }
inline float comp_touch(const Velocity& v) noexcept { return v.vx; }
inline float comp_touch(const Health& h) noexcept { return static_cast<float>(h.current); }
inline float comp_touch(const Damage& d) noexcept { return static_cast<float>(d.amount); }
inline float comp_touch(const Armor& a) noexcept { return static_cast<float>(a.defense); }
inline float comp_touch(const Speed& s) noexcept { return s.value; }
inline float comp_touch(const Name& n) noexcept { return static_cast<float>(n.value.size()); }
inline float comp_touch(const Rotation& r) noexcept { return r.x; }
inline float comp_touch(const Scale& s) noexcept { return s.x; }
inline float comp_touch(const Mass& m) noexcept { return m.value; }

// 通用 multi_view for_each 基准: 对任意 1~10 组件组合统一测量
template <typename... Comps>
size_t bench_multi_view(ecs::manager& mgr, const char* label) noexcept
{
    timer t;
    size_t cnt = 0;
    t.reset();
    mgr.view<Comps...>().for_each([&](Comps&... comps) {
        ++cnt;
        volatile float d = (comp_touch(comps) + ... + 0.0f);
        (void)d;
    });
    print_perf(label, cnt, t.elapsed_ms());
    return cnt;
}

// 带 entity 的 multi_view 基准
template <typename... Comps>
size_t bench_multi_view_ent(ecs::manager& mgr, const char* label) noexcept
{
    timer t;
    size_t cnt = 0;
    t.reset();
    mgr.view<Comps...>().for_each([&](entity e, Comps&... comps) {
        ++cnt;
        volatile float d = (comp_touch(comps) + ... + 0.0f) + static_cast<float>(e.parts_.index_);
        (void)d;
    });
    print_perf(label, cnt, t.elapsed_ms());
    return cnt;
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "========================================================\n"
              << "  lcf-ecs 性能测试 / Performance Benchmark\n"
              << "  编译器: MinGW GCC 15.2.0  (-O3 -std=c++20 -mavx2 -mbmi -mbmi2)\n"
              << "========================================================\n";

    constexpr size_t N = 1000000;  // 1M / 百万实体
    ecs::manager ecss;
    timer t;

    // 实体句柄池 (全局共享)
    dense<entity> entities;
    entities.increase_capacity(N);

    // ============================================================
    // Section 0: Data Setup / 数据准备 (1M/百万)
    // ============================================================
    print_section(0, "Data Setup / 数据准备 (1M/百万)");
    {
        print_perf_sub("0.1 实体预分配与创建 (1M/百万)");

        t.reset();
        ecss.append_preallocated_entities(N);
        print_perf("append_preallocated_entities", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            entities.emplace_back(ecss.create_entity());
        }
        print_perf("create_entity", N, t.elapsed_ms());

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
        print_perf("add_batch Position", N, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), N),
                       std::span<const Health>(healths.data(), N));
        print_perf("add_batch Health", N, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count),
                       std::span<const Velocity>(velocities.data(), vel_count));
        print_perf("add_batch Velocity", vel_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count),
                       std::span<const Damage>(damages.data(), vel_count));
        print_perf("add_batch Damage", vel_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count),
                       std::span<const Armor>(armors.data(), vel_count));
        print_perf("add_batch Armor", vel_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count),
                       std::span<const Rotation>(rotations.data(), vel_count));
        print_perf("add_batch Rotation", vel_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), speed_count),
                       std::span<const Speed>(speeds.data(), speed_count));
        print_perf("add_batch Speed", speed_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), speed_count),
                       std::span<const Scale>(scales.data(), speed_count));
        print_perf("add_batch Scale", speed_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), name_count),
                       std::span<const Name>(names.data(), name_count));
        print_perf("add_batch Name", name_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), name_count),
                       std::span<const Mass>(masses.data(), name_count));
        print_perf("add_batch Mass", name_count, t.elapsed_ms());

        // 数据分布摘要
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

    // ============================================================
    // Section 1: Basic Types / 基础类型 (entity / type_id) (1M/百万)
    // ============================================================
    print_section(1, "Basic Types / 基础类型 (entity / type_id) (1M/百万)");
    {
        print_perf_sub("1.1 entity 构造与比较 (1M/百万)");
        t.reset();
        entity e_test;
        volatile uint32_t e_idx = 0, e_ver = 0;
        for (size_t i = 0; i < N; ++i)
        {
            e_test = entity(static_cast<uint32_t>(i), static_cast<uint32_t>(i));
            e_idx = e_test.parts_.index_;
            e_ver = e_test.parts_.version_;
        }
        print_perf("entity construct", N, t.elapsed_ms());

        t.reset();
        entity e1(1, 1), e2(1, 1), e3(2, 1);
        volatile bool b1 = false, b2 = false, b3 = false;
        for (size_t i = 0; i < N; ++i)
        {
            b1 = e1.is_valid();
            b2 = (e1 == e2);
            b3 = (e1 != e3);
        }
        print_perf("entity is_valid/==/!=", N * 3, t.elapsed_ms());

        t.reset();
        std::hash<entity> eh;
        volatile size_t hv = 0;
        for (size_t i = 0; i < N; ++i)
        {
            hv = eh(entity(static_cast<uint32_t>(i), 1));
        }
        print_perf("std::hash<entity>", N, t.elapsed_ms());

        print_perf_sub("1.2 type_id 与 entity_mask (1M/百万)");
        t.reset();
        volatile int tid = 0;
        for (int i = 0; i < static_cast<int>(N); ++i)
        {
            tid = type_id::get_type_id<Position>();
        }
        print_perf("type_id::get_type_id", N, t.elapsed_ms());

        t.reset();
        volatile uint64_t mask = 0;
        for (size_t i = 0; i < N; ++i)
        {
            mask = ecss.get_entity_mask(entities[i]);
        }
        print_perf("get_entity_mask", N, t.elapsed_ms());

        t.reset();
        volatile uint64_t blk = 0;
        for (size_t i = 0; i < N; ++i)
        {
            blk = ecss.get_entity_block(entities[i], 0);
        }
        print_perf("get_entity_block", N, t.elapsed_ms());

        t.reset();
        volatile uint32_t nmb = 0;
        for (int i = 0; i < 1000000; ++i)
        {
            nmb = ecss.num_mask_blocks();
        }
        print_perf("num_mask_blocks", 1000000, t.elapsed_ms());
    }

    // ============================================================
    // Section 2: class_pool / 组件池 (1M/百万)
    // ============================================================
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
        print_perf("emplace_back", N, t.elapsed_ms());

        t.reset();
        dense<int> cp_pb;
        cp_pb.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_pb.push_back_unchecked(static_cast<int>(i));
        }
        print_perf("push_back_unchecked", N, t.elapsed_ms());

        t.reset();
        dense<int> cp_ub;
        cp_ub.emplace_back(0);
        cp_ub.increase_capacity(N + 1);
        for (size_t i = 0; i < N; ++i)
        {
            cp_ub.emplace_back_unchecked(static_cast<int>(i));
        }
        print_perf("emplace_back_unchecked", N, t.elapsed_ms());

        t.reset();
        dense<int> cp_db;
        cp_db.emplace_back(0);
        cp_db.increase_capacity(N + 1);
        for (size_t i = 0; i < N; ++i)
        {
            cp_db.emplace_back_dense_unchecked(static_cast<int>(i));
        }
        print_perf("emplace_back_dense_unchecked", N, t.elapsed_ms());

        t.reset();
        dense<int> cp_an;
        cp_an.increase_capacity(N + 1);
        cp_an.append_n(N, 42);
        print_perf("append_n", N, t.elapsed_ms());

        t.reset();
        class_pool<int> cp_ea;
        cp_ea.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_ea.emplace_at(i, static_cast<int>(i));
        }
        print_perf("emplace_at", N, t.elapsed_ms());

        t.reset();
        class_pool<int> cp_sea;
        cp_sea.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_sea.sparse_emplace_at(i, static_cast<int>(i));
        }
        print_perf("sparse_emplace_at", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; i += 2)
        {
            cp_sea.sparse_erase_at(i);
        }
        print_perf("sparse_erase_at", N / 2, t.elapsed_ms());

        print_perf_sub("2.2 容量与拷贝 (1M/百万)");
        t.reset();
        dense<int> cp_rz;
        cp_rz.reserve_exact(N);
        print_perf("reserve_exact", N, t.elapsed_ms());

        t.reset();
        dense<int> cp_rzv;
        cp_rzv.resize(N, 77);
        print_perf("resize(cap,val)", N, t.elapsed_ms());

        t.reset();
        dense<int> cp_ic;
        cp_ic.emplace_back(1);
        cp_ic.increase_capacity(N, 99);
        print_perf("increase_capacity(cap,val)", N, t.elapsed_ms());

        t.reset();
        dense<int> cp_copy(cp_em);
        print_perf("copy ctor", 1, t.elapsed_ms());

        t.reset();
        dense<int> cp_assign;
        cp_assign = cp_em;
        print_perf("copy assign", 1, t.elapsed_ms());

        t.reset();
        dense<int> cp_move(std::move(cp_assign));
        print_perf("move ctor", 1, t.elapsed_ms());

        t.reset();
        dense<int> cp_sf;
        cp_sf.increase_capacity(N);
        for (size_t i = 0; i < N / 2; ++i)
        {
            cp_sf.emplace_back(static_cast<int>(i));
        }
        cp_sf.shrink_to_fit();
        print_perf("shrink_to_fit", N / 2, t.elapsed_ms());

        t.reset();
        dense<int> cp_cl;
        cp_cl.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_cl.emplace_back(static_cast<int>(i));
        }
        cp_cl.clear();
        print_perf("clear", N, t.elapsed_ms());

        print_perf_sub("2.3 访问与遍历 (1M/百万)");
        class_pool<int> cp_acc;
        cp_acc.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            cp_acc.emplace_back(static_cast<int>(i));
        }

        t.reset();
        volatile int v_idx = 0;
        for (size_t i = 0; i < N; ++i)
        {
            v_idx = cp_acc[i];
        }
        print_perf("operator[]", N, t.elapsed_ms());

        t.reset();
        volatile int v_get = 0;
        for (size_t i = 0; i < N; ++i)
        {
            v_get = cp_acc.get(i);
        }
        print_perf("get(i) 等价 operator[]", N, t.elapsed_ms());

        // get(index, error_index): 越界保护访问, 一半合法一半越界
        t.reset();
        volatile int v_getb = 0;
        for (size_t i = 0; i < N; ++i)
        {
            // i < N 时合法, 用 (i + N) 模拟越界, error_index=0
            v_getb = cp_acc.get(i + N, 0);
        }
        print_perf("get(i+N, 0) 全越界", N, t.elapsed_ms());

        t.reset();
        volatile int v_fb = 0;
        for (size_t i = 0; i < N; ++i)
        {
            v_fb = cp_acc.front();
            v_fb = cp_acc.back();
        }
        print_perf("front/back", N * 2, t.elapsed_ms());

        t.reset();
        volatile long long sum = 0;
        for (int iter = 0; iter < 10; ++iter)
        {
            for (auto it = cp_acc.cbegin(); it != cp_acc.cend(); ++it)
            {
                sum = *it;
            }
        }
        print_perf("cbegin/cend iter (10x)", N * 10, t.elapsed_ms());

        // range-for (begin/end 迭代器): 基准对照
        t.reset();
        volatile long long sum_rf = 0;
        for (int iter = 0; iter < 10; ++iter)
        {
            for (auto& v : cp_acc)
            {
                sum_rf = v;
            }
        }
        print_perf("range-for iter (10x)", N * 10, t.elapsed_ms());

        print_perf_sub("2.4 状态查询 (1M/百万)");
        t.reset();
        volatile bool d = false;
        for (int i = 0; i < 1000000; ++i)
        {
            d = cp_acc.is_dense();
        }
        print_perf("is_dense", 1000000, t.elapsed_ms());

        t.reset();
        volatile size_t sq = 0;
        volatile bool eq = false;
        for (int i = 0; i < 1000000; ++i)
        {
            sq = cp_acc.size();
            sq = cp_acc.capacity();
            sq = cp_acc.sparse_capacity();
            sq = cp_acc.size_bytes();
            sq = cp_acc.capacity_bytes();
            eq = cp_acc.empty();
        }
        print_perf("capacity queries", 1000000 * 6, t.elapsed_ms());
    }

    // ============================================================
    // Section 3: void_any / 类型擦除 (1M/百万)
    // ============================================================
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
        print_perf("ctor(T&&)", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            va_pool[i].set(static_cast<double>(i));
        }
        print_perf("set<T>", N, t.elapsed_ms());

        print_perf_sub("3.2 查询与访问 (1M/百万)");
        t.reset();
        size_t hv = 0;
        for (size_t i = 0; i < N; ++i)
        {
            if (va_pool[i].has_value()) ++hv;
        }
        print_perf("has_value", N, t.elapsed_ms());

        t.reset();
        volatile int tidv = 0;
        for (size_t i = 0; i < N; ++i)
        {
            tidv = va_pool[i].type_id();
        }
        print_perf("type_id", N, t.elapsed_ms());

        t.reset();
        volatile double* dp = nullptr;
        for (size_t i = 0; i < N; ++i)
        {
            dp = va_pool[i].get_ptr<double>();
        }
        print_perf("get_ptr<T>", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            dp = va_pool[i].fast_get_ptr<double>();
        }
        print_perf("fast_get_ptr<T>", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            dp = va_pool[i].get_ptr_unchecked<double>();
        }
        print_perf("get_ptr_unchecked<T>", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            double v = va_pool[i].get<double>();
            (void)v;
        }
        print_perf("get<T>", N, t.elapsed_ms());

        print_perf_sub("3.3 重置与拷贝 (1M/百万)");
        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            va_pool[i].reset();
        }
        print_perf("reset", N, t.elapsed_ms());

        t.reset();
        void_any va_src(42);
        for (size_t i = 0; i < N; ++i)
        {
            void_any va_copy_obj(va_src);
        }
        print_perf("copy ctor", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            void_any va_tmp(42);
            void_any va_move(std::move(va_tmp));
        }
        print_perf("move ctor", N, t.elapsed_ms());
    }

    // ============================================================
    // Section 4: memory_pool / 内存池 (1M/百万)
    // ============================================================
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
        print_perf("allocate(64)", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            mp.deallocate(ptrs[i]);
        }
        print_perf("deallocate", N, t.elapsed_ms());

        t.reset();
        dense<int*> iptrs;
        iptrs.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            iptrs.emplace_back(mp.construct<int>(static_cast<int>(i)));
        }
        print_perf("construct<int>", N, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < N; ++i)
        {
            mp.destroy(iptrs[i]);
        }
        print_perf("destroy<int>", N, t.elapsed_ms());

        print_perf_sub("4.2 容量与状态 (1M/百万)");
        t.reset();
        memory_pool mp2(4096);
        mp2.increase_capacity(8 * 1024 * 1024);
        print_perf("increase_capacity", 1, t.elapsed_ms());

        t.reset();
        mp2.reduce_capacity(0);
        print_perf("reduce_capacity", 1, t.elapsed_ms());

        t.reset();
        memory_pool mp3(4096);
        for (size_t i = 0; i < 10000; ++i)
        {
            void* p = mp3.allocate(64);
            (void)p;
        }
        mp3.reset();
        print_perf("reset", 10000, t.elapsed_ms());

        t.reset();
        volatile bool own = false;
        for (int i = 0; i < 1000000; ++i)
        {
            own = mp.owns(ptrs[i % 10000]);
        }
        print_perf("owns", 1000000, t.elapsed_ms());

        t.reset();
        volatile size_t ta = 0, tu = 0;
        volatile bool em = false;
        volatile size_t cs = 0;
        for (int i = 0; i < 1000000; ++i)
        {
            ta = mp.total_allocated();
            tu = mp.total_used();
            em = mp.empty();
            cs = mp.chunk_size();
        }
        print_perf("state queries", 1000000 * 4, t.elapsed_ms());

        t.reset();
        pool_stats st;
        for (int i = 0; i < 1000000; ++i)
        {
            st = mp.stats();
        }
        print_perf("stats", 1000000, t.elapsed_ms());
    }

    // ============================================================
    // Section 5: Allocators / 分配器 (arena / slab / layered) (1M/百万)
    // ============================================================
    print_section(5, "Allocators / 分配器 (arena/slab/layered) (1M/百万)");
    {
        const size_t alloc_count = 1000000;

        print_perf_sub("5.1 arena_allocator (1M/百万)");
        t.reset();
        arena_allocator ar(16 * 1024 * 1024);
        volatile void* p = nullptr;
        for (size_t i = 0; i < alloc_count; ++i)
        {
            p = ar.allocate(16);
            if (!p)
            {
                ar.reset();
                p = ar.allocate(16);
            }
        }
        print_perf("arena allocate(16)", alloc_count, t.elapsed_ms());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            ar.reset();
        }
        print_perf("arena reset", 1000000, t.elapsed_ms());

        t.reset();
        volatile size_t au = 0, ac = 0, ar2 = 0;
        volatile bool ae = false;
        for (int i = 0; i < 1000000; ++i)
        {
            au = ar.used();
            ac = ar.capacity();
            ar2 = ar.remaining();
            ae = ar.empty();
        }
        print_perf("arena state queries", 1000000 * 4, t.elapsed_ms());

        void* q = ar.allocate(32);
        t.reset();
        volatile bool ao = false;
        for (int i = 0; i < 1000000; ++i)
        {
            ao = ar.owns(q);
        }
        print_perf("arena owns", 1000000, t.elapsed_ms());

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
        print_perf("slab allocate(64)", alloc_count, t.elapsed_ms());

        t.reset();
        for (void* sp : sptrs)
        {
            sl.deallocate(sp);
        }
        print_perf("slab deallocate", alloc_count, t.elapsed_ms());

        void* test_p = sl.allocate();
        t.reset();
        volatile bool so = false;
        for (int i = 0; i < 1000000; ++i)
        {
            so = sl.owns(test_p);
        }
        print_perf("slab owns", 1000000, t.elapsed_ms());

        t.reset();
        volatile size_t sb = 0, st2 = 0, sf = 0;
        volatile bool se = false;
        for (int i = 0; i < 1000000; ++i)
        {
            sb = sl.block_size();
            st2 = sl.total_blocks();
            sf = sl.free_blocks();
            se = sl.empty();
        }
        print_perf("slab state queries", 1000000 * 4, t.elapsed_ms());

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
        print_perf("layered allocate(64) slab", alloc_count, t.elapsed_ms());

        t.reset();
        for (void* lp : small_ptrs)
        {
            la.deallocate(lp);
        }
        print_perf("layered deallocate(slab)", alloc_count, t.elapsed_ms());

        t.reset();
        dense<int*> liptrs;
        liptrs.increase_capacity(alloc_count);
        for (size_t i = 0; i < alloc_count; ++i)
        {
            liptrs.emplace_back(la.construct<int>(static_cast<int>(i)));
        }
        print_perf("layered construct<int>", alloc_count, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < alloc_count; ++i)
        {
            la.destroy(liptrs[i]);
        }
        print_perf("layered destroy<int>", alloc_count, t.elapsed_ms());

        void* lo_p = la.allocate(48);
        t.reset();
        volatile bool lo = false;
        for (int i = 0; i < 1000000; ++i)
        {
            lo = la.owns(lo_p);
        }
        print_perf("layered owns", 1000000, t.elapsed_ms());

        t.reset();
        volatile size_t ls = 0;
        for (int i = 0; i < 1000000; ++i)
        {
            ls = la.slab_max();
        }
        print_perf("layered slab_max", 1000000, t.elapsed_ms());
    }

    // ============================================================
    // Section 6: operating_message / 操作消息 (1M/百万)
    // ============================================================
    print_section(6, "operating_message / 操作消息 (1M/百万)");
    {
        const size_t om_count = 1000000;
        volatile size_t sink = 0;

        print_perf_sub("6.1 write_message 系列 (1M/百万)");
        t.reset();
        operating_message om1;
        for (size_t i = 0; i < om_count; ++i)
        {
            om1.reset();
            om1.write_message(true, "msg", i);
        }
        sink += om1.message_size();
        print_perf("write_message", om_count, t.elapsed_ms());

        t.reset();
        operating_message om2;
        for (size_t i = 0; i < om_count; ++i)
        {
            om2.reset();
            om2.write_message_fmt(true, "fmt: {} + {}", i, i + 1);
        }
        sink += om2.message_size();
        print_perf("write_message_fmt", om_count, t.elapsed_ms());

        t.reset();
        operating_message om_lv;
        om_lv.set_min_level(msg_level::debug);
        for (size_t i = 0; i < om_count; ++i)
        {
            om_lv.reset();
            om_lv.write_message_level(msg_level::info, true, "msg", i);
        }
        sink += om_lv.message_size();
        print_perf("write_message_level", om_count, t.elapsed_ms());

        t.reset();
        operating_message om_lv2;
        om_lv2.set_min_level(msg_level::debug);
        for (size_t i = 0; i < om_count; ++i)
        {
            om_lv2.reset();
            om_lv2.write_message_fmt_level(msg_level::warn, true, "v={}", i);
        }
        sink += om_lv2.message_size();
        print_perf("write_message_fmt_level", om_count, t.elapsed_ms());

        // 等级过滤快速路径
        t.reset();
        operating_message om_f;
        om_f.set_min_level(msg_level::error);
        for (size_t i = 0; i < om_count; ++i)
        {
            om_f.reset();
            om_f.write_message_level(msg_level::debug, true, "filtered", i);
        }
        sink += om_f.message_size();
        print_perf("level filter fast path", om_count, t.elapsed_ms());

        // 混合类型
        t.reset();
        operating_message om_mix;
        for (size_t i = 0; i < om_count; ++i)
        {
            om_mix.reset();
            om_mix.write_message(true, "i=", i, " d=", 3.14, " s=", std::string_view("x"));
        }
        sink += om_mix.message_size();
        print_perf("write_message mixed", om_count, t.elapsed_ms());

        print_perf_sub("6.2 operator+= 与状态查询 (1M/百万)");
        t.reset();
        for (size_t i = 0; i < om_count; ++i)
        {
            operating_message om3;
            om3 += "hello";
            om3 += " world";
            sink += om3.message_size();
        }
        print_perf("operator+=(str)", om_count * 2, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < om_count; ++i)
        {
            operating_message om4, om5;
            om5.write_message(true, "src");
            om4 += std::move(om5);
            sink += om4.message_size();
        }
        print_perf("operator+=(om&&)", om_count, t.elapsed_ms());

        t.reset();
        operating_message om6;
        om6.reserve(4096);
        for (size_t i = 0; i < om_count; ++i)
        {
            om6.reset();
            om6.set_switch_bool(false);
            volatile bool b = om6.get_switch_bool();
            om6.clear_message();
            (void)b;
        }
        print_perf("reset/clear/switch", om_count, t.elapsed_ms());

        t.reset();
        om6.reset();
        om6.write_message(true, "test");
        for (size_t i = 0; i < om_count; ++i)
        {
            volatile bool b = (bool)om6;
            auto sv = om6.read_message();
            (void)sv;
            (void)b;
        }
        print_perf("read/bool", om_count, t.elapsed_ms());

        (void)sink;
    }

    // ============================================================
    // Section 7: id_allocation / ID 分配 (1M/百万)
    // ============================================================
    print_section(7, "id_allocation / ID 分配 (1M/百万)");
    {
        const size_t id_count = 1000000;

        print_perf_sub("7.1 分配与回收 (1M/百万)");
        t.reset();
        id_allocation<int> ida;
        volatile int id_sink = 0;
        for (size_t i = 0; i < id_count; ++i)
        {
            id_sink = ida.get_id();
        }
        (void)id_sink;
        print_perf("get_id", id_count, t.elapsed_ms());

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
        print_perf("free_id", id_count, t.elapsed_ms());

        // 回收再分配
        t.reset();
        volatile int id_sink2 = 0;
        for (size_t i = 0; i < id_count; ++i)
        {
            id_sink2 = ida.get_id();
        }
        (void)id_sink2;
        print_perf("recycle get_id", id_count, t.elapsed_ms());

        t.reset();
        volatile size_t tn = 0, mx = 0;
        for (int i = 0; i < 1000000; ++i)
        {
            tn = ida.total_number_of_ids();
            mx = ida.maximum_id();
        }
        print_perf("total/maximum", 1000000, t.elapsed_ms());
    }

    // ============================================================
    // Section 8: single_class_set / 组件集合 (1M/百万)
    // ============================================================
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

        // add_batch (span)
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
        print_perf("add_batch(span)", scs_count / 10, t.elapsed_ms());

        // add_batch (class_pool 右值)
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
        print_perf("add_batch(rvalue)", scs_count / 10, t.elapsed_ms());

        print_perf_sub("8.2 查询接口 (1M/百万)");
        t.reset();
        volatile uint32_t gv = 0;
        for (size_t i = 0; i < scs_count; ++i)
        {
            gv = scs.get_version(static_cast<uint32_t>(i));
        }
        print_perf("get_version", scs_count, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            gv = scs.get_version_unchecked(static_cast<uint32_t>(i));
        }
        print_perf("get_version_unchecked", scs_count, t.elapsed_ms());

        t.reset();
        volatile uint32_t gd = 0;
        for (size_t i = 0; i < scs_count; ++i)
        {
            gd = scs.get_dense_at(static_cast<uint32_t>(i));
        }
        print_perf("get_dense_at", scs_count, t.elapsed_ms());

        t.reset();
        volatile uint32_t sda = 0;
        for (size_t i = 0; i < scs_count; ++i)
        {
            sda = scs.sparse_dense_at_public(static_cast<uint32_t>(i));
        }
        print_perf("sparse_dense_at_public", scs_count, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            sda = scs.sparse_version_at_public(static_cast<uint32_t>(i));
        }
        print_perf("sparse_version_at_public", scs_count, t.elapsed_ms());

        t.reset();
        volatile bool ce = false;
        for (size_t i = 0; i < scs_count; ++i)
        {
            ce = scs.contains_entity(ents[i]);
        }
        print_perf("contains_entity", scs_count, t.elapsed_ms());

        print_perf_sub("8.3 get_ptr 与预取 (1M/百万)");
        t.reset();
        volatile Position* pp = nullptr;
        for (size_t i = 0; i < scs_count; ++i)
        {
            pp = scs.get_ptr<Position>(ents[i]);
        }
        print_perf("get_ptr<T>", scs_count, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            pp = scs.get_ptr_fast<Position>(ents[i]);
        }
        print_perf("get_ptr_fast<T>", scs_count, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            scs.prefetch_ptr(ents[i]);
        }
        print_perf("prefetch_ptr", scs_count, t.elapsed_ms());

        t.reset();
        for (size_t i = 0; i < scs_count; ++i)
        {
            scs.prefetch_ptr_data<Position>(ents[i]);
        }
        print_perf("prefetch_ptr_data", scs_count, t.elapsed_ms());

        t.reset();
        for (int iter = 0; iter < 100; ++iter)
        {
            scs.prefetch_ptr_batch(ents.data(), 64);
        }
        print_perf("prefetch_ptr_batch", 100 * 64, t.elapsed_ms());

        // get_ptr_batch
        {
            dense<Position*> results;
            results.increase_capacity(64);
            t.reset();
            for (int iter = 0; iter < 10000; ++iter)
            {
                scs.get_ptr_batch<Position>(ents.data(), results.data(), 64);
            }
            print_perf("get_ptr_batch", 10000 * 64, t.elapsed_ms());
        }

        print_perf_sub("8.4 元数据与状态 (1M/百万)");
        t.reset();
        volatile size_t ss = 0;
        for (int i = 0; i < 1000000; ++i)
        {
            ss = scs.get_sparse_size();
        }
        print_perf("sparse_size", 1000000, t.elapsed_ms());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            scs.clear_hot_set();
        }
        print_perf("clear_hot_set", 1000000, t.elapsed_ms());

        t.reset();
        volatile dense<Position>* tpp = nullptr;
        volatile dense<uint32_t>* eip = nullptr;
        volatile uint64_t pv = 0;
        for (int i = 0; i < 1000000; ++i)
        {
            tpp = scs.get_typed_pool_ptr<Position>();
            eip = &scs.get_entity_indices();
            pv = scs.get_pool_version();
        }
        print_perf("metadata queries", 1000000 * 3, t.elapsed_ms());

        t.reset();
        volatile size_t sz = 0;
        volatile bool ey = false;
        volatile int ti = 0;
        for (int i = 0; i < 1000000; ++i)
        {
            sz = scs.size();
            ey = scs.empty();
            ti = scs.get_type_id();
        }
        print_perf("size/empty/type_id", 1000000 * 3, t.elapsed_ms());

        // soft_remove / hard_remove
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
        print_perf("soft_remove", scs_count, t.elapsed_ms());
    }

    // ============================================================
    // Section 9: radix_sort / 基数排序 (1M/百万)
    // ============================================================
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
            print_perf("radix_sort_entries<int>", rdx_count, t.elapsed_ms());
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
            print_perf("radix_sort_entries<float>", rdx_count, t.elapsed_ms());
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
            print_perf("radix_sort_entries<uint64_t>", rdx_count, t.elapsed_ms());
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
            print_perf("radix_sort_indices<int>", rdx_count, t.elapsed_ms());
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
            print_perf("pdqsort<int>", rdx_count, t.elapsed_ms());

            t.reset();
            tiered_sort<int>(data.data(), rdx_count, [](const int& a, const int& b) noexcept { return a < b; });
            print_perf("tiered_sort<int>", rdx_count, t.elapsed_ms());
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
            print_perf("sort_n<5> network", small_n * 5, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < small_n; ++i)
            {
                ::sort_n<16>(&v16[i * 16]);
            }
            print_perf("sort_n<16> network", small_n * 16, t.elapsed_ms());

            ::operator delete(v5);
            ::operator delete(v16);
        }

        // radix_key
        t.reset();
        volatile unsigned int rk = 0;
        for (size_t i = 0; i < rdx_count; ++i)
        {
            rk = radix_key(static_cast<int>(i));
        }
        print_perf("radix_key<int>", rdx_count, t.elapsed_ms());
    }

    // ============================================================
    // Section 10: Manager / 管理器核心接口 (1M/百万)
    // ============================================================
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
            print_perf("create_entity", op_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr3.add(op_ents[i], Position{1.0f, 0.0f, 0.0f});
            }
            print_perf("add<T>", op_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr3.hard_remove<Position>(op_ents[i]);
            }
            print_perf("hard_remove<T>", op_count, t.elapsed_ms());

            for (size_t i = 0; i < op_count; ++i)
            {
                mgr3.add(op_ents[i], Velocity{1.0f, 0.0f, 0.0f});
            }
            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr3.soft_remove<Velocity>(op_ents[i]);
            }
            print_perf("soft_remove<T>", op_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < op_count / 2; ++i)
            {
                mgr3.delete_entity(op_ents[i]);
            }
            print_perf("delete_entity", op_count / 2, t.elapsed_ms());

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
            print_perf("addc chain", op_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr_ch.hard_removec<Position>(ents_ch[i]);
            }
            print_perf("hard_removec chain", op_count, t.elapsed_ms());

            // add(T, e) 反向参数
            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                mgr_ch.add(Velocity{1.0f, 0, 0}, ents_ch[i]);
            }
            print_perf("add(T,e) reverse", op_count, t.elapsed_ms());
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
                volatile bool b = (p != nullptr);
                (void)b;
            }
            print_perf("get_ptr<T>", op_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
            {
                auto* p = ecss.get_ptr_fast<Position>(query_ents[i]);
                volatile bool b = (p != nullptr);
                (void)b;
            }
            print_perf("get_ptr_fast<T>", op_count, t.elapsed_ms());

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
                    volatile float d = p->x;
                    (void)d;
                }
            }
            print_perf("prefetch+get_ptr", pf_hit, t.elapsed_ms());

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
                    volatile float d = p->x;
                    (void)d;
                }
            }
            print_perf("cached+prefetch", cached_hit, t.elapsed_ms());

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
                        volatile float d = p->x;
                        (void)d;
                    }
                }
            }
            print_perf("query_context+prefetch", ctx_hit, t.elapsed_ms());

            // get_ptr_batch
            {
                dense<Position*> results;
                results.reserve_exact(op_count);
                t.reset();
                ecss.get_ptr_batch<Position>(query_ents.data(), results.data(), op_count);
                print_perf("get_ptr_batch", op_count, t.elapsed_ms());
            }

            // prefetch_ptr_batch
            t.reset();
            for (int iter = 0; iter < 100; ++iter)
            {
                ecss.prefetch_ptr_batch<Position>(query_ents.data(), 64);
            }
            print_perf("prefetch_ptr_batch", 100 * 64, t.elapsed_ms());
        }

        print_perf_sub("10.3 元数据与状态查询 (1M/百万)");
        {
            t.reset();
            volatile bool ev = false;
            for (size_t i = 0; i < N; ++i)
            {
                ev = ecss.is_entity_valid(entities[i]);
            }
            print_perf("is_entity_valid", N, t.elapsed_ms());

            t.reset();
            volatile uint64_t cb = 0;
            for (int i = 0; i < 1000000; ++i)
            {
                cb = ecss.get_component_bit<Position>();
            }
            print_perf("get_component_bit", 1000000, t.elapsed_ms());

            t.reset();
            volatile const ecs::component_meta* cm = nullptr;
            int pid = type_id::get_type_id<Position>();
            for (int i = 0; i < 1000000; ++i)
            {
                cm = ecss.get_component_meta(pid);
            }
            print_perf("get_component_meta", 1000000, t.elapsed_ms());

            t.reset();
            volatile single_class_set* scs = nullptr;
            for (int i = 0; i < 1000000; ++i)
            {
                scs = ecss.get_single_class_set<Position>();
            }
            print_perf("get_single_class_set", 1000000, t.elapsed_ms());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                scs = ecss.get_single_class_set_by_id(pid);
            }
            print_perf("get_single_class_set_by_id", 1000000, t.elapsed_ms());

            t.reset();
            volatile dense<Position>* cv = nullptr;
            for (int i = 0; i < 1000000; ++i)
            {
                cv = ecss.get_component_container<Position>();
            }
            print_perf("get_component_container", 1000000, t.elapsed_ms());

            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                auto& emr = ecss.get_entity_manager();
                (void)emr;
            }
            print_perf("get_entity_manager", 1000000, t.elapsed_ms());
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
            print_perf("sort_entities_by_component", sort_n_, t.elapsed_ms());

            t.reset();
            sort_mgr.reorder_by_component<Position, Velocity>(
                [](const Velocity& a, const Velocity& b) noexcept { return a.vx < b.vx; });
            print_perf("reorder_by_component", sort_n_, t.elapsed_ms());

            // delete_type_container
            t.reset();
            sort_mgr.delete_type_container<Velocity>();
            print_perf("delete_type_container", sort_n_, t.elapsed_ms());
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
                print_perf("signal entity_created", sig_count, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                {
                    mgr.delete_entity(ents[i]);
                }
                print_perf("signal entity_destroyed", sig_count, t.elapsed_ms());
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
                print_perf("signal on_add<Position>", sig_count, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                {
                    mgr.hard_remove<Position>(ents[i]);
                }
                print_perf("signal on_remove<Position>", sig_count, t.elapsed_ms());
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
                print_perf("signal on_modify overwrite", sig_count, t.elapsed_ms());
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
                print_perf("flush_entity_signals", sig_count, t.elapsed_ms());
            }

            // 信号开关 / 容量
            {
                ecs::manager mgr_chk;
                t.reset();
                volatile bool hp = false;
                for (int i = 0; i < 1000000; ++i)
                {
                    hp = mgr_chk.has_pending_entity_signals();
                    hp = mgr_chk.has_pending_component_signals();
                }
                print_perf("has_pending_signals", 1000000 * 2, t.elapsed_ms());

                t.reset();
                mgr_chk.reserve_entity_signal_capacity(2048);
                mgr_chk.reserve_comp_signal_capacity(2048);
                print_perf("reserve_signal_capacity", 2, t.elapsed_ms());

                t.reset();
                volatile uint64_t ov = 0;
                for (int i = 0; i < 1000000; ++i)
                {
                    ov = mgr_chk.entity_signal_overflow_count();
                    ov = mgr_chk.comp_signal_overflow_count();
                }
                print_perf("overflow_count query", 1000000 * 2, t.elapsed_ms());

                t.reset();
                for (int i = 0; i < 1000000; ++i)
                {
                    mgr_chk.disable_comp_signals();
                    mgr_chk.enable_comp_signals();
                    mgr_chk.disable_track_changes();
                    mgr_chk.enable_track_changes();
                }
                print_perf("enable/disable switches", 1000000 * 4, t.elapsed_ms());
            }
        }
    }

    // ============================================================
    // Section 11: Views / 视图查询 (1M/百万)
    // ============================================================
    print_section(11, "Views / 视图查询 (1M/百万)");
    {
        const size_t view_count = N;

        print_perf_sub("11.1 single_view 遍历 (1M/百万)");
        t.reset();
        size_t cnt_sv = 0;
        ecss.view<Position>().for_each([&](Position& p) {
            ++cnt_sv;
            volatile float d = p.x;
            (void)d;
        });
        print_perf("single_view for_each", cnt_sv, t.elapsed_ms());

        // single_view 索引接口
        {
            auto v = ecss.view<Position>();
            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = v.get_first_entity();
            }
            print_perf("single_view get_first_entity", 1000000, t.elapsed_ms());

            t.reset();
            entity le{};
            for (int i = 0; i < 1000000; ++i)
            {
                le = v.get_last_entity();
            }
            print_perf("single_view get_last_entity", 1000000, t.elapsed_ms());

            t.reset();
            entity ee{};
            for (size_t i = 0; i < view_count; ++i)
            {
                ee = v.get_entity_at_index(i);
            }
            print_perf("single_view get_entity_at_index", view_count, t.elapsed_ms());

            t.reset();
            volatile Position* cp = nullptr;
            for (size_t i = 0; i < view_count; ++i)
            {
                cp = v.get_component_at_index(i);
            }
            print_perf("single_view get_component_at_index", view_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < view_count; ++i)
            {
                cp = v.get_component_for_entity(entities[i]);
            }
            print_perf("single_view get_component_for_entity", view_count, t.elapsed_ms());

            t.reset();
            volatile bool ct = false;
            for (size_t i = 0; i < view_count; ++i)
            {
                ct = v.contains(entities[i]);
            }
            print_perf("single_view contains", view_count, t.elapsed_ms());

            t.reset();
            volatile float sum = 0;
            for (auto it = v.component_begin(); it != v.component_end(); ++it)
            {
                sum = it->x;
            }
            print_perf("single_view component_begin/end", view_count, t.elapsed_ms());
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

        // include_optional_component
        {
            auto v_opt = ecss.view<Position, Velocity>().include_optional_component<Health>();
            t.reset();
            volatile size_t cnt = 0;
            v_opt.for_each([&](entity, Position&, Velocity&, Health* h) {
                if (h)
                {
                    cnt = cnt + 1;
                }
            });
            print_perf("include_optional_component", view_count, t.elapsed_ms());
        }

        // multi_view 索引接口
        {
            auto v2 = ecss.view<Position, Velocity>();
            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = v2.get_first_entity();
            }
            print_perf("multi_view get_first_entity", 1000000, t.elapsed_ms());

            t.reset();
            volatile Position* pp = nullptr;
            for (size_t i = 0; i < view_count; ++i)
            {
                pp = v2.get_component_for_entity<Position>(entities[i]);
            }
            print_perf("multi_view get_component_for_entity", view_count, t.elapsed_ms());

            t.reset();
            volatile bool ct = false;
            for (size_t i = 0; i < view_count; ++i)
            {
                ct = v2.contains(entities[i]);
            }
            print_perf("multi_view contains", view_count, t.elapsed_ms());
        }

        // page / track_changes / sorted
        print_perf_sub("11.3 page / track_changes / sorted (500K/五十万)");
        t.reset();
        size_t cnt_page = 0;
        {
            auto mv = ecss.view<Position, Velocity>();
            mv.page(0, mv.size()).for_each([&](Position&, Velocity&) { ++cnt_page; });
        }
        print_perf("multi_view page", cnt_page, t.elapsed_ms());

        t.reset();
        size_t cnt_changed = 0;
        {
            auto mv = ecss.view<Position, Velocity>();
            auto cv = mv.track_changes();
            cv.for_each([&](Position&, Velocity&) { ++cnt_changed; });
        }
        print_perf("track_changes", cnt_changed, t.elapsed_ms());

        // sorted_by_component (小规模)
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
            print_perf("multi_view sorted_by_component", cnt_sorted, t.elapsed_ms());

            t.reset();
            size_t cnt_grp = 0;
            {
                auto sv = sort_mgr.view<Position>();
                auto gv = sv.sorted_by_component_value(
                    [](Position& p) -> int { return static_cast<int>(p.x) / 10; });
                gv.for_each([&](Position&) { ++cnt_grp; });
            }
            print_perf("sorted_by_component_value", cnt_grp, t.elapsed_ms());
        }

        // filter_changed / filter_added
        print_perf_sub("11.4 filter_changed / filter_added / exactly_one / find_one");
        {
            auto cv = ecss.view<Position>().filter_changed();
            t.reset();
            volatile size_t cnt = 0;
            cv.for_each([&](Position&) { cnt = cnt + 1; });
            print_perf("single_view filter_changed first", view_count, t.elapsed_ms());

            // 修改部分组件后增量
            for (size_t i = 0; i < view_count; i += 10)
            {
                ecss.add(entities[i], Position{999.0f, 0, 0});
            }
            t.reset();
            cnt = 0;
            cv.for_each([&](Position&) { cnt = cnt + 1; });
            print_perf("single_view filter_changed delta", view_count / 10, t.elapsed_ms());
            cv.reset_tracking();

            // multi_view filter_changed
            auto mcv = ecss.view<Position, Velocity>().filter_changed<Position>();
            t.reset();
            volatile size_t mcnt = 0;
            mcv.for_each([&](Position&, Velocity&) { mcnt = mcnt + 1; });
            print_perf("multi_view filter_changed", view_count, t.elapsed_ms());

            // filter_added
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
            volatile size_t acnt = 0;
            av.for_each([&](Position&) { acnt = acnt + 1; });
            print_perf("single_view filter_added", view_count, t.elapsed_ms());
        }

        // exactly_one / find_one / iter_over_entities
        {
            ecs::manager mgr_eo;
            mgr_eo.append_preallocated_entities(10);
            entity e1 = mgr_eo.create_entity();
            mgr_eo.add(e1, Position{42.0f, 0, 0});
            mgr_eo.add(e1, Velocity{1.0f, 0, 0});

            auto v_eo = mgr_eo.view<Position, Velocity>();
            t.reset();
            volatile float px = 0;
            for (int i = 0; i < 1000000; ++i)
            {
                auto [p, v] = v_eo.exactly_one();
                px = p.x;
            }
            print_perf("multi_view exactly_one", 1000000, t.elapsed_ms());

            auto v_eo2 = mgr_eo.view<Position>();
            t.reset();
            for (int i = 0; i < 1000000; ++i)
            {
                auto& p = v_eo2.exactly_one();
                px = p.x;
            }
            print_perf("single_view exactly_one", 1000000, t.elapsed_ms());

            // find_one
            auto v_fo = ecss.view<Position, Velocity>();
            t.reset();
            volatile Position* pp = nullptr;
            for (size_t i = 0; i < view_count; ++i)
            {
                auto [p, v] = v_fo.find_one(entities[i]);
                pp = p;
            }
            print_perf("multi_view find_one", view_count, t.elapsed_ms());

            // iter_over_entities
            dense<entity> targets;
            targets.increase_capacity(view_count / 10);
            for (size_t i = 0; i < view_count; i += 10)
            {
                targets.emplace_back(entities[i]);
            }
            auto v_ie = ecss.view<Position, Velocity>().iter_over_entities(targets);
            t.reset();
            volatile size_t cnt = 0;
            v_ie.for_each([&](Position&, Velocity&) { cnt = cnt + 1; });
            print_perf("iter_over_entities", targets.size(), t.elapsed_ms());
        }

        print_perf_sub("11.5 复合视图 without/with/or/any_of (1M/百万)");
        t.reset();
        size_t cnt_excl = 0;
        ecss.view<Position>(ecs::without<Velocity>).for_each([&](Position& p) {
            ++cnt_excl;
            (void)p;
        });
        print_perf("without<Velocity>", cnt_excl, t.elapsed_ms());

        t.reset();
        size_t cnt_with = 0;
        ecss.view<Position>(ecs::with<Health>).for_each([&](Position& p, Health* hp) {
            ++cnt_with;
            (void)p;
            (void)hp;
        });
        print_perf("with<Health>", cnt_with, t.elapsed_ms());

        t.reset();
        size_t cnt_or = 0;
        ecss.view_or<Position, Velocity>().for_each([&](entity, Position* p, Velocity* v) {
            ++cnt_or;
            (void)p;
            (void)v;
        });
        print_perf("view_or<Pos,Vel>", cnt_or, t.elapsed_ms());

        t.reset();
        size_t cnt_any = 0;
        ecss.view_any_of<Position, Velocity, Health>().for_each([&](Position* p, Velocity* v, Health* h) {
            ++cnt_any;
            (void)p;
            (void)v;
            (void)h;
        });
        print_perf("view_any_of<3 types>", cnt_any, t.elapsed_ms());

        print_perf_sub("11.6 filter_view / filter_and / filter_or");
        t.reset();
        size_t cnt_filt = 0;
        {
            auto fv = ecss.view_filtered<Position>([](Position& p) noexcept { return p.x > 0.0f; });
            fv.for_each([&](Position&) { ++cnt_filt; });
        }
        print_perf("filter_view", cnt_filt, t.elapsed_ms());

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
            double elapsed = t.elapsed_ms() / warmup;
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
            double elapsed = t.elapsed_ms() / warmup;
            print_perf("filter_or_view", cnt_for, elapsed);
        }

        // 匹配数汇总
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

    // ============================================================
    // Section 12: Groups / Group 系统 (500K/五十万)
    // ============================================================
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
            print_perf("group rebuild", 1, t.elapsed_ms());

            t.reset();
            size_t cnt = 0;
            g.for_each([&](Position& p, Velocity& v) {
                ++cnt;
                volatile float d = p.x * v.vx;
                (void)d;
            });
            print_perf("group for_each", cnt, t.elapsed_ms());

            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = g.front();
            }
            print_perf("group front", 1000000, t.elapsed_ms());

            t.reset();
            entity le{};
            for (int i = 0; i < 1000000; ++i)
            {
                le = g.back();
            }
            print_perf("group back", 1000000, t.elapsed_ms());

            t.reset();
            volatile Position* gp = nullptr;
            for (size_t i = 0; i < grp_count; ++i)
            {
                gp = g.get<Position>(ents[i]);
            }
            print_perf("group get<T>", grp_count, t.elapsed_ms());

            t.reset();
            volatile bool ct = false;
            for (size_t i = 0; i < grp_count; ++i)
            {
                ct = g.contains(ents[i]);
            }
            print_perf("group contains", grp_count, t.elapsed_ms());

            t.reset();
            volatile size_t gs = 0;
            volatile bool ge = false;
            for (int i = 0; i < 1000000; ++i)
            {
                gs = g.size();
                ge = g.empty();
            }
            print_perf("group size/empty", 1000000 * 2, t.elapsed_ms());
        }

        print_perf_sub("12.2 Owning Group (500K/五十万)");
        {
            auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);

            t.reset();
            og.rebuild();
            print_perf("owning_group rebuild", 1, t.elapsed_ms());

            t.reset();
            size_t cnt = 0;
            og.for_each([&](Position& p, Velocity& v) {
                ++cnt;
                volatile float d = p.x * v.vx;
                (void)d;
            });
            print_perf("owning_group for_each", cnt, t.elapsed_ms());

            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = og.front();
            }
            print_perf("owning_group front", 1000000, t.elapsed_ms());

            t.reset();
            volatile Position* gp = nullptr;
            for (size_t i = 0; i < grp_count; ++i)
            {
                gp = og.get<Position>(ents[i]);
            }
            print_perf("owning_group get<T>", grp_count, t.elapsed_ms());

            t.reset();
            volatile bool ct = false;
            for (size_t i = 0; i < grp_count; ++i)
            {
                ct = og.contains(ents[i]);
            }
            print_perf("owning_group contains", grp_count, t.elapsed_ms());
        }

        print_perf_sub("12.3 Reorder Group (500K/五十万)");
        {
            auto rg = mgr.group<Position, Velocity>(ecs::reorder<Position>);

            t.reset();
            rg.rebuild();
            print_perf("reorder_group rebuild", 1, t.elapsed_ms());

            t.reset();
            size_t cnt = 0;
            rg.for_each([&](Position& p, Velocity& v) {
                ++cnt;
                volatile float d = p.x * v.vx;
                (void)d;
            });
            print_perf("reorder_group for_each", cnt, t.elapsed_ms());

            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i)
            {
                fe = rg.front();
            }
            print_perf("reorder_group front", 1000000, t.elapsed_ms());

            t.reset();
            volatile Position* gp = nullptr;
            for (size_t i = 0; i < grp_count; ++i)
            {
                gp = rg.get<Position>(ents[i]);
            }
            print_perf("reorder_group get<T>", grp_count, t.elapsed_ms());

            t.reset();
            volatile bool ct = false;
            for (size_t i = 0; i < grp_count; ++i)
            {
                ct = rg.contains(ents[i]);
            }
            print_perf("reorder_group contains", grp_count, t.elapsed_ms());

            // share_with
            auto rg2 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
            t.reset();
            rg2.share_with(rg);
            print_perf("reorder_group share_with", 1, t.elapsed_ms());

            t.reset();
            volatile size_t gs = 0;
            for (int i = 0; i < 1000000; ++i)
            {
                gs = rg2.size();
            }
            print_perf("reorder_group shared size", 1000000, t.elapsed_ms());
        }
    }

    // ============================================================
    // Section 13: runtime_view / 运行时视图 (500K/五十万)
    // ============================================================
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
        print_perf("runtime_view_create", 1, t.elapsed_ms());

        t.reset();
        volatile size_t rs = 0;
        volatile bool re = false;
        for (int i = 0; i < 1000000; ++i)
        {
            rs = rv.size();
            re = rv.empty();
        }
        print_perf("size/empty", 1000000 * 2, t.elapsed_ms());

        t.reset();
        volatile bool ct = false;
        for (size_t i = 0; i < rv_count; ++i)
        {
            ct = rv.contains(ents[i]);
        }
        print_perf("contains", rv_count, t.elapsed_ms());

        t.reset();
        volatile Position* rp = nullptr;
        for (size_t i = 0; i < rv_count; ++i)
        {
            rp = rv.get_ptr<Position>(ents[i]);
        }
        print_perf("get_ptr<T>", rv_count, t.elapsed_ms());

        t.reset();
        entity fe{};
        for (int i = 0; i < 1000000; ++i)
        {
            fe = rv.get_first_entity();
        }
        print_perf("get_first_entity", 1000000, t.elapsed_ms());

        t.reset();
        volatile size_t rc = 0;
        for (int i = 0; i < 10; ++i)
        {
            rc = rv.count();
        }
        print_perf("count()", 10, t.elapsed_ms());

        print_perf_sub("13.2 for_each 系列 (500K/五十万)");
        t.reset();
        volatile size_t cnt = 0;
        rv.for_each([&](entity) { cnt = cnt + 1; });
        print_perf("for_each", rv_count, t.elapsed_ms());

        t.reset();
        cnt = 0;
        rv.for_each_typed<Position, Velocity>([&](entity, Position&, Velocity&) { cnt = cnt + 1; });
        print_perf("for_each_typed", rv_count, t.elapsed_ms());

        t.reset();
        cnt = 0;
        rv.for_each_parallel(0, 2, [&](entity, size_t) { cnt = cnt + 1; });
        rv.for_each_parallel(1, 2, [&](entity) { cnt = cnt + 1; });
        print_perf("for_each_parallel(2w)", rv_count, t.elapsed_ms());

        t.reset();
        cnt = 0;
        rv.for_each_paged(0, rv_count / 2, [&](entity) { cnt = cnt + 1; });
        print_perf("for_each_paged", rv_count / 2, t.elapsed_ms());

        t.reset();
        cnt = 0;
        for (auto it = rv.begin(); it != rv.end(); ++it)
        {
            cnt = cnt + 1;
        }
        print_perf("begin/end iter", rv_count, t.elapsed_ms());

        print_perf_sub("13.3 变更追踪 (500K/五十万)");
        rv.reset_change_tracking();
        for (size_t i = 0; i < rv_count; i += 100)
        {
            mgr.add(ents[i], Position{999.0f, 0, 0});
        }

        t.reset();
        volatile bool ch = false;
        for (int i = 0; i < 1000000; ++i)
        {
            ch = rv.changed();
        }
        print_perf("changed()", 1000000, t.elapsed_ms());

        t.reset();
        cnt = 0;
        if (rv.changed())
        {
            rv.for_each_changed([&](entity) { cnt = cnt + 1; });
        }
        print_perf("for_each_changed", rv_count, t.elapsed_ms());

        t.reset();
        for (int i = 0; i < 1000000; ++i)
        {
            rv.reset_change_tracking();
        }
        print_perf("reset_change_tracking", 1000000, t.elapsed_ms());

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
        print_perf("create_from_terms", 1, t.elapsed_ms());

        t.reset();
        cnt = 0;
        rv_term.for_each([&](entity) { cnt = cnt + 1; });
        print_perf("term OR/NOT/OPTIONAL", rv_count, t.elapsed_ms());

        // rebuild
        t.reset();
        rv.rebuild();
        print_perf("rebuild", 1, t.elapsed_ms());
    }

    // ============================================================
    // Section 14: command_buffer / 命令缓冲 (500K/五十万)
    // ============================================================
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
        print_perf("add_component record", cb_count, t.elapsed_ms());

        t.reset();
        volatile size_t cs = 0;
        volatile bool ce = false;
        for (int i = 0; i < 1000000; ++i)
        {
            cs = cb.size();
            ce = cb.empty();
        }
        print_perf("size/empty", 1000000 * 2, t.elapsed_ms());

        t.reset();
        cb.flush();
        print_perf("flush(add)", cb_count, t.elapsed_ms());

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
        print_perf("flush(remove+destroy)", cb_count + cb_count / 2, t.elapsed_ms());

        print_perf_sub("14.3 clear (500K/五十万)");
        auto cb3 = mgr.create_command_buffer();
        for (size_t i = 0; i < cb_count; ++i)
        {
            cb3.add_component<Velocity>(ents[i], Velocity{1.0f, 0, 0});
        }
        t.reset();
        cb3.clear();
        print_perf("clear", cb_count, t.elapsed_ms());
    }

    // ============================================================
    // Section 15: Cache / 缓存命中率测试 (100K/十万 + 1M/百万)
    // ============================================================
    print_section(15, "Cache Analysis / 缓存命中率测试");
    {
        print_perf_sub("15.1 class_pool 顺序 vs 随机 (Position 12B × 100K = 1.2MB)");
        {
            const size_t cache_n = 100000;
            dense<Position> cp;
            cp.increase_capacity(cache_n);
            for (size_t i = 0; i < cache_n; ++i)
            {
                cp.emplace_back(static_cast<float>(i), 0, 0);
            }

            const Position* base = cp.data();
            const size_t stride = sizeof(Position);

            // 顺序访问
            auto seq_addrs = make_sequential_addresses(base, cache_n, stride);
            auto seq_report = measure_cache_hits(seq_addrs);
            print_cache_report("顺序访问 (逐次)", seq_report);
            auto seq_batch = measure_cache_batch(seq_addrs, 10);
            print_cache_batch("顺序访问 (批量×10)", seq_batch);

            // 随机访问
            auto rnd_addrs = make_random_addresses(base, cache_n, stride, 42);
            auto rnd_report = measure_cache_hits(rnd_addrs);
            print_cache_report("随机访问 (逐次)", rnd_report);
            auto rnd_batch = measure_cache_batch(rnd_addrs, 10);
            print_cache_batch("随机访问 (批量×10)", rnd_batch);

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

            auto seq_big = make_sequential_addresses(base_big, big_n, stride_big);
            auto seq_big_r = measure_cache_hits(seq_big);
            print_cache_report("大组件顺序 (逐次)", seq_big_r);
            auto seq_big_b = measure_cache_batch(seq_big, 5);
            print_cache_batch("大组件顺序 (批量×5)", seq_big_b);

            auto rnd_big = make_random_addresses(base_big, big_n, stride_big, 99);
            auto rnd_big_r = measure_cache_hits(rnd_big);
            print_cache_report("大组件随机 (逐次)", rnd_big_r);
            auto rnd_big_b = measure_cache_batch(rnd_big, 5);
            print_cache_batch("大组件随机 (批量×5)", rnd_big_b);

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

                auto ecs_seq = make_sequential_addresses(ecs_base, ecs_n, ecs_stride);
                auto ecs_seq_r = measure_cache_hits(ecs_seq);
                print_cache_report("ECS顺序 (逐次)", ecs_seq_r);
                auto ecs_seq_b = measure_cache_batch(ecs_seq, 3);
                print_cache_batch("ECS顺序 (批量×3)", ecs_seq_b);

                auto ecs_rnd = make_random_addresses(ecs_base, ecs_n, ecs_stride, 77);
                auto ecs_rnd_r = measure_cache_hits(ecs_rnd);
                print_cache_report("ECS随机 (逐次)", ecs_rnd_r);
                auto ecs_rnd_b = measure_cache_batch(ecs_rnd, 3);
                print_cache_batch("ECS随机 (批量×3)", ecs_rnd_b);

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

        print_perf_sub("15.4 ECS get_ptr 延迟 (含稀疏表间接寻址)");
        {
            // benchmark_cycles 测量 get_ptr 延迟 (含稀疏表查找 + 数据访问)
            size_t gi = 0;
            auto getptr_stats = benchmark_cycles(2000, 200, [&]() {
                entity e = entities[gi % N];
                Position* p = ecss.get_ptr<Position>(e);
                volatile float sink = p ? p->x : 0.0f;
                (void)sink;
                ++gi;
            });
            print_stats("get_ptr 随机延迟", getptr_stats, "周期");

            // 对比: 直接数组访问 (无稀疏表开销)
            dense<Position>* pos_pool = ecss.get_component_container<Position>();
            if (pos_pool && pos_pool->size() > 0)
            {
                size_t di = 0;
                size_t pn = pos_pool->size();
                auto direct_stats = benchmark_cycles(2000, 200, [&]() {
                    Position& p = (*pos_pool)[di % pn];
                    volatile float sink = p.x;
                    (void)sink;
                    ++di;
                });
                print_stats("直接数组访问", direct_stats, "周期");

                double overhead = getptr_stats.mean - direct_stats.mean;
                std::cout << "  >> 稀疏表间接开销: " << std::fixed << std::setprecision(3)
                          << overhead << " 周期/次\n";
            }
        }

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
                    cp_sz.emplace_back(static_cast<float>(i), 0, 0);
                }

                const Position* base = cp_sz.data();
                size_t stride = sizeof(Position);

                auto seq = make_sequential_addresses(base, tc.count, stride);
                auto rnd = make_random_addresses(base, tc.count, stride, 314);

                auto seq_b = measure_cache_batch(seq, 20);
                auto rnd_b = measure_cache_batch(rnd, 20);

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
            latency_thresholds auto_th = detect_cache_latency_thresholds();
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

            int buf[4096];
            auto addrs = make_sequential_addresses(buf, 4096, sizeof(int));
            std::cout << "  >> 默认三级阈值 → 测量结果:\n";
            cache_report r_default = measure_cache_hits(addrs);
            std::cout << "     L1: " << std::setprecision(1) << r_default.l1_hit_rate * 100 << "%"
                      << "  L2: " << r_default.l2_hit_rate * 100 << "%"
                      << "  L3: " << r_default.l3_hit_rate * 100 << "%"
                      << "  Miss: " << r_default.miss_rate * 100 << "%"
                      << "  levels=" << r_default.active_levels << "\n";

            std::cout << "  >> 自适应阈值 → 测量结果:\n";
            cache_report r_auto = measure_cache_hits(addrs, auto_th);
            std::cout << "     L1: " << r_auto.l1_hit_rate * 100 << "%"
                      << "  L2: " << r_auto.l2_hit_rate * 100 << "%"
                      << "  L3: " << r_auto.l3_hit_rate * 100 << "%"
                      << "  Miss: " << r_auto.miss_rate * 100 << "%"
                      << "  levels=" << r_auto.active_levels << "\n";
        }
    }

    // ========================================================
    // 16. entity_mask_manager 扩容/缩容/状态查询 (1M/百万)
    // ========================================================
    print_section(16, "entity_mask_manager 掩码管理 (1M/百万)");
    {
        // 16.1 单块 (num_blocks_==1) 扩容与写入
        print_perf_sub("16.1 单块扩容与写入 (1M/百万)");
        {
            entity_mask_manager m1;

            t.reset();
            m1.increase_capacity(N);
            print_perf("increase_capacity(N)", N, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                m1.ensure_entity(static_cast<uint32_t>(i));
            }
            print_perf("ensure_entity ×N", N, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                m1.set_bit_no_check(static_cast<uint32_t>(i), 0,
                                    static_cast<uint32_t>(i & 63));
            }
            print_perf("set_bit_no_check ×N", N, t.elapsed_ms());

            t.reset();
            volatile uint64_t sink = 0;
            for (size_t i = 0; i < N; ++i)
            {
                sink += m1.get_block(static_cast<uint32_t>(i), 0);
            }
            (void)sink;
            print_perf("get_block ×N", N, t.elapsed_ms());
        }

        // 16.2 状态查询开销 (百万次调用)
        print_perf_sub("16.2 状态查询开销 (1M/百万次)");
        {
            entity_mask_manager m1;
            m1.increase_capacity(N);
            for (size_t i = 0; i < N; ++i)
            {
                m1.ensure_entity(static_cast<uint32_t>(i));
            }

            t.reset();
            volatile size_t s = 0;
            for (size_t i = 0; i < N; ++i)
            {
                s += m1.size();
            }
            (void)s;
            print_perf("size() ×N", N, t.elapsed_ms());

            t.reset();
            volatile size_t c = 0;
            for (size_t i = 0; i < N; ++i)
            {
                c += m1.capacity();
            }
            (void)c;
            print_perf("capacity() ×N", N, t.elapsed_ms());

            t.reset();
            volatile bool e = false;
            for (size_t i = 0; i < N; ++i)
            {
                e = m1.empty();
            }
            (void)e;
            print_perf("empty() ×N", N, t.elapsed_ms());

            t.reset();
            volatile size_t b = 0;
            for (size_t i = 0; i < N; ++i)
            {
                b += m1.size_bytes();
            }
            (void)b;
            print_perf("size_bytes() ×N", N, t.elapsed_ms());
        }

        // 16.3 缩容/清空/预留 (单次, 规模 N)
        print_perf_sub("16.3 缩容/清空/预留 (单次, N=1M)");
        {
            entity_mask_manager m1;
            m1.increase_capacity(N);
            for (size_t i = 0; i < N; ++i)
            {
                m1.ensure_entity(static_cast<uint32_t>(i));
            }

            t.reset();
            m1.reserve_exact(N * 2);
            print_perf("reserve_exact(2N)", N, t.elapsed_ms());

            t.reset();
            m1.shrink_to_fit();
            print_perf("shrink_to_fit", N, t.elapsed_ms());

            t.reset();
            m1.reduce_capacity(N / 2);
            print_perf("reduce_capacity(N/2)", N / 2, t.elapsed_ms());

            t.reset();
            m1.clear();
            print_perf("clear", 1, t.elapsed_ms());
        }

        // 16.4 多块 (num_blocks_==2) 单位转换开销
        print_perf_sub("16.4 多块写入与查询 (2块, 1M/百万)");
        {
            entity_mask_manager m2;
            m2.reserve_blocks(2);
            m2.increase_capacity(N);

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                m2.ensure_entity(static_cast<uint32_t>(i));
            }
            print_perf("多块 ensure_entity ×N", N, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < N; ++i)
            {
                m2.set_bit_no_check(static_cast<uint32_t>(i), 0,
                                    static_cast<uint32_t>(i & 63));
                m2.set_bit_no_check(static_cast<uint32_t>(i), 1,
                                    static_cast<uint32_t>(i & 63));
            }
            print_perf("多块 set_bit_no_check ×2N", N, t.elapsed_ms());

            t.reset();
            volatile uint64_t sink = 0;
            for (size_t i = 0; i < N; ++i)
            {
                sink += m2.get_block(static_cast<uint32_t>(i), 0);
                sink += m2.get_block(static_cast<uint32_t>(i), 1);
            }
            (void)sink;
            print_perf("多块 get_block ×2N", N, t.elapsed_ms());

            t.reset();
            volatile size_t cb = 0;
            for (size_t i = 0; i < N; ++i)
            {
                cb += m2.capacity_bytes();
            }
            (void)cb;
            print_perf("多块 capacity_bytes() ×N", N, t.elapsed_ms());
        }
    }

    print_summary("性能测试");
    return 0;
}
