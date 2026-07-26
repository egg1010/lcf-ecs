// test_dense_perf.cpp - dense<T> 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/dense.hpp"

using namespace std;

// === 测试组件 (POD4/POD12/POD32 来自 perf_common.hpp, 此处仅补 POD128) ===
struct POD128 { float a[32]; };

template <typename T>
static T make_value(uint32_t i) noexcept
{
    if constexpr (is_same_v<T, POD4>)  return {i};
    else if constexpr (is_same_v<T, POD12>) return {static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2)};
    else if constexpr (is_same_v<T, POD32>) { POD32 p; for (int k = 0; k < 8; ++k) p.a[k] = static_cast<float>(i + k); return p; }
    else if constexpr (is_same_v<T, POD128>) { POD128 p; for (int k = 0; k < 32; ++k) p.a[k] = static_cast<float>(i + k); return p; }
    else return T{};
}

// === Section 1: 构造与赋值 ===
template <typename T>
static void test_construct(size_t n)
{
    print_header(("Section 1: construct (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 1.1 默认构造
    {
        double ns = best_ns(REPEAT, [&]() { dense<T> d; compiler_barrier(); return d.size(); });
        print_ns("default ctor", 1, ns);
    }

    // 1.2 预分配容量
    {
        double ns = best_ns(REPEAT, [&]() { dense<T> d(n); compiler_barrier(); return d.capacity(); });
        print_ns("reserve ctor", 1, ns);
    }

    // 1.3 count 个 value 副本
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() { dense<T> d(n, v); compiler_barrier(); return d.size(); });
        print_ns("count-value ctor", n, ns / static_cast<double>(n));
    }

    // 1.4 移动构造
    {
        dense<T> src(n);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> tmp = std::move(src);
            compiler_barrier();
            dense<T> r = std::move(tmp);
            return r.size();
        });
        print_ns("move ctor", 1, ns);
    }

    // 1.5 拷贝构造
    {
        dense<T> src(n);
        for (size_t i = 0; i < n; ++i) src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() { dense<T> d(src); compiler_barrier(); return d.size(); });
        print_ns("copy ctor", n, ns / static_cast<double>(n));
    }

    // 1.6 initializer_list 构造
    {
        // 仅测小数组
        double ns = best_ns(REPEAT, [&]() { dense<T> d{T{}, T{}, T{}}; compiler_barrier(); return d.size(); });
        print_ns("init_list ctor", 3, ns / 3.0);
    }

    print_footer();
}

// === Section 2: 元素访问 ===
template <typename T>
static void test_access(size_t n)
{
    print_header(("Section 2: access (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // 2.1 operator[]
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = d[opaque(i)]; }
            compiler_barrier();
            (void)sink;
        });
        print_ns("operator[]", n, ns / static_cast<double>(n));
    }

    // 2.2 get (含越界回退版)
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = d.get(opaque(i)); }
            compiler_barrier();
            (void)sink;
        });
        print_ns("get(idx)", n, ns / static_cast<double>(n));
    }

    // 2.3 get(idx, error_index) 越界回退版
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = d.get(opaque(i + n), 0); }
            compiler_barrier();
            (void)sink;
        });
        print_ns("get(idx, err_idx)", n, ns / static_cast<double>(n));
    }

    // 2.4 front / back
    {
        double ns = best_ns(REPEAT, [&]() {
            T f{}, b{};
            for (size_t i = 0; i < n; ++i) { f = d.front(); b = d.back(); }
            compiler_barrier();
            (void)f; (void)b;
        });
        print_ns("front/back", 2 * n, ns / static_cast<double>(2 * n));
    }

    // 2.5 data()
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = d.data();
            touch_ptr(p);
            return p;
        });
        print_ns("data()", 1, ns);
    }

    print_footer();
}

