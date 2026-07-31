// test_utf8_perf.cpp - utf8 性能测试 (编解码函数 + utf8pp + utf8_view)
#include "test_common.hpp"
#include "include/part/utf8pp.hpp"
#include "include/part/utf8_view.hpp"
#include "include/part/dense.hpp"

int main()
{
    // ================================================================
    //  模块 1: 编解码函数
    // ================================================================
    print_section(1, "编解码函数");

    // === 1.1 单值转换 to_char/to_int ===
    print_perf_sub("1.1 单值转换 to_char/to_int");
    {
        uint32_t cp = 0x4E2D;
        size_t N = 100000000;

        timer t;
        char32_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink = to_char(cp);
            sink = to_int(sink);
        }
        double ms = t.elapsed_ms();
        print_perf("to_char/to_int 往返", N, ms);
        (void)sink;
    }

    // === 1.2 UTF-8 解码 ASCII ===
    print_perf_sub("1.2 UTF-8 解码 ASCII");
    {
        std::string s(1024, 'A');
        uint32_t out[1024];
        size_t N = 100000;

        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            total += utf8_to_codepoints(s.data(), s.size(), out, 1024);
        }
        double ms = t.elapsed_ms();
        print_perf("UTF-8 解码 1KB ASCII", N, ms);
        (void)total;
    }

    // === 1.3 UTF-8 解码 中文 ===
    print_perf_sub("1.3 UTF-8 解码 中文");
    {
        std::string s;
        for (int i = 0; i < 256; ++i) { s += (char)0xE4; s += (char)0xBD; s += (char)0xA0; }
        uint32_t out[1024];
        size_t N = 100000;

        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            total += utf8_to_codepoints(s.data(), s.size(), out, 1024);
        }
        double ms = t.elapsed_ms();
        print_perf("UTF-8 解码 256 中文", N, ms);
        (void)total;
    }

    // === 1.4 codepoints_to_char32 ===
    print_perf_sub("1.4 codepoints_to_char32");
    {
        std::vector<uint32_t> cps(1024, 0x4E2D);
        char32_t out[1024];
        size_t N = 1000000;

        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            total += codepoints_to_char32(cps.data(), cps.size(), out, 1024);
        }
        double ms = t.elapsed_ms();
        print_perf("codepoints_to_char32 1K 码点", N, ms);
        (void)total;
    }

    // === 1.5 char32_to_utf8 ===
    print_perf_sub("1.5 char32_to_utf8");
    {
        std::vector<char32_t> src(1024, char32_t(0x4E2D));
        char out[4096];
        size_t N = 1000000;

        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            total += char32_to_utf8(src.data(), src.size(), out, 4096);
        }
        double ms = t.elapsed_ms();
        print_perf("char32_to_utf8 1K 中文", N, ms);
        (void)total;
    }

    // ================================================================
    //  模块 2: utf8pp 类
    // ================================================================
    print_section(2, "utf8pp 类");

    // === 2.1 构造 (含偏移缓存构建) ===
    print_perf_sub("2.1 构造 (含偏移缓存)");
    {
        std::string s;
        for (int i = 0; i < 256; ++i) { s += "你好"; }
        size_t N = 1000000;

        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            utf8pp u(s.data(), s.size());
            total += u.size();
        }
        double ms = t.elapsed_ms();
        print_perf("utf8pp 构造 512 中文", N, ms);
        (void)total;
    }

    // === 2.2 at() O(1) 随机访问 ===
    print_perf_sub("2.2 at() O(1) 随机访问");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8pp u(s.data(), s.size());
        size_t N = 10000000;

        timer t;
        char32_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink = u.at(i % u.size());
        }
        double ms = t.elapsed_ms();
        print_perf("utf8pp at() 随机访问", N, ms);
        (void)sink;
    }

    // === 2.3 迭代器遍历 ===
    print_perf_sub("2.3 迭代器遍历");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8pp u(s.data(), s.size());
        size_t N = 100000;

        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            for (char32_t cp : u) total += static_cast<size_t>(cp);
        }
        double ms = t.elapsed_ms();
        print_perf("utf8pp 迭代器遍历 2048 中文", N, ms);
        (void)total;
    }

    // === 2.4 push_back ===
    print_perf_sub("2.4 push_back");
    {
        size_t N = 1000000;
        timer t;
        utf8pp u;
        for (size_t i = 0; i < N; ++i)
        {
            u.push_back(char32_t(0x4E2D));
        }
        double ms = t.elapsed_ms();
        print_perf("utf8pp push_back 1M 中文", N, ms);
    }

    // === 2.5 insert/erase 首位 ===
    print_perf_sub("2.5 insert/erase 首位");
    {
        std::string s;
        for (int i = 0; i < 1000; ++i) s += "你";
        utf8pp u(s.data(), s.size());
        size_t N = 100000;

        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            u.insert(0, char32_t('X'));
            u.erase(0);
        }
        double ms = t.elapsed_ms();
        print_perf("utf8pp insert+erase 首位", N, ms);
    }

    // === 2.6 starts_with / ends_with ===
    print_perf_sub("2.6 starts_with / ends_with");
    {
        utf8pp s("HelloWorld");
        utf8pp prefix("Hello");
        utf8pp suffix("World");
        size_t N = 10000000;

        timer t;
        bool sink = false;
        for (size_t i = 0; i < N; ++i)
        {
            sink = s.starts_with(prefix);
            sink = s.ends_with(suffix);
        }
        double ms = t.elapsed_ms();
        print_perf("starts_with+ends_with", N, ms);
        (void)sink;
    }

    // === 2.7 contains ===
    print_perf_sub("2.7 contains");
    {
        utf8pp s("HelloWorldHelloWorld");
        utf8pp sub("World");
        size_t N = 10000000;

        timer t;
        bool sink = false;
        for (size_t i = 0; i < N; ++i)
        {
            sink = s.contains(sub);
        }
        double ms = t.elapsed_ms();
        print_perf("contains(utf8pp)", N, ms);
        (void)sink;
    }

    // === 2.8 count ===
    print_perf_sub("2.8 count");
    {
        utf8pp s("abcabcabcabc");
        utf8pp sub("abc");
        size_t N = 1000000;

        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink = s.count(sub);
        }
        double ms = t.elapsed_ms();
        print_perf("count(utf8pp)", N, ms);
        (void)sink;
    }

    // === 2.9 replace_all ===
    print_perf_sub("2.9 replace_all");
    {
        size_t N = 100000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            utf8pp s("abc abc abc abc");
            s.replace_all(utf8pp("abc"), utf8pp("XY"));
        }
        double ms = t.elapsed_ms();
        print_perf("replace_all(4次替换)", N, ms);
    }

    // === 2.10 trim ===
    print_perf_sub("2.10 trim");
    {
        utf8pp tmpl("   HelloWorld   ");
        size_t N = 1000000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            utf8pp s(tmpl);
            s.trim();
        }
        double ms = t.elapsed_ms();
        print_perf("trim (含拷贝)", N, ms);
    }

    // === 2.11 to_lower / to_upper ===
    print_perf_sub("2.11 to_lower / to_upper");
    {
        utf8pp tmpl("HelloWorldHELLOWORLD");
        size_t N = 1000000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            utf8pp s(tmpl);
            s.to_lower();
        }
        double ms = t.elapsed_ms();
        print_perf("to_lower (20字符)", N, ms);
    }

    // === 2.12 reverse ===
    print_perf_sub("2.12 reverse");
    {
        utf8pp tmpl("HelloWorldHelloWorld");
        size_t N = 1000000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            utf8pp s(tmpl);
            s.reverse();
        }
        double ms = t.elapsed_ms();
        print_perf("reverse (20字符)", N, ms);
    }

    // === 2.13 split / join ===
    print_perf_sub("2.13 split / join");
    {
        utf8pp tmpl("a,b,c,d,e,f,g,h,i,j");
        utf8pp delim(",");
        size_t N = 1000000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            dense<utf8pp> parts = tmpl.split(delim);
            utf8pp r = utf8pp::join(parts, delim);
            (void)r;
        }
        double ms = t.elapsed_ms();
        print_perf("split+join (10段)", N, ms);
    }

    // === 2.14 resize_cp / pop_back / append_cp ===
    print_perf_sub("2.14 resize_cp / pop_back / append_cp");
    {
        size_t N = 1000000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            utf8pp s;
            s.append_cp(100, char32_t('A'));
            s.resize_cp(50);
            s.resize_cp(100, char32_t('B'));
            while (!s.empty()) s.pop_back();
        }
        double ms = t.elapsed_ms();
        print_perf("append_cp+resize_cp+pop_back", N, ms);
    }

    // === 2.15 copy ===
    print_perf_sub("2.15 copy");
    {
        utf8pp s("HelloWorldHelloWorldHelloWorld");
        char buf[64];
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += s.copy(buf, 10, i % 10);
        }
        double ms = t.elapsed_ms();
        print_perf("copy(buf,10,pos)", N, ms);
        (void)sink;
    }

    // === 2.16 find_first_of / find_last_of ===
    print_perf_sub("2.16 find_first_of / find_last_of");
    {
        utf8pp s("HelloWorldHelloWorld");
        utf8pp chars("xyzow");
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += s.find_first_of(chars);
            sink += s.find_last_of(chars);
        }
        double ms = t.elapsed_ms();
        print_perf("find_first_of+find_last_of", N, ms);
        (void)sink;
    }

    // === 2.17 find_first_not_of / find_last_not_of ===
    print_perf_sub("2.17 find_first_not_of / find_last_not_of");
    {
        utf8pp s("HelloWorldHelloWorld");
        utf8pp chars("HeloWrd");
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += s.find_first_not_of(chars);
            sink += s.find_last_not_of(chars);
        }
        double ms = t.elapsed_ms();
        print_perf("find_first_not_of+find_last_not_of", N, ms);
        (void)sink;
    }

    // === 2.18 rfind ===
    print_perf_sub("2.18 rfind");
    {
        utf8pp s("HelloWorldHelloWorld");
        utf8pp pat("World");
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += s.rfind(pat);
        }
        double ms = t.elapsed_ms();
        print_perf("rfind(utf8pp)", N, ms);
        (void)sink;
    }

    // === 2.19 reverse_iterator ===
    print_perf_sub("2.19 reverse_iterator");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8pp u(s.data(), s.size());
        size_t N = 100000;
        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            for (auto it = u.rbegin(); it != u.rend(); ++it) total += static_cast<size_t>(*it);
        }
        double ms = t.elapsed_ms();
        print_perf("reverse_iterator 遍历 2048 中文", N, ms);
        (void)total;
    }

    // === 2.20 operator+ ===
    print_perf_sub("2.20 operator+");
    {
        utf8pp a("Hello");
        utf8pp b("World");
        size_t N = 1000000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            utf8pp r = a + b;
            (void)r;
        }
        double ms = t.elapsed_ms();
        print_perf("operator+ (utf8pp+utf8pp)", N, ms);
    }

    // === 2.21 operator<=> ===
    print_perf_sub("2.21 operator<=>");
    {
        utf8pp a("HelloWorldHelloWorld");
        utf8pp b("HelloWorldHelloWorlX");
        size_t N = 10000000;
        timer t;
        bool sink = false;
        for (size_t i = 0; i < N; ++i)
        {
            sink = (a <=> b) == std::strong_ordering::less;
        }
        double ms = t.elapsed_ms();
        print_perf("operator<=>", N, ms);
        (void)sink;
    }

    // === 2.22 迭代器版 insert/erase ===
    print_perf_sub("2.22 迭代器版 insert/erase");
    {
        size_t N = 100000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            utf8pp s("acd");
            auto it = s.begin();
            ++it;
            s.insert(it, char32_t('b'));
            auto it2 = s.begin();
            ++it2;
            s.erase(it2);
        }
        double ms = t.elapsed_ms();
        print_perf("insert(iter)+erase(iter)", N, ms);
    }

    // === 2.23 std::hash ===
    print_perf_sub("2.23 std::hash");
    {
        std::hash<utf8pp> hasher;
        utf8pp s("HelloWorldHelloWorld");
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += hasher(s);
        }
        double ms = t.elapsed_ms();
        print_perf("std::hash<utf8pp>", N, ms);
        (void)sink;
    }

    // === 2.24 std::swap 非成员 ===
    print_perf_sub("2.24 std::swap 非成员");
    {
        utf8pp a("HelloWorldHelloWorld");
        utf8pp b("WorldHelloWorldHello");
        size_t N = 10000000;
        timer t;
        for (size_t i = 0; i < N; ++i)
        {
            swap(a, b);
        }
        double ms = t.elapsed_ms();
        print_perf("swap(utf8pp,utf8pp)", N, ms);
    }

    // ================================================================
    //  模块 3: utf8_view 类
    // ================================================================
    print_section(3, "utf8_view 类");

    // === 3.1 构造 (O(1), 仅指针赋值) ===
    print_perf_sub("3.1 构造 (O(1))");
    {
        const char* s = "HelloWorldHelloWorldHelloWorld";
        size_t N = 100000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            utf8_view v(s);
            sink += v.byte_size();
        }
        double ms = t.elapsed_ms();
        print_perf("构造 (const char*)", N, ms);
        lcf_sink(sink);
    }

    // === 3.2 byte_size (O(1)) ===
    print_perf_sub("3.2 byte_size (O(1))");
    {
        utf8_view v("HelloWorldHelloWorld");
        size_t N = 100000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += v.byte_size();
        }
        double ms = t.elapsed_ms();
        print_perf("byte_size()", N, ms);
        lcf_sink(sink);
    }

    // === 3.3 size (码点数, O(n)) ===
    print_perf_sub("3.3 size (码点数, O(n))");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8_view v(s.data(), s.size());
        size_t N = 1000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += v.size();
        }
        double ms = t.elapsed_ms();
        print_perf("size() 2048 中文", N, ms);
        lcf_sink(sink);
    }

    // === 3.4 at 随机访问 (O(n)) ===
    print_perf_sub("3.4 at 随机访问 (O(n))");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8_view v(s.data(), s.size());
        size_t N = 1000000;
        timer t;
        char32_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink = v.at(i % v.size());
        }
        double ms = t.elapsed_ms();
        print_perf("at() 随机访问", N, ms);
        lcf_sink(sink);
    }

    // === 3.5 迭代器遍历 ===
    print_perf_sub("3.5 迭代器遍历");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8_view v(s.data(), s.size());
        size_t N = 100000;
        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            for (char32_t cp : v) total += static_cast<size_t>(cp);
        }
        double ms = t.elapsed_ms();
        print_perf("迭代器遍历 2048 中文", N, ms);
        lcf_sink(total);
    }

    // === 3.6 反向迭代器遍历 ===
    print_perf_sub("3.6 反向迭代器遍历");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8_view v(s.data(), s.size());
        size_t N = 100000;
        timer t;
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            for (auto it = v.rbegin(); it != v.rend(); ++it) total += static_cast<size_t>(*it);
        }
        double ms = t.elapsed_ms();
        print_perf("反向迭代器 2048 中文", N, ms);
        lcf_sink(total);
    }

    // === 3.7 find_byte (O(n), memchr) ===
    print_perf_sub("3.7 find_byte (memchr)");
    {
        std::string s(1024, 'A');
        s[512] = 'Z';
        utf8_view v(s.data(), s.size());
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += v.find_byte('Z');
        }
        double ms = t.elapsed_ms();
        print_perf("find_byte (1KB)", N, ms);
        lcf_sink(sink);
    }

    // === 3.8 find_bytes (字节子串) ===
    print_perf_sub("3.8 find_bytes");
    {
        std::string s = "Hello";
        for (int i = 0; i < 200; ++i) s += "Hello";
        s[1000] = 'W'; s[1001] = 'o'; s[1002] = 'r'; s[1003] = 'l'; s[1004] = 'd';
        utf8_view v(s.data(), s.size());
        size_t N = 1000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += v.find_bytes("World");
        }
        double ms = t.elapsed_ms();
        print_perf("find_bytes (1KB)", N, ms);
        lcf_sink(sink);
    }

    // === 3.9 find (码点查找) ===
    print_perf_sub("3.9 find (码点查找)");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你"; }
        s += "好";
        utf8_view v(s.data(), s.size());
        size_t N = 1000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += v.find(char32_t(0x597D));
        }
        double ms = t.elapsed_ms();
        print_perf("find(好) 1025 中文", N, ms);
        lcf_sink(sink);
    }

    // === 3.10 find (码点子串查找) ===
    print_perf_sub("3.10 find (码点子串)");
    {
        std::string s;
        for (int i = 0; i < 512; ++i) { s += "你好"; }
        utf8_view v(s.data(), s.size());
        utf8_view pat(u8"你好");
        size_t N = 1000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += v.find(pat);
        }
        double ms = t.elapsed_ms();
        print_perf("find(你好) 1024 中文", N, ms);
        lcf_sink(sink);
    }

    // === 3.11 starts_with / ends_with ===
    print_perf_sub("3.11 starts_with / ends_with");
    {
        utf8_view v("HelloWorldHelloWorld");
        utf8_view prefix("Hello");
        utf8_view suffix("World");
        size_t N = 10000000;
        timer t;
        bool sink = false;
        for (size_t i = 0; i < N; ++i)
        {
            sink = v.starts_with(prefix);
            sink = v.ends_with(suffix);
        }
        double ms = t.elapsed_ms();
        print_perf("starts_with+ends_with", N, ms);
        lcf_sink(sink);
    }

    // === 3.12 contains (码点级) ===
    print_perf_sub("3.12 contains");
    {
        utf8_view v("HelloWorldHelloWorld");
        utf8_view sub("World");
        size_t N = 10000000;
        timer t;
        bool sink = false;
        for (size_t i = 0; i < N; ++i)
        {
            sink = v.contains(sub);
        }
        double ms = t.elapsed_ms();
        print_perf("contains(utf8_view)", N, ms);
        lcf_sink(sink);
    }

    // === 3.13 compare ===
    print_perf_sub("3.13 compare");
    {
        utf8_view a("HelloWorldHelloWorld");
        utf8_view b("HelloWorldHelloWorlX");
        size_t N = 10000000;
        timer t;
        bool sink = false;
        for (size_t i = 0; i < N; ++i)
        {
            sink = a < b;
        }
        double ms = t.elapsed_ms();
        print_perf("compare (20字节)", N, ms);
        lcf_sink(sink);
    }

    // === 3.14 operator<=> ===
    print_perf_sub("3.14 operator<=>");
    {
        utf8_view a("HelloWorldHelloWorld");
        utf8_view b("HelloWorldHelloWorlX");
        size_t N = 10000000;
        timer t;
        bool sink = false;
        for (size_t i = 0; i < N; ++i)
        {
            sink = (a <=> b) == std::strong_ordering::less;
        }
        double ms = t.elapsed_ms();
        print_perf("operator<=>", N, ms);
        lcf_sink(sink);
    }

    // === 3.15 substr_bytes (字节级 O(1)) ===
    print_perf_sub("3.15 substr_bytes (O(1))");
    {
        utf8_view v("HelloWorldHelloWorld");
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            utf8_view s = v.substr_bytes(5, 10);
            sink += s.byte_size();
        }
        double ms = t.elapsed_ms();
        print_perf("substr_bytes (O(1))", N, ms);
        lcf_sink(sink);
    }

    // === 3.16 substr (码点级 O(n)) ===
    print_perf_sub("3.16 substr (码点级 O(n))");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8_view v(s.data(), s.size());
        size_t N = 1000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            utf8_view sub = v.substr(512, 256);
            sink += sub.byte_size();
        }
        double ms = t.elapsed_ms();
        print_perf("substr(512,256) 2048 中文", N, ms);
        lcf_sink(sink);
    }

    // === 3.17 copy (字节级 memcpy) ===
    print_perf_sub("3.17 copy");
    {
        utf8_view v("HelloWorldHelloWorldHelloWorld");
        char buf[64];
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += v.copy(buf, 20);
        }
        double ms = t.elapsed_ms();
        print_perf("copy(buf,20)", N, ms);
        lcf_sink(sink);
    }

    // === 3.18 std::hash ===
    print_perf_sub("3.18 std::hash");
    {
        std::hash<utf8_view> hasher;
        utf8_view v("HelloWorldHelloWorld");
        size_t N = 10000000;
        timer t;
        size_t sink = 0;
        for (size_t i = 0; i < N; ++i)
        {
            sink += hasher(v);
        }
        double ms = t.elapsed_ms();
        print_perf("std::hash<utf8_view>", N, ms);
        lcf_sink(sink);
    }

    // === 3.19 对比: utf8_view vs std::string_view (字节查找) ===
    print_perf_sub("3.19 对比 utf8_view vs string_view");
    {
        std::string s(1024, 'A');
        s[512] = 'Z';
        utf8_view uv(s.data(), s.size());
        std::string_view sv(s.data(), s.size());
        size_t N = 10000000;

        timer t1;
        size_t sink1 = 0;
        for (size_t i = 0; i < N; ++i) sink1 += uv.find_byte('Z');
        double ms1 = t1.elapsed_ms();
        print_perf("utf8_view::find_byte", N, ms1);
        lcf_sink(sink1);

        timer t2;
        size_t sink2 = 0;
        for (size_t i = 0; i < N; ++i) sink2 += sv.find('Z');
        double ms2 = t2.elapsed_ms();
        print_perf("string_view::find", N, ms2);
        lcf_sink(sink2);
    }

    // === 3.20 对比: utf8_view::size vs utf8pp::size (码点计数) ===
    print_perf_sub("3.20 对比 utf8_view::size vs utf8pp::size");
    {
        std::string s;
        for (int i = 0; i < 1024; ++i) { s += "你好"; }
        utf8_view uv(s.data(), s.size());
        utf8pp up(s.data(), s.size());
        size_t N = 1000000;

        timer t1;
        size_t sink1 = 0;
        for (size_t i = 0; i < N; ++i) sink1 += uv.size();
        double ms1 = t1.elapsed_ms();
        print_perf("utf8_view::size (O(n))", N, ms1);
        lcf_sink(sink1);

        timer t2;
        size_t sink2 = 0;
        for (size_t i = 0; i < N; ++i) sink2 += up.size();
        double ms2 = t2.elapsed_ms();
        print_perf("utf8pp::size (O(1) 缓存)", N, ms2);
        lcf_sink(sink2);
    }

    print_summary("性能测试");
    return 0;
}
