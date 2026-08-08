// test_dense_vs_vector_perf.cpp - dense<T> vs std::vector<T> 性能对比
// 覆盖: 构造/访问/追加/插入/删除/批量操作/查找
// 使用 volatile sink 阻止 DCE, 确保真实数据
#include "perf_common.hpp"
#include "include/part/dense.hpp"
#include <vector>
#include <algorithm>

using namespace std;

// === 测试组件 ===
struct POD128 { float a[32]; };

// POD4/POD32/POD128 比较运算符 (std::sort/find 需要)
inline bool operator==(const POD4& a, const POD4& b) noexcept { return a.v == b.v; }
inline bool operator!=(const POD4& a, const POD4& b) noexcept { return a.v != b.v; }
inline bool operator<(const POD4& a, const POD4& b) noexcept { return a.v < b.v; }

inline bool operator==(const POD32& a, const POD32& b) noexcept
{
    for (int i = 0; i < 8; ++i) { if (a.a[i] != b.a[i]) return false; }
    return true;
}
inline bool operator!=(const POD32& a, const POD32& b) noexcept { return !(a == b); }
inline bool operator<(const POD32& a, const POD32& b) noexcept
{
    for (int i = 0; i < 8; ++i) { if (a.a[i] != b.a[i]) return a.a[i] < b.a[i]; }
    return false;
}

inline bool operator==(const POD128& a, const POD128& b) noexcept
{
    for (int i = 0; i < 32; ++i) { if (a.a[i] != b.a[i]) return false; }
    return true;
}
inline bool operator!=(const POD128& a, const POD128& b) noexcept { return !(a == b); }
inline bool operator<(const POD128& a, const POD128& b) noexcept
{
    for (int i = 0; i < 32; ++i) { if (a.a[i] != b.a[i]) return a.a[i] < b.a[i]; }
    return false;
}

template <typename T>
static T make_value(uint32_t i) noexcept
{
    if constexpr (is_same_v<T, POD4>)  return {i};
    else if constexpr (is_same_v<T, POD12>) return {static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2)};
    else if constexpr (is_same_v<T, POD32>) { POD32 p; for (int k = 0; k < 8; ++k) p.a[k] = static_cast<float>(i + k); return p; }
    else if constexpr (is_same_v<T, POD128>) { POD128 p; for (int k = 0; k < 32; ++k) p.a[k] = static_cast<float>(i + k); return p; }
    else return T{};
}

// 全局 volatile sink, 阻止 DCE
static volatile size_t g_sink = 0;

// 强类型 sink: 通过 memcpy 写入 + volatile 读回, 防止 DCE 消除整个表达式
// 兼容无 volatile operator= 的 POD 结构体 (POD32/POD128)
template <typename T>
inline void sink_write(const T& x) noexcept
{
    // alignas 必须在 static 之前 (MinGW GCC 语法要求)
    alignas(alignof(T)) static char buf[sizeof(T)];
    std::memcpy(buf, &x, sizeof(T));
    // volatile 读回首字节, 强制 memcpy 真实执行
    volatile uint8_t v = static_cast<volatile uint8_t*>(static_cast<void*>(buf))[0];
    (void)v;
}

// === 对比输出辅助 ===
inline void print_compare(const char* label, size_t n,
                          double dense_ns, double vec_ns) noexcept
{
    double d_tp = (dense_ns > 0 && n > 0) ? static_cast<double>(n) / dense_ns : 0;
    double v_tp = (vec_ns   > 0 && n > 0) ? static_cast<double>(n) / vec_ns   : 0;
    const char* verdict;
    if (dense_ns < vec_ns * 0.95)       verdict = "[dense WIN]";
    else if (vec_ns < dense_ns * 0.95)   verdict = "[vector WIN]";
    else                                verdict = "[TIE]";

    cout << "  " << left << setw(28) << label
         << " | dense: " << fixed << setprecision(3) << setw(9) << dense_ns << " ns"
         << " (" << setprecision(2) << setw(8) << d_tp << " G/s)"
         << " | vector: " << setw(9) << vec_ns << " ns"
         << " (" << setw(8) << v_tp << " G/s)"
         << " " << verdict << "\n";
}