// === Section 3: 迭代器 ===
template <typename T>
static void test_iterator(size_t n)
{
    print_header(("Section 3: iterator (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // 3.1 begin/end 顺序遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = d.begin(); it != d.end(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("begin/end iter", n, ns / static_cast<double>(n));
    }

    // 3.2 cbegin/cend const 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = d.cbegin(); it != d.cend(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("cbegin/cend iter", n, ns / static_cast<double>(n));
    }

    // 3.3 for_each (DENSE_FLATTEN + ivdep)
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            d.for_each([&](T& v) { sink = v; });
            compiler_barrier();
            (void)sink;
        });
        print_ns("for_each (ivdep)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 4: 容量查询 ===
template <typename T>
static void test_capacity_query(size_t n)
{
    print_header(("Section 4: capacity query (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    const size_t OPS = 1000000;

    // 4.1 size() / capacity() / empty() / valid() / max_size()
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += d.size(); s += d.capacity(); s += d.empty() ? 1 : 0;
                s += d.valid() ? 1 : 0; s += d.max_size();
            }
            (void)s;
        });
        print_ns("size/cap/empty/valid/max", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 size_bytes / capacity_bytes / count / span
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += d.size_bytes(); s += d.capacity_bytes(); s += d.count();
                auto sp = d.span();
                s += sp.size();
            }
            (void)s;
        });
        print_ns("size_bytes/cap_bytes/count/span", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 追加类操作 ===
template <typename T>
static void test_append(size_t n)
{
    print_header(("Section 5: append (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 5.1 push_back_unchecked (预分配容量)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("push_back_unchecked", n, ns / static_cast<double>(n));
    }

    // 5.2 emplace_back_unchecked
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.emplace_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("emplace_back_unchecked", n, ns / static_cast<double>(n));
    }

    // 5.3 emplace_back (带容量检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.emplace_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("emplace_back", n, ns / static_cast<double>(n));
    }

    // 5.4 append_n (count 个相同值)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            d.append_n(n, v);
            compiler_barrier();
            return d.size();
        });
        print_ns("append_n", n, ns / static_cast<double>(n));
    }

    // 5.5 append_bulk (批量拷贝)
    {
        vector<T> src;
        src.reserve(n);
        for (size_t i = 0; i < n; ++i) src.push_back(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            d.append_bulk(src.data(), n);
            compiler_barrier();
            return d.size();
        });
        print_ns("append_bulk", n, ns / static_cast<double>(n));
    }

    // 5.6 append_bulk_move (批量移动)
    {
        double ns = best_ns(REPEAT, [&]() {
            vector<T> src; src.reserve(n);
            for (size_t i = 0; i < n; ++i) src.push_back(make_value<T>(static_cast<uint32_t>(i)));
            dense<T> d; d.increase_capacity(n);
            d.append_bulk_move(src.data(), n);
            compiler_barrier();
            return d.size();
        });
        print_ns("append_bulk_move", n, ns / static_cast<double>(n));
    }

    // 5.7 append_incrementing (要求 T 可从 uint64_t 赋值, 仅基础整数类型适用)
    // 单独在 main 中用 uint32_t 测试, 此处跳过
    (void)0;

    // 5.8 fill_bulk (区间填充, AVX2 广播)
    {
        T v = make_value<T>(42);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            d.fill_bulk(v, 0, n);
            compiler_barrier();
            return d.size();
        });
        print_ns("fill_bulk (AVX2 broadcast)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 6: 容量调整 ===
template <typename T>
static void test_resize(size_t n)
{
    print_header(("Section 6: resize (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 6.1 increase_capacity (扩容, 不初始化)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            compiler_barrier();
            return d.capacity();
        });
        print_ns("increase_capacity", 1, ns);
    }

    // 6.2 increase_capacity + fill
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            compiler_barrier();
            return d.capacity();
        });
        print_ns("increase_capacity+fill", n, ns / static_cast<double>(n));
    }

    // 6.3 reserve_exact
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.reserve_exact(n);
            compiler_barrier();
            return d.capacity();
        });
        print_ns("reserve_exact", 1, ns);
    }

    // 6.4 resize
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            compiler_barrier();
            return d.size();
        });
        print_ns("resize", n, ns / static_cast<double>(n));
    }

    // 6.5 shrink_to_fit
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            d.shrink_to_fit();
            compiler_barrier();
            return d.capacity();
        });
        print_ns("shrink_to_fit", 1, ns);
    }

    // 6.6 reduce_capacity
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            d.reduce_capacity(n / 2);
            compiler_barrier();
            return d.capacity();
        });
        print_ns("reduce_capacity", 1, ns);
    }

    // 6.7 reduce_capacity + dst (尾部搬运)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            dense<T> dst;
            d.reduce_capacity(n / 2, dst);
            compiler_barrier();
            return d.capacity() + dst.capacity();
        });
        print_ns("reduce_capacity+dst", 1, ns);
    }

    // 6.8 clear
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            d.clear();
            compiler_barrier();
            return d.size();
        });
        print_ns("clear", 1, ns);
    }

    print_footer();
}

