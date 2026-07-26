// test_class_pool_perf.cpp - class_pool<T> 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/class_pool.hpp"

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
        double ns = best_ns(REPEAT, [&]() { class_pool<T> p; compiler_barrier(); return p.size(); });
        print_ns("default ctor", 1, ns);
    }

    // 1.2 预分配容量
    {
        double ns = best_ns(REPEAT, [&]() { class_pool<T> p(n); compiler_barrier(); return p.capacity(); });
        print_ns("reserve ctor", 1, ns);
    }

    // 1.3 count 个 value 副本
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() { class_pool<T> p(n, v); compiler_barrier(); return p.size(); });
        print_ns("count-value ctor", n, ns / static_cast<double>(n));
    }

    // 1.4 拷贝构造
    {
        class_pool<T> src(n);
        for (size_t i = 0; i < n; ++i) src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() { class_pool<T> p(src); compiler_barrier(); return p.size(); });
        print_ns("copy ctor", n, ns / static_cast<double>(n));
    }

    // 1.5 移动构造
    {
        class_pool<T> src(n);
        for (size_t i = 0; i < n; ++i) src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> tmp = std::move(src);
            compiler_barrier();
            class_pool<T> r = std::move(tmp);
            return r.size();
        });
        print_ns("move ctor", 1, ns);
    }

    print_footer();
}

// === Section 2: 元素访问 ===
template <typename T>
static void test_access(size_t n)
{
    print_header(("Section 2: access (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // 2.1 operator[]
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = p[opaque(i)]; }
            compiler_barrier();
            (void)sink;
        });
        print_ns("operator[]", n, ns / static_cast<double>(n));
    }

    // 2.2 get(idx)
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = p.get(opaque(i)); }
            compiler_barrier();
            (void)sink;
        });
        print_ns("get(idx)", n, ns / static_cast<double>(n));
    }

    // 2.3 get(idx, err_idx) 越界回退
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = p.get(opaque(i + n), 0); }
            compiler_barrier();
            (void)sink;
        });
        print_ns("get(idx, err_idx)", n, ns / static_cast<double>(n));
    }

    // 2.4 front / back
    {
        double ns = best_ns(REPEAT, [&]() {
            T f{}, b{};
            for (size_t i = 0; i < n; ++i) { f = p.front(); b = p.back(); }
            compiler_barrier();
            (void)f; (void)b;
        });
        print_ns("front/back", 2 * n, ns / static_cast<double>(2 * n));
    }

    // 2.5 data()
    {
        double ns = best_ns(REPEAT, [&]() {
            T* ptr = p.data();
            touch_ptr(ptr);
            return ptr;
        });
        print_ns("data()", 1, ns);
    }

    print_footer();
}

// === Section 3: 迭代器 (含稀疏模式) ===
template <typename T>
static void test_iterator(size_t n)
{
    print_header(("Section 3: iterator (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // 3.1 begin/end 顺序遍历 (稠密模式)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = p.begin(); it != p.end(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("begin/end iter (dense)", n, ns / static_cast<double>(n));
    }

    // 3.2 cbegin/cend
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = p.cbegin(); it != p.cend(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("cbegin/cend iter", n, ns / static_cast<double>(n));
    }

    // 3.3 rbegin/rend 反向遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = p.rbegin(); it != p.rend(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("rbegin/rend iter", n, ns / static_cast<double>(n));
    }

    // 3.4 稀疏模式遍历 (隔一个删一个)
    {
        class_pool<T> ps(n);
        for (size_t i = 0; i < n; ++i) ps.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        for (size_t i = 0; i < n; i += 2) ps.soft_sparse_delete(i);
        size_t live = n / 2;

        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = ps.begin(); it != ps.end(); ++it) { sink += 1; touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("begin/end iter (sparse 50%)", live, ns / static_cast<double>(live));
    }

    print_footer();
}

