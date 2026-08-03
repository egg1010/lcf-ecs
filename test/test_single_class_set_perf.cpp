// test_single_class_set_perf.cpp - single_class_set 独立性能测试
#include "test_common.hpp"
#include "include/part/analysis.hpp"
#include <bit>
#include <span>
#include <cstdio>

// opaque: 使值对编译器不透明, 阻止常量折叠和死代码消除
#if defined(_MSC_VER)
    // MSVC x64 无 inline asm, 用 volatile + 屏障
    template <typename T>
    inline T opaque(T v) noexcept
    {
        volatile T sink = v;
        _ReadWriteBarrier();
        T result = sink;
        return result;
    }
#else
    // GCC/Clang: asm "+r" 寄存器往返, 零开销
    template <typename T>
    inline T opaque(T v) noexcept
    {
        asm volatile("" : "+r"(v) : :);
        return v;
    }
#endif

// touch_ptr: 强制读取指针首字节, 防止返回值被消除
inline void touch_ptr(const void* p) noexcept
{
    if (p) [[likely]]
    {
        volatile uint8_t v = *static_cast<const volatile uint8_t*>(p);
        (void)v;
    }
}

// 编译器内存屏障: 阻止跨屏障的重排和提升
inline void compiler_barrier() noexcept
{
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    asm volatile("" : : : "memory");
#endif
}

// 多次重复取最小值 (减少单次测量的调度噪声)
template <typename F>
double best_ns(int repeat, F&& fn) noexcept
{
    double best = 1e18;
    for (int r = 0; r < repeat; ++r)
    {
        stopwatch sw;
        fn();
        double ns = sw.ns();
        if (ns < best) best = ns;
    }
    return best;
}

// CPU TSC 频率估算 (首次调用校准, 后续返回缓存值)
// time.hpp 不提供 CPU 频率, 此处用 stopwatch (周期 + 墙钟) 自校准
inline double cpu_ghz_cached() noexcept
{
    static const double ghz = []() noexcept -> double {
        stopwatch sw;
        volatile double sink = 0.0;
        while (sw.ns() < 5'000'000.0)
        {
            sink += 1.0;
        }
        (void)sink;
        time_snapshot snap = sw.snapshot();
        if (snap.ns_val > 0.0)
        {
            return static_cast<double>(snap.cycles) / snap.ns_val;
        }
        return 0.0;
    }();
    return ghz;
}

// 格式化辅助 (避免与 test_common.hpp 的 print_stats 冲突, 重命名为 print_dist)
inline void print_header(const char* title) noexcept
{
    std::cout << "\n┌─ " << title << "\n";
}

inline void print_footer() noexcept
{
    std::cout << "└──────────────────────────────────────────────\n";
}

// 单次操作纳秒 + 吞吐量 (亿/s)
inline void print_ns(const char* label, size_t n, double ns) noexcept
{
    double throughput = (ns > 0 && n > 0) ? static_cast<double>(n) / ns : 0;
    std::cout << "  " << std::left << std::setw(36) << label
              << " | " << std::right << std::setw(10) << n << " 次"
              << " | " << std::fixed << std::setprecision(3) << std::setw(8) << ns << " ns"
              << " | " << std::setprecision(2) << std::setw(8) << throughput << " 亿/s\n";
}

// 统计分布输出 (min/p50/p95/p99/max + mean)
// unit="ns" 显示纳秒, unit="cyc" 显示周期
inline void print_dist(const char* label, const stats& s, const char* unit = "ns") noexcept
{
    std::cout << "  " << std::left << std::setw(36) << label
              << " | n=" << std::right << std::setw(8) << s.count
              << " | min=" << std::fixed << std::setprecision(3) << std::setw(7) << s.min
              << " p50=" << std::setw(7) << s.p50
              << " p95=" << std::setw(7) << s.p95
              << " p99=" << std::setw(7) << s.p99
              << " max=" << std::setw(7) << s.max
              << " mean=" << std::setw(7) << s.mean << " " << unit;
    std::cout << "\n";
}

// 测试组件 (全部 trivially copyable, 覆盖 4B/12B/32B 三个量级)
struct POD4  { uint32_t v; };                  // 4B  trivially copyable
struct POD12 { float x, y, z; };               // 12B trivially copyable
struct POD32 { float a[8]; };                  // 32B trivially copyable

