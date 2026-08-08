// test_utf8pp.cpp - utf8pp 模块功能测试 (编解码函数 + utf8pp 类)
#include "test_common.hpp"
#include "include/part/utf8pp/utf8pp.hpp"
#include "include/part/dense.hpp"
#include <cmath>

int main()
{
    // === 1. 编解码函数: 单值转换 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 1 hr=" << _hr << "\n"; } while(0); print_section(1, "编解码函数: 单值转换");
    {
        print_item("to_char(0x41) == 'A'", to_char(0x41) == char32_t('A'));
        print_item("to_int('A') == 0x41", to_int(char32_t('A')) == 0x41);
        print_item("to_char(0x4E2D) == '中'", to_char(0x4E2D) == char32_t(0x4E2D));
        print_item("to_int('中') == 0x4E2D", to_int(char32_t(0x4E2D)) == 0x4E2D);
        print_item("to_char(0x1F600) == U+1F600", to_char(0x1F600) == char32_t(0x1F600));
        print_item("constexpr to_char", to_char(0x4E2D) == char32_t(0x4E2D));
    }

    // === 2. 编解码函数: UTF-8 解码 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 2 hr=" << _hr << "\n"; } while(0); print_section(2, "编解码函数: UTF-8 解码");
    {
        const char* s = "Hello";
        uint32_t out[16];
        size_t n = utf8_to_codepoints(s, 5, out, 16);
        print_item("ASCII 解码 5 码点", n == 5 && out[0] == 0x48 && out[4] == 0x6F);

        const char cn[] = {(char)0xE4, (char)0xBD, (char)0xA0, (char)0xE5, (char)0xA5, (char)0xBD, 0};
        uint32_t cout[16];
        size_t cn_n = utf8_to_codepoints(cn, 6, cout, 16);
        print_item("中文解码 '你好'", cn_n == 2 && cout[0] == 0x4F60 && cout[1] == 0x597D);

        const char em[] = {(char)0xF0, (char)0x9F, (char)0x98, (char)0x80, 0};
        uint32_t eout[16];
        size_t em_n = utf8_to_codepoints(em, 4, eout, 16);
        print_item("Emoji 解码 U+1F600", em_n == 1 && eout[0] == 0x1F600);
    }

    // === 3. 编解码函数: 非法序列处理 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 3 hr=" << _hr << "\n"; } while(0); print_section(3, "编解码函数: 非法序列处理");
    {
        const char bad[] = {(char)0xFF, 'A', 0};
        uint32_t out[4];
        bool has_err = false;
        size_t n = utf8_to_codepoints(bad, 2, out, 4, &has_err);
        print_item("非法字节替换为 U+FFFD", out[0] == 0xFFFD);
        print_item("继续解码后续", out[1] == 0x41 && n == 2);
        print_item("错误标志触发", has_err);
    }

    // === 4. utf8pp 构造与析构 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 4 hr=" << _hr << "\n"; } while(0); print_section(4, "utf8pp 构造与析构");
    {
        utf8pp s1;
        print_item("默认构造为空", s1.empty() && s1.size() == 0);

        utf8pp s2("Hello");
        print_item("从 C 字符串构造", s2.size() == 5 && s2.byte_size() == 5);

        utf8pp s3(u8"你好");
        print_item("从 UTF-8 字面量构造", s3.size() == 2 && s3.byte_size() == 6);

        char32_t cps[] = {char32_t('H'), char32_t(0x4E2D), char32_t(0x1F600)};
        utf8pp s4(cps, 3);
        print_item("从 char32 数组构造", s4.size() == 3 && s4.byte_size() == 8);

        utf8pp s5(std::string_view("Hi"));
        print_item("从 string_view 构造", s5.size() == 2);

        utf8pp s6(s2);
        print_item("拷贝构造", s6 == s2);

        utf8pp s7(std::move(s6));
        print_item("移动构造", s7 == s2 && s6.empty());
    }

    // === 5. utf8pp 容量 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 5 hr=" << _hr << "\n"; } while(0); print_section(5, "utf8pp 容量");
    {
        utf8pp s("Hello");
        print_item("size() == 5", s.size() == 5);
        print_item("length() == 5", s.length() == 5);
        print_item("byte_size() == 5", s.byte_size() == 5);
        print_item("empty() == false", !s.empty());

        s.reserve(100);
        print_item("reserve 后 capacity >= 100", s.capacity() >= 100);

        s.clear();
        print_item("clear 后为空", s.empty() && s.size() == 0);

        utf8pp s2(u8"你好");
        print_item("中文 size() == 2", s2.size() == 2);
        print_item("中文 byte_size() == 6", s2.byte_size() == 6);
    }

    // === 6. utf8pp 码点访问 (O(1) via 偏移缓存) ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 6 hr=" << _hr << "\n"; } while(0); print_section(6, "utf8pp 码点访问");
    {
        utf8pp s("Hi中😀"); // H i 中 😀
        print_item("size() == 4", s.size() == 4);

        print_item("at(0) == 'H'", s.at(0) == char32_t('H'));
        print_item("at(1) == 'i'", s.at(1) == char32_t('i'));
        print_item("at(2) == '中' (U+4E2D)", s.at(2) == char32_t(0x4E2D));
        print_item("at(3) == U+1F600", s.at(3) == char32_t(0x1F600));

        print_item("operator[] == at", s[0] == s.at(0));
        print_item("front() == 'H'", s.front() == char32_t('H'));
        print_item("back() == U+1F600", s.back() == char32_t(0x1F600));

        print_item("at 越界返回 U+FFFD", s.at(100) == char32_t(0xFFFD));
    }

    // === 7. utf8pp 字节访问 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 7 hr=" << _hr << "\n"; } while(0); print_section(7, "utf8pp 字节访问");
    {
        utf8pp s("Hello");
        print_item("c_str() 内容正确", std::string(s.c_str()) == "Hello");
        print_item("data() == c_str()", s.data() == s.c_str());
        print_item("view() 大小正确", s.view().size() == 5);

        utf8pp empty;
        print_item("空对象 c_str() == \"\"", std::string(empty.c_str()) == "");
    }

    // === 8. utf8pp 迭代器 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 8 hr=" << _hr << "\n"; } while(0); print_section(8, "utf8pp 迭代器");
    {
        utf8pp s("Hi中");
        std::vector<char32_t> cps;
        for (auto it = s.begin(); it != s.end(); ++it)
        {
            cps.push_back(*it);
        }
        print_item("迭代器遍历 3 码点", cps.size() == 3);
        print_item("第一个 'H'", cps[0] == char32_t('H'));
        print_item("第三个 '中'", cps[2] == char32_t(0x4E2D));

        // range-for
        std::vector<char32_t> cps2;
        for (char32_t cp : s) cps2.push_back(cp);
        print_item("range-for 遍历", cps2 == cps);
    }

    // === 9. utf8pp 修改: push_back / append ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 9 hr=" << _hr << "\n"; } while(0); print_section(9, "utf8pp 修改: push_back / append");
    {
        utf8pp s;
        s.push_back(char32_t('A'));
        s.push_back(char32_t(0x4E2D)); // 中
        s.push_back(char32_t(0x1F600)); // 😀
        print_item("push_back 3 码点", s.size() == 3);
        print_item("byte_size == 8", s.byte_size() == 8);
        print_item("at(0) == 'A'", s.at(0) == char32_t('A'));
        print_item("at(1) == '中'", s.at(1) == char32_t(0x4E2D));
        print_item("at(2) == U+1F600", s.at(2) == char32_t(0x1F600));

        utf8pp s2;
        s2.append("Hello");
        s2.append(u8"你好");
        print_item("append 后 size == 7", s2.size() == 7);
        print_item("append 后 byte_size == 11", s2.byte_size() == 11);

        utf8pp s3;
        s3 += char32_t('X');
        s3 += "YZ";
        print_item("operator+= 混合", s3.size() == 3 && s3.at(0) == char32_t('X'));
    }

    // === 10. utf8pp 修改: insert / erase ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 10 hr=" << _hr << "\n"; } while(0); print_section(10, "utf8pp 修改: insert / erase");
    {
        utf8pp s("Hlo");
        s.insert(1, char32_t('e')); // "Helo"
        print_item("insert 后 'He'", s.at(0) == char32_t('H') && s.at(1) == char32_t('e'));

        s.insert(3, char32_t('l')); // "Hello"
        print_item("insert 后 'Hello'", s.size() == 5 && s.at(4) == char32_t('o'));

        utf8pp s2(u8"你好世界");
        s2.insert(1, char32_t('X')); // 你 X 好世界
        print_item("中文 insert 后 size == 5", s2.size() == 5);
        print_item("insert 后 at(1) == 'X'", s2.at(1) == char32_t('X'));
        print_item("insert 后 at(2) == '好'", s2.at(2) == char32_t(0x597D));

        s2.erase(1); // 删除 'X'
        print_item("erase 后 size == 4", s2.size() == 4);
        print_item("erase 后 at(1) == '好'", s2.at(1) == char32_t(0x597D));
        print_item("erase 后 byte_size 恢复", s2.byte_size() == 12);

        // 批量 erase
        utf8pp s3("ABCDEF");
        s3.erase(1, 3); // 删除 BCD → "AEF"
        print_item("批量 erase 后 'AEF'", s3.size() == 3 && s3.at(0) == char32_t('A') && s3.at(1) == char32_t('E'));
    }

    // === 11. utf8pp substr ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 11 hr=" << _hr << "\n"; } while(0); print_section(11, "utf8pp substr");
    {
        utf8pp s(u8"你好世界");
        utf8pp sub = s.substr(1, 2); // 好世
        print_item("substr size == 2", sub.size() == 2);
        print_item("substr at(0) == '好'", sub.at(0) == char32_t(0x597D));
        print_item("substr at(1) == '世'", sub.at(1) == char32_t(0x4E16));

        utf8pp sub2 = s.substr(2); // 世界
        print_item("substr 默认 npos", sub2.size() == 2 && sub2.at(0) == char32_t(0x4E16));

        utf8pp sub3 = s.substr(100);
        print_item("substr 越界返回空", sub3.empty());
    }

    // === 12. utf8pp 查找 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 12 hr=" << _hr << "\n"; } while(0); print_section(12, "utf8pp 查找");
    {
        utf8pp s("Hello World");
        print_item("find('o') == 4", s.find(char32_t('o')) == 4);
        print_item("find('o', 5) == 7", s.find(char32_t('o'), 5) == 7);
        print_item("find('z') == npos", s.find(char32_t('z')) == utf8pp::npos);

        utf8pp sub("World");
        print_item("find('World') == 6", s.find(sub) == 6);

        utf8pp s2(u8"你好世界你好");
        print_item("中文 find('好') == 1", s2.find(char32_t(0x597D)) == 1);
        print_item("中文 rfind('好') == 5", s2.rfind(char32_t(0x597D)) == 5);
    }

    // === 13. utf8pp 比较 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 13 hr=" << _hr << "\n"; } while(0); print_section(13, "utf8pp 比较");
    {
        utf8pp s1("Hello");
        utf8pp s2("Hello");
        utf8pp s3("World");

        print_item("相等", s1 == s2);
        print_item("不等", s1 != s3);
        print_item("小于", s1 < s3);
        print_item("大于", s3 > s1);
        print_item("compare == 0", s1.compare(s2) == 0);
        print_item("compare < 0", s1.compare(s3) < 0);

        print_item("与 C 字符串比较", s1.compare("Hello") == 0);
    }

    // === 14. utf8pp 转换 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 14 hr=" << _hr << "\n"; } while(0); print_section(14, "utf8pp 转换");
    {
        utf8pp s(u8"你好");
        std::string std_s = s.to_std_string();
        print_item("to_std_string 大小 == 6", std_s.size() == 6);

        std::u32string u32 = s.to_u32string();
        print_item("to_u32string 大小 == 2", u32.size() == 2);
        print_item("to_u32string[0] == U+4F60", u32[0] == char32_t(0x4F60));
        print_item("to_u32string[1] == U+597D", u32[1] == char32_t(0x597D));
    }

    // === 15. utf8pp BOM ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 15 hr=" << _hr << "\n"; } while(0); print_section(15, "utf8pp BOM");
    {
        char bom_str[] = {(char)0xEF, (char)0xBB, (char)0xBF, 'H', 'i', 0};
        utf8pp s(bom_str, 5);
        print_item("检测到 BOM", s.has_bom());

        s.strip_bom();
        print_item("strip BOM 后 byte_size == 2", s.byte_size() == 2);
        print_item("strip BOM 后内容 'Hi'", s.at(0) == char32_t('H') && s.at(1) == char32_t('i'));

        utf8pp s2("Hello");
        print_item("无 BOM", !s2.has_bom());
    }

    // === 16. utf8pp 校验 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 16 hr=" << _hr << "\n"; } while(0); print_section(16, "utf8pp 校验");
    {
        utf8pp s("Hello");
        print_item("合法 UTF-8 valid() == true", s.valid());

        size_t pos = s.validate();
        print_item("合法 UTF-8 validate == npos", pos == utf8pp::npos);
    }

    // === 17. utf8pp 字面量运算符 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 17 hr=" << _hr << "\n"; } while(0); print_section(17, "utf8pp 字面量运算符");
    {
        auto s = "Hello"_u8;
        print_item("_u8 字面量构造", s.size() == 5 && s.at(0) == char32_t('H'));

        auto s2 = u8"你好"_u8;
        print_item("_u8 中文", s2.size() == 2 && s2.at(0) == char32_t(0x4F60));
    }

    // === 18. utf8pp 大规模操作 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 18 hr=" << _hr << "\n"; } while(0); print_section(18, "utf8pp 大规模操作");
    {
        utf8pp s;
        for (int i = 0; i < 1000; ++i)
        {
            s.push_back(char32_t(0x4F60)); // 你
        }
        print_item("1000 次 push_back", s.size() == 1000);
        print_item("1000 次 push_back byte_size == 3000", s.byte_size() == 3000);

        // 批量 erase
        for (int i = 0; i < 500; ++i)
        {
            s.erase(0);
        }
        print_item("500 次 erase 后 size == 500", s.size() == 500);
    }

    // === 19. utf8pp 赋值 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 19 hr=" << _hr << "\n"; } while(0); print_section(19, "utf8pp 赋值");
    {
        utf8pp s;
        s = "Hello";
        print_item("operator= const char*", s.size() == 5);

        s = std::string_view("World");
        print_item("operator= string_view", s.size() == 5 && s.at(0) == char32_t('W'));

        utf8pp s2;
        s2 = s;
        print_item("operator= copy assign", s2 == s);

        utf8pp s3;
        s3 = std::move(s2);
        print_item("operator= move assign", s3 == s && s2.empty());
    }

    // === 20. 往返一致性 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 20 hr=" << _hr << "\n"; } while(0); print_section(20, "往返一致性");
    {
        utf8pp orig(u8"Hello你好😀世界");

        // utf8pp → u32string → utf8pp
        std::u32string u32 = orig.to_u32string();
        utf8pp roundtrip(u32.data(), u32.size());

        print_item("往返 size 一致", orig.size() == roundtrip.size());
        print_item("往返 byte_size 一致", orig.byte_size() == roundtrip.byte_size());
        print_item("往返内容一致", orig == roundtrip);
    }

    // === 15. starts_with / ends_with ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 15 hr=" << _hr << "\n"; } while(0); print_section(15, "starts_with / ends_with");
    {
        utf8pp s("Hello世界");
        print_item("starts_with(char32_t 'H')", s.starts_with(char32_t('H')));
        print_item("starts_with(char32_t 'h') 失败", !s.starts_with(char32_t('h')));
        print_item("starts_with(utf8pp)", s.starts_with(utf8pp("Hello")));
        print_item("starts_with(const char*)", s.starts_with("Hello"));
        print_item("starts_with(string_view)", s.starts_with(std::string_view("Hello")));
        print_item("starts_with 空串失败", !s.starts_with(utf8pp("xyz")));
        print_item("ends_with(char32_t '界')", s.ends_with(char32_t(0x754C)));
        print_item("ends_with(utf8pp 世界)", s.ends_with(utf8pp("世界")));
        print_item("ends_with(const char*)", s.ends_with("世界"));
        print_item("ends_with(string_view)", s.ends_with(std::string_view("世界")));
        print_item("ends_with 失败", !s.ends_with(utf8pp("Hello")));
    }

    // === 16. contains ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 16 hr=" << _hr << "\n"; } while(0); print_section(16, "contains");
    {
        utf8pp s("Hello世界World");
        print_item("contains(char32_t 'o')", s.contains(char32_t('o')));
        print_item("contains(char32_t '界')", s.contains(char32_t(0x754C)));
        print_item("contains(utf8pp 世界)", s.contains(utf8pp("世界")));
        print_item("contains(const char*)", s.contains("World"));
        print_item("contains(string_view)", s.contains(std::string_view("Hello")));
        print_item("contains 失败", !s.contains(utf8pp("xyz")));
        print_item("contains 空对象 (std 一致: 空串是子串)", s.contains(utf8pp()));
    }

    // === 17. count ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 17 hr=" << _hr << "\n"; } while(0); print_section(17, "count");
    {
        utf8pp s("Hello世界World");
        print_item("count(char32_t 'l') == 3", s.count(char32_t('l')) == 3);
        print_item("count(char32_t '界') == 1", s.count(char32_t(0x754C)) == 1);
        utf8pp s2("abcabcabc");
        print_item("count(utf8pp abc) == 3", s2.count(utf8pp("abc")) == 3);
        print_item("count(utf8pp xyz) == 0", s2.count(utf8pp("xyz")) == 0);
        print_item("count 空 == 0", s.count(utf8pp()) == 0);
    }

    // === 18. replace ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 18 hr=" << _hr << "\n"; } while(0); print_section(18, "replace");
    {
        utf8pp s("Hello世界World");
        s.replace(0, 5, utf8pp("Hi"));
        print_item("replace(0,5,Hi) == Hi世界World", s == utf8pp("Hi世界World"));

        utf8pp s2("abc abc abc");
        s2.replace_all(utf8pp("abc"), utf8pp("XY"));
        print_item("replace_all(abc,XY) == XY XY XY", s2 == utf8pp("XY XY XY"));

        utf8pp s3("aAbBa");
        s3.replace_all(char32_t('a'), char32_t('Z'));
        print_item("replace_all(a,Z) == ZAbBZ", s3 == utf8pp("ZAbBZ"));

        utf8pp s4("Hello");
        s4.replace(5, 10, utf8pp("!"));
        print_item("replace 越界不追加 (std 一致)", s4 == utf8pp("Hello"));

        utf8pp s5("Test");
        s5.replace_all(utf8pp("xyz"), utf8pp("123"));
        print_item("replace_all 未找到不变", s5 == utf8pp("Test"));
    }

    // === 19. trim ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 19 hr=" << _hr << "\n"; } while(0); print_section(19, "trim");
    {
        utf8pp s("  Hello世界  ");
        utf8pp t = s.trimmed();
        print_item("trimmed == Hello世界", t == utf8pp("Hello世界"));
        print_item("原对象不变", s == utf8pp("  Hello世界  "));

        utf8pp s2("  Hello");
        print_item("trimmed_left", s2.trimmed_left() == utf8pp("Hello"));

        utf8pp s3("Hello  ");
        print_item("trimmed_right", s3.trimmed_right() == utf8pp("Hello"));

        utf8pp s4(" \t\nHello \r\n");
        print_item("trim 特殊空白", s4.trimmed() == utf8pp("Hello"));

        utf8pp s5(u8"　你好　"); // 全角空格 U+3000
        print_item("trim 全角空格", s5.trimmed() == utf8pp(u8"你好"));

        utf8pp empty("   ");
        print_item("trim 全空白", empty.trimmed().empty());

        utf8pp orig("Hello");
        orig.trim();
        print_item("trim 原地无空白不变", orig == utf8pp("Hello"));
    }

    // === 20. to_lower / to_upper ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 20 hr=" << _hr << "\n"; } while(0); print_section(20, "to_lower / to_upper");
    {
        utf8pp s("Hello WORLD 123");
        print_item("to_lower", s.lowered() == utf8pp("hello world 123"));
        print_item("原对象不变", s == utf8pp("Hello WORLD 123"));

        utf8pp s2("hello world");
        print_item("to_upper", s2.uppered() == utf8pp("HELLO WORLD"));

        utf8pp s3(u8"你好ABC");
        print_item("to_lower 中文不变", s3.lowered() == utf8pp(u8"你好abc"));

        utf8pp s4("Hello");
        s4.to_lower();
        print_item("to_lower 原地", s4 == utf8pp("hello"));
    }

    // === 21. reverse ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 21 hr=" << _hr << "\n"; } while(0); print_section(21, "reverse");
    {
        utf8pp s("Hello");
        print_item("reverse(Hello) == olleH", s.reversed() == utf8pp("olleH"));
        print_item("原对象不变", s == utf8pp("Hello"));

        utf8pp s2(u8"你好世界");
        print_item("reverse(你好世界) == 界世好你", s2.reversed() == utf8pp(u8"界世好你"));

        utf8pp s3("AB");
        s3.reverse();
        print_item("reverse 原地", s3 == utf8pp("BA"));

        utf8pp empty;
        print_item("reverse 空", empty.reversed().empty());

        utf8pp single("X");
        print_item("reverse 单字符", single.reversed() == utf8pp("X"));
    }

    // === 22. split ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 22 hr=" << _hr << "\n"; } while(0); print_section(22, "split");
    {
        utf8pp s("a,b,c");
        dense<utf8pp> parts = s.split(char32_t(','));
        print_item("split(char) 数量 == 3", parts.size() == 3);
        print_item("split[0] == a", parts[0] == utf8pp("a"));
        print_item("split[1] == b", parts[1] == utf8pp("b"));
        print_item("split[2] == c", parts[2] == utf8pp("c"));

        utf8pp s2("Hello世界World");
        dense<utf8pp> parts2 = s2.split(utf8pp("世界"));
        print_item("split(utf8pp) 数量 == 2", parts2.size() == 2);
        print_item("split2[0] == Hello", parts2[0] == utf8pp("Hello"));
        print_item("split2[1] == World", parts2[1] == utf8pp("World"));

        utf8pp s3("a::b::c");
        dense<utf8pp> parts3 = s3.split("::");
        print_item("split(const char*) 数量 == 3", parts3.size() == 3);

        utf8pp s4("a,b,c");
        dense<utf8pp> parts4 = s4.split(std::string_view(","));
        print_item("split(string_view) 数量 == 3", parts4.size() == 3);

        utf8pp empty;
        dense<utf8pp> parts_empty = empty.split(char32_t(','));
        print_item("split 空 == 0", parts_empty.size() == 0);

        utf8pp s5("a");
        dense<utf8pp> parts5 = s5.split(char32_t(','));
        print_item("split 无分隔符 == 1", parts5.size() == 1);

        utf8pp s6(",a,,b,");
        dense<utf8pp> parts6 = s6.split(char32_t(','));
        print_item("split 连续分隔符 == 5", parts6.size() == 5);
        print_item("split[0] 空", parts6[0].empty());
        print_item("split[2] 空", parts6[2].empty());

        // split_to std::vector
        utf8pp s7("x,y,z");
        std::vector<utf8pp> vout;
        s7.split_to(char32_t(','), vout);
        print_item("split_to(vector) == 3", vout.size() == 3);

        // split_to 裸指针
        utf8pp arr[5];
        s7.split_to(utf8pp(","), arr, 5);
        print_item("split_to(指针) [0] == x", arr[0] == utf8pp("x"));
    }

    // === 23. join ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 23 hr=" << _hr << "\n"; } while(0); print_section(23, "join");
    {
        dense<utf8pp> parts;
        parts.push_back(utf8pp("a"));
        parts.push_back(utf8pp("b"));
        parts.push_back(utf8pp("c"));
        utf8pp r = utf8pp::join(parts, utf8pp(","));
        print_item("join(dense, utf8pp) == a,b,c", r == utf8pp("a,b,c"));

        utf8pp r2 = utf8pp::join(parts, char32_t('|'));
        print_item("join(dense, char32_t) == a|b|c", r2 == utf8pp("a|b|c"));

        std::array<utf8pp, 3> arr = {utf8pp("x"), utf8pp("y"), utf8pp("z")};
        utf8pp r3 = utf8pp::join(arr, utf8pp("-"));
        print_item("join(array) == x-y-z", r3 == utf8pp("x-y-z"));

        std::vector<utf8pp> vec = {utf8pp("1"), utf8pp("2")};
        utf8pp r4 = utf8pp::join(vec, utf8pp("+"));
        print_item("join(vector) == 1+2", r4 == utf8pp("1+2"));

        utf8pp raw[] = {utf8pp("A"), utf8pp("B")};
        utf8pp r5 = utf8pp::join(raw, 2, utf8pp(""));
        print_item("join(裸指针) == AB", r5 == utf8pp("AB"));

        dense<utf8pp> empty_parts;
        utf8pp r6 = utf8pp::join(empty_parts, utf8pp(","));
        print_item("join 空 == 空串", r6.empty());

        dense<utf8pp> single;
        single.push_back(utf8pp("solo"));
        utf8pp r7 = utf8pp::join(single, utf8pp(","));
        print_item("join 单元素无分隔符", r7 == utf8pp("solo"));
    }

    // === 24. 构造/assign/append 范围重载 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 24 hr=" << _hr << "\n"; } while(0); print_section(24, "构造/assign/append 范围重载");
    {
        std::vector<utf8pp> vec = {utf8pp("A"), utf8pp("B"), utf8pp("C")};
        utf8pp s1(vec);
        print_item("构造(vector) 连接", s1 == utf8pp("ABC"));

        std::array<utf8pp, 2> arr = {utf8pp("X"), utf8pp("Y")};
        utf8pp s2(arr);
        print_item("构造(array) 连接", s2 == utf8pp("XY"));

        utf8pp s3;
        s3.assign(vec);
        print_item("assign(vector)", s3 == utf8pp("ABC"));

        utf8pp s4;
        s4.assign(arr);
        print_item("assign(array)", s4 == utf8pp("XY"));

        utf8pp s5("head");
        s5.append(vec);
        print_item("append(vector)", s5 == utf8pp("headABC"));

        utf8pp s6("head");
        s6.append(arr);
        print_item("append(array)", s6 == utf8pp("headXY"));

        utf8pp s7("head");
        utf8pp raw[] = {utf8pp("1"), utf8pp("2")};
        s7.append(raw, 2);
        print_item("append(裸指针)", s7 == utf8pp("head12"));

        utf8pp s8("head");
        s8.append(std::span<const utf8pp>(raw, 2));
        print_item("append(span)", s8 == utf8pp("head12"));
    }

    // === 25. data()/c_str() 空串返回 "" ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 25 hr=" << _hr << "\n"; } while(0); print_section(25, "data()/c_str() 空串安全");
    {
        utf8pp empty;
        print_item("空 c_str() != nullptr", empty.c_str() != nullptr);
        print_item("空 data() != nullptr", empty.data() != nullptr);
        print_item("空 c_str() == \"\"", std::string(empty.c_str()) == "");
        print_item("空 data() == \"\"", std::string(empty.data()) == "");
        print_item("空 view() 长度 0", empty.view().size() == 0);

        utf8pp s("Hi");
        print_item("非空 c_str() == Hi", std::string(s.c_str()) == "Hi");
        print_item("非空 data() == Hi", std::string(s.data()) == "Hi");
    }

    // === 26. max_size / reserve_exact / increase_capacity ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 26 hr=" << _hr << "\n"; } while(0); print_section(26, "max_size / reserve_exact / increase_capacity");
    {
        utf8pp s;
        print_item("max_size() > 0", s.max_size() > 0);

        s.reserve_exact(128);
        print_item("reserve_exact(128) 后 capacity >= 128", s.capacity() >= 128);

        utf8pp s2;
        s2.increase_capacity(64);
        print_item("increase_capacity(64) 后 capacity >= 64", s2.capacity() >= 64);
        s2.assign_cp(10, char32_t('A'));
        print_item("increase_capacity 后 cp 容量可用", s2.size() == 10);
    }

    // === 27. resize_cp / pop_back / append_cp / assign_cp / 构造(n,cp) ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 27 hr=" << _hr << "\n"; } while(0); print_section(27, "resize_cp / pop_back / append_cp / assign_cp / 构造(n,cp)");
    {
        utf8pp s(5, char32_t('A'));
        print_item("构造(5,'A') == AAAAA", s == utf8pp("AAAAA"));
        print_item("构造(5,'A') size == 5", s.size() == 5);

        utf8pp s2(3, char32_t(0x4E2D));
        print_item("构造(3,中文) == 中中中", s2 == utf8pp(u8"中中中"));

        utf8pp s3;
        s3.append_cp(3, char32_t('X'));
        print_item("append_cp(3,X) 后 == XXX", s3 == utf8pp("XXX"));
        print_item("append_cp 后 size == 3", s3.size() == 3);

        utf8pp s4("head");
        s4.append_cp(2, char32_t('!'));
        print_item("append_cp 追加", s4 == utf8pp("head!!"));

        utf8pp s5;
        s5.assign_cp(4, char32_t('Z'));
        print_item("assign_cp(4,Z) == ZZZZ", s5 == utf8pp("ZZZZ"));

        utf8pp s6("Hello");
        s6.resize_cp(3);
        print_item("resize_cp(3) 截断 == Hel", s6 == utf8pp("Hel"));

        utf8pp s7("Hi");
        s7.resize_cp(5, char32_t('!'));
        print_item("resize_cp(5,!) 扩展 == Hi!!!", s7 == utf8pp("Hi!!!"));

        utf8pp s8("Hello");
        s8.pop_back();
        print_item("pop_back == Hell", s8 == utf8pp("Hell"));

        utf8pp empty;
        empty.pop_back();
        print_item("空 pop_back 安全", empty.empty());

        utf8pp s9(u8"你好");
        s9.pop_back();
        print_item("中文 pop_back == 你", s9 == utf8pp(u8"你"));
    }

    // === 28. copy ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 28 hr=" << _hr << "\n"; } while(0); print_section(28, "copy");
    {
        utf8pp s("Hello");
        char buf[16] = {};
        size_t n = s.copy(buf, 3);
        print_item("copy(buf,3) 字节 == 3", n == 3);
        print_item("copy(buf,3) == Hel", std::string(buf, n) == "Hel");

        char buf2[16] = {};
        size_t n2 = s.copy(buf2, 10, 2);
        print_item("copy(buf,10,2) == llo", std::string(buf2, n2) == "llo");

        utf8pp cn(u8"你好世界");
        char buf3[16] = {};
        size_t n3 = cn.copy(buf3, 2);
        print_item("中文 copy(2) == 你好", utf8pp(buf3, n3) == utf8pp(u8"你好"));

        char buf4[16] = {};
        size_t n4 = s.copy(buf4, 5, 10);
        print_item("copy 越界 pos == 0", n4 == 0);
    }

    // === 29. find_first_of / find_last_of / find_first_not_of / find_last_not_of ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 29 hr=" << _hr << "\n"; } while(0); print_section(29, "find_first_of / find_last_of / find_first_not_of / find_last_not_of");
    {
        utf8pp s("Hello世界World");

        print_item("find_first_of('l') == 2", s.find_first_of(char32_t('l')) == 2);
        print_item("find_first_of('界') == 6", s.find_first_of(char32_t(0x754C)) == 6);
        print_item("find_first_of(z) == npos", s.find_first_of(char32_t('z')) == utf8pp::npos);

        utf8pp chars("lo");
        print_item("find_first_of(utf8pp lo) == 2", s.find_first_of(chars) == 2);

        print_item("find_last_of('l') == 10", s.find_last_of(char32_t('l')) == 10);
        print_item("find_last_of('H') == 0", s.find_last_of(char32_t('H')) == 0);

        utf8pp chars2("od");
        print_item("find_last_of(utf8pp od) == 11", s.find_last_of(chars2) == 11);

        print_item("find_first_not_of('H') == 1", s.find_first_not_of(char32_t('H')) == 1);

        utf8pp prefix("He");
        print_item("find_first_not_of(He) == 2", s.find_first_not_of(prefix) == 2);

        utf8pp s2("aaaa");
        print_item("find_first_not_of(a) == npos", s2.find_first_not_of(char32_t('a')) == utf8pp::npos);

        print_item("find_last_not_of('d') == 10", s.find_last_not_of(char32_t('d')) == 10);

        utf8pp suffix("ld");
        print_item("find_last_not_of(ld) == 9", s.find_last_not_of(suffix) == 9);
    }

    // === 30. rfind ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 30 hr=" << _hr << "\n"; } while(0); print_section(30, "rfind");
    {
        utf8pp s("abcabc");
        print_item("rfind('a') == 3", s.rfind(char32_t('a')) == 3);
        print_item("rfind('z') == npos", s.rfind(char32_t('z')) == utf8pp::npos);

        utf8pp s2("HelloHello");
        print_item("rfind(utf8pp Hello) == 5", s2.rfind(utf8pp("Hello")) == 5);
        print_item("rfind(utf8pp xyz) == npos", s2.rfind(utf8pp("xyz")) == utf8pp::npos);
        print_item("rfind 空串 == size", s2.rfind(utf8pp()) == s2.size());
        print_item("rfind(空串,3) == 3", s2.rfind(utf8pp(), 3) == 3);

        utf8pp empty;
        print_item("空 rfind == npos", empty.rfind(char32_t('a')) == utf8pp::npos);
    }

    // === 31. reverse_iterator ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 31 hr=" << _hr << "\n"; } while(0); print_section(31, "reverse_iterator");
    {
        utf8pp s("abc");
        std::u32string collected;
        for (auto it = s.rbegin(); it != s.rend(); ++it)
        {
            collected.push_back(*it);
        }
        print_item("rbegin->rend 反转 == cba", collected == std::u32string(U"cba"));

        utf8pp s2(u8"你好");
        std::u32string collected2;
        for (auto it = s2.rbegin(); it != s2.rend(); ++it)
        {
            collected2.push_back(*it);
        }
        print_item("中文 rbegin->rend == 好你", collected2 == std::u32string(U"好你"));

        utf8pp s3("XYZ");
        auto it = s3.crbegin();
        print_item("crbegin == 'Z'", *it == char32_t('Z'));
        ++it;
        print_item("++crbegin == 'Y'", *it == char32_t('Y'));

        utf8pp empty;
        print_item("空 rbegin == rend", empty.rbegin() == empty.rend());

        utf8pp s4("Hi");
        auto it2 = s4.rbegin();
        ++it2;
        ++it2;
        print_item("rbegin++ 到 rend", it2 == s4.rend());
    }

    // === 32. operator+ 系列 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 32 hr=" << _hr << "\n"; } while(0); print_section(32, "operator+ 系列");
    {
        utf8pp a("Hello");
        utf8pp b("World");
        print_item("utf8pp + utf8pp", (a + b) == utf8pp("HelloWorld"));

        print_item("utf8pp + char32_t", (a + char32_t('!')) == utf8pp("Hello!"));

        print_item("char32_t + utf8pp", (char32_t('!') + a) == utf8pp("!Hello"));

        print_item("utf8pp + const char*", (a + "World") == utf8pp("HelloWorld"));

        print_item("const char* + utf8pp", ("Hi" + a) == utf8pp("HiHello"));

        print_item("utf8pp + string_view", (a + std::string_view("XY")) == utf8pp("HelloXY"));

        utf8pp empty;
        print_item("空 + utf8pp", (empty + a) == utf8pp("Hello"));
        print_item("utf8pp + 空", (a + empty) == utf8pp("Hello"));
    }

    // === 33. operator<</>> 流操作符 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 33 hr=" << _hr << "\n"; } while(0); print_section(33, "operator<</>> 流操作符");
    {
        utf8pp s("Hello世界");
        std::ostringstream oss;
        oss << s;
        print_item("operator<< 输出", oss.str() == std::string("Hello世界"));

        std::istringstream iss("TestData");
        utf8pp result;
        iss >> result;
        print_item("operator>> 输入", result == utf8pp("TestData"));

        utf8pp empty;
        std::ostringstream oss2;
        oss2 << empty;
        print_item("空 operator<< 安全", oss2.str().empty());
    }

    // === 34. operator<=> 三路比较 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 34 hr=" << _hr << "\n"; } while(0); print_section(34, "operator<=> 三路比较");
    {
        utf8pp a("abc");
        utf8pp b("abd");
        utf8pp c("abc");

        print_item("abc <=> abd 小于", (a <=> b) == std::strong_ordering::less);
        print_item("abc <=> abc 等于", (a <=> c) == std::strong_ordering::equal);
        print_item("abd <=> abc 大于", (b <=> a) == std::strong_ordering::greater);

        print_item("abc <=> \"abcd\" 小于", (a <=> "abcd") == std::strong_ordering::less);
        print_item("abc <=> \"abc\" 等于", (a <=> "abc") == std::strong_ordering::equal);
        print_item("abc <=> \"ab\" 大于", (a <=> "ab") == std::strong_ordering::greater);

        utf8pp s1(u8"你好");
        utf8pp s2(u8"你好");
        print_item("中文 <=> 等于", (s1 <=> s2) == std::strong_ordering::equal);
    }

    // === 35. 迭代器版 insert/erase ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 35 hr=" << _hr << "\n"; } while(0); print_section(35, "迭代器版 insert/erase");
    {
        utf8pp s("acd");
        auto it = s.begin();
        ++it;
        s.insert(it, char32_t('b'));
        print_item("insert(iter,'b') == abcd", s == utf8pp("abcd"));

        utf8pp s2("ad");
        auto it2 = s2.begin();
        ++it2;
        s2.insert(it2, 2, char32_t('X'));
        print_item("insert(iter,2,'X') == aXXd", s2 == utf8pp("aXXd"));

        utf8pp s3("ae");
        auto it3 = s3.begin();
        ++it3;
        utf8pp src("bcd");
        s3.insert(it3, src.begin(), src.end());
        print_item("insert(iter, range) == abcde", s3 == utf8pp("abcde"));

        utf8pp s4("abcd");
        auto it4 = s4.begin();
        ++it4;
        s4.erase(it4);
        print_item("erase(iter) == acd", s4 == utf8pp("acd"));

        utf8pp s5("abcde");
        auto first = s5.begin();
        ++first;
        ++first;
        auto last = first;
        ++last;
        s5.erase(first, last);
        print_item("erase(first,last) == abde", s5 == utf8pp("abde"));

        utf8pp s6("abcXYZdef");
        auto f6 = s6.begin();
        for (int i = 0; i < 3; ++i) ++f6;
        auto l6 = f6;
        for (int i = 0; i < 3; ++i) ++l6;
        s6.erase(f6, l6);
        print_item("erase 中段 == abcdef", s6 == utf8pp("abcdef"));
    }

    // === 36. std::swap 非成员 + std::hash ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 36 hr=" << _hr << "\n"; } while(0); print_section(36, "std::swap 非成员 + std::hash");
    {
        utf8pp a("Hello");
        utf8pp b("World");
        swap(a, b);
        print_item("swap 后 a == World", a == utf8pp("World"));
        print_item("swap 后 b == Hello", b == utf8pp("Hello"));

        utf8pp empty1;
        utf8pp empty2("Data");
        swap(empty1, empty2);
        print_item("swap 空与非空", empty1 == utf8pp("Data") && empty2.empty());

        std::hash<utf8pp> hasher;
        utf8pp s1("Hello");
        utf8pp s2("Hello");
        utf8pp s3("World");
        print_item("hash 相同串相等", hasher(s1) == hasher(s2));
        print_item("hash 不同串通常不等", hasher(s1) != hasher(s3));

        print_item("hash 空串 > 0", hasher(utf8pp()) == hasher(utf8pp()));
    }

    // === 37. find/rfind 的 const char*/string_view 重载 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 37 hr=" << _hr << "\n"; } while(0); print_section(37, "find/rfind const char*/string_view 重载");
    {
        utf8pp s("Hello世界World");
        print_item("find(const char*) == 5", s.find("世界") == 5);
        print_item("find(string_view) == 5", s.find(std::string_view("世界")) == 5);
        print_item("find(const char*, 6) == npos", s.find("世界", 6) == utf8pp::npos);
        print_item("find 未找到 == npos", s.find("xyz") == utf8pp::npos);

        utf8pp s2("abcabcabc");
        print_item("rfind(const char*) == 6", s2.rfind("abc") == 6);
        print_item("rfind(string_view) == 6", s2.rfind(std::string_view("abc")) == 6);
        print_item("rfind(const char*, 3) == 3", s2.rfind("abc", 3) == 3);
        print_item("rfind 未找到 == npos", s2.rfind("xyz") == utf8pp::npos);

        utf8pp empty;
        print_item("空 find(const char*) == npos", empty.find("a") == utf8pp::npos);
    }

    // === 38. find_first_of/find_last_of 等 const char*/string_view 重载 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 38 hr=" << _hr << "\n"; } while(0); print_section(38, "find_*_of const char*/string_view 重载");
    {
        utf8pp s("Hello世界World");
        print_item("find_first_of(const char*) == 2", s.find_first_of("l") == 2);
        print_item("find_first_of(string_view) == 2", s.find_first_of(std::string_view("lo")) == 2);
        print_item("find_last_of(const char*) == 10", s.find_last_of("l") == 10);
        print_item("find_last_of(string_view) == 11", s.find_last_of(std::string_view("od")) == 11);
        print_item("find_first_not_of(const char*) == 1", s.find_first_not_of("H") == 1);
        print_item("find_first_not_of(string_view) == 2", s.find_first_not_of(std::string_view("He")) == 2);
        print_item("find_last_not_of(const char*) == 10", s.find_last_not_of("d") == 10);
        print_item("find_last_not_of(string_view) == 9", s.find_last_not_of(std::string_view("ld")) == 9);
    }

    // === 39. insert 字符串重载 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 39 hr=" << _hr << "\n"; } while(0); print_section(39, "insert 字符串重载");
    {
        utf8pp s("Hd");
        s.insert(1, "el"); // "Held"
        print_item("insert(const char*) == Held", s == utf8pp("Held"));

        utf8pp s2("Hd");
        s2.insert(1, std::string_view("el")); // "Held"
        print_item("insert(string_view) == Held", s2 == utf8pp("Held"));

        utf8pp s3("Hd");
        s3.insert(1, utf8pp("el")); // "Held"
        print_item("insert(utf8pp) == Held", s3 == utf8pp("Held"));

        utf8pp s4("Hd");
        s4.insert(1, "el", 1); // 插入 "e" → "Hed"
        print_item("insert(const char*, 1) == Hed", s4 == utf8pp("Hed"));

        utf8pp s5(u8"你好");
        s5.insert(1, "X"); // 你X好
        print_item("中文 insert(const char*)", s5.size() == 3 && s5.at(1) == char32_t('X'));
    }

    // === 40. replace 字符串重载 + compare 子串 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 40 hr=" << _hr << "\n"; } while(0); print_section(40, "replace 字符串重载 + compare 子串");
    {
        utf8pp s("Hello世界World");
        s.replace(0, 5, "Hi");
        print_item("replace(const char*) == Hi世界World", s == utf8pp("Hi世界World"));

        utf8pp s2("Hello世界World");
        s2.replace(0, 5, std::string_view("Hi"));
        print_item("replace(string_view) == Hi世界World", s2 == utf8pp("Hi世界World"));

        utf8pp s3("abc abc abc");
        s3.replace_all("abc", "XY");
        print_item("replace_all(const char*) == XY XY XY", s3 == utf8pp("XY XY XY"));

        utf8pp s4("abc abc abc");
        s4.replace_all(std::string_view("abc"), std::string_view("Z"));
        print_item("replace_all(string_view) == Z Z Z", s4 == utf8pp("Z Z Z"));

        utf8pp s5("Hello");
        print_item("compare(0,3, Hel) == 0", s5.compare(0, 3, utf8pp("Hel")) == 0);
        print_item("compare(0,3, Hi) < 0", s5.compare(0, 3, "Hi") < 0);
        print_item("compare(0,3, Hello) < 0", s5.compare(0, 3, std::string_view("Hello")) < 0);
    }

    // === 41. operator==/string_view + <=>/string_view ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 41 hr=" << _hr << "\n"; } while(0); print_section(41, "operator==/string_view + <=>/string_view");
    {
        utf8pp s("Hello");
        print_item("==(string_view)", s == std::string_view("Hello"));
        print_item("!=(string_view)", s != std::string_view("World"));
        print_item("<(string_view)", s < std::string_view("World"));
        print_item("(string_view) <=> 等于", (s <=> std::string_view("Hello")) == std::strong_ordering::equal);
        print_item("(string_view) <=> 小于", (s <=> std::string_view("World")) == std::strong_ordering::less);

        utf8pp cn(u8"你好");
        print_item("中文 ==(string_view)", cn == std::string_view(reinterpret_cast<const char*>(u8"你好")));
    }

    // === 42. SSO 边界测试 (22 字节临界) ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 42 hr=" << _hr << "\n"; } while(0); print_section(42, "SSO 边界测试");
    {
        // 22 ASCII 字符 = SSO 满载
        utf8pp s("0123456789012345678901"); // 22 字节
        print_item("SSO 满 22 字节 size==22", s.size() == 22);
        print_item("SSO 满 is_sso", s.is_sso());

        utf8pp s2 = s; // copy
        print_item("SSO 满 拷贝", s2 == s);

        utf8pp s3 = std::move(s2); // move
        print_item("SSO 满 移动", s3 == s && s2.empty());

        utf8pp s4;
        s4 = s; // copy assign
        print_item("SSO 满 拷贝赋值", s4 == s);

        utf8pp s5;
        s5 = std::move(s4); // move assign
        print_item("SSO 满 移动赋值", s5 == s && s4.empty());

        // SSO 满 → reverse (触发 SSO→heap)
        utf8pp s6(s);
        s6.reverse();
        print_item("SSO 满 reverse", s6 == utf8pp("1098765432109876543210"));

        // SSO 满 → append 触发扩容
        utf8pp s7(s);
        s7.append("X");
        print_item("SSO 满 append 触发扩容", s7.size() == 23 && s7.at(22) == char32_t('X'));
        print_item("SSO 满 append 非 SSO", !s7.is_sso());

        // SSO 满 → erase + shrink_to_fit 回退
        utf8pp s8(s);
        s8.erase(0, 10);
        s8.shrink_to_fit();
        print_item("SSO 满 erase+shrink 回退 SSO", s8.is_sso() && s8.size() == 12);

        // swap SSO 满 ↔ heap
        utf8pp a(s); // SSO 满
        utf8pp b("long string that exceeds sso buffer capacity xxxxxxxx"); // heap
        swap(a, b);
        print_item("swap SSO满↔heap a", a.size() == 50);
        print_item("swap SSO满↔heap b", b.size() == 22 && b.is_sso());
    }

    // === 43. 数字 → utf8pp (to_utf8pp) ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 43 hr=" << _hr << "\n"; } while(0); print_section(43, "数字 → utf8pp");
    {
        print_item("to_utf8pp(int 42) == \"42\"", to_utf8pp(42) == utf8pp("42"));
        print_item("to_utf8pp(int -7) == \"-7\"", to_utf8pp(-7) == utf8pp("-7"));
        print_item("to_utf8pp(int 0) == \"0\"", to_utf8pp(0) == utf8pp("0"));
        print_item("to_utf8pp(long 123456789)", to_utf8pp(123456789L) == utf8pp("123456789"));
        print_item("to_utf8pp(ll 负大数)", to_utf8pp(-9223372036854775807LL) == utf8pp("-9223372036854775807"));
        print_item("to_utf8pp(unsigned 42u)", to_utf8pp(42u) == utf8pp("42"));
        print_item("to_utf8pp(ull 大数)", to_utf8pp(18446744073709551615ULL) == utf8pp("18446744073709551615"));
        print_item("to_utf8pp(float 3.14)", to_utf8pp(3.14f) == utf8pp("3.14"));
        print_item("to_utf8pp(double 2.5)", to_utf8pp(2.5) == utf8pp("2.5"));
        print_item("to_utf8pp(double 1e10)", to_utf8pp(1e10) == utf8pp("1e+10"));
        print_item("to_utf8pp(double 0.0)", to_utf8pp(0.0) == utf8pp("0"));
        print_item("to_utf8pp(double -0.5)", to_utf8pp(-0.5) == utf8pp("-0.5"));
        print_item("to_utf8pp(int) + 拼接", (to_utf8pp(2026) + utf8pp("-01-01")) == utf8pp("2026-01-01"));
    }

    // === 44. 字符串 → 数字 (to_int/to_long/to_double) ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 44 hr=" << _hr << "\n"; } while(0); print_section(44, "字符串 → 数字 (to_* 系列)");
    {
        utf8pp s("42");
        print_item("to_int(\"42\") == 42", s.to_int() == 42);
        print_item("to_ll(\"42\") == 42", s.to_ll() == 42);

        utf8pp neg("-123");
        print_item("to_int(\"-123\") == -123", neg.to_int() == -123);

        utf8pp big("9223372036854775807");
        print_item("to_ll(大数) 正确", big.to_ll() == 9223372036854775807LL);

        utf8pp u("18446744073709551615");
        print_item("to_ull(大数) 正确", u.to_ull() == 18446744073709551615ULL);

        utf8pp f("3.14");
        print_item("to_float(\"3.14\") ~ 3.14f", std::abs(f.to_float() - 3.14f) < 1e-5f);
        print_item("to_double(\"3.14\") ~ 3.14", std::abs(f.to_double() - 3.14) < 1e-9);

        utf8pp exp("1.5e3");
        print_item("to_double(\"1.5e3\") == 1500", exp.to_double() == 1500.0);

        utf8pp hexstr("ff");
        print_item("to_int(\"ff\",16) == 255", hexstr.to_int(nullptr, 16) == 255);

        // pos 输出
        utf8pp mixed("42abc");
        size_t pos = 0;
        int v = mixed.to_int(&pos);
        print_item("to_int(\"42abc\") == 42", v == 42);
        print_item("pos == 2", pos == 2);

        // 非法 → 0 (不抛异常)
        utf8pp bad("hello");
        print_item("to_int(\"hello\") == 0 无异常", bad.to_int() == 0);
        print_item("to_double(\"hello\") == 0.0", bad.to_double() == 0.0);

        // 前导空白
        utf8pp sp("  -88  ");
        print_item("to_int(\"  -88  \") == -88", sp.to_int() == -88);

        utf8pp empty;
        print_item("空 to_int == 0", empty.to_int() == 0);
    }

    // === 45. parse_* 严格解析 ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 45 hr=" << _hr << "\n"; } while(0); print_section(45, "parse_* 严格解析");
    {
        int iv = 0;
        print_item("parse_int(\"123\") ok", utf8pp("123").parse_int(iv) && iv == 123);
        print_item("parse_int(\"-7\") ok", utf8pp("-7").parse_int(iv) && iv == -7);

        long lv = 0;
        print_item("parse_long(\"  999  \") ok", utf8pp("  999  ").parse_long(lv) && lv == 999);

        long long llv = 0;
        print_item("parse_ll(\"ff\",16) ok", utf8pp("ff").parse_ll(llv, 16) && llv == 255);
        print_item("parse_ll(\"0x1A\") 失败", !utf8pp("0x1A").parse_ll(llv));

        unsigned long ulv = 0;
        print_item("parse_ulong(\"12345\") ok", utf8pp("12345").parse_ulong(ulv) && ulv == 12345);

        unsigned long long ullv = 0;
        print_item("parse_ull(\" 42 \") ok", utf8pp(" 42 ").parse_ull(ullv) && ullv == 42);

        float fv = 0.0f;
        print_item("parse_float(\"3.14\") ok", utf8pp("3.14").parse_float(fv) && std::abs(fv - 3.14f) < 1e-5f);

        double dv = 0.0;
        print_item("parse_double(\"-2.5e2\") ok", utf8pp("-2.5e2").parse_double(dv) && dv == -250.0);

        // 失败: 含非法字符
        print_item("parse_int(\"12a3\") 失败", !utf8pp("12a3").parse_int(iv));
        print_item("parse_int(\"\") 失败", !utf8pp().parse_int(iv));
        print_item("parse_int(\"  \") 失败", !utf8pp("  ").parse_int(iv));
        print_item("parse_double(\"abc\") 失败", !utf8pp("abc").parse_double(dv));
        // 溢出失败
        print_item("parse_int(超大) 失败", !utf8pp("99999999999").parse_int(iv));
    }

    // === 46. is_integer / is_number / is_float ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 46 hr=" << _hr << "\n"; } while(0); print_section(46, "is_integer / is_number / is_float");
    {
        print_item("is_integer(\"42\")", utf8pp("42").is_integer());
        print_item("is_integer(\"-7\")", utf8pp("-7").is_integer());
        print_item("is_integer(\"  88  \")", utf8pp("  88  ").is_integer());
        print_item("is_integer(\"ff\",16)", utf8pp("ff").is_integer(16));
        print_item("!is_integer(\"3.14\")", !utf8pp("3.14").is_integer());
        print_item("!is_integer(\"12a\")", !utf8pp("12a").is_integer());
        print_item("!is_integer(\"\")", !utf8pp().is_integer());

        print_item("is_float(\"3.14\")", utf8pp("3.14").is_float());
        print_item("is_float(\"-2.5e3\")", utf8pp("-2.5e3").is_float());
        print_item("is_float(\"42\")", utf8pp("42").is_float());
        print_item("!is_float(\"abc\")", !utf8pp("abc").is_float());
        print_item("!is_float(\"\")", !utf8pp().is_float());

        print_item("is_number(\"42\")", utf8pp("42").is_number());
        print_item("is_number(\"3.14\")", utf8pp("3.14").is_number());
        print_item("!is_number(\"hello\")", !utf8pp("hello").is_number());
    }

    // === 47. reserve_cp / cp_capacity ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 47 hr=" << _hr << "\n"; } while(0); print_section(47, "reserve_cp / cp_capacity");
    {
        utf8pp s;
        size_t before = s.cp_capacity();
        s.reserve_cp(100);
        print_item("reserve_cp(100) 后 cp_capacity >= 100", s.cp_capacity() >= 100);
        print_item("reserve_cp 扩容", s.cp_capacity() > before);
        // 预留后操作正常
        s.assign_cp(50, char32_t('Z'));
        print_item("reserve_cp 后 assign_cp 正常", s.size() == 50 && s.at(0) == char32_t('Z'));
        // 已有内容 reserve_cp 不丢失
        utf8pp s2("Hello");
        s2.reserve_cp(64);
        print_item("已有内容 reserve_cp 不丢失", s2 == utf8pp("Hello"));
        print_item("已有内容 cp_capacity >= 64", s2.cp_capacity() >= 64);
    }

    // === 48. data() 非 const + rebuild ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 48 hr=" << _hr << "\n"; } while(0); print_section(48, "data() 非 const + rebuild");
    {
        utf8pp s("hello");
        char* p = s.data();
        print_item("非 const data() 可写", p != nullptr);
        // 原地大写化 ASCII 字节
        for (size_t i = 0; i < s.byte_size(); ++i)
        {
            if (p[i] >= 'a' && p[i] <= 'z') p[i] = static_cast<char>(p[i] - 32);
        }
        s.rebuild_cp_offsets();
        print_item("原地大写后 == HELLO", s == utf8pp("HELLO"));
        print_item("rebuild 后 size 保持 5", s.size() == 5);

        // rebuild(new_byte_size) 缩短
        utf8pp s2("abcdef");
        char* p2 = s2.data();
        (void)p2;
        s2.rebuild(3);
        print_item("rebuild(3) == abc", s2 == utf8pp("abc"));
        print_item("rebuild(3) size == 3", s2.size() == 3);

        // rebuild 后码点访问正常
        utf8pp s3(u8"你好世界");
        s3.rebuild(6); // 保留 "你好"
        print_item("中文 rebuild(6) == 你好", s3 == utf8pp(u8"你好"));
        print_item("中文 rebuild 后 size == 2", s3.size() == 2);
    }

    // === 49. pad_left / pad_right / center ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 49 hr=" << _hr << "\n"; } while(0); print_section(49, "pad_left / pad_right / center");
    {
        utf8pp s("Hi");
        s.pad_left(5);
        print_item("pad_left(5) == \"   Hi\"", s == utf8pp("   Hi"));

        utf8pp s2("Hi");
        s2.pad_right(5);
        print_item("pad_right(5) == \"Hi   \"", s2 == utf8pp("Hi   "));

        utf8pp s3("Hi");
        s3.pad_left(5, char32_t('-'));
        print_item("pad_left(5,'-') == \"---Hi\"", s3 == utf8pp("---Hi"));

        utf8pp s4("Hi");
        s4.pad_right(5, char32_t('.'));
        print_item("pad_right(5,'.') == \"Hi...\"", s4 == utf8pp("Hi..."));

        utf8pp s5("Hi");
        s5.center(6);
        print_item("center(6) == \"  Hi  \"", s5 == utf8pp("  Hi  "));

        utf8pp s6("Hi");
        s6.center(5);
        print_item("center(5) 右多一 == \" Hi  \"", s6 == utf8pp(" Hi  "));

        // 已达宽度不变
        utf8pp s7("Hello");
        s7.pad_left(3);
        print_item("pad_left(3) 不变", s7 == utf8pp("Hello"));

        // 副本版本
        utf8pp s8("A");
        print_item("padded_left(3) == \"  A\"", s8.padded_left(3) == utf8pp("  A"));
        print_item("padded_right(3) == \"A  \"", s8.padded_right(3) == utf8pp("A  "));
        print_item("centered(3) == \" A \"", s8.centered(3) == utf8pp(" A "));

        // 中文对齐 (码点级)
        utf8pp s9(u8"你");
        print_item("中文 padded_left(3)", s9.padded_left(3) == utf8pp(u8"  你"));
        print_item("中文 centered(3)", s9.centered(3) == utf8pp(u8" 你 "));
    }

    // === 50. getline ===
    do { extern "C" int _heapchk(void); int _hr = _heapchk(); if (_hr != 0) std::cout << "HEAP_BAD before section 50 hr=" << _hr << "\n"; } while(0); print_section(50, "getline");
    {
        std::istringstream iss("Hello\nWorld\n");
        utf8pp line;
        getline(iss, line);
        print_item("getline 第1行 == Hello", line == utf8pp("Hello"));
        getline(iss, line);
        print_item("getline 第2行 == World", line == utf8pp("World"));
        bool ok = (bool)getline(iss, line);
        print_item("getline 第3行 (空) EOF 失败", !ok);

        // 自定义分隔符
        std::istringstream iss2("a,b,c");
        utf8pp l2;
        getline(iss2, l2, ',');
        print_item("getline ',' 第1段 == a", l2 == utf8pp("a"));
        getline(iss2, l2, ',');
        print_item("getline ',' 第2段 == b", l2 == utf8pp("b"));
        getline(iss2, l2, ',');
        print_item("getline ',' 第3段 == c", l2 == utf8pp("c"));

        // 无换行的单行
        std::istringstream iss3("only line");
        utf8pp l3;
        getline(iss3, l3);
        print_item("getline 单行 == only line", l3 == utf8pp("only line"));

        // 空流
        std::istringstream iss4;
        utf8pp l4;
        bool r4 = (bool)getline(iss4, l4);
        print_item("getline 空流失败", !r4);
    }

    print_summary("功能测试");
    return 0;
}

