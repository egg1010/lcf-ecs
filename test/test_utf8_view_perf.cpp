// test_utf8_view_perf.cpp - utf8_view 模块性能测试 (非拥有视图, 字节级 O(1) / 码点级 O(n))
#include "test_common.hpp"
#include "include/part/utf8_view.hpp"
#include "include/part/utf8pp.hpp"

int main()
{
    // === 1. 构造性能 (O(1), 仅指针赋值) ===
    print_perf_sub("1. 构造 (O(1))");
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

    // === 2. byte_size (O(1)) ===
    print_perf_sub("2. byte_size (O(1))");
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

    // === 3. size (码点数, O(n)) ===
    print_perf_sub("3. size (码点数, O(n))");
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

    // === 4. at 随机访问 (O(n)) ===
    print_perf_sub("4. at 随机访问 (O(n))");
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

    // === 5. 迭代器遍历 ===
    print_perf_sub("5. 迭代器遍历");
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

    // === 6. 反向迭代器遍历 ===
    print_perf_sub("6. 反向迭代器遍历");
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

    // === 7. find_byte (O(n), memchr) ===
    print_perf_sub("7. find_byte (memchr)");
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

    // === 8. find_bytes (字节子串) ===
    print_perf_sub("8. find_bytes");
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

    // === 9. find (码点查找) ===
    print_perf_sub("9. find (码点查找)");
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

    // === 10. find (码点子串查找) ===
    print_perf_sub("10. find (码点子串)");
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

    // === 11. starts_with / ends_with ===
    print_perf_sub("11. starts_with / ends_with");
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

    // === 12. contains (码点级) ===
    print_perf_sub("12. contains");
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

    // === 13. compare ===
    print_perf_sub("13. compare");
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

    // === 14. operator<=> ===
    print_perf_sub("14. operator<=>");
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

    // === 15. substr (字节级 O(1)) ===
    print_perf_sub("15. substr_bytes (O(1))");
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

    // === 16. substr (码点级 O(n)) ===
    print_perf_sub("16. substr (码点级 O(n))");
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

    // === 17. copy (字节级 memcpy) ===
    print_perf_sub("17. copy");
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

    // === 18. std::hash ===
    print_perf_sub("18. std::hash");
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

    // === 19. 对比: utf8_view vs std::string_view (字节查找) ===
    print_perf_sub("19. 对比 utf8_view vs string_view");
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

    // === 20. 对比: utf8_view::size vs utf8pp::size (码点计数) ===
    print_perf_sub("20. 对比 utf8_view::size vs utf8pp::size");
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
