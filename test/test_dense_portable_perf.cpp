// test_dense_portable_perf.cpp - dense<T> vs std::vector<T> 移植性性能基线
// 场景: 无指令集 flags (纯 -O2 标量编译, CMake 目标追加 -mno-avx2/-mno-fma/-mno-bmi/-mno-bmi2),
//       允许内存对齐 (dense 的 assume_aligned 与 operator new 16B 对齐保留)。
// 对照: test_dense_portable_perf_isa 同源码在全局 -mavx2/-mfma/-mbmi/-mbmi2 下编译。
// 说明: dense 内部 8 处 [[gnu::target("avx2")]] 函数级定向在 __AVX2__ 未定义时激活
//       (erase 大块移动/广播填充等路径), 移植到非 x86 或无 AVX2 的 CPU 需删除;
//       libc memcpy/memmove 的 ifunc 运行时分发两容器共享, 对比公平。
#include "perf_common.hpp"
#include "include/part/dense.hpp"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <random>

using namespace std;

// === 测试组件: 覆盖 4B/12B/32B 三个量级 (trivially copyable) ===
struct P4  { uint32_t v; };
struct P12 { float x, y, z; };
struct P32 { float a[8]; };

template <typename T>
static T make_value(uint32_t i) noexcept
{
    if constexpr (is_same_v<T, P4>)   return {i};
    else if constexpr (is_same_v<T, P12>) return {static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2)};
    else { P32 p; for (int k = 0; k < 8; ++k) p.a[k] = static_cast<float>(i + k); return p; }
}

// 求和标量 (防 DCE, 两容器同一代码路径)
template <typename T>
static uint64_t sum_key(const T& e) noexcept
{
    if constexpr (is_same_v<T, P4>)   return e.v;
    else if constexpr (is_same_v<T, P12>) return static_cast<uint64_t>(e.x);
    else return static_cast<uint64_t>(e.a[0]);
}

static volatile uint64_t g_sink = 0;

// === 对比输出 (双语标签 + 判定) ===
inline void print_compare(const char* label, size_t n,
                          double dense_ns, double vec_ns) noexcept
{
    double d_tp = (dense_ns > 0 && n > 0) ? static_cast<double>(n) / dense_ns : 0;
    double v_tp = (vec_ns   > 0 && n > 0) ? static_cast<double>(n) / vec_ns   : 0;
    const char* verdict;
    if (dense_ns < vec_ns * 0.95)       verdict = "[dense WIN]";
    else if (vec_ns < dense_ns * 0.95)   verdict = "[vector WIN]";
    else                                verdict = "[TIE]";
    cout << "  " << left << setw(30) << label
         << " | dense: " << fixed << setprecision(3) << setw(10) << dense_ns << " ns"
         << " (" << setprecision(2) << setw(9) << d_tp << " G/s)"
         << " | vector: " << setw(10) << vec_ns << " ns"
         << " (" << setw(9) << v_tp << " G/s)"
         << " " << verdict << "\n";
}

// === Section 0: 编译模式自证 + 内存对齐验证 ===
static void print_build_mode_and_alignment() noexcept
{
    cout << "\n=== dense portable baseline (移植性基线) ===\n";
#if defined(__AVX2__)
    cout << "  编译模式 (Build mode): __AVX2__ 已定义 — 指令集版本 (ISA reference)\n";
#else
    cout << "  编译模式 (Build mode): __AVX2__ 未定义 — 纯 -O2 标量基线 (portable, 无指令集 flags)\n";
#endif
#if defined(__BMI__)
    cout << "  __BMI__ 已定义\n";
#else
    cout << "  __BMI__ 未定义\n";
#endif
    cout << "  对齐说明 (Alignment): operator new >= 16B, assume_aligned 保留 (非指令集依赖)\n";

    dense<P4>  d4(64);
    dense<P12> d12(64);
    dense<P32> d32(64);
    vector<P4>  v4(64);
    vector<P12> v12(64);
    vector<P32> v32(64);

    auto align_of_ptr = [](const void* p) noexcept -> size_t {
        uintptr_t a = reinterpret_cast<uintptr_t>(p);
        if ((a % 64) == 0) return 64;
        if ((a % 32) == 0) return 32;
        if ((a % 16) == 0) return 16;
        if ((a % 8)  == 0) return 8;
        return 4;
    };
    cout << "  对齐实测 (P4):  dense=" << align_of_ptr(d4.data())  << "B | vector=" << align_of_ptr(v4.data())  << "B\n";
    cout << "  对齐实测 (P12): dense=" << align_of_ptr(d12.data()) << "B | vector=" << align_of_ptr(v12.data()) << "B\n";
    cout << "  对齐实测 (P32): dense=" << align_of_ptr(d32.data()) << "B | vector=" << align_of_ptr(v32.data()) << "B\n";
}

