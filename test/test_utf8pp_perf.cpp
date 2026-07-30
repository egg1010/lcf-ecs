// test_utf8pp_perf.cpp - utf8pp 模块性能测试 (编解码函数 + utf8pp 类)
#include "test_common.hpp"
#include "include/part/utf8pp.hpp"
#include "include/part/dense.hpp"

int main()
{
    // === 1. 单值转换性能 ===
    print_perf_sub("1. 单值转换 to_char/to_int");
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

    // === 2. UTF-8 解码性能: ASCII ===
    print_perf_sub("2. UTF-8 解码 ASCII");
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

    // === 3. UTF-8 解码性能: 中文 ===
    print_perf_sub("3. UTF-8 解码 中文");
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

    // === 4. codepoints_to_char32 性能 ===
    print_perf_sub("4. codepoints_to_char32");
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

    // === 5. char32_to_utf8 性能 ===
    print_perf_sub("5. char32_to_utf8");
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

    // === 6. utf8pp 构造性能 (含偏移缓存构建) ===
    print_perf_sub("6. utf8pp 构造 (含偏移缓存)");
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

    // === 7. utf8pp 随机访问性能 (O(1) via 偏移缓存) ===
    print_perf_sub("7. utf8pp at() O(1) 随机访问");
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

    // === 8. utf8pp 迭代器性能 ===
    print_perf_sub("8. utf8pp 迭代器遍历");
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

    // === 9. utf8pp push_back 性能 ===
    print_perf_sub("9. utf8pp push_back");
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

    // === 10. utf8pp insert/erase 性能 ===
    print_perf_sub("10. utf8pp insert/erase 首位");
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

    // === 11. starts_with / ends_with ===
    print_perf_sub("11. starts_with / ends_with");
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

    // === 12. contains ===
    print_perf_sub("12. contains");
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

    // === 13. count ===
    print_perf_sub("13. count");
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

    // === 14. replace_all ===
    print_perf_sub("14. replace_all");
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

    // === 15. trim ===
    print_perf_sub("15. trim");
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

    // === 16. to_lower / to_upper ===
    print_perf_sub("16. to_lower / to_upper");
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

    // === 17. reverse ===
    print_perf_sub("17. reverse");
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

    // === 18. split / join ===
    print_perf_sub("18. split / join");
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

    // === 19. resize_cp / pop_back / append_cp ===
    print_perf_sub("19. resize_cp / pop_back / append_cp");
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

    // === 20. copy ===
    print_perf_sub("20. copy");
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

    // === 21. find_first_of / find_last_of ===
    print_perf_sub("21. find_first_of / find_last_of");
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

    // === 22. find_first_not_of / find_last_not_of ===
    print_perf_sub("22. find_first_not_of / find_last_not_of");
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

    // === 23. rfind ===
    print_perf_sub("23. rfind");
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

    // === 24. reverse_iterator ===
    print_perf_sub("24. reverse_iterator");
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

    // === 25. operator+ ===
    print_perf_sub("25. operator+");
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

    // === 26. operator<=> ===
    print_perf_sub("26. operator<=>");
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

    // === 27. 迭代器版 insert/erase ===
    print_perf_sub("27. 迭代器版 insert/erase");
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

    // === 28. std::hash ===
    print_perf_sub("28. std::hash");
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

    // === 29. std::swap 非成员 ===
    print_perf_sub("29. std::swap 非成员");
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

    print_summary("性能测试");
    return 0;
}
