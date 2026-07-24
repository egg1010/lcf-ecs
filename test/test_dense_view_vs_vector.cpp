// ============================================================
// test_dense_view_vs_vector.cpp
// dense<T> vs std::vector 全面对比性能测试
// 含: 遍历 / 下标 / 累加 / 写入 / 变量添加
// 编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2
// ============================================================
#include "test_common.hpp"
#include "include/part/dense.hpp"

// ============================================================
// UTF-8 显示宽度 (CJK 字符算 2, 其他算 1) + 左对齐填充
// ============================================================
size_t utf8_display_width(const char* s)
{
    size_t w = 0;
    while (unsigned char c = static_cast<unsigned char>(*s))
    {
        if (c < 0x80) { ++w; ++s; }
        else if ((c & 0xE0) == 0xC0) { ++w; s += 2; }
        else if ((c & 0xF0) == 0xE0) { w += 2; s += 3; }
        else { ++w; s += 4; }
    }
    return w;
}

std::string pad_right(const char* s, size_t width)
{
    std::string out(s);
    size_t w = utf8_display_width(s);
    if (w < width) { out.append(width - w, ' '); }
    return out;
}

std::string pad_left(const char* s, size_t width)
{
    size_t w = utf8_display_width(s);
    std::string out;
    if (w < width) { out.append(width - w, ' '); }
    out.append(s);
    return out;
}

// ============================================================
// 通用基准模板: 重复 R 轮, 取单次延迟
// ============================================================
static constexpr size_t LABEL_W = 48;

template <typename BenchFn>
void bench(const char* label, size_t ops, int rounds, BenchFn&& fn)
{
    // 预热
    fn();
    // 计时
    timer t;
    t.reset();
    for (int r = 0; r < rounds; ++r)
    {
        fn();
    }
    double total_ms = t.elapsed_ms();
    double latency_ns = (total_ms * 1e6) / static_cast<double>(ops * rounds);
    double throughput = static_cast<double>(ops * rounds) / (total_ms / 1000.0);

    auto fmt_tps = [](double tps) -> std::string
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        if (tps >= 1e8) { ss << (tps / 1e8) << "亿/s"; }
        else if (tps >= 1e4) { ss << (tps / 1e4) << "万/s"; }
        else { ss << static_cast<size_t>(tps) << "/s"; }
        return ss.str();
    };

    std::string cnt_str = std::to_string(ops * rounds) + " 次";
    std::string ms_str = std::to_string(total_ms).substr(0, std::to_string(total_ms).find('.') + 4) + " ms";
    std::string ns_str = std::to_string(latency_ns).substr(0, std::to_string(latency_ns).find('.') + 4) + " ns";
    std::string tps_str = fmt_tps(throughput);

    std::cout << "  " << pad_right(label, LABEL_W)
              << " | " << pad_left(cnt_str.c_str(), 12)
              << " | " << pad_left(ms_str.c_str(), 11)
              << " | " << pad_left(ns_str.c_str(), 11)
              << " | " << pad_left(tps_str.c_str(), 11)
              << "\n";
}