// === Section 1: push_back 预分配追加 ===
template <typename T>
static void test_pushback_reserved(size_t n, int repeat)
{
    print_header(("Section 1: push_back reserved (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    double dns = best_ns(repeat, [&] {
        dense<T> d; d.increase_capacity(n);
        for (size_t i = 0; i < n; ++i) d.push_back(make_value<T>(static_cast<uint32_t>(i)));
        g_sink = d.size();
    });
    double vns = best_ns(repeat, [&] {
        vector<T> v; v.reserve(n);
        for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i)));
        g_sink = v.size();
    });
    print_compare("push_back reserved (追加)", n, dns, vns);
}

// === Section 2: push_back 自动扩容 (含 grow + memcpy 重分配) ===
template <typename T>
static void test_pushback_grow(size_t n, int repeat)
{
    print_header(("Section 2: push_back auto-grow (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    double dns = best_ns(repeat, [&] {
        dense<T> d;
        for (size_t i = 0; i < n; ++i) d.push_back(make_value<T>(static_cast<uint32_t>(i)));
        g_sink = d.size();
    });
    double vns = best_ns(repeat, [&] {
        vector<T> v;
        for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i)));
        g_sink = v.size();
    });
    print_compare("push_back auto-grow (扩容)", n, dns, vns);
}