// === Section 4: 容量查询 ===
template <typename T>
static void test_capacity_query(size_t n)
{
    print_header(("Section 4: capacity query (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    const size_t OPS = 1000000;

    // 4.1 size / capacity / empty / valid (class_pool 无 max_size)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += p.size(); s += p.capacity(); s += p.empty() ? 1 : 0;
                s += p.valid() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/cap/empty/valid", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 size_bytes / capacity_bytes / count / span
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += p.size_bytes(); s += p.capacity_bytes();
                s += p.count();
                auto sp = p.span();
                s += sp.size();
            }
            (void)s;
        });
        print_ns("size_bytes/cap_bytes/count/span", OPS, ns / static_cast<double>(OPS));
    }

    // 4.3 is_dense / is_constructed_at / sparse_capacity
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += p.is_dense() ? 1 : 0;
                s += p.is_constructed_at(opaque(static_cast<size_t>(i % n))) ? 1 : 0;
                s += p.sparse_capacity();
            }
            (void)s;
        });
        print_ns("is_dense/is_constructed_at/sparse_cap", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 追加类操作 ===
template <typename T>
static void test_append(size_t n)
{
    print_header(("Section 5: append (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 5.1 push_back_unchecked
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("push_back_unchecked", n, ns / static_cast<double>(n));
    }

    // 5.2 emplace_back_unchecked
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.emplace_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("emplace_back_unchecked", n, ns / static_cast<double>(n));
    }

    // 5.3 emplace_back (带容量检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.emplace_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("emplace_back", n, ns / static_cast<double>(n));
    }

    // 5.4 append_n
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            p.append_n(n, v);
            compiler_barrier();
            return p.size();
        });
        print_ns("append_n", n, ns / static_cast<double>(n));
    }

    // 5.5 fill_bulk (区间填充, AVX2 广播)
    {
        T v = make_value<T>(42);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            p.fill_bulk(v, 0, n);
            compiler_barrier();
            return p.size();
        });
        print_ns("fill_bulk (AVX2)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 6: 容量调整 ===
template <typename T>
static void test_resize(size_t n)
{
    print_header(("Section 6: resize (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 6.1 increase_capacity
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            compiler_barrier();
            return p.capacity();
        });
        print_ns("increase_capacity", 1, ns);
    }

    // 6.2 increase_capacity + fill
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            compiler_barrier();
            return p.capacity();
        });
        print_ns("increase_capacity+fill", n, ns / static_cast<double>(n));
    }

    // 6.3 reserve_exact
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.reserve_exact(n);
            compiler_barrier();
            return p.capacity();
        });
        print_ns("reserve_exact", 1, ns);
    }

    // 6.4 resize
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            compiler_barrier();
            return p.size();
        });
        print_ns("resize", n, ns / static_cast<double>(n));
    }

    // 6.5 shrink_to_fit
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            p.shrink_to_fit();
            compiler_barrier();
            return p.capacity();
        });
        print_ns("shrink_to_fit", 1, ns);
    }

    // 6.6 reduce_capacity
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            p.reduce_capacity(n / 2);
            compiler_barrier();
            return p.capacity();
        });
        print_ns("reduce_capacity", 1, ns);
    }

    // 6.7 reduce_capacity + dst
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            class_pool<T> dst;
            p.reduce_capacity(n / 2, dst);
            compiler_barrier();
            return p.capacity() + dst.capacity();
        });
        print_ns("reduce_capacity+dst", 1, ns);
    }

    // 6.8 clear
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            p.clear();
            compiler_barrier();
            return p.size();
        });
        print_ns("clear", 1, ns);
    }

    print_footer();
}

