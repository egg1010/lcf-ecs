// test_type_def_perf.cpp - type_id 运行期名字类型注册性能测试
// 分组: 键计算对比 / 注册 (冷) / 查询 (热) / 反查
// 方法: opaque() + 数组轮转阻止常量折叠与 CSE (同 test_string_to_code_perf)
#include "perf_common.hpp"
#include "include/part/type_id.hpp"
#include "include/part/string_to_code.hpp"
#include "include/part/fnv1a.hpp"

#include <string>
#include <vector>

using namespace string_to_code;

// 全局 volatile sink, 阻止 DCE
static volatile uint64_t g_u64_sink = 0;
static volatile int g_int_sink = 0;
static volatile size_t g_size_sink = 0;
static volatile const type_def* g_def_sink = nullptr;

static constexpr size_t MASK = 3;  // 4 路轮转

static type_def make_def(size_t size, size_t alignment)
{
    type_def d;
    d.size = size;
    d.alignment = alignment;
    d.trivially_copyable = true;
    return d;
}

// === Section 1: 名字键计算对比 (字节编码 vs fnv1a) ===
static void test_key_encode_vs_fnv1a()
{
    print_header("Section: 名字键计算 (编码 vs fnv1a)");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 短名 8B (运行时填充, 阻止常量折叠)
    {
        static char bufs[4][9];
        for (int j = 0; j < 4; ++j)
        {
            std::memset(bufs[j], 'a' + j, 8);
            bufs[j][8] = '\0';
        }
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        constexpr size_t LEN = 8;
        double enc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_u64_sink = code_value::encode_inline(opaque(arr[i & MASK]), LEN);
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_ns("短名 8B 字节编码", OPS, enc_ns / static_cast<double>(OPS));

        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_u64_sink = fnv1a_runtime(opaque(arr[i & MASK]), LEN);
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_ns("短名 8B fnv1a 哈希", OPS, fnv_ns / static_cast<double>(OPS));
    }

    // 长名 24B
    {
        static char bufs[4][25];
        for (int j = 0; j < 4; ++j)
        {
            std::memset(bufs[j], 'a' + j, 24);
            bufs[j][24] = '\0';
        }
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        constexpr size_t LEN = 24;
        double enc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_u64_sink = code_value::encode_inline_n<8>(opaque(arr[i & MASK]));
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_ns("长名 24B 编码 (前 8B)", OPS, enc_ns / static_cast<double>(OPS));

        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_u64_sink = fnv1a_runtime(opaque(arr[i & MASK]), LEN);
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_ns("长名 24B fnv1a 哈希", OPS, fnv_ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 2: register_type_def (冷路径) ===
static void test_register_cold()
{
    print_header("Section: register_type_def (冷路径)");
    const type_def d = make_def(12, 4);

    // 1000 个短名 + 1000 个长名, 各注册一次
    {
        std::vector<std::string> names;
        names.reserve(2000);
        for (int i = 0; i < 1000; ++i)
        {
            names.push_back("S" + std::to_string(i));          // 2~5B 短名
        }
        for (int i = 0; i < 1000; ++i)
        {
            names.push_back("LongName_" + std::to_string(i));  // 9~13B 长名
        }

        timer t;
        for (int i = 0; i < 2000; ++i)
        {
            type_id::register_type_def(names[static_cast<size_t>(i)], d);
        }
        print_ns("注册 2000 名 (短+长混合)", 2000,
                 t.elapsed_nanoseconds() / 2000.0);
    }

    // 幂等重注册 (名字已存在, 运行时缓冲阻止折叠)
    {
        constexpr size_t OPS = 1000000;
        static char buf[16];
        std::memcpy(buf, "LongName_0", 10);
        double ns = best_ns(5, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_int_sink = type_id::register_type_def(
                    std::string_view(opaque<const char*>(buf), 10), d);
            }
            compiler_barrier();
            return g_int_sink;
        });
        print_ns("幂等重注册 (已存在)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: get_def_type_id (热路径) ===
static void test_get_hot()
{
    print_header("Section: get_def_type_id (热路径)");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;
    const type_def d = make_def(12, 4);

    // 3.1 短名命中 (8B, 运行时填充)
    {
        static char bufs[4][9];
        for (int j = 0; j < 4; ++j)
        {
            std::memset(bufs[j], 'a' + j, 8);
            bufs[j][8] = '\0';
        }
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        for (int j = 0; j < 4; ++j)
        {
            type_id::register_type_def(std::string_view(bufs[j], 8), d);
        }
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_int_sink = type_id::get_def_type_id(
                    std::string_view(opaque(arr[i & MASK]), 8));
            }
            compiler_barrier();
            return g_int_sink;
        });
        print_ns("短名命中 (8B, 4 名轮转)", OPS, ns / static_cast<double>(OPS));
    }

    // 3.2 长名命中 (24B)
    {
        static char bufs[4][25];
        for (int j = 0; j < 4; ++j)
        {
            std::memset(bufs[j], 'a' + j, 24);
            bufs[j][24] = '\0';
        }
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        for (int j = 0; j < 4; ++j)
        {
            type_id::register_type_def(std::string_view(bufs[j], 24), d);
        }
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_int_sink = type_id::get_def_type_id(
                    std::string_view(opaque(arr[i & MASK]), 24));
            }
            compiler_barrier();
            return g_int_sink;
        });
        print_ns("长名命中 (24B, 4 名轮转)", OPS, ns / static_cast<double>(OPS));
    }

    // 3.3 未注册名 (miss)
    {
        static char bufs[4][9];
        for (int j = 0; j < 4; ++j)
        {
            std::memset(bufs[j], 'z' - j, 8);  // 未注册名字
            bufs[j][8] = '\0';
        }
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_int_sink = type_id::get_def_type_id(
                    std::string_view(opaque(arr[i & MASK]), 8));
            }
            compiler_barrier();
            return g_int_sink;
        });
        print_ns("未注册名查询 (miss)", OPS, ns / static_cast<double>(OPS));
    }

    // 3.4 混合轮转 64 名 (长度 10, 模拟存档批量反查)
    {
        static char blob[64 * 16];
        for (int i = 0; i < 64; ++i)
        {
            char* p = blob + i * 16;
            const int written = std::snprintf(p, 16, "Mix%02dxxxxx", i);
            (void)written;
            type_id::register_type_def(std::string_view(p, 10), d);
        }
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                const size_t idx = (i * 31) & 63;
                g_int_sink = type_id::get_def_type_id(
                    std::string_view(opaque(blob + idx * 16), 10));
            }
            compiler_barrier();
            return g_int_sink;
        });
        print_ns("64 名混合轮转 (10B)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 4: 反查 (线性扫描) ===
static void test_reverse_lookup()
{
    print_header("Section: 反查 (线性扫描)");
    constexpr size_t OPS = 10000;
    const type_def d = make_def(4, 4);

    // 前面 section 已注册 ~2070 条, 再填充 2000 → ~4067 条 (接近容量)
    for (int i = 0; i < 2000; ++i)
    {
        type_id::register_type_def("Filler" + std::to_string(i), d);
    }
    const int target = type_id::register_type_def("RevTarget", d);

    {
        double ns = best_ns(5, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_def_sink = type_id::get_type_def(opaque(target));
            }
            compiler_barrier();
            return g_def_sink != nullptr;
        });
        print_ns("按 id 反查语义 (4067 条目)", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(5, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                g_size_sink = type_id::get_def_type_name(opaque(target)).size();
            }
            compiler_barrier();
            return g_size_sink;
        });
        print_ns("按 id 反查名字 (4067 条目)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  type_id 运行期名字类型注册 性能测试\n";
    std::cout << "============================================================\n";

    test_key_encode_vs_fnv1a();
    test_register_cold();
    test_get_hot();
    test_reverse_lookup();

    std::cout << "\n============================================================\n";
    std::cout << "  测试完成\n";
    std::cout << "============================================================\n";
    return 0;
}