template <typename T>
static single_class_set build_set(size_t n, std::mt19937& gen) noexcept
{
    single_class_set set;
    set.increase_capacity(n);
    if constexpr (std::is_same_v<T, POD4>)
    {
        std::uniform_int_distribution<uint32_t> d(0, 1000000);
        for (size_t i = 0; i < n; ++i)
        {
            set.add(entity(static_cast<uint32_t>(i), 1), POD4{d(gen)});
        }
    }
    else if constexpr (std::is_same_v<T, POD12>)
    {
        std::uniform_real_distribution<float> d(-1000.0f, 1000.0f);
        for (size_t i = 0; i < n; ++i)
        {
            set.add(entity(static_cast<uint32_t>(i), 1), POD12{d(gen), d(gen), d(gen)});
        }
    }
    else if constexpr (std::is_same_v<T, POD32>)
    {
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        for (size_t i = 0; i < n; ++i)
        {
            POD32 p;
            for (int k = 0; k < 8; ++k) p.a[k] = d(gen);
            set.add(entity(static_cast<uint32_t>(i), 1), p);
        }
    }
    return set;
}

// === Section 1: 增删接口性能 ===
// 修复: 1) RNG 数据在计时区外预生成 2) best_ns(3) 取最小值 3) opaque() 防止 DCE

template <typename T>
static std::vector<T> make_components(size_t n, std::mt19937& gen) noexcept
{
    std::vector<T> out;
    out.reserve(n);
    if constexpr (std::is_same_v<T, POD4>)
    {
        std::uniform_int_distribution<uint32_t> d(0, 1000000);
        for (size_t i = 0; i < n; ++i) out.push_back(POD4{d(gen)});
    }
    else if constexpr (std::is_same_v<T, POD12>)
    {
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        for (size_t i = 0; i < n; ++i) out.push_back(POD12{d(gen), d(gen), d(gen)});
    }
    else if constexpr (std::is_same_v<T, POD32>)
    {
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        for (size_t i = 0; i < n; ++i)
        {
            POD32 p;
            for (int k = 0; k < 8; ++k) p.a[k] = d(gen);
            out.push_back(p);
        }
    }
    return out;
}