// === Section 3: 顺序遍历求和 ===
template <typename T>
static void test_sequential_sum(size_t n, int repeat)
{
    print_header(("Section 3: sequential sum (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d; d.increase_capacity(n);
    vector<T> v; v.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        T e = make_value<T>(static_cast<uint32_t>(i));
        d.push_back(e); v.push_back(e);
    }
    double dns = best_ns(repeat, [&] {
        uint64_t s = 0;
        for (size_t i = 0; i < d.size(); ++i) s += sum_key(d[i]);
        g_sink = s;
    });
    double vns = best_ns(repeat, [&] {
        uint64_t s = 0;
        for (size_t i = 0; i < v.size(); ++i) s += sum_key(v[i]);
        g_sink = s;
    });
    print_compare("sequential sum (顺序求和)", n, dns, vns);
}

// === Section 4: 随机访问 (乱序索引, 两容器共用同一索引序列) ===
template <typename T>
static void test_random_access(size_t n, int repeat)
{
    print_header(("Section 4: random access (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d; d.increase_capacity(n);
    vector<T> v; v.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        T e = make_value<T>(static_cast<uint32_t>(i));
        d.push_back(e); v.push_back(e);
    }
    vector<uint32_t> idx(n);
    mt19937 rng(12345);
    for (size_t i = 0; i < n; ++i) idx[i] = static_cast<uint32_t>(i);
    for (size_t i = n; i > 1; --i) swap(idx[i - 1], idx[static_cast<size_t>(rng()) % i]);

    double dns = best_ns(repeat, [&] {
        uint64_t s = 0;
        for (size_t i = 0; i < n; ++i) s += sum_key(d[idx[i]]);
        g_sink = s;
    });
    double vns = best_ns(repeat, [&] {
        uint64_t s = 0;
        for (size_t i = 0; i < n; ++i) s += sum_key(v[idx[i]]);
        g_sink = s;
    });
    print_compare("random access (随机访问)", n, dns, vns);
}

// === Section 5: 拷贝构造 (trivial 路径走 libc memcpy, ifunc 两边共享) ===
template <typename T>
static void test_copy_construct(size_t n, int repeat)
{
    print_header(("Section 5: copy construct (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d; d.increase_capacity(n);
    vector<T> v; v.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        T e = make_value<T>(static_cast<uint32_t>(i));
        d.push_back(e); v.push_back(e);
    }
    double dns = best_ns(repeat, [&] {
        dense<T> c(d);
        g_sink = c.size() + sum_key(c.back());
    });
    double vns = best_ns(repeat, [&] {
        vector<T> c(v);
        g_sink = c.size() + sum_key(c.back());
    });
    print_compare("copy construct (拷贝构造)", n, dns, vns);
}

// === Section 6: 线性查找 (手写标量循环, 两容器同一代码形态) ===
template <typename T>
static void test_linear_find(size_t n, int repeat)
{
    print_header(("Section 6: linear find (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d; d.increase_capacity(n);
    vector<T> v; v.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        T e = make_value<T>(static_cast<uint32_t>(i));
        d.push_back(e); v.push_back(e);
    }
    // 查找表中不存在的 key: 完整扫描 N 次 (最坏情况, 测扫描吞吐)
    uint32_t miss_key = 0xDEADBEEFu;
    double dns = best_ns(repeat, [&] {
        size_t hits = 0;
        for (size_t i = 0; i < d.size(); ++i) if (d[i].v == miss_key) ++hits;
        g_sink = hits;
    });
    double vns = best_ns(repeat, [&] {
        size_t hits = 0;
        for (size_t i = 0; i < v.size(); ++i) if (v[i].v == miss_key) ++hits;
        g_sink = hits;
    });
    print_compare("linear find miss (线性查找)", n, dns, vns);
}

// === Section 7: 整体写填充 (写入 N 个相同值)
// 注意: dense 该路径为 [[gnu::target("avx2")]] 广播填充, 标量基线下仍嵌 AVX2 (注明) ===
template <typename T>
static void test_fill(size_t n, int repeat)
{
    print_header(("Section 7: fill/assign (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d; d.increase_capacity(n);
    vector<T> v; v.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        d.push_back(make_value<T>(static_cast<uint32_t>(i)));
        v.push_back(make_value<T>(static_cast<uint32_t>(i)));
    }
    T val = make_value<T>(77u);
    double dns = best_ns(repeat, [&] {
        for (size_t i = 0; i < d.size(); ++i) d[i] = val;
        g_sink = sum_key(d.back());
    });
    double vns = best_ns(repeat, [&] {
        for (size_t i = 0; i < v.size(); ++i) v[i] = val;
        g_sink = sum_key(v.back());
    });
    print_compare("indexed fill (逐位填充)", n, dns, vns);
}

int main()
{
    const size_t N1 = 1u << 20;   // 1M 元素: 测吞吐与扩容行为
    const int    R  = 7;          // 重复取最小 (best_ns)

    print_build_mode_and_alignment();

    test_pushback_reserved<P4>(N1, R);
    test_pushback_reserved<P12>(N1, R);
    test_pushback_reserved<P32>(N1, R);

    test_pushback_grow<P4>(N1, R);
    test_pushback_grow<P12>(N1, R);
    test_pushback_grow<P32>(N1, R);

    test_sequential_sum<P4>(N1, R);
    test_sequential_sum<P12>(N1, R);
    test_sequential_sum<P32>(N1, R);

    test_random_access<P4>(N1, R);
    test_random_access<P12>(N1, R);

    test_copy_construct<P4>(N1, R);
    test_copy_construct<P12>(N1, R);
    test_copy_construct<P32>(N1, R);

    test_linear_find<P4>(N1, R);

    test_fill<P4>(N1, R);
    test_fill<P32>(N1, R);

    print_footer();
    cout << "\n完成: " << __FILE__ << "\n";
    return 0;
}