// === Section 1: 构造 ===
template <typename T>
static void test_construct_compare(size_t n)
{
    print_header(("Section 1: construct (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    // 默认构造
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; compiler_barrier();
            g_sink = d.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; compiler_barrier();
            g_sink = v.size(); return g_sink;
        });
        print_compare("default ctor", 1, d_ns, v_ns);
    }

    // 预分配容量 (REPEAT=9 + warm-up, 稳定内存分配测量)
    {
        volatile size_t vn = n;
        // warm-up: 让堆进入稳定状态, 避免首次分配的额外开销
        for (int w = 0; w < 3; ++w) {
            dense<T> d_warm(vn);
            vector<T> v_warm; v_warm.reserve(vn);
        }
        double d_ns = best_ns(9, [&]() {
            dense<T> d(vn); compiler_barrier();
            touch_ptr(d.data());
            g_sink = d.capacity(); return g_sink;
        });
        double v_ns = best_ns(9, [&]() {
            vector<T> v; v.reserve(vn); compiler_barrier();
            touch_ptr(v.data());
            g_sink = v.capacity(); return g_sink;
        });
        print_compare("reserve(n)", 1, d_ns, v_ns);
    }

    // count 个 value 副本 (使用真实写入, 防止 DCE)
    {
        T val = make_value<T>(0);
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d(n, val); compiler_barrier();
            sink_write<T>(d.back()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v(n, val); compiler_barrier();
            sink_write<T>(v.back()); return g_sink;
        });
        print_compare("count-value ctor", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // 移动构造
    {
        dense<T> d_src(n);
        vector<T> v_src(n);
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> tmp = std::move(d_src); compiler_barrier();
            dense<T> r = std::move(tmp);
            touch_ptr(r.data());
            g_sink = r.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> tmp = std::move(v_src); compiler_barrier();
            vector<T> r = std::move(tmp);
            touch_ptr(r.data());
            g_sink = r.size(); return g_sink;
        });
        print_compare("move ctor", 1, d_ns, v_ns);
    }

    // 拷贝构造
    {
        dense<T> d_src(n);
        for (size_t i = 0; i < n; ++i) d_src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        vector<T> v_src(n);
        for (size_t i = 0; i < n; ++i) v_src[i] = make_value<T>(static_cast<uint32_t>(i));

        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d(d_src); compiler_barrier();
            sink_write<T>(d.back()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v(v_src); compiler_barrier();
            sink_write<T>(v.back()); return g_sink;
        });
        print_compare("copy ctor", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 2: 元素访问 ===
template <typename T>
static void test_access_compare(size_t n)
{
    print_header(("Section 2: access (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());

    dense<T> d; d.increase_capacity(n);
    vector<T> v; v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        T val = make_value<T>(static_cast<uint32_t>(i));
        d.push_back_unchecked(val);
        v.push_back(val);
    }

    constexpr int REPEAT = 5;

    // operator[] - 真实读写以防止 DCE
    {
        double d_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i) {
                sink_write<T>(d[opaque(i)]);
            }
            compiler_barrier();
            g_sink = opaque(0); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i) {
                sink_write<T>(v[opaque(i)]);
            }
            compiler_barrier();
            g_sink = opaque(0); return g_sink;
        });
        print_compare("operator[]", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // 顺序遍历 (range-for) - 真实写 volatile
    {
        double d_ns = best_ns(REPEAT, [&]() {
            for (const auto& x : d) { sink_write<T>(x); }
            compiler_barrier();
            g_sink = opaque(0); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            for (const auto& x : v) { sink_write<T>(x); }
            compiler_barrier();
            g_sink = opaque(0); return g_sink;
        });
        print_compare("range-for iter", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // data() 指针访问 + 真实读
    {
        double d_ns = best_ns(REPEAT, [&]() {
            T* p = d.data(); touch_ptr(p);
            sink_write<T>(p[0]);
            g_sink = reinterpret_cast<size_t>(p); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            T* p = v.data(); touch_ptr(p);
            sink_write<T>(p[0]);
            g_sink = reinterpret_cast<size_t>(p); return g_sink;
        });
        print_compare("data()", 1, d_ns, v_ns);
    }

    // 累加求和 - 真实计算
    {
        double d_ns = best_ns(REPEAT, [&]() {
            const T* p = d.data();
            T acc{}; for (size_t i = 0; i < n; ++i) {
                if constexpr (is_same_v<T, POD4>) acc.v += p[i].v;
                else if constexpr (is_same_v<T, POD32>) { for (int k = 0; k < 8; ++k) acc.a[k] += p[i].a[k]; }
                else if constexpr (is_same_v<T, POD128>) { for (int k = 0; k < 32; ++k) acc.a[k] += p[i].a[k]; }
            }
            compiler_barrier(); sink_write<T>(acc); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            const T* p = v.data();
            T acc{}; for (size_t i = 0; i < n; ++i) {
                if constexpr (is_same_v<T, POD4>) acc.v += p[i].v;
                else if constexpr (is_same_v<T, POD32>) { for (int k = 0; k < 8; ++k) acc.a[k] += p[i].a[k]; }
                else if constexpr (is_same_v<T, POD128>) { for (int k = 0; k < 32; ++k) acc.a[k] += p[i].a[k]; }
            }
            compiler_barrier(); sink_write<T>(acc); return g_sink;
        });
        print_compare("accumulate (sum)", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 3: 追加操作 ===
template <typename T>
static void test_append_compare(size_t n)
{
    print_header(("Section 3: append (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // push_back (预分配)
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier(); sink_write<T>(d.back()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; v.reserve(n);
            for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier(); sink_write<T>(v.back()); return g_sink;
        });
        print_compare("push_back (reserved)", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // push_back (自动扩容)
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d;
            for (size_t i = 0; i < n; ++i) d.push_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier(); sink_write<T>(d.back()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v;
            for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier(); sink_write<T>(v.back()); return g_sink;
        });
        print_compare("push_back (grow)", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // emplace_back (预分配)
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.emplace_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier(); sink_write<T>(d.back()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; v.reserve(n);
            for (size_t i = 0; i < n; ++i) v.emplace_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier(); sink_write<T>(v.back()); return g_sink;
        });
        print_compare("emplace_back (reserved)", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // 批量追加 (append_bulk / insert range)
    {
        vector<T> src; src.reserve(n);
        for (size_t i = 0; i < n; ++i) src.push_back(make_value<T>(static_cast<uint32_t>(i)));

        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            d.append_bulk(src.data(), n);
            compiler_barrier(); sink_write<T>(d.back()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; v.reserve(n);
            v.insert(v.end(), src.begin(), src.end());
            compiler_barrier(); sink_write<T>(v.back()); return g_sink;
        });
        print_compare("append_bulk / insert range", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // fill (count 个相同值)
    {
        T val = make_value<T>(42);
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            d.append_n(n, val);
            compiler_barrier(); sink_write<T>(d.back()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; v.reserve(n);
            v.assign(n, val);
            compiler_barrier(); sink_write<T>(v.back()); return g_sink;
        });
        print_compare("fill (n, val)", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 4: 插入/删除 ===
// O(N^2) 操作使用更小的 N (N_SQ), 避免极端长时间运行
template <typename T>
static void test_insert_erase_compare(size_t n, size_t n_sq)
{
    print_header(("Section 4: insert/erase (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ", N_sq=" + to_string(n_sq) + ")").c_str());
    constexpr int REPEAT = 3;

    // emplace(begin) - 头部插入 O(N²) - 用小 N
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n_sq + 1);
            for (size_t i = 0; i < n_sq; ++i) d.emplace(d.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier(); sink_write<T>(d.front()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; v.reserve(n_sq + 1);
            for (size_t i = 0; i < n_sq; ++i) v.emplace(v.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier(); sink_write<T>(v.front()); return g_sink;
        });
        print_compare("emplace(begin) O(N^2)", n_sq, d_ns / static_cast<double>(n_sq), v_ns / static_cast<double>(n_sq));
    }

    // erase(begin) - 头部删除 O(N²) - 用小 N
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n_sq);
            for (size_t i = 0; i < n_sq; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            while (!d.empty()) d.erase(d.begin());
            compiler_barrier(); g_sink = d.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; v.reserve(n_sq);
            for (size_t i = 0; i < n_sq; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i)));
            while (!v.empty()) v.erase(v.begin());
            compiler_barrier(); g_sink = v.size(); return g_sink;
        });
        print_compare("erase(begin) O(N^2)", n_sq, d_ns / static_cast<double>(n_sq), v_ns / static_cast<double>(n_sq));
    }

    // erase(end-1) - 尾部删除 O(1) - 用大 N
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < n; ++i) d.erase(d.end() - 1);
            compiler_barrier(); g_sink = d.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; v.reserve(n);
            for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < n; ++i) v.erase(v.end() - 1);
            compiler_barrier(); g_sink = v.size(); return g_sink;
        });
        print_compare("erase(end-1) O(1)", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // pop_back - 用大 N
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < n; ++i) d.pop_back();
            compiler_barrier(); g_sink = d.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v; v.reserve(n);
            for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < n; ++i) v.pop_back();
            compiler_barrier(); g_sink = v.size(); return g_sink;
        });
        print_compare("pop_back", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 5: 容量管理 ===
template <typename T>
static void test_capacity_compare(size_t n)
{
    print_header(("Section 5: capacity (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // shrink_to_fit
    {
        dense<T> d_src; d_src.increase_capacity(n);
        for (size_t i = 0; i < n; ++i) d_src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        vector<T> v_src; v_src.reserve(n);
        for (size_t i = 0; i < n; ++i) v_src.push_back(make_value<T>(static_cast<uint32_t>(i)));

        double d_ns = best_ns(REPEAT, [&]() {
            dense<T> d(d_src);
            d.reduce_capacity(d.size());
            compiler_barrier(); sink_write<T>(d.back()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<T> v(v_src);
            v.shrink_to_fit();
            compiler_barrier(); sink_write<T>(v.back()); return g_sink;
        });
        print_compare("shrink_to_fit", 1, d_ns, v_ns);
    }

    print_footer();
}

// === Section 6: 算法/查找 ===
template <typename T>
static void test_algorithm_compare(size_t n)
{
    print_header(("Section 6: algorithm (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 排序 (std::sort)
    if constexpr (is_same_v<T, POD4>)
    {
        dense<T> d; d.increase_capacity(n);
        vector<T> v; v.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            T val = make_value<T>(static_cast<uint32_t>(n - i));
            d.push_back_unchecked(val);
            v.push_back(val);
        }

        double d_ns = best_ns(REPEAT, [&]() {
            std::sort(d.begin(), d.end());
            compiler_barrier(); sink_write<T>(d.front()); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            std::sort(v.begin(), v.end());
            compiler_barrier(); sink_write<T>(v.front()); return g_sink;
        });
        print_compare("std::sort", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // std::find (线性查找)
    {
        dense<T> d; d.increase_capacity(n);
        vector<T> v; v.reserve(n);
        T target = make_value<T>(static_cast<uint32_t>(n - 1));
        for (size_t i = 0; i < n; ++i) {
            T val = make_value<T>(static_cast<uint32_t>(i));
            d.push_back_unchecked(val);
            v.push_back(val);
        }

        double d_ns = best_ns(REPEAT, [&]() {
            auto it = std::find(d.begin(), d.end(), target);
            compiler_barrier(); g_sink = (it == d.end()) ? 1 : 0; return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            auto it = std::find(v.begin(), v.end(), target);
            compiler_barrier(); g_sink = (it == v.end()) ? 1 : 0; return g_sink;
        });
        print_compare("std::find (last)", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    // std::accumulate (规约)
    if constexpr (is_same_v<T, POD4>)
    {
        dense<T> d; d.increase_capacity(n);
        vector<T> v; v.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            T val = make_value<T>(static_cast<uint32_t>(i));
            d.push_back_unchecked(val);
            v.push_back(val);
        }

        double d_ns = best_ns(REPEAT, [&]() {
            uint32_t sum = std::accumulate(d.begin(), d.end(), uint32_t{0},
                [](uint32_t a, const POD4& b) { return a + b.v; });
            compiler_barrier(); g_sink = sum; return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            uint32_t sum = std::accumulate(v.begin(), v.end(), uint32_t{0},
                [](uint32_t a, const POD4& b) { return a + b.v; });
            compiler_barrier(); g_sink = sum; return g_sink;
        });
        print_compare("std::accumulate", n, d_ns / static_cast<double>(n), v_ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 7: 内存占用 ===
template <typename T>
static void test_memory_compare(size_t n)
{
    print_header(("Section 7: memory footprint (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());

    dense<T> d; d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    vector<T> v; v.reserve(n);
    for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i)));

    size_t d_bytes = d.size_bytes();
    size_t d_cap_bytes = d.capacity_bytes();
    size_t v_bytes = v.size() * sizeof(T);
    size_t v_cap_bytes = v.capacity() * sizeof(T);

    cout << "  dense<T>   : used " << d_bytes << " B / cap " << d_cap_bytes
         << " B / struct " << sizeof(dense<T>) << " B\n";
    cout << "  vector<T>  : used " << v_bytes << " B / cap " << v_cap_bytes
         << " B / struct " << sizeof(vector<T>) << " B\n";

    cout << "  struct size: dense=" << sizeof(dense<T>) << " B vs vector="
         << sizeof(vector<T>) << " B (diff "
         << (long long)sizeof(dense<T>) - (long long)sizeof(vector<T>) << " B)\n";

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  dense<T> vs std::vector<T> performance comparison\n";
    cout << "  N=" << (1 << 18) << " (256K), N_sq=" << (1 << 13) << " (8K)\n";
    cout << "  3-5 repeats, min\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;       // 256K - 用于 O(N) / O(1) 操作
    const size_t N_SQ = 1 << 13;   // 8K   - 用于 O(N^2) 操作

    cout << "\n>>> T = POD4 (4 bytes) <<<\n";
    test_construct_compare<POD4>(N);
    test_access_compare<POD4>(N);
    test_append_compare<POD4>(N);
    test_insert_erase_compare<POD4>(N, N_SQ);
    test_capacity_compare<POD4>(N);
    test_algorithm_compare<POD4>(N);
    test_memory_compare<POD4>(N);

    cout << "\n>>> T = POD32 (32 bytes) <<<\n";
    test_construct_compare<POD32>(N);
    test_access_compare<POD32>(N);
    test_append_compare<POD32>(N);
    test_insert_erase_compare<POD32>(N, N_SQ);
    test_capacity_compare<POD32>(N);
    test_algorithm_compare<POD32>(N);
    test_memory_compare<POD32>(N);

    cout << "\n>>> T = POD128 (128 bytes) <<<\n";
    test_construct_compare<POD128>(N);
    test_append_compare<POD128>(N);
    test_insert_erase_compare<POD128>(N, N_SQ);
    test_memory_compare<POD128>(N);

    cout << "\n============================================================\n";
    cout << "  Comparison complete\n";
    cout << "============================================================\n";
    return 0;
}