template <typename T>
static void test_add_remove(size_t n) noexcept
{
    print_header(("Section 1: add/remove (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    auto comps = make_components<T>(n, gen);
    std::vector<entity> ents;
    ents.reserve(n);
    for (size_t i = 0; i < n; ++i) ents.emplace_back(static_cast<uint32_t>(i), 1);

    constexpr int REPEAT = 3;

    {
        double ns = best_ns(REPEAT, [&]() {
            single_class_set set;
            set.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
                set.add(ents[i], comps[i]);
            compiler_barrier();
            return set.size();
        });
        print_ns("add (single, append)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            single_class_set set;
            set.increase_capacity(n);
            set.add_batch(std::span<const entity>(ents.data(), n),
                          std::span<const T>(comps.data(), n));
            compiler_barrier();
            return set.size();
        });
        print_ns("add_batch (bulk)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            auto set = build_set<T>(n, gen);
            for (size_t i = n; i > 0; --i)
            {
                uint32_t idx = static_cast<uint32_t>(i - 1);
                set.hard_remove(entity(idx, 1));
            }
            compiler_barrier();
            return set.size();
        });
        print_ns("hard_remove (tail, swap_pop)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            auto set = build_set<T>(n, gen);
            for (size_t i = 0; i < n; ++i)
                set.hard_remove(entity(static_cast<uint32_t>(i), 1));
            compiler_barrier();
            return set.size();
        });
        print_ns("hard_remove (head, swap_move)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            auto set = build_set<T>(n, gen);
            for (size_t i = 0; i < n; ++i)
                set.soft_remove(entity(static_cast<uint32_t>(i), 1));
            compiler_barrier();
            return set.size();
        });
        print_ns("soft_remove (mark only)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 2: 查询接口性能 ===
template <typename T>
static void test_query(size_t n) noexcept
{
    print_header(("Section 2: query (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    auto set = build_set<T>(n, gen);

    // 准备访问序列: 顺序 + 随机
    std::vector<entity> seq_ents;
    seq_ents.reserve(n);
    for (size_t i = 0; i < n; ++i)
        seq_ents.emplace_back(static_cast<uint32_t>(i), 1);

    std::vector<entity> rnd_ents = seq_ents;
    std::shuffle(rnd_ents.begin(), rnd_ents.end(), gen);

    constexpr int REPEAT = 3;

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                size_t oi = opaque(i);
                T* p = set.template get_ptr<T>(seq_ents[oi]);
                touch_ptr(p);
            }
        });
        print_ns("get_ptr (sequential)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = set.template get_ptr<T>(rnd_ents[i]);
                touch_ptr(p);
            }
        });
        print_ns("get_ptr (random)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                size_t oi = opaque(i);
                T* p = set.template get_ptr_fast<T>(seq_ents[oi]);
                touch_ptr(p);
            }
        });
        print_ns("get_ptr_fast (sequential)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                size_t oi = opaque(i);
                T* p = set.template get_ptr_fast_inline<T>(seq_ents[oi]);
                touch_ptr(p);
            }
        });
        print_ns("get_ptr_fast_inline (sequential)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                size_t oi = opaque(i);
                T* p = set.template get_ptr_raw<T>(seq_ents[oi]);
                touch_ptr(p);
            }
        });
        print_ns("get_ptr_raw (unchecked)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < n; ++i)
            {
                size_t oi = opaque(i);
                sink = set.contains_entity(seq_ents[oi]);
            }
            (void)sink;
        });
        print_ns("contains_entity (sequential)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                sink = set.get_version(opaque(static_cast<uint32_t>(i)));
            }
            (void)sink;
        });
        print_ns("get_version", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                sink = set.get_version_unchecked(opaque(static_cast<uint32_t>(i)));
            }
            (void)sink;
        });
        print_ns("get_version_unchecked", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                sink = set.sparse_dense_at_public(opaque(static_cast<uint32_t>(i)));
            }
            (void)sink;
        });
        print_ns("sparse_dense_at_public", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                sink = set.sparse_version_at_public(opaque(static_cast<uint32_t>(i)));
            }
            (void)sink;
        });
        print_ns("sparse_version_at_public", n, ns / static_cast<double>(n));
    }

    {
        std::vector<T*> results(n, nullptr);
        double ns = best_ns(REPEAT, [&]() {
            set.get_ptr_batch(seq_ents.data(), results.data(), n);
            compiler_barrier();
        });
        print_ns("get_ptr_batch (sequential)", n, ns / static_cast<double>(n));
    }

    {
        std::vector<T*> results(n, nullptr);
        double ns = best_ns(REPEAT, [&]() {
            set.get_ptr_batch(rnd_ents.data(), results.data(), n);
            compiler_barrier();
        });
        print_ns("get_ptr_batch (random)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < n; ++i)
            {
                sink = set.contains_entity(rnd_ents[i]);
            }
            (void)sink;
        });
        print_ns("contains_entity (random)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                compiler_barrier();
                sink = set.size();
            }
            (void)sink;
        });
        print_ns("size (O(1) baseline)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                compiler_barrier();
                sink = set.get_pool_version();
            }
            (void)sink;
        });
        print_ns("get_pool_version (O(1))", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 3: 遍历接口性能 ===
template <typename T>
static void test_iterate(size_t n) noexcept
{
    print_header(("Section 3: iterate (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    auto set = build_set<T>(n, gen);

    constexpr int REPEAT = 3;

    {
        auto& indices = set.get_entity_indices();
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t sink = 0;
            for (size_t i = 0; i < indices.size(); ++i)
            {
                sink = indices[i];
            }
            (void)sink;
        });
        print_ns("iterate indices (raw loop)", n, ns / static_cast<double>(n));
    }

    {
        auto& versions = set.get_entity_versions();
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t sink = 0;
            for (size_t i = 0; i < versions.size(); ++i)
            {
                sink = versions[i];
            }
            (void)sink;
        });
        print_ns("iterate versions (raw loop)", n, ns / static_cast<double>(n));
    }

    {
        auto* pool = set.template get_typed_pool_ptr<T>();
        T* data = pool->data();
        double ns = best_ns(REPEAT, [&]() {
            volatile float sink = 0;
            for (size_t i = 0; i < pool->size(); ++i)
            {
                if constexpr (std::is_same_v<T, POD4>)
                    sink = std::bit_cast<float>(data[i].v);
                else if constexpr (std::is_same_v<T, POD12>)
                    sink = data[i].x;
                else if constexpr (std::is_same_v<T, POD32>)
                    sink = data[i].a[0];
            }
            (void)sink;
        });
        print_ns("iterate typed_pool data", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t sink = 0;
            for (uint32_t i = 0; i < n; ++i)
            {
                sink = set.sparse_dense_at_public(i);
            }
            (void)sink;
        });
        print_ns("iterate sparse_dense (sequential)", n, ns / static_cast<double>(n));
    }

    {
        std::vector<uint32_t> rnd_idx(n);
        for (size_t i = 0; i < n; ++i) rnd_idx[i] = static_cast<uint32_t>(i);
        std::shuffle(rnd_idx.begin(), rnd_idx.end(), gen);
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                sink = set.sparse_dense_at_public(rnd_idx[i]);
            }
            (void)sink;
        });
        print_ns("iterate sparse_dense (random)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 4: 预取接口性能 ===
template <typename T>
static void test_prefetch(size_t n) noexcept
{
    print_header(("Section 4: prefetch (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    auto set = build_set<T>(n, gen);

    std::vector<entity> rnd_ents;
    rnd_ents.reserve(n);
    for (size_t i = 0; i < n; ++i)
        rnd_ents.emplace_back(static_cast<uint32_t>(i), 1);
    std::shuffle(rnd_ents.begin(), rnd_ents.end(), gen);

    constexpr int REPEAT = 3;

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                set.prefetch_ptr(rnd_ents[i]);
            }
            compiler_barrier();
        });
        print_ns("prefetch_ptr (single)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            set.prefetch_ptr_batch(rnd_ents.data(), n);
            compiler_barrier();
        });
        print_ns("prefetch_ptr_batch", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                set.prefetch_component(static_cast<uint32_t>(i));
            }
            compiler_barrier();
        });
        print_ns("prefetch_component", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                set.prefetch_sparse_entry(static_cast<uint32_t>(i));
            }
            compiler_barrier();
        });
        print_ns("prefetch_sparse_entry", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                set.template prefetch_ptr_data<T>(rnd_ents[i]);
            }
            compiler_barrier();
        });
        print_ns("prefetch_ptr_data", n, ns / static_cast<double>(n));
    }

    {
        constexpr size_t PF_DIST = 8;
        // 直接访问
        double ns_direct = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = set.template get_ptr_fast<T>(rnd_ents[i]);
                touch_ptr(p);
            }
        });
        // 预取 + 延迟访问
        double ns_prefetch = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                if (i + PF_DIST < n)
                    set.prefetch_ptr(rnd_ents[i + PF_DIST]);
                T* p = set.template get_ptr_fast<T>(rnd_ents[i]);
                touch_ptr(p);
            }
        });
        print_ns("get_ptr_fast (random, no pf)", n, ns_direct / static_cast<double>(n));
        print_ns("get_ptr_fast (random, pf=8)", n, ns_prefetch / static_cast<double>(n));
    }

    print_footer();
}