// === Section 7: 插入/删除 ===
template <typename T>
static void test_insert_erase(size_t n)
{
    print_header(("Section 7: insert/erase (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 7.1 emplace (中间位置)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.emplace(d.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("emplace(begin)", n, ns / static_cast<double>(n));
    }

    // 7.2 insert (中间位置, const ref)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.insert(d.begin(), v);
            compiler_barrier();
            return d.size();
        });
        print_ns("insert(begin, const&)", n, ns / static_cast<double>(n));
    }

    // 7.3 insert (中间位置, rvalue)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.insert(d.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("insert(begin, &&)", n, ns / static_cast<double>(n));
    }

    // 7.4 erase (单点, 末尾)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            for (size_t i = 0; i < n; ++i) d.erase(d.begin() + (d.size() - 1));
            compiler_barrier();
            return d.size();
        });
        print_ns("erase(end-1)", n, ns / static_cast<double>(n));
    }

    // 7.5 erase (单点, 头部)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            for (size_t i = 0; i < n; ++i) d.erase(d.begin());
            compiler_barrier();
            return d.size();
        });
        print_ns("erase(begin)", n, ns / static_cast<double>(n));
    }

    // 7.6 erase (区间)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            d.erase(d.begin(), d.end());
            compiler_barrier();
            return d.size();
        });
        print_ns("erase(range)", n, ns / static_cast<double>(n));
    }

    // 7.7 pop_back
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.resize(n, v);
            for (size_t i = 0; i < n; ++i) d.pop_back();
            compiler_barrier();
            return d.size();
        });
        print_ns("pop_back", n, ns / static_cast<double>(n));
    }

    // 7.8 swap
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> a; a.resize(n, v);
            dense<T> b; b.resize(n, v);
            a.swap(b);
            compiler_barrier();
            return a.size() + b.size();
        });
        print_ns("swap", 1, ns);
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  dense<T> 独立性能测试\n";
    cout << "  编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K

    test_construct<POD4>(N);
    test_construct<POD12>(N);
    test_construct<POD32>(N);

    test_access<POD4>(N);
    test_access<POD32>(N);

    test_iterator<POD4>(N);
    test_iterator<POD32>(N);

    test_capacity_query<POD4>(N);

    test_append<POD4>(N);
    test_append<POD12>(N);
    test_append<POD32>(N);
    test_append<POD128>(N);

    test_resize<POD4>(N);
    test_resize<POD32>(N);

    test_insert_erase<POD4>(N);
    test_insert_erase<POD32>(N);

    // append_incrementing 专用测试 (要求 T 可从 uint64_t 赋值)
    {
        print_header("Section 8: append_incrementing (uint32_t)");
        constexpr int REPEAT = 3;
        const size_t n = N;
        uint64_t counter = 0;
        double ns = best_ns(REPEAT, [&]() {
            dense<uint32_t> d; d.increase_capacity(n);
            d.append_incrementing(n, counter);
            compiler_barrier();
            return d.size();
        });
        print_ns("append_incrementing<uint32_t>", n, ns / static_cast<double>(n));
        print_footer();
    }

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
