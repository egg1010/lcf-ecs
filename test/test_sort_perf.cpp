// test_sort_perf.cpp - tiered_sort / radix_sort_helper 独立性能测试
#include "perf_common.hpp"
#include "include/part/tiered_sort.hpp"
#include "include/part/radix_sort_helper.hpp"

using namespace std;

// === Section 1: tiered_sort ===
template <typename T>
static void test_tiered_sort(size_t n)
{
    print_header(("Section 1: tiered_sort (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;
    mt19937 rng(42);

    {
        vector<T> data(n);
        if constexpr (is_integral_v<T>) { iota(data.begin(), data.end(), T{}); }
        else { for (size_t i = 0; i < n; ++i) data[i] = static_cast<T>(i); }

        double ns = best_ns(REPEAT, [&]() {
            vector<T> tmp = data;
            tiered_sort(tmp.data(), n, less<T>{});
            compiler_barrier();
            return tmp.size();
        });
        print_ns("tiered_sort (sorted)", n, ns / static_cast<double>(n));
    }

    {
        vector<T> data(n);
        if constexpr (is_integral_v<T>) { for (size_t i = 0; i < n; ++i) data[i] = static_cast<T>(n - i); }
        else { for (size_t i = 0; i < n; ++i) data[i] = static_cast<T>(n - i); }

        double ns = best_ns(REPEAT, [&]() {
            vector<T> tmp = data;
            tiered_sort(tmp.data(), n, less<T>{});
            compiler_barrier();
            return tmp.size();
        });
        print_ns("tiered_sort (reverse)", n, ns / static_cast<double>(n));
    }

    {
        vector<T> data(n);
        if constexpr (is_integral_v<T>)
        {
            uniform_int_distribution<T> dist(0, static_cast<T>(n));
            for (size_t i = 0; i < n; ++i) data[i] = dist(rng);
        }
        else if constexpr (is_floating_point_v<T>)
        {
            uniform_real_distribution<T> dist(0, static_cast<T>(n));
            for (size_t i = 0; i < n; ++i) data[i] = dist(rng);
        }

        double ns = best_ns(REPEAT, [&]() {
            vector<T> tmp = data;
            tiered_sort(tmp.data(), n, less<T>{});
            compiler_barrier();
            return tmp.size();
        });
        print_ns("tiered_sort (random)", n, ns / static_cast<double>(n));
    }

    {
        vector<T> data(n);
        uniform_int_distribution<int> dist(0, 9);
        for (size_t i = 0; i < n; ++i) data[i] = static_cast<T>(dist(rng));

        double ns = best_ns(REPEAT, [&]() {
            vector<T> tmp = data;
            tiered_sort(tmp.data(), n, less<T>{});
            compiler_barrier();
            return tmp.size();
        });
        print_ns("tiered_sort (low cardinality)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 2: pdqsort ===
template <typename T>
static void test_pdqsort(size_t n)
{
    print_header(("Section 2: pdqsort (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;
    mt19937 rng(42);

    vector<T> data(n);
    if constexpr (is_integral_v<T>)
    {
        uniform_int_distribution<T> dist(0, static_cast<T>(n));
        for (size_t i = 0; i < n; ++i) data[i] = dist(rng);
    }

    double ns = best_ns(REPEAT, [&]() {
        vector<T> tmp = data;
        pdqsort(tmp.data(), n, less<T>{});
        compiler_barrier();
        return tmp.size();
    });
    print_ns("pdqsort (random)", n, ns / static_cast<double>(n));

    print_footer();
}

// === Section 3: sort / sort_n / sort_indices ===
template <typename T>
static void test_sort_variants(size_t n)
{
    print_header(("Section 3: sort variants (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;
    mt19937 rng(42);

    {
        vector<T> data(n);
        if constexpr (is_integral_v<T>) { iota(data.begin(), data.end(), T{}); }
        shuffle(data.begin(), data.end(), rng);

        double ns = best_ns(REPEAT, [&]() {
            vector<T> tmp = data;
            sort(tmp.data(), n, less<T>{});
            compiler_barrier();
            return tmp.size();
        });
        print_ns("sort (unified)", n, ns / static_cast<double>(n));
    }

    {
        constexpr size_t SN = 64;
        vector<T> data(SN);
        if constexpr (is_integral_v<T>) { iota(data.begin(), data.end(), T{}); }
        shuffle(data.begin(), data.end(), rng);

        double ns = best_ns(REPEAT, [&]() {
            T tmp[SN];
            copy(data.begin(), data.end(), tmp);
            sort_n<SN, T>(tmp, less<T>{});
            compiler_barrier();
            return tmp[0];
        });
        print_ns("sort_n<64> (compile-time)", SN, ns / static_cast<double>(SN));
    }

    {
        vector<T> values(n);
        if constexpr (is_integral_v<T>) { iota(values.begin(), values.end(), T{}); }
        shuffle(values.begin(), values.end(), rng);

        double ns = best_ns(REPEAT, [&]() {
            vector<size_t> idx(n);
            iota(idx.begin(), idx.end(), 0);
            sort_indices(idx.data(), values.data(), n);
            compiler_barrier();
            return idx.size();
        });
        print_ns("sort_indices", n, ns / static_cast<double>(n));
    }

    {
        constexpr size_t SN = 64;
        vector<T> values(SN);
        if constexpr (is_integral_v<T>) { iota(values.begin(), values.end(), T{}); }
        shuffle(values.begin(), values.end(), rng);

        double ns = best_ns(REPEAT, [&]() {
            size_t idx[SN];
            iota(idx, idx + SN, 0);
            sort_indices_n<SN, T>(idx, values.data());
            compiler_barrier();
            return idx[0];
        });
        print_ns("sort_indices_n<64>", SN, ns / static_cast<double>(SN));
    }

    {
        vector<T> values(n);
        if constexpr (is_integral_v<T>) { iota(values.begin(), values.end(), T{}); }
        shuffle(values.begin(), values.end(), rng);

        double ns = best_ns(REPEAT, [&]() {
            vector<size_t> idx(n);
            iota(idx.begin(), idx.end(), 0);
            tiered_sort_indices(idx.data(), values.data(), n);
            compiler_barrier();
            return idx.size();
        });
        print_ns("tiered_sort_indices", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 4: radix_sort_helper ===
static void test_radix_helper()
{
    print_header("Section 4: radix_sort_helper");
    constexpr int REPEAT = 3;
    constexpr size_t N = 1 << 18;
    mt19937 rng(42);

    {
        vector<uint32_t> data(N);
        uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
        for (size_t i = 0; i < N; ++i) data[i] = dist(rng);

        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t s = 0;
            for (size_t i = 0; i < N; ++i) s += radix_key(data[i]);
            (void)s;
        });
        print_ns("radix_key<uint32_t>", N, ns / static_cast<double>(N));
    }

    {
        vector<int32_t> data(N);
        uniform_int_distribution<int32_t> dist(INT32_MIN, INT32_MAX);
        for (size_t i = 0; i < N; ++i) data[i] = dist(rng);

        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t s = 0;
            for (size_t i = 0; i < N; ++i) s += radix_key(data[i]);
            (void)s;
        });
        print_ns("radix_key<int32_t>", N, ns / static_cast<double>(N));
    }

    {
        vector<float> data(N);
        uniform_real_distribution<float> dist(-1e6f, 1e6f);
        for (size_t i = 0; i < N; ++i) data[i] = dist(rng);

        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t s = 0;
            for (size_t i = 0; i < N; ++i) s += radix_key(data[i]);
            (void)s;
        });
        print_ns("radix_key<float>", N, ns / static_cast<double>(N));
    }

    {
        vector<double> data(N);
        uniform_real_distribution<double> dist(-1e12, 1e12);
        for (size_t i = 0; i < N; ++i) data[i] = dist(rng);

        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t s = 0;
            for (size_t i = 0; i < N; ++i) s += radix_key(data[i]);
            (void)s;
        });
        print_ns("radix_key<double>", N, ns / static_cast<double>(N));
    }

    {
        vector<uint32_t> keys(N);
        uniform_int_distribution<uint32_t> dist(0, static_cast<uint32_t>(N));
        for (size_t i = 0; i < N; ++i) keys[i] = dist(rng);
        vector<size_t> temp(N);

        double ns = best_ns(REPEAT, [&]() {
            vector<size_t> idx(N);
            iota(idx.begin(), idx.end(), 0);
            radix_sort_indices(idx.data(), keys.data(), N, temp.data());
            compiler_barrier();
            return idx.size();
        });
        print_ns("radix_sort_indices<uint32_t>", N, ns / static_cast<double>(N));
    }

    {
        vector<float> keys(N);
        uniform_real_distribution<float> dist(-1e6f, 1e6f);
        for (size_t i = 0; i < N; ++i) keys[i] = dist(rng);
        vector<size_t> temp(N);

        double ns = best_ns(REPEAT, [&]() {
            vector<size_t> idx(N);
            iota(idx.begin(), idx.end(), 0);
            radix_sort_indices(idx.data(), keys.data(), N, temp.data());
            compiler_barrier();
            return idx.size();
        });
        print_ns("radix_sort_indices<float>", N, ns / static_cast<double>(N));
    }

    {
        struct sort_entry { uint32_t key; size_t index; };
        vector<sort_entry> entries(N);
        uniform_int_distribution<uint32_t> dist(0, static_cast<uint32_t>(N));
        for (size_t i = 0; i < N; ++i) { entries[i].key = dist(rng); entries[i].index = i; }

        double ns = best_ns(REPEAT, [&]() {
            vector<sort_entry> tmp = entries;
            radix_sort_entries<uint32_t>(tmp.data(), N);
            compiler_barrier();
            return tmp.size();
        });
        print_ns("radix_sort_entries<uint32_t>", N, ns / static_cast<double>(N));
    }

    {
        constexpr size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
            {
                s = is_radix_sortable_v<uint32_t>;
                s = is_radix_sortable_v<float>;
                s = is_radix_sortable_v<double>;
                s = is_radix_sortable_v<int64_t>;
            }
            (void)s;
        });
        print_ns("is_radix_sortable_v (compile-time)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 不同 N 的分层性能 ===
template <typename T>
static void test_size_sweep()
{
    print_header(("Section 5: size sweep (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 3;
    mt19937 rng(42);

    for (size_t n : {16, 32, 64, 128, 256, 1024, 4096, 16384, 65536, 262144})
    {
        vector<T> data(n);
        if constexpr (is_integral_v<T>)
        {
            uniform_int_distribution<T> dist(0, static_cast<T>(n));
            for (size_t i = 0; i < n; ++i) data[i] = dist(rng);
        }

        double ns = best_ns(REPEAT, [&]() {
            vector<T> tmp = data;
            tiered_sort(tmp.data(), n, less<T>{});
            compiler_barrier();
            return tmp.size();
        });
        char label[64];
        snprintf(label, sizeof(label), "tiered_sort N=%zu", n);
        print_ns(label, n, ns / static_cast<double>(n));
    }

    print_footer();
}

int main()
{
    cout << "============================================================\n";
    cout << "  tiered_sort / radix_sort_helper 独立性能测试\n";
    cout << "  编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K

    test_tiered_sort<uint32_t>(N);
    test_tiered_sort<int32_t>(N);
    test_tiered_sort<float>(N);
    test_tiered_sort<double>(N);

    test_pdqsort<uint32_t>(N);

    test_sort_variants<uint32_t>(N);
    test_sort_variants<float>(N);

    test_radix_helper();

    test_size_sweep<uint32_t>();
    test_size_sweep<float>();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