// === Section 5: 排序/重排接口性能 ===
template <typename T>
static void test_reorder(size_t n) noexcept
{
    print_header(("Section 5: reorder/swap (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    constexpr int REPEAT = 3;

    {
        size_t ops = std::min(n, size_t{100000});
        std::vector<std::pair<size_t, size_t>> pairs;
        pairs.reserve(ops);
        std::uniform_int_distribution<size_t> d(0, n - 1);
        for (size_t i = 0; i < ops; ++i)
            pairs.emplace_back(d(gen), d(gen));

        double ns = best_ns(REPEAT, [&]() {
            auto set = build_set<T>(n, gen);
            for (size_t i = 0; i < ops; ++i)
                set.swap_dense_and_pool(pairs[i].first, pairs[i].second);
            compiler_barrier();
        });
        print_ns("swap_dense_and_pool (random)", ops, ns / static_cast<double>(ops));
    }

    {
        dense<size_t> sorted_idx;
        sorted_idx.increase_capacity(n);
        for (size_t i = 0; i < n; ++i)
            sorted_idx.emplace_back(n - 1 - i);

        double ns = best_ns(REPEAT, [&]() {
            auto set = build_set<T>(n, gen);
            set.template reorder_dense_by_indices<T>(sorted_idx);
            compiler_barrier();
        });
        print_ns("reorder_dense_by_indices (reverse)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 6: hot_set 缓存效果分析 ===
template <typename T>
static void test_hot_set(size_t n) noexcept
{
    print_header(("Section 6: hot_set cache effect (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    auto set = build_set<T>(n, gen);

    constexpr int REPEAT = 3;

    {
        set.clear_hot_set();
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = set.template get_ptr_fast_inline<T>(entity(static_cast<uint32_t>(opaque(i)), 1));
                touch_ptr(p);
            }
        });
        print_ns("cold start (hot_set empty)", n, ns / static_cast<double>(n));
    }

    {
        set.clear_hot_set();
        constexpr size_t HOT = 256;
        // 预热 hot_set
        for (int r = 0; r < 3; ++r)
            for (size_t i = 0; i < HOT; ++i)
            {
                T* p = set.template get_ptr_fast_inline<T>(entity(static_cast<uint32_t>(i), 1));
                touch_ptr(p);
            }
        size_t ops = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < ops; ++i)
            {
                size_t idx = opaque(i) % HOT;
                T* p = set.template get_ptr_fast_inline<T>(entity(static_cast<uint32_t>(idx), 1));
                touch_ptr(p);
            }
        });
        print_ns("hot access (256 entries, hit)", ops, ns / static_cast<double>(ops));
    }

    {
        set.clear_hot_set();
        if (n > 1024)
        {
            constexpr size_t CONFLICT = 4;
            uint32_t ents[CONFLICT] = {0, 256, 512, 768};
            // 预热
            for (int r = 0; r < 3; ++r)
                for (size_t i = 0; i < CONFLICT; ++i)
                {
                    T* p = set.template get_ptr_fast_inline<T>(entity(ents[i], 1));
                    touch_ptr(p);
                }
            size_t ops = 1000000;
            double ns = best_ns(REPEAT, [&]() {
                for (size_t i = 0; i < ops; ++i)
                {
                    size_t idx = opaque(i) % CONFLICT;
                    T* p = set.template get_ptr_fast_inline<T>(entity(ents[idx], 1));
                    touch_ptr(p);
                }
            });
            print_ns("conflict access (4 ents, slot 0)", ops, ns / static_cast<double>(ops));
        }
    }

    {
        std::cout << "  ── 工作集大小 vs 延迟分布 (cycle-based, 1000 次采样) ──\n";
        size_t worksets[] = {16, 64, 256, 1024, 4096, 16384, 65536, n};
        const char* labels[] = {"16", "64", "256", "1K", "4K", "16K", "64K", "N"};
        for (size_t w = 0; w < sizeof(worksets)/sizeof(worksets[0]); ++w)
        {
            size_t ws = worksets[w];
            if (ws > n) ws = n;
            set.clear_hot_set();
            // 预热
            for (int r = 0; r < 2; ++r)
                for (size_t i = 0; i < ws; ++i)
                {
                    T* p = set.template get_ptr_fast_inline<T>(entity(static_cast<uint32_t>(i), 1));
                    touch_ptr(p);
                }
            auto s = benchmark_precise_cycles(1000, 100, [&]() {
                for (size_t i = 0; i < ws; ++i)
                {
                    T* p = set.template get_ptr_fast_inline<T>(entity(static_cast<uint32_t>(opaque(i)), 1));
                    touch_ptr(p);
                }
            });
            char buf[64];
            std::snprintf(buf, sizeof(buf), "workset=%s (min cyc/call)", labels[w]);
            print_dist(buf, s);
        }
    }

    print_footer();
}

// === Section 7: 缓存层级实测 ===
// 修复: 1) detect_cache_latency_thresholds 检测失败时使用经验阈值兜底
//       2) sparse_table 访问改为真实 sparse_entry 地址 (而非 indices 代理)
template <typename T>
static void test_cache_hierarchy(size_t n) noexcept
{
    // 缓存测量功能已迁移至 part/analysis.hpp
#if 1
    print_header(("Section 7: cache hierarchy (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    auto set = build_set<T>(n, gen);

    std::cout << "  ── CPU 缓存层级 ──\n";
    latency_thresholds th = detect_cache_latency_thresholds();
    if (th.l1_max < 1.0)
    {
        // 经验阈值 (x86 desktop/server): L1~4cyc, L2~15cyc, L3~50cyc, DRAM~200+cyc
        th.l1_max = 4.0;
        th.l2_max = 15.0;
        th.l3_max = 50.0;
        th.cache_levels = 3;
        std::cout << "    (自动检测失败, 使用经验阈值)\n";
    }
    std::cout << "    缓存层级: " << th.cache_levels << " 级\n";
    std::cout << "    L1 阈值: < " << th.l1_max << " cycles\n";
    if (th.cache_levels >= 2)
        std::cout << "    L2 阈值: < " << th.l2_max << " cycles\n";
    if (th.cache_levels >= 3)
        std::cout << "    L3 阈值: < " << th.l3_max << " cycles\n";
    std::cout << "    CPU 频率: " << std::fixed << std::setprecision(3)
              << cpu_ghz_cached() << " GHz (TSC)\n";

    std::cout << "\n  ── typed_pool 顺序访问 (baseline, L1) ──\n";
    {
        auto* pool = set.template get_typed_pool_ptr<T>();
        T* data = pool->data();
        dense<const void*> addrs;
        addrs.increase_capacity(n);
        for (size_t i = 0; i < n; ++i)
            addrs.emplace_back(&data[i]);
        address_view addrs_av{addrs.data(), addrs.size()};
        cache_report cr = measure_cache_hits(addrs_av, th);
        std::cout << "    typed_pool 顺序访问 (size=" << n << "*" << sizeof(T) << "B="
                  << (n * sizeof(T) / 1024) << "KB):\n";
        std::cout << "      L1=" << (cr.l1_hit_rate * 100) << "%"
                  << " L2=" << (cr.l2_hit_rate * 100) << "%"
                  << " L3=" << (cr.l3_hit_rate * 100) << "%"
                  << " Miss=" << (cr.miss_rate * 100) << "%\n";
        std::cout << "      avg=" << cr.avg_cycles << " cyc"
                  << " p50=" << cr.p50_cycles << " cyc"
                  << " p99=" << cr.p99_cycles << " cyc\n";
    }

    std::cout << "\n  ── typed_pool 随机访问 (cache miss 分布) ──\n";
    {
        auto* pool = set.template get_typed_pool_ptr<T>();
        T* data = pool->data();
        std::vector<size_t> rnd_idx(n);
        for (size_t i = 0; i < n; ++i) rnd_idx[i] = i;
        std::shuffle(rnd_idx.begin(), rnd_idx.end(), gen);

        dense<const void*> addrs;
        addrs.increase_capacity(n);
        for (size_t i = 0; i < n; ++i)
            addrs.emplace_back(&data[rnd_idx[i]]);
        address_view addrs_av{addrs.data(), addrs.size()};
        cache_report cr = measure_cache_hits(addrs_av, th);
        std::cout << "    typed_pool 随机访问 (size=" << n << "*" << sizeof(T) << "B="
                  << (n * sizeof(T) / 1024) << "KB):\n";
        std::cout << "      L1=" << (cr.l1_hit_rate * 100) << "%"
                  << " L2=" << (cr.l2_hit_rate * 100) << "%"
                  << " L3=" << (cr.l3_hit_rate * 100) << "%"
                  << " Miss=" << (cr.miss_rate * 100) << "%\n";
        std::cout << "      avg=" << cr.avg_cycles << " cyc"
                  << " p50=" << cr.p50_cycles << " cyc"
                  << " p99=" << cr.p99_cycles << " cyc\n";
    }

    std::cout << "\n  ── sparse_table 随机访问 (sparse_entry 分布) ──\n";
    {
        std::vector<uint32_t> rnd_idx(n);
        for (size_t i = 0; i < n; ++i) rnd_idx[i] = static_cast<uint32_t>(i);
        std::shuffle(rnd_idx.begin(), rnd_idx.end(), gen);

        // 直接周期测量: 随机查 sparse_dense_at_public, 取 3 次最小值
        double cpu_ghz = cpu_ghz_cached();
        uint64_t total_cyc = 0;
        constexpr int TRIALS = 3;
        for (int trial = 0; trial < TRIALS; ++trial)
        {
            // 预热 (让 sparse_table 部分进入缓存)
            volatile uint32_t warmup = 0;
            for (size_t i = 0; i < std::min(n, size_t{1024}); ++i)
                warmup = set.sparse_dense_at_public(rnd_idx[i]);
            (void)warmup;

            uint64_t c0 = rdtscp();
            volatile uint32_t sink = 0;
            for (size_t i = 0; i < n; ++i)
                sink = set.sparse_dense_at_public(rnd_idx[i]);
            (void)sink;
            uint64_t c1 = rdtscp();
            if (trial == 0 || (c1 - c0) < total_cyc) total_cyc = c1 - c0;
        }
        double avg_cyc = static_cast<double>(total_cyc) / static_cast<double>(n);
        double avg_ns = (cpu_ghz > 0) ? avg_cyc / cpu_ghz : 0;
        std::cout << "    sparse_dense_at_public 随机访问 (" << n << " 次, 取 " << TRIALS << " 次最小):\n";
        std::cout << "      avg=" << std::fixed << std::setprecision(2) << avg_cyc << " cyc"
                  << " (" << std::setprecision(3) << avg_ns << " ns/call)\n";
    }

    print_footer();
#endif
}

// === Section 8: 容量管理接口性能 ===
template <typename T>
static void test_capacity(size_t n) noexcept
{
    print_header(("Section 8: capacity (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());

    constexpr int REPEAT = 3;

    {
        double ns = best_ns(REPEAT, [&]() {
            single_class_set set;
            for (size_t i = 0; i < 100; ++i)
                set.increase_capacity(1024 * (i + 1));
            compiler_barrier();
        });
        print_ns("increase_capacity (100 calls)", 100, ns / 100.0);
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            single_class_set set;
            set.add(entity(0, 1), T{});
            for (size_t i = 0; i < 100; ++i)
                set.increase_capacity(1024 * (i + 1));
            compiler_barrier();
        });
        print_ns("increase_capacity (typed, 100 calls)", 100, ns / 100.0);
    }

    {
        std::mt19937 gen(42);
        double ns = best_ns(REPEAT, [&]() {
            auto set = build_set<T>(n, gen);
            set.clear();
            compiler_barrier();
        });
        print_ns("clear (after N adds)", 1, ns);
    }

    print_footer();
}

// === Section 9: 变更追踪接口性能 ===
// 修复: bump_pool_version 必须读回 version_ 防 DCE (无 sink 时编译器
//       可证明 ++version_ 无外部可见副作用而整体删除循环)
template <typename T>
static void test_change_tracking(size_t n) noexcept
{
    print_header(("Section 9: change tracking (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    auto set = build_set<T>(n, gen);

    constexpr int REPEAT = 3;

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                size_t oi = opaque(i);
                sink = set.get_entity_change_version(oi);
                compiler_barrier();
            }
            (void)sink;
        });
        print_ns("get_entity_change_version", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                size_t oi = opaque(i);
                sink = set.get_entity_added_version(oi);
                compiler_barrier();
            }
            (void)sink;
        });
        print_ns("get_entity_added_version", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                compiler_barrier();
                sink = set.get_global_change_counter();
            }
            (void)sink;
        });
        print_ns("get_global_change_counter", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t sink = 0;
            for (size_t i = 0; i < n; ++i)
            {
                compiler_barrier();
                sink = set.get_global_added_counter();
            }
            (void)sink;
        });
        print_ns("get_global_added_counter", n, ns / static_cast<double>(n));
    }

    {
        size_t ops = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < ops; ++i)
            {
                set.bump_pool_version();
#if defined(_MSC_VER)
                _ReadWriteBarrier();
#else
                asm volatile("" : "+m"(set) : :);
#endif
            }
            volatile uint64_t sink = set.get_pool_version();
            (void)sink;
        });
        print_ns("bump_pool_version", ops, ns / static_cast<double>(ops));
    }

    print_footer();
}

