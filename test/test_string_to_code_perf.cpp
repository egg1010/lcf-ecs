// test_string_to_code_perf.cpp - string_to_code vs fnv1a 哈希性能对比
// 覆盖: 编码/比较/解码/端到端 四类操作的延迟对比
// 方法: opaque() 在循环内阻止常量折叠; 数组索引 (i & MASK) 阻止 CSE/LICM
//       (fnv1a 是纯函数, 输入不变时被 CSE 提到循环外, 数组访问强制每次重新计算)
#include "perf_common.hpp"
#include "include/part/string_to_code.hpp"
#include "include/part/fnv1a.hpp"
#include <string>
#include <string_view>

using namespace string_to_code;

// 全局 volatile sink, 阻止 DCE
static volatile size_t g_sink = 0;
static volatile uint64_t g_u64_sink = 0;
static volatile bool g_bool_sink = false;

// 数组掩码: 4 路, 阻止 CSE (编译器无法确定 arr[i & 3] 每次相同)
static constexpr size_t MASK = 3;

// 对比输出
inline void print_compare(const char* label, size_t n,
                          double sc_ns, double fnv_ns) noexcept
{
    double sc_tp = (sc_ns > 0 && n > 0) ? static_cast<double>(n) / sc_ns : 0;
    double fnv_tp = (fnv_ns > 0 && n > 0) ? static_cast<double>(n) / fnv_ns : 0;
    const char* verdict;
    if (sc_ns < fnv_ns * 0.95)       verdict = "[code WIN]";
    else if (fnv_ns < sc_ns * 0.95) verdict = "[fnv WIN]";
    else                           verdict = "[TIE]";

    std::cout << "  " << std::left << std::setw(36) << label
              << " | code: " << std::fixed << std::setprecision(3) << std::setw(9) << sc_ns << " ns"
              << " (" << std::setprecision(2) << std::setw(7) << sc_tp << " G/s)"
              << " | fnv1a: " << std::setw(9) << fnv_ns << " ns"
              << " (" << std::setw(7) << fnv_tp << " G/s)"
              << " " << verdict << "\n";
}

// === Section 1: 编码 (字符串 -> 数字) ===
// arr[i & MASK] 强制每次数组访问, 阻止 fnv1a 纯函数被 CSE 提到循环外
static void test_encode_compare(size_t n)
{
    print_header(("Section 1: encode (N=" + std::to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    // 短串: 4 字节 (len 用常量, data 用 opaque + 运行时填充, 阻止 CSE 和变长 memcpy)
    {
        static char bufs[4][5];
        for (int j = 0; j < 4; ++j) std::memset(bufs[j], 'a' + j, 4);
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        constexpr size_t LEN = 4;
        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_u64_sink = code_value::encode_inline(opaque(arr[i & MASK]), LEN);
            }
            compiler_barrier();
            return g_u64_sink;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_u64_sink = fnv1a_runtime(opaque(arr[i & MASK]), LEN);
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_compare("encode short(4B)", n, sc_ns, fnv_ns);
    }

    // 短串: 8 字节 (内联边界, len 常量, 运行时填充)
    {
        static char bufs[4][9];
        for (int j = 0; j < 4; ++j) std::memset(bufs[j], 'a' + j, 8);
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        constexpr size_t LEN = 8;
        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                code_value v{std::string_view(opaque(arr[i & MASK]), LEN)};
                g_u64_sink = v.inline_value();
            }
            compiler_barrier();
            return g_u64_sink;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_u64_sink = fnv1a_runtime(opaque(arr[i & MASK]), LEN);
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_compare("encode short(8B)", n, sc_ns, fnv_ns);
    }

    // 长串: 16 字节
    {
        const char* arr[4] = {"0123456789abcdef", "fedcba9876543210",
                              "abcdefghijklmnop", "ponmlkjihgfedcba"};
        size_t lens[4] = {16, 16, 16, 16};
        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                code_value v{std::string_view(opaque(arr[i & MASK]), opaque(lens[i & MASK]))};
                g_sink = v.string_size();
            }
            compiler_barrier();
            return g_sink;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_u64_sink = fnv1a_runtime(opaque(arr[i & MASK]), opaque(lens[i & MASK]));
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_compare("encode long(16B)", n, sc_ns, fnv_ns);
    }

    // 长串: 64 字节
    {
        static char bufs[4][65];
        for (int j = 0; j < 4; ++j) std::memset(bufs[j], 'a' + j, 64);
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        size_t lens[4] = {64, 64, 64, 64};
        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                code_value v{std::string_view(opaque(arr[i & MASK]), opaque(lens[i & MASK]))};
                g_sink = v.string_size();
            }
            compiler_barrier();
            return g_sink;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_u64_sink = fnv1a_runtime(opaque(arr[i & MASK]), opaque(lens[i & MASK]));
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_compare("encode long(64B)", n, sc_ns, fnv_ns);
    }

    // 长串: 256 字节
    {
        static char bufs[4][257];
        for (int j = 0; j < 4; ++j) std::memset(bufs[j], 'a' + j, 256);
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        size_t lens[4] = {256, 256, 256, 256};
        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                code_value v{std::string_view(opaque(arr[i & MASK]), opaque(lens[i & MASK]))};
                g_sink = v.string_size();
            }
            compiler_barrier();
            return g_sink;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_u64_sink = fnv1a_runtime(opaque(arr[i & MASK]), opaque(lens[i & MASK]));
            }
            compiler_barrier();
            return g_u64_sink;
        });
        print_compare("encode long(256B)", n, sc_ns, fnv_ns);
    }

    print_footer();
}