// === Section 7: 稀疏专用接口 ===
template <typename T>
static void test_sparse(size_t n)
{
    print_header(("Section 7: sparse (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 7.1 emplace_at (原位构造)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.emplace_at(i, make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("emplace_at", n, ns / static_cast<double>(n));
    }

    // 7.2 sparse_emplace_at
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.sparse_emplace_at(i, make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("sparse_emplace_at", n, ns / static_cast<double>(n));
    }

    // 7.3 sparse_erase_at (稀疏擦除)
    {
        class_pool<T> p(n);
        for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> tmp; tmp.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) tmp.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < n; ++i) tmp.sparse_erase_at(i);
            compiler_barrier();
            return tmp.size();
        });
        print_ns("sparse_erase_at", n, ns / static_cast<double>(n));
    }

    // 7.4 soft_sparse_delete (软删除)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < n; ++i) p.soft_sparse_delete(i);
            compiler_barrier();
            return p.size();
        });
        print_ns("soft_sparse_delete", n, ns / static_cast<double>(n));
    }

    // 7.5 soft_dense_delete (区间软删除)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            p.soft_dense_delete(0, n);
            compiler_barrier();
            return p.size();
        });
        print_ns("soft_dense_delete(range)", n, ns / static_cast<double>(n));
    }

    // 7.6 fill_the_hole (填洞或追加)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.fill_the_hole(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("fill_the_hole", n, ns / static_cast<double>(n));
    }

    // 7.7 fill_the_hole_at (返回索引)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.fill_the_hole_at(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("fill_the_hole_at", n, ns / static_cast<double>(n));
    }

    // 7.8 prepare_dense (预备稠密段)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            p.prepare_dense(n);
            compiler_barrier();
            return p.size();
        });
        print_ns("prepare_dense", 1, ns);
    }

    // 7.9 invalidate_count_cache
    {
        class_pool<T> p(n);
        for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) p.invalidate_count_cache();
            compiler_barrier();
            return OPS;
        });
        print_ns("invalidate_count_cache", OPS, ns / static_cast<double>(OPS));
    }

    // 7.10 count (缓存失效后重算)
    {
        class_pool<T> p(n);
        for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        p.invalidate_count_cache();
        double ns = best_ns(REPEAT, [&]() {
            p.invalidate_count_cache();
            size_t c = p.count();
            compiler_barrier();
            return c;
        });
        print_ns("count (cache miss)", 1, ns);
    }

    print_footer();
}

// === Section 8: 插入/删除 (中间位置) ===
template <typename T>
static void test_insert_erase(size_t n)
{
    print_header(("Section 8: insert/erase (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 8.1 emplace (中间位置)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.emplace(p.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("emplace(begin)", n, ns / static_cast<double>(n));
    }

    // 8.2 insert (中间位置, const ref)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.insert(p.begin(), v);
            compiler_barrier();
            return p.size();
        });
        print_ns("insert(begin, const&)", n, ns / static_cast<double>(n));
    }

    // 8.3 insert (中间位置, rvalue)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.insert(p.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("insert(begin, &&)", n, ns / static_cast<double>(n));
    }

    // 8.4 erase (单点, 末尾) — class_pool iterator 不支持 operator+, 用 pop_back 等价
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            for (size_t i = 0; i < n; ++i) p.pop_back();
            compiler_barrier();
            return p.size();
        });
        print_ns("erase(end-1) [pop_back]", n, ns / static_cast<double>(n));
    }

    // 8.5 erase (单点, 头部)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            for (size_t i = 0; i < n; ++i) p.erase(p.begin());
            compiler_barrier();
            return p.size();
        });
        print_ns("erase(begin)", n, ns / static_cast<double>(n));
    }

    // 8.6 erase (区间)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            p.erase(p.begin(), p.end());
            compiler_barrier();
            return p.size();
        });
        print_ns("erase(range)", n, ns / static_cast<double>(n));
    }

    // 8.7 pop_back
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.resize(n, v);
            for (size_t i = 0; i < n; ++i) p.pop_back();
            compiler_barrier();
            return p.size();
        });
        print_ns("pop_back", n, ns / static_cast<double>(n));
    }

    // 8.8 swap
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> a; a.resize(n, v);
            class_pool<T> b; b.resize(n, v);
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
    cout << "  class_pool<T> 独立性能测试\n";
    cout << "  编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K

    test_construct<POD4>(N);
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

    test_sparse<POD4>(N);
    test_sparse<POD32>(N);

    test_insert_erase<POD4>(N);
    test_insert_erase<POD32>(N);

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