// === Section 10: 统计分布基准 (使用 benchmark_precise_cycles 周期级精度) ===
// 修复: benchmark_ns → benchmark_precise_cycles (chrono ~15ns 分辨率无法测亚 ns 操作)
template <typename T>
static void test_stats_distribution(size_t n) noexcept
{
    print_header(("Section 10: latency distribution (T=" + std::to_string(sizeof(T)) + "B, N=" + std::to_string(n) + ")").c_str());
    std::mt19937 gen(42);
    auto set = build_set<T>(n, gen);

    {
        size_t idx = 0;
        auto s = benchmark_precise_cycles(10000, 1000, [&]() {
            T* p = set.template get_ptr_fast_inline<T>(entity(static_cast<uint32_t>(idx), 1));
            touch_ptr(p);
            idx = (idx + 1) % n;
        });
        print_dist("get_ptr_fast_inline (seq)", s, "cyc");
    }

    {
        std::uniform_int_distribution<uint32_t> d(0, static_cast<uint32_t>(n - 1));
        auto s = benchmark_precise_cycles(10000, 1000, [&]() {
            T* p = set.template get_ptr_fast_inline<T>(entity(d(gen), 1));
            touch_ptr(p);
        });
        print_dist("get_ptr_fast_inline (rnd)", s, "cyc");
    }

    {
        std::uniform_int_distribution<uint32_t> d(0, static_cast<uint32_t>(n - 1));
        auto s = benchmark_precise_cycles(10000, 1000, [&]() {
            volatile bool b = set.contains_entity(entity(d(gen), 1));
            (void)b;
        });
        print_dist("contains_entity (rnd)", s, "cyc");
    }

    // 10.4 add 延迟分布 (append 模式, 复用同一 set)
    //   修复前: 每次迭代 new single_class_set → 测的是分配+add 而非 add
    //   修复后: 复用预扩容 set, 仅测 add 本身; 用递增 entity_index 避免冲突
    {
        single_class_set tmp;
        tmp.increase_capacity(n + 10000);
        tmp.add(entity(0, 1), T{});
        uint32_t next_idx = 1;
        auto s = benchmark_precise_cycles(10000, 1000, [&]() {
            tmp.add(entity(next_idx, 1), T{});
            ++next_idx;
        });
        print_dist("add (append, reused set)", s, "cyc");
    }

    print_footer();
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "========================================================\n"
              << "  single_class_set 独立性能测试\n"
              << "  工具: time.hpp (stopwatch/benchmark/scope_time) + analysis.hpp (cache/barrier)\n"
              << "========================================================\n";

    constexpr size_t N = 1000000;  // 1M 实体

    std::cout << "\n┌─ CPU 信息\n";
    latency_thresholds auto_th = detect_cache_latency_thresholds();
    std::cout << "  缓存层级: " << auto_th.cache_levels << " 级"
              << "  L1<=" << auto_th.l1_max << "  L2<=" << auto_th.l2_max
              << "  L3<=" << auto_th.l3_max << "  cyc\n";
    std::cout << "└──────────────────────────────────────────────\n";

    // 对 3 种大小组件测试: POD4 (4B), POD12 (12B), POD32 (32B)
    test_add_remove<POD4>(N);
    test_add_remove<POD12>(N);
    test_add_remove<POD32>(N);

    test_query<POD4>(N);
    test_query<POD12>(N);
    test_query<POD32>(N);

    test_iterate<POD4>(N);
    test_iterate<POD12>(N);
    test_iterate<POD32>(N);

    test_prefetch<POD4>(N);
    test_prefetch<POD32>(N);

    test_reorder<POD4>(N);
    test_reorder<POD32>(N);

    test_hot_set<POD12>(N);

    test_cache_hierarchy<POD12>(N);

    test_capacity<POD12>(N);

    test_change_tracking<POD12>(N);

    test_stats_distribution<POD12>(N);

    std::cout << "\n========================================================\n"
              << "  测试完成 / Test Complete\n"
              << "========================================================\n";
    return 0;
}