// === Section 2: 比较 (两个数字码/哈希是否相等) ===
// v1/v2 循环外构造, equals 是纯读操作, 用 volatile bool sink 阻止 DCE
static void test_equals_compare(size_t n)
{
    print_header(("Section 2: equals (N=" + std::to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    // 短串比较
    {
        code_value v1{"player"};
        code_value v2{"player"};
        uint64_t h1 = fnv1a_runtime("player", 6);
        uint64_t h2 = fnv1a_runtime("player", 6);

        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = v1.equals(v2);
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = (opaque(h1) == opaque(h2));
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        print_compare("equals short(6B)", n, sc_ns, fnv_ns);
    }

    // 长串比较 (相同)
    {
        code_value v1{"this_is_a_long_string_for_test"};
        code_value v2{"this_is_a_long_string_for_test"};
        uint64_t h1 = fnv1a_runtime("this_is_a_long_string_for_test", 29);
        uint64_t h2 = fnv1a_runtime("this_is_a_long_string_for_test", 29);

        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = v1.equals(v2);
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = (opaque(h1) == opaque(h2));
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        print_compare("equals long(29B, same)", n, sc_ns, fnv_ns);
    }

    // 长串比较 (不同, 第1字节就不同)
    {
        code_value v1{"this_is_a_long_string_for_test"};
        code_value v2{"xhis_is_a_long_string_for_test"};
        uint64_t h1 = fnv1a_runtime("this_is_a_long_string_for_test", 29);
        uint64_t h2 = fnv1a_runtime("xhis_is_a_long_string_for_test", 29);

        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = v1.equals(v2);
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = (opaque(h1) == opaque(h2));
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        print_compare("equals long(29B, diff)", n, sc_ns, fnv_ns);
    }

    print_footer();
}

// === Section 3: 解码 (数字 -> 字符串) ===
// string_to_code 可逆, fnv1a 不可逆
static void test_decode_compare(size_t n)
{
    print_header(("Section 3: decode (N=" + std::to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    // 短串解码
    {
        code_value v{"player"};
        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                std::string_view sv = v.decode();
                g_sink = sv.size();
            }
            compiler_barrier();
            return g_sink;
        });
        print_ns("decode short(6B) [code only]", n, sc_ns);
        std::cout << "  " << std::left << std::setw(36)
                  << " | fnv1a: 不可逆, 无法解码\n";
    }

    // 长串解码
    {
        code_value v{"this_is_a_long_string_for_test"};
        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                std::string_view sv = v.decode();
                g_sink = sv.size();
            }
            compiler_barrier();
            return g_sink;
        });
        print_ns("decode long(29B) [code only]", n, sc_ns);
        std::cout << "  " << std::left << std::setw(36)
                  << " | fnv1a: 不可逆, 无法解码\n";
    }

    print_footer();
}