// ============================================================
// 表头
// ============================================================
void print_compare_header()
{
    std::cout << "\n  " << std::string(LABEL_W + 52, '-') << "\n";
    std::cout << "  " << pad_right("接口", LABEL_W)
              << " | " << pad_left("总次数", 12)
              << " | " << pad_left("耗时ms", 11)
              << " | " << pad_left("单次ns", 11)
              << " | " << pad_left("吞吐", 11)
              << "\n";
    std::cout << "  " << std::string(LABEL_W + 52, '-') << "\n";
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    constexpr size_t N = 1000000;  // 1M / 百万元素
    constexpr int ROUNDS = 10;     // 每个基准重复 10 轮

    std::cout << "========================================================\n"
              << "  dense<T> vs class_pool vs std::vector 全面对比\n"
              << "  编译器: MinGW GCC 15.2.0 (-O3 -std=c++20 -mavx2 -mbmi)\n"
              << "  数据量: " << N << " 元素 (int), 重复 " << ROUNDS << " 轮\n"
              << "========================================================\n";

    // ============================================================
    // 准备数据: dense + class_pool + std::vector 各一份相同数据
    // ============================================================
    class_pool<int> cp;
    cp.increase_capacity(N);
    for (size_t i = 0; i < N; ++i)
    {
        cp.emplace_back(static_cast<int>(i));
    }

    std::vector<int> vec;
    vec.reserve(N);
    for (size_t i = 0; i < N; ++i)
    {
        vec.push_back(static_cast<int>(i));
    }

    dense<int> dn;
    dn.increase_capacity(N);
    for (size_t i = 0; i < N; ++i)
    {
        dn.emplace_back(static_cast<int>(i));
    }

    const class_pool<int>& cp_c = cp;
    const std::vector<int>& vec_c = vec;
    const dense<int>& dn_c = dn;

    // ============================================================
    // Section 1: range-for 遍历对比
    // ============================================================
    print_section(1, "range-for 遍历对比 (1M x 10轮)");
    print_compare_header();

    bench("vector range-for (iter)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : vec)
        {
            sum = v;
        }
    });

    bench("vector range-for (const iter)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : vec_c)
        {
            sum = v;
        }
    });

    bench("class_pool range-for (iter)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : cp)
        {
            sum = v;
        }
    });

    bench("class_pool range-for (const iter)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : cp_c)
        {
            sum = v;
        }
    });

    bench("class_pool cbegin/cend (10x)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto it = cp.cbegin(); it != cp.cend(); ++it)
        {
            sum = *it;
        }
    });

    bench("dense range-for (iter)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : dn)
        {
            sum = v;
        }
    });

    bench("dense range-for (const iter)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : dn_c)
        {
            sum = v;
        }
    });

    bench("dense cbegin/cend (10x)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto it = dn.cbegin(); it != dn.cend(); ++it)
        {
            sum = *it;
        }
    });

    // ============================================================
    // Section 2: range-for 遍历对比
    // ============================================================
    print_section(2, "range-for 遍历对比 (1M x 10轮)");
    print_compare_header();

    bench("vector range-for", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : vec)
        {
            sum = v;
        }
    });

    bench("dense range-for", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& x : dn)
        {
            sum = x;
        }
    });

    bench("dense span range-for", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& x : dn.span())
        {
            sum = x;
        }
    });

    // ============================================================
    // Section 3: for_each 回调对比
    // ============================================================
    print_section(3, "for_each 回调对比 (1M x 10轮)");
    print_compare_header();

    bench("vector for_each (lambda)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        std::for_each(vec.begin(), vec.end(), [&sum](int& v) { sum = v; });
    });

    bench("dense for_each (lambda, ivdep)", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        dn.for_each([&sum](int& v) { sum = v; });
    });

    bench("dense const for_each", N, ROUNDS, [&]() {
        volatile long long sum = 0;
        dn_c.for_each([&sum](const int& v) { sum = v; });
    });

    // ============================================================
    // Section 4: 下标访问对比
    // ============================================================
    print_section(4, "下标访问对比 (1M x 10轮)");
    print_compare_header();

    bench("vector operator[]", N, ROUNDS, [&]() {
        volatile int v = 0;
        for (size_t i = 0; i < N; ++i)
        {
            v = vec[i];
        }
    });

    bench("vector data()[i]", N, ROUNDS, [&]() {
        volatile int v = 0;
        const int* p = vec.data();
        for (size_t i = 0; i < N; ++i)
        {
            v = p[i];
        }
    });

    bench("class_pool operator[]", N, ROUNDS, [&]() {
        volatile int v = 0;
        for (size_t i = 0; i < N; ++i)
        {
            v = cp[i];
        }
    });

    bench("class_pool get(i)", N, ROUNDS, [&]() {
        volatile int v = 0;
        for (size_t i = 0; i < N; ++i)
        {
            v = cp.get(i);
        }
    });

    bench("class_pool data()[i]", N, ROUNDS, [&]() {
        volatile int v = 0;
        const int* p = cp.data();
        for (size_t i = 0; i < N; ++i)
        {
            v = p[i];
        }
    });

    bench("dense operator[]", N, ROUNDS, [&]() {
        volatile int v = 0;
        for (size_t i = 0; i < N; ++i)
        {
            v = dn[i];
        }
    });

    bench("dense get(i)", N, ROUNDS, [&]() {
        volatile int v = 0;
        for (size_t i = 0; i < N; ++i)
        {
            v = dn.get(i);
        }
    });

    bench("dense data()[i]", N, ROUNDS, [&]() {
        volatile int v = 0;
        const int* p = dn.data();
        for (size_t i = 0; i < N; ++i)
        {
            v = p[i];
        }
    });

    // ============================================================
    // Section 5: 累加求和对比 (真实计算场景)
    // ============================================================
    print_section(5, "累加求和对比 (1M x 10轮, 真实计算)");
    print_compare_header();

    bench("vector 累加 range-for", N, ROUNDS, [&]() {
        long long sum = 0;
        for (auto v : vec)
        {
            sum += v;
        }
        volatile long long sink = sum;
    });

    bench("vector 累加 std::accumulate", N, ROUNDS, [&]() {
        long long sum = std::accumulate(vec.begin(), vec.end(), 0LL);
        volatile long long sink = sum;
    });

    bench("class_pool 累加 range-for", N, ROUNDS, [&]() {
        long long sum = 0;
        for (auto v : cp)
        {
            sum += v;
        }
        volatile long long sink = sum;
    });

    bench("dense 累加 range-for", N, ROUNDS, [&]() {
        long long sum = 0;
        for (auto v : dn)
        {
            sum += v;
        }
        volatile long long sink = sum;
    });

    bench("dense 累加 for_each (ivdep)", N, ROUNDS, [&]() {
        long long sum = 0;
        dn.for_each([&sum](int& v) { sum += v; });
        volatile long long sink = sum;
    });

    // ============================================================
    // Section 6: Position 结构体 (12 字节) 对比
    // ============================================================
    print_section(6, "Position 结构体 (12 字节) 遍历对比 (1M x 10轮)");
    print_compare_header();

    class_pool<Position> cp_pos;
    cp_pos.increase_capacity(N);
    for (size_t i = 0; i < N; ++i)
    {
        cp_pos.emplace_back(
            static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3));
    }

    std::vector<Position> vec_pos;
    vec_pos.reserve(N);
    for (size_t i = 0; i < N; ++i)
    {
        vec_pos.emplace_back(
            static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3));
    }

    dense<Position> dn_pos;
    dn_pos.increase_capacity(N);
    for (size_t i = 0; i < N; ++i)
    {
        dn_pos.emplace_back(
            static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3));
    }

    bench("vector Position range-for", N, ROUNDS, [&]() {
        volatile float sx = 0, sy = 0, sz = 0;
        for (auto& p : vec_pos)
        {
            sx = p.x; sy = p.y; sz = p.z;
        }
    });

    bench("class_pool Position range-for", N, ROUNDS, [&]() {
        volatile float sx = 0, sy = 0, sz = 0;
        for (auto& p : cp_pos)
        {
            sx = p.x; sy = p.y; sz = p.z;
        }
    });

    bench("dense Position range-for", N, ROUNDS, [&]() {
        volatile float sx = 0, sy = 0, sz = 0;
        for (auto& p : dn_pos)
        {
            sx = p.x; sy = p.y; sz = p.z;
        }
    });

    bench("dense Position for_each (ivdep)", N, ROUNDS, [&]() {
        volatile float sx = 0, sy = 0, sz = 0;
        dn_pos.for_each([&sx, &sy, &sz](Position& p) {
            sx = p.x; sy = p.y; sz = p.z;
        });
    });

    // ============================================================
    // Section 7: 写入对比 (修改元素)
    // ============================================================
    print_section(7, "写入对比 (1M x 10轮, 修改元素)");
    print_compare_header();

    bench("vector 写入 range-for", N, ROUNDS, [&]() {
        int val = 0;
        for (auto& v : vec)
        {
            v = val++;
        }
    });

    bench("class_pool 写入 range-for", N, ROUNDS, [&]() {
        int val = 0;
        for (auto& v : cp)
        {
            v = val++;
        }
    });

    bench("dense 写入 range-for", N, ROUNDS, [&]() {
        int val = 0;
        for (auto& v : dn)
        {
            v = val++;
        }
    });

    bench("dense 写入 for_each (ivdep)", N, ROUNDS, [&]() {
        int val = 0;
        dn.for_each([&val](int& v) { v = val++; });
    });

    // ============================================================
    // Section 8: 变量添加速度对比 (尾部追加)
    // ============================================================
    print_section(8, "变量添加速度对比 (1M 元素追加)");
    print_compare_header();

    bench("vector push_back", N, 1, [&]() {
        std::vector<int> v;
        v.reserve(N);
        for (size_t i = 0; i < N; ++i)
        {
            v.push_back(static_cast<int>(i));
        }
    });

    bench("vector emplace_back", N, 1, [&]() {
        std::vector<int> v;
        v.reserve(N);
        for (size_t i = 0; i < N; ++i)
        {
            v.emplace_back(static_cast<int>(i));
        }
    });

    bench("class_pool emplace_back (无预分配)", N, 1, [&]() {
        class_pool<int> p;
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back(static_cast<int>(i));
        }
    });

    bench("class_pool emplace_back (预分配)", N, 1, [&]() {
        class_pool<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back(static_cast<int>(i));
        }
    });

    bench("class_pool push_back_unchecked (预分配)", N, 1, [&]() {
        class_pool<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.push_back_unchecked(static_cast<int>(i));
        }
    });

    bench("class_pool emplace_back_unchecked (预分配)", N, 1, [&]() {
        class_pool<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back_unchecked(static_cast<int>(i));
        }
    });

    bench("class_pool emplace_back_dense_unchecked (预分配)", N, 1, [&]() {
        class_pool<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back_dense_unchecked(static_cast<int>(i));
        }
    });

    bench("class_pool append_n (批量)", N, 1, [&]() {
        class_pool<int> p;
        p.increase_capacity(N);
        p.append_n(N, 0);
    });

    bench("dense emplace_back (无预分配)", N, 1, [&]() {
        dense<int> p;
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back(static_cast<int>(i));
        }
    });

    bench("dense emplace_back (预分配)", N, 1, [&]() {
        dense<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back(static_cast<int>(i));
        }
    });

    bench("dense push_back_unchecked (预分配)", N, 1, [&]() {
        dense<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.push_back_unchecked(static_cast<int>(i));
        }
    });

    bench("dense emplace_back_unchecked (预分配)", N, 1, [&]() {
        dense<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back_unchecked(static_cast<int>(i));
        }
    });

    bench("dense emplace_back_dense_unchecked (预分配)", N, 1, [&]() {
        dense<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back_dense_unchecked(static_cast<int>(i));
        }
    });

    bench("dense append_n (批量)", N, 1, [&]() {
        dense<int> p;
        p.increase_capacity(N);
        p.append_n(N, 0);
    });

    // ============================================================
    // Section 9: 稀疏模式对比 (N/2 有效元素, 隔位删除)
    // ============================================================
    print_section(9, "稀疏模式对比 (1M 元素隔位删除, N/2 有效)");
    print_compare_header();

    // 准备稀疏数据: class_pool 隔位删除产生 N/2 空洞
    class_pool<int> cp_sparse;
    cp_sparse.increase_capacity(N);
    for (size_t i = 0; i < N; ++i)
    {
        cp_sparse.emplace_back_dense_unchecked(static_cast<int>(i));
    }
    for (size_t i = 0; i < N; i += 2)
    {
        cp_sparse.sparse_erase_at(i);
    }

    // 准备对照 vector: 仅保留奇数索引元素 (N/2 个)
    std::vector<int> vec_sparse;
    vec_sparse.reserve(N / 2);
    for (size_t i = 1; i < N; i += 2)
    {
        vec_sparse.push_back(static_cast<int>(i));
    }

    const size_t VALID = N / 2;

    bench("vector 稀疏对照 range-for", VALID, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : vec_sparse)
        {
            sum = v;
        }
    });

    bench("vector 稀疏对照 累加", VALID, ROUNDS, [&]() {
        long long sum = 0;
        for (auto v : vec_sparse)
        {
            sum += v;
        }
        volatile long long sink = sum;
    });

    bench("class_pool range-for (sparse 模式)", VALID, ROUNDS, [&]() {
        volatile long long sum = 0;
        for (auto& v : cp_sparse)
        {
            sum = v;
        }
    });

    bench("class_pool range-for 累加 (sparse)", VALID, ROUNDS, [&]() {
        long long sum = 0;
        for (auto v : cp_sparse)
        {
            sum += v;
        }
        volatile long long sink = sum;
    });

    // ============================================================
    // Section 10: 填洞对比 (fill_the_hole vs vector push_back)
    // ============================================================
    print_section(10, "填洞对比 (N/2 次 fill_the_hole vs vector push_back)");
    print_compare_header();

    bench("vector push_back (N/2 次)", VALID, 1, [&]() {
        std::vector<int> v;
        v.reserve(VALID);
        for (size_t i = 0; i < VALID; ++i)
        {
            v.push_back(static_cast<int>(i));
        }
    });

    bench("class_pool fill_the_hole (填 N/2 空洞)", VALID, 1, [&]() {
        class_pool<int> p;
        p.increase_capacity(N);
        for (size_t i = 0; i < N; ++i)
        {
            p.emplace_back_dense_unchecked(static_cast<int>(i));
        }
        for (size_t i = 0; i < N; i += 2)
        {
            p.sparse_erase_at(i);
        }
        for (size_t i = 0; i < VALID; ++i)
        {
            p.fill_the_hole(static_cast<int>(i));
        }
    });

    // ============================================================
    // Section 11: 汇总
    // ============================================================
    std::cout << "\n" << std::string(56, '=') << "\n";
    std::cout << "  对比汇总 / Summary\n"
              << "  - dense<T> 纯密集容器, 不参与稀疏模式 (Section 9-10)\n"
              << "  - 密集场景下 dense 与 class_pool 性能等价 (无 sparse 开销)\n";
    std::cout << std::string(56, '=') << "\n";
    std::cout << "\n";

    return 0;
}