// === Section 4: 端到端 (编码+查找) 模拟 map 场景 ===
// arr[i & MASK] 强制每次数组访问, 阻止 fnv1a 纯函数被 CSE
static void test_end_to_end_compare(size_t n)
{
    print_header(("Section 4: end-to-end (encode+compare, N=" + std::to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    // 短串端到端: 编码 + 比较 (len 常量, data opaque, base 按值捕获到寄存器)
    {
        static char bufs[4][7];
        for (int j = 0; j < 4; ++j) std::memset(bufs[j], 'a' + j, 6);
        const char* arr[4] = {bufs[0], bufs[1], bufs[2], bufs[3]};
        constexpr size_t LEN = 6;
        // base 用第一个, 预提取到局部变量
        code_value v_base{std::string_view(arr[0], LEN)};
        const uint64_t base_inline = v_base.inline_value();
        const uint64_t h_base = fnv1a_runtime(arr[0], LEN);

        double sc_ns = best_ns(REPEAT, [=, &arr]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = (base_inline == code_value::encode_inline(opaque(arr[i & MASK]), LEN));
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        double fnv_ns = best_ns(REPEAT, [=, &arr]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = (h_base == fnv1a_runtime(opaque(arr[i & MASK]), LEN));
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        print_compare("e2e short(6B)", n, sc_ns, fnv_ns);
    }

    // 长串端到端: 编码 + 比较 (用 encode_equals 避免构造中间对象)
    {
        const char* base_s = "this_is_a_long_string_for_test";
        size_t base_len = 29;
        // 4 个相同长度的长串 (内容不同)
        std::string variants[4];
        for (int j = 0; j < 4; ++j)
        {
            variants[j] = base_s;
            if (j > 0) variants[j][0] = 'a' + j;  // 改首字节
        }
        const char* arr[4] = {variants[0].c_str(), variants[1].c_str(),
                              variants[2].c_str(), variants[3].c_str()};
        size_t lens[4] = {base_len, base_len, base_len, base_len};
        code_value v_base{std::string_view(base_s, base_len)};
        uint64_t h_base = fnv1a_runtime(base_s, base_len);

        double sc_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                g_bool_sink = v_base.encode_equals(opaque(arr[i & MASK]), opaque(lens[i & MASK]));
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        double fnv_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                uint64_t h = fnv1a_runtime(opaque(arr[i & MASK]), opaque(lens[i & MASK]));
                g_bool_sink = (h_base == h);
            }
            compiler_barrier();
            return g_bool_sink ? 1 : 0;
        });
        print_compare("e2e long(29B)", n, sc_ns, fnv_ns);
    }

    print_footer();
}

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  string_to_code vs fnv1a 性能对比\n";
    std::cout << "  (数组索引 i&MASK 阻止 CSE, opaque 阻止常量折叠)\n";
    std::cout << "============================================================\n";

    const size_t N = 1000000;  // 100 万次

    test_encode_compare(N);
    test_equals_compare(N);
    test_decode_compare(N);
    test_end_to_end_compare(N);

    std::cout << "\n============================================================\n";
    std::cout << "  对比完成\n";
    std::cout << "============================================================\n";
    return 0;
}
