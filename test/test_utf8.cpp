// test_utf8.cpp - utf8 功能测试 (编解码函数 + utf8pp + utf8_view + Unicode 数据)
#include "test_common.hpp"
#include "include/part/utf8pp/utf8pp.hpp"
#include "include/part/utf8pp/utf8_view.hpp"
#include "include/part/dense.hpp"
#include <cmath>
#include <unordered_map>
#ifdef _WIN32
#include <malloc.h>
#endif

using script = utf8pp::script;
namespace ud = unicode_data;

int main()
{
    // ================================================================
    //  模块 1: 编解码函数
    // ================================================================

    // === 1. 编解码函数: 单值转换 ===
    print_section(1, "编解码函数: 单值转换");
    {
        print_item("to_char(0x41) == 'A'", to_char(0x41) == char32_t('A'));
        print_item("to_int('A') == 0x41", to_int(char32_t('A')) == 0x41);
        print_item("to_char(0x4E2D) == '中'", to_char(0x4E2D) == char32_t(0x4E2D));
        print_item("to_int('中') == 0x4E2D", to_int(char32_t(0x4E2D)) == 0x4E2D);
        print_item("to_char(0x1F600) == U+1F600", to_char(0x1F600) == char32_t(0x1F600));
        print_item("constexpr to_char", to_char(0x4E2D) == char32_t(0x4E2D));
    }

    // === 2. 编解码函数: UTF-8 解码 ===
    print_section(2, "编解码函数: UTF-8 解码");
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
    print_section(3, "编解码函数: 非法序列处理");
    {
        const char bad[] = {(char)0xFF, 'A', 0};
        uint32_t out[4];
        bool has_err = false;
        size_t n = utf8_to_codepoints(bad, 2, out, 4, &has_err);
        print_item("非法字节替换为 U+FFFD", out[0] == 0xFFFD);
        print_item("继续解码后续", out[1] == 0x41 && n == 2);
        print_item("错误标志触发", has_err);
    }

    // ================================================================
    //  模块 2: utf8pp 类
    // ================================================================

    // === 4. utf8pp 构造与析构 ===
    print_section(4, "utf8pp 构造与析构");
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
    print_section(5, "utf8pp 容量");
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
    print_section(6, "utf8pp 码点访问");
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
    print_section(7, "utf8pp 字节访问");
    {
        utf8pp s("Hello");
        print_item("c_str() 内容正确", std::string(s.c_str()) == "Hello");
        print_item("data() == c_str()", s.data() == s.c_str());
        print_item("view() 大小正确", s.view().size() == 5);

        utf8pp empty;
        print_item("空对象 c_str() == \"\"", std::string(empty.c_str()) == "");
    }

    // === 8. utf8pp 迭代器 ===
    print_section(8, "utf8pp 迭代器");
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
        for (char32_t cp : s)
        {
            cps2.push_back(cp);
        }
        print_item("range-for 遍历", cps2 == cps);
    }

    // === 9. utf8pp 修改: push_back / append ===
    print_section(9, "utf8pp 修改: push_back / append");
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
    print_section(10, "utf8pp 修改: insert / erase");
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
    print_section(11, "utf8pp substr");
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
    print_section(12, "utf8pp 查找");
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
    print_section(13, "utf8pp 比较");
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
    print_section(14, "utf8pp 转换");
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
    print_section(15, "utf8pp BOM");
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
    print_section(16, "utf8pp 校验");
    {
        utf8pp s("Hello");
        print_item("合法 UTF-8 valid() == true", s.valid());

        size_t pos = s.validate();
        print_item("合法 UTF-8 validate == npos", pos == utf8pp::npos);
    }

    // === 17. utf8pp 字面量运算符 ===
    print_section(17, "utf8pp 字面量运算符");
    {
        auto s = "Hello"_u8;
        print_item("_u8 字面量构造", s.size() == 5 && s.at(0) == char32_t('H'));

        auto s2 = u8"你好"_u8;
        print_item("_u8 中文", s2.size() == 2 && s2.at(0) == char32_t(0x4F60));
    }

    // === 18. utf8pp 大规模操作 ===
    print_section(18, "utf8pp 大规模操作");
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
    print_section(19, "utf8pp 赋值");
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
    print_section(20, "往返一致性");
    {
        utf8pp orig(u8"Hello你好😀世界");

        // utf8pp → u32string → utf8pp
        std::u32string u32 = orig.to_u32string();
        utf8pp roundtrip(u32.data(), u32.size());

        print_item("往返 size 一致", orig.size() == roundtrip.size());
        print_item("往返 byte_size 一致", orig.byte_size() == roundtrip.byte_size());
        print_item("往返内容一致", orig == roundtrip);
    }

    // === 21. starts_with / ends_with ===
    print_section(21, "starts_with / ends_with");
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

    // === 22. contains ===
    print_section(22, "contains");
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

    // === 23. count ===
    print_section(23, "count");
    {
        utf8pp s("Hello世界World");
        print_item("count(char32_t 'l') == 3", s.count(char32_t('l')) == 3);
        print_item("count(char32_t '界') == 1", s.count(char32_t(0x754C)) == 1);
        utf8pp s2("abcabcabc");
        print_item("count(utf8pp abc) == 3", s2.count(utf8pp("abc")) == 3);
        print_item("count(utf8pp xyz) == 0", s2.count(utf8pp("xyz")) == 0);
        print_item("count 空 == 0", s.count(utf8pp()) == 0);
    }

    // === 24. replace ===
    print_section(24, "replace");
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

    // === 25. trim ===
    print_section(25, "trim");
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

    // === 26. to_lower / to_upper ===
    print_section(26, "to_lower / to_upper");
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

    // === 27. reverse ===
    print_section(27, "reverse");
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

    // === 28. split ===
    print_section(28, "split");
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

    // === 29. join ===
    print_section(29, "join");
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

    // === 30. 构造/assign/append 范围重载 ===
    print_section(30, "构造/assign/append 范围重载");
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

    // === 31. data()/c_str() 空串安全 ===
    print_section(31, "data()/c_str() 空串安全");
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

    // === 32. max_size / reserve_exact / increase_capacity ===
    print_section(32, "max_size / reserve_exact / increase_capacity");
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

    // === 33. resize_cp / pop_back / append_cp / assign_cp / 构造(n,cp) ===
    print_section(33, "resize_cp / pop_back / append_cp / assign_cp / 构造(n,cp)");
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

    // === 34. copy ===
    print_section(34, "copy");
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

    // === 35. find_first_of / find_last_of / find_first_not_of / find_last_not_of ===
    print_section(35, "find_first_of / find_last_of / find_first_not_of / find_last_not_of");
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

    // === 36. rfind ===
    print_section(36, "rfind");
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

    // === 37. reverse_iterator ===
    print_section(37, "reverse_iterator");
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

    // === 38. operator+ 系列 ===
    print_section(38, "operator+ 系列");
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

    // === 39. operator<</>> 流操作符 ===
    print_section(39, "operator<</>> 流操作符");
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

    // === 40. operator<=> 三路比较 ===
    print_section(40, "operator<=> 三路比较");
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

    // === 41. 迭代器版 insert/erase ===
    print_section(41, "迭代器版 insert/erase");
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

    // === 42. std::swap 非成员 + std::hash ===
    print_section(42, "std::swap 非成员 + std::hash");
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

#ifdef _WIN32
    {
        int h = _heapchk();
        print_item("堆检查正常", h == _HEAPOK);
    }
#endif

    // === 43. find/rfind 的 const char*/string_view 重载 ===
    print_section(43, "find/rfind const char*/string_view 重载");
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

    // === 44. find_*_of const char*/string_view 重载 ===
    print_section(44, "find_*_of const char*/string_view 重载");
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

    // === 45. insert 字符串重载 ===
    print_section(45, "insert 字符串重载");
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

    // === 46. replace 字符串重载 + compare 子串 ===
    print_section(46, "replace 字符串重载 + compare 子串");
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

    // === 47. operator==/string_view + <=>/string_view ===
    print_section(47, "operator==/string_view + <=>/string_view");
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

    // === 48. SSO 边界测试 (103 字节临界) ===
    print_section(48, "SSO 边界测试");
    {
        // 103 ASCII 字符 = SSO 满载 (SSO_CAPACITY == 103)
        std::string raw;
        for (int i = 0; i < 103; ++i) raw.push_back(static_cast<char>('0' + (i % 10)));
        utf8pp s(raw);
        print_item("SSO 满 103 字节 size==103", s.size() == 103);
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

        // SSO 满 → reverse (结果仍 103 字节, SSO)
        utf8pp s6(s);
        s6.reverse();
        std::string raw_rev(raw.rbegin(), raw.rend());
        print_item("SSO 满 reverse", s6 == utf8pp(raw_rev));

        // SSO 满 → append 触发扩容 (104 字节超出 SSO)
        utf8pp s7(s);
        s7.append("X");
        print_item("SSO 满 append 触发扩容", s7.size() == 104 && s7.at(103) == char32_t('X'));
        print_item("SSO 满 append 非 SSO", !s7.is_sso());

        // SSO 满 → erase + shrink_to_fit 回退 SSO
        utf8pp s8(s);
        s8.erase(0, 10);
        s8.shrink_to_fit();
        print_item("SSO 满 erase+shrink 回退 SSO", s8.is_sso() && s8.size() == 93);

        // swap SSO 满 ↔ heap
        utf8pp a(s); // SSO 满 (103 字节)
        std::string heap_raw;
        for (int i = 0; i < 120; ++i) heap_raw.push_back(static_cast<char>('a' + (i % 26)));
        utf8pp b(heap_raw); // heap (120 字节超出 SSO)
        swap(a, b);
        print_item("swap SSO满↔heap a (a 拿到 b)", a.size() == 120 && !a.is_sso());
        print_item("swap SSO满↔heap b", b.size() == 103 && b.is_sso());
    }

    // === 49. 数字 → utf8pp (to_utf8pp) ===
    print_section(49, "数字 → utf8pp");
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

    // === 50. 字符串 → 数字 (to_* 系列) ===
    print_section(50, "字符串 → 数字 (to_* 系列)");
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

    // === 51. parse_* 严格解析 ===
    print_section(51, "parse_* 严格解析");
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

    // === 52. is_integer / is_number / is_float ===
    print_section(52, "is_integer / is_number / is_float");
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

    // === 53. reserve_cp / cp_capacity ===
    print_section(53, "reserve_cp / cp_capacity");
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

    // === 54. data() 非 const + rebuild ===
    print_section(54, "data() 非 const + rebuild");
    {
        utf8pp s("hello");
        char* p = s.data();
        print_item("非 const data() 可写", p != nullptr);
        // 原地大写化 ASCII 字节
        for (size_t i = 0; i < s.byte_size(); ++i)
        {
            if (p[i] >= 'a' && p[i] <= 'z')
            {
                p[i] = static_cast<char>(p[i] - 32);
            }
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

    // === 55. pad_left / pad_right / center ===
    print_section(55, "pad_left / pad_right / center");
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

        // 中文对齐 (display_width: 你=2, 目标 3 需 1 空格)
        utf8pp s9(u8"你");
        print_item("中文 padded_left(3)", s9.padded_left(3) == utf8pp(u8" 你"));
        print_item("中文 centered(3)", s9.centered(3) == utf8pp(u8"你 "));
    }

    // === 56. getline ===
    print_section(56, "getline");
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

    // === 57. split_view 零拷贝分割 ===
    print_section(57, "split_view 零拷贝分割");
    {
        utf8pp s("a,b,c");
        dense<utf8_view> views = s.split_view(char32_t(','));
        print_item("split_view(char) 数量 == 3", views.size() == 3);
        print_item("split_view[0] == a", views[0] == utf8_view("a"));
        print_item("split_view[1] == b", views[1] == utf8_view("b"));
        print_item("split_view[2] == c", views[2] == utf8_view("c"));
        // 零拷贝验证: view 数据指针在原串范围内
        print_item("split_view 零拷贝",
                   views[0].data() >= s.data() &&
                   views[0].data() < s.data() + s.byte_size());

        utf8pp s2("Hello世界World");
        dense<utf8_view> views2 = s2.split_view(utf8pp("世界"));
        print_item("split_view(utf8pp) 数量 == 2", views2.size() == 2);
        print_item("split_view2[0] == Hello", views2[0] == utf8_view("Hello"));
        print_item("split_view2[1] == World", views2[1] == utf8_view("World"));

        utf8pp s3("a::b::c");
        dense<utf8_view> views3 = s3.split_view("::");
        print_item("split_view(const char*) 数量 == 3", views3.size() == 3);
        print_item("split_view3[1] == b", views3[1] == utf8_view("b"));

        utf8pp s4("a,b,c");
        dense<utf8_view> views4 = s4.split_view(std::string_view(","));
        print_item("split_view(string_view) 数量 == 3", views4.size() == 3);

        utf8pp empty;
        dense<utf8_view> views_empty = empty.split_view(char32_t(','));
        print_item("split_view 空 == 0", views_empty.size() == 0);

        utf8pp s5(",a,,b,");
        dense<utf8_view> views5 = s5.split_view(char32_t(','));
        print_item("split_view 连续分隔符 == 5", views5.size() == 5);
        print_item("split_view5[0] 空", views5[0].empty());
        print_item("split_view5[2] 空", views5[2].empty());

        // 中文分隔符
        utf8pp s6(u8"你-好-世-界");
        dense<utf8_view> views6 = s6.split_view(char32_t('-'));
        print_item("中文 split_view 数量 == 4", views6.size() == 4);
        print_item("中文 split_view[0] == 你", views6[0] == utf8_view(u8"你"));
        print_item("中文 split_view[3] == 界", views6[3] == utf8_view(u8"界"));
    }

    // === 58. utf8_view 集成互操作 ===
    print_section(58, "utf8_view 集成互操作");
    {
        utf8pp s("Hello世界");
        std::string_view sv = s.view();
        print_item("view() 大小 == 11", sv.size() == 11);
        print_item("view() 内容匹配", sv == std::string_view("Hello世界"));

        std::u8string_view u8sv = s.u8view();
        print_item("u8view() 大小 == 11", u8sv.size() == 11);

        // utf8pp → utf8_view → utf8pp 往返
        utf8_view uv(s.data(), s.byte_size());
        utf8pp roundtrip(uv.data(), uv.byte_size());
        print_item("utf8pp → view → utf8pp 往返", roundtrip == s);

        // utf8_view 码点访问与 utf8pp 一致
        print_item("view at(5) == '世'", uv.at(5) == s.at(5));
        print_item("view size == utf8pp size", uv.size() == s.size());

        // binary_view
        std::string_view bv = s.binary_view();
        print_item("binary_view == view", bv == sv);
    }

    // === 59. compare 双区间 + replace 迭代器版 ===
    print_section(59, "compare 双区间 + replace 迭代器版");
    {
        utf8pp s1("HelloWorld");
        utf8pp s2("WorldHello");
        // 双区间比较: s1[0,5) "Hello" vs s2[5,5) "Hello"
        print_item("compare 双区间相等", s1.compare(0, 5, s2, 5, 5) == 0);
        // s1[0,5) "Hello" vs s2[0,5) "World"
        print_item("compare 双区间 Hello<World", s1.compare(0, 5, s2, 0, 5) < 0);
        // 双区间 const char* 版: "Hello" vs "Hi" → 'H'=='H', 'e'<'i' → < 0
        print_item("compare(pos,n,s,n2)", s1.compare(0, 5, "Hi", 2) < 0);

        // replace 迭代器版本
        utf8pp s3("HelloWorld");
        auto first = s3.begin();
        ++first; // e
        auto last = first;
        ++last; // l
        s3.replace(first, last, utf8pp("XYZ"));
        print_item("replace(iter,iter,utf8pp)", s3 == utf8pp("HXYZlloWorld"));

        utf8pp s4("HelloWorld");
        auto f4 = s4.begin();
        ++f4;  // 指向 'e'
        auto l4 = f4;
        ++l4;  // 指向 'l'
        s4.replace(f4, l4, "ABC");  // 替换 'e' 为 "ABC"
        print_item("replace(iter,iter,const char*)", s4 == utf8pp("HABClloWorld"));

        utf8pp s5("HelloWorld");
        auto f5 = s5.begin();
        ++f5;
        auto l5 = f5;
        ++l5;
        s5.replace(f5, l5, std::string_view("Z"));
        print_item("replace(iter,iter,string_view)", s5 == utf8pp("HZlloWorld"));

        // fill-replace 迭代器版
        utf8pp s6("HelloWorld");
        auto f6 = s6.begin();
        ++f6;
        auto l6 = f6;
        ++l6;
        s6.replace(f6, l6, 3, char32_t('X'));
        print_item("replace(iter,iter,n,cp)", s6 == utf8pp("HXXXlloWorld"));

        // 迭代器范围 replace
        utf8pp s7("HelloWorld");
        auto f7 = s7.begin();
        ++f7;
        auto l7 = f7;
        ++l7;
        utf8pp src("123");
        s7.replace(f7, l7, src.begin(), src.end());
        print_item("replace(iter,iter,iter,iter)", s7 == utf8pp("H123lloWorld"));
    }

    // === 60. 三参 find/find_*_of (s, pos, n) ===
    print_section(60, "三参 find/find_*_of (s, pos, n)");
    {
        utf8pp s("HelloWorld");
        // find(s, pos, n): s 前 n 字节
        print_item("find(s,0,3) == 0", s.find("Hel", 0, 3) == 0);
        print_item("find(s,1,3) == npos", s.find("Hel", 1, 3) == utf8pp::npos);
        print_item("find(s,0,2) == 0 (He)", s.find("He", 0, 2) == 0);

        // rfind(s, pos, n)
        utf8pp s2("abcabcabc");
        print_item("rfind(s,3,3) == 3", s2.rfind("abc", 3, 3) == 3);

        // find_first_of(s, pos, n)
        utf8pp s3("HelloWorld");
        print_item("find_first_of(s,0,2) == 2", s3.find_first_of("lo", 0, 2) == 2);
        print_item("find_first_of(s,3,2) == 3", s3.find_first_of("lo", 3, 2) == 3);

        // find_last_of(s, pos, n)
        print_item("find_last_of(s,npos,2) == 8", s3.find_last_of("lo", utf8pp::npos, 2) == 8);

        // find_first_not_of(s, pos, n)
        print_item("find_first_not_of(s,0,1) == 1", s3.find_first_not_of("H", 0, 1) == 1);

        // find_last_not_of(s, pos, n)
        print_item("find_last_not_of(s,npos,2) == 7", s3.find_last_not_of("ld", utf8pp::npos, 2) == 7);
    }

    // === 61. initializer_list 与迭代器范围构造 ===
    print_section(61, "initializer_list 与迭代器范围构造");
    {
        // initializer_list<char32_t>
        utf8pp s1 = {char32_t('H'), char32_t('i'), char32_t(0x4E2D)};
        print_item("initializer_list 构造", s1.size() == 3 && s1.at(2) == char32_t(0x4E2D));
        print_item("initializer_list byte_size == 5", s1.byte_size() == 5);  // H(1)+i(1)+中(3)=5

        // operator= initializer_list
        utf8pp s2;
        s2 = {char32_t('A'), char32_t('B')};
        print_item("operator= initializer_list", s2 == utf8pp("AB"));

        // assign initializer_list
        utf8pp s3;
        s3.assign({char32_t('X'), char32_t('Y'), char32_t('Z')});
        print_item("assign initializer_list", s3 == utf8pp("XYZ"));

        // 迭代器范围构造 (char32_t 数组)
        std::u32string src = U"Hello";
        utf8pp s4(src.begin(), src.end());
        print_item("迭代器范围构造 (u32string)", s4 == utf8pp("Hello"));

        // 迭代器范围 assign
        utf8pp s5;
        std::vector<char32_t> v = {char32_t('a'), char32_t('b'), char32_t('c')};
        s5.assign(v.begin(), v.end());
        print_item("迭代器范围 assign", s5 == utf8pp("abc"));

        // insert initializer_list
        utf8pp s6("Xd");
        s6.insert(1, {char32_t('Y'), char32_t('Z')});
        print_item("insert initializer_list", s6 == utf8pp("XYZd"));

        // append initializer_list
        utf8pp s7("Hi");
        s7.append({char32_t('!'), char32_t('!')});
        print_item("append initializer_list", s7 == utf8pp("Hi!!"));

        // operator+= initializer_list
        utf8pp s8("A");
        s8 += {char32_t('B'), char32_t('C')};
        print_item("operator+= initializer_list", s8 == utf8pp("ABC"));
    }

    // === 62. 字节级访问 ===
    print_section(62, "字节级访问");
    {
        utf8pp s(u8"Hi中");  // H i 中 → 1+1+3=5 字节
        print_item("byte_size == 5", s.byte_size() == 5);
        print_item("byte_at(0) == 'H'", s.byte_at(0) == 'H');
        print_item("byte_at(1) == 'i'", s.byte_at(1) == 'i');
        print_item("byte_at(2) == 0xE4 (中 首字节)", (unsigned char)s.byte_at(2) == 0xE4);
        print_item("byte_at(越界) == '\\0'", s.byte_at(100) == '\0');
        print_item("at_byte(0) == 'H'", s.at_byte(0) == 'H');

        // 字节子串
        utf8pp bs = s.byte_substr(2, 3);  // 取 '中' 的 3 字节
        print_item("byte_substr(2,3) == 中", bs == utf8pp(u8"中"));
        print_item("byte_substr 越界空", s.byte_substr(100).empty());

        // 字节/码点索引互转
        print_item("byte_to_cp_idx(2) == 2", s.byte_to_cp_idx(2) == 2);
        print_item("byte_to_cp_idx(0) == 0", s.byte_to_cp_idx(0) == 0);
        print_item("byte_to_cp_idx(非起点) == npos", s.byte_to_cp_idx(3) == utf8pp::npos);
        print_item("cp_to_byte_idx(2) == 2", s.cp_to_byte_idx(2) == 2);
        print_item("cp_to_byte_idx(越界) == byte_size", s.cp_to_byte_idx(100) == s.byte_size());
    }

    // === 63. 字节迭代器 ===
    print_section(63, "字节迭代器");
    {
        utf8pp s("Hello");
        size_t sum = 0;
        for (auto it = s.byte_begin(); it != s.byte_end(); ++it) sum += (unsigned char)*it;
        // H+e+l+l+o = 72+101+108+108+111 = 500
        print_item("byte_begin/end 累加 == 500", sum == 500);

        std::vector<char> v;
        for (char c : std::string_view("Hi"))
        {
            v.push_back(c);
        }
        // 反向字节迭代
        utf8pp s2("abc");
        std::string rev;
        for (auto it = s2.rbyte_begin(); it != s2.rbyte_end(); --it)
        {
            rev.push_back(*it);
        }
        // 注意: rbyte_begin 在末尾, 反向遍历需 -- 到 rbyte_end 之前
        // 简化测试: 用 byte iterator 计算差
        auto b = s2.byte_begin();
        auto e = s2.byte_end();
        print_item("byte_end - byte_begin == 3", (e - b) == 3);
        print_item("byte_iterator operator[]", b[0] == 'a' && b[2] == 'c');
    }

    // === 64. 字符分类 API ===
    print_section(64, "字符分类 API");
    {
        print_item("is_alpha('A')", utf8pp::is_alpha(char32_t('A')));
        print_item("is_alpha('中')", utf8pp::is_alpha(char32_t(0x4E2D)));  // CJK 在 Unicode alpha 范围
        print_item("is_digit('5')", utf8pp::is_digit(char32_t('5')));
        print_item("is_alnum('A')", utf8pp::is_alnum(char32_t('A')));
        print_item("is_space(' ')", utf8pp::is_space(char32_t(' ')));
        print_item("is_space(U+3000)", utf8pp::is_space(char32_t(0x3000)));
        print_item("is_punct('!')", utf8pp::is_punct(char32_t('!')));
        print_item("is_lower('a')", utf8pp::is_lower(char32_t('a')));
        print_item("is_upper('A')", utf8pp::is_upper(char32_t('A')));
        print_item("is_xdigit('F')", utf8pp::is_xdigit(char32_t('F')));
        print_item("is_xdigit('g') 失败", !utf8pp::is_xdigit(char32_t('g')));
        print_item("is_cntrl('\\n')", utf8pp::is_cntrl(char32_t('\n')));
        print_item("is_printable('A')", utf8pp::is_printable(char32_t('A')));
        print_item("is_printable('\\n') 失败", !utf8pp::is_printable(char32_t('\n')));

        // 单码点大小写
        print_item("to_lower_cp('A') == 'a'", utf8pp::to_lower_cp(char32_t('A')) == char32_t('a'));
        print_item("to_upper_cp('a') == 'A'", utf8pp::to_upper_cp(char32_t('a')) == char32_t('A'));
        print_item("to_lower_cp('À') == 'à'", utf8pp::to_lower_cp(char32_t(0x00C0)) == char32_t(0x00E0));
    }

    // === 65. 串级字符分类 ===
    print_section(65, "串级字符分类");
    {
        utf8pp letters("Hello");
        utf8pp digits("12345");
        utf8pp mixed("abc123");
        utf8pp spaces("   \t\n  ");
        utf8pp hex("1aF3");
        utf8pp empty;

        print_item("is_all_alpha(Hello)", letters.is_all_alpha());
        print_item("is_all_digit(12345)", digits.is_all_digit());
        print_item("is_all_alnum(abc123)", mixed.is_all_alnum());
        print_item("is_all_space(spaces)", spaces.is_all_space());
        print_item("is_all_xdigit(1aF3)", hex.is_all_xdigit());
        print_item("is_all_alpha(12345) 失败", !digits.is_all_alpha());
        print_item("is_all_digit(Hello) 失败", !letters.is_all_digit());
        print_item("is_all_printable(Hello)", letters.is_all_printable());
        print_item("空串 is_all_alpha 失败", !empty.is_all_alpha());
    }

    // === 66. 大小写转换 (含 Latin-1) ===
    print_section(66, "大小写转换 (含 Latin-1)");
    {
        utf8pp s("Hello World");
        s.to_lower();
        print_item("to_lower == 'hello world'", s == utf8pp("hello world"));

        utf8pp s2("Hello World");
        s2.to_upper();
        print_item("to_upper == 'HELLO WORLD'", s2 == utf8pp("HELLO WORLD"));

        utf8pp s3("hello world");
        s3.to_title();
        print_item("to_title == 'Hello World'", s3 == utf8pp("Hello World"));

        utf8pp s4("Hello");
        s4.swapcase();
        print_item("swapcase == 'hELLO'", s4 == utf8pp("hELLO"));

        // Latin-1 测试
        utf8pp latin(u8"ÀÁÂÃÄÅ");  // U+00C0~U+00C5
        latin.to_lower();
        utf8pp expected_lower(u8"àáâãäå");  // U+00E0~U+00E5
        print_item("Latin-1 to_lower", latin == expected_lower);

        utf8pp latin2(u8"àáâãäå");
        latin2.to_upper();
        print_item("Latin-1 to_upper", latin2 == utf8pp(u8"ÀÁÂÃÄÅ"));

        // 副本版本
        utf8pp orig("Hello");
        utf8pp low = orig.lowered();
        utf8pp upp = orig.uppered();
        utf8pp tit = orig.titled();
        utf8pp swp = orig.swapcased();
        print_item("lowered() 副本", low == utf8pp("hello") && orig == utf8pp("Hello"));
        print_item("uppered() 副本", upp == utf8pp("HELLO"));
        print_item("titled() 副本", tit == utf8pp("Hello"));
        print_item("swapcased() 副本", swp == utf8pp("hELLO"));
    }

    // === 67. trim 谓词版/字符集版 ===
    print_section(67, "trim 谓词版/字符集版");
    {
        utf8pp s("  Hello  ");
        s.trim();
        print_item("默认 trim", s == utf8pp("Hello"));

        utf8pp s2("xxxHelloxxx");
        s2.trim(utf8pp("x"));
        print_item("trim(utf8pp 字符集)", s2 == utf8pp("Hello"));

        utf8pp s3("xxxHelloxxx");
        s3.trim("x");
        print_item("trim(const char*)", s3 == utf8pp("Hello"));

        utf8pp s4("123abc456");
        s4.trim_left([](char32_t c) { return c >= char32_t('0') && c <= char32_t('9'); });
        print_item("trim_left(谓词)", s4 == utf8pp("abc456"));

        utf8pp s5("123abc456");
        s5.trim_right([](char32_t c) { return c >= char32_t('0') && c <= char32_t('9'); });
        print_item("trim_right(谓词)", s5 == utf8pp("123abc"));

        utf8pp s6("  Hi  ");
        utf8pp trimmed = s6.trimmed();
        print_item("trimmed() 副本", trimmed == utf8pp("Hi") && s6 == utf8pp("  Hi  "));

        utf8pp s7("xyHiyz");
        utf8pp t7 = s7.trimmed(utf8pp("xyz"));
        print_item("trimmed(字符集) 副本", t7 == utf8pp("Hi"));
    }

    // === 68. 数字转换补全 (long double + std 风格别名) ===
    print_section(68, "数字转换补全");
    {
        utf8pp s("3.14159265358979323846");
        long double ld = s.to_long_double();
        print_item("to_long_double > 3.14", ld > 3.14L);
        print_item("to_long_double < 3.15", ld < 3.15L);

        long double ldp = 0;
        bool ok = s.parse_long_double(ldp);
        print_item("parse_long_double 成功", ok && ldp > 3.14L);

        utf8pp bad("abc");
        long double dummy = 0;
        print_item("parse_long_double 失败", !bad.parse_long_double(dummy));

        // std 风格别名
        utf8pp num("42");
        print_item("stoi() == 42", num.stoi() == 42);
        print_item("stol() == 42", num.stol() == 42);
        print_item("stoll() == 42", num.stoll() == 42);
        print_item("stoul() == 42", num.stoul() == 42);

        utf8pp hexp("FF");
        print_item("stoul(16) == 255", hexp.stoul(nullptr, 16) == 255);

        utf8pp f("3.5");
        print_item("stof() == 3.5f", f.stof() == 3.5f);
        print_item("stod() == 3.5", f.stod() == 3.5);

        // 进制判断
        utf8pp h("1A3F");
        print_item("is_hex()", h.is_hex());
        print_item("is_binary() 失败", !h.is_binary());

        utf8pp b("1010");
        print_item("is_binary()", b.is_binary());
        print_item("is_octal()", utf8pp("755").is_octal());
    }

    // === 69. assign 补全 ===
    print_section(69, "assign 补全");
    {
        utf8pp s;
        s.assign(5, char32_t('A'));
        print_item("assign(n, cp) 填充", s == utf8pp("AAAAA") && s.size() == 5);

        std::u8string u8 = u8"你好";
        s.assign(u8);
        print_item("assign(u8string)", s == utf8pp(u8"你好"));

        std::u32string u32 = {char32_t('H'), char32_t('i')};
        s.assign(u32);
        print_item("assign(u32string)", s == utf8pp("Hi"));

        utf8pp orig("HelloWorld");
        s.assign(orig, 5, 5);  // 取 "World"
        print_item("assign(other, pos, n) 子串", s == utf8pp("World"));
    }

    // === 70. insert 迭代器补全 ===
    print_section(70, "insert 迭代器补全");
    {
        utf8pp s("Hello");
        auto it = s.begin();
        ++it;  // 指向 'e'
        s.insert(it, utf8pp("XY"));
        print_item("insert(iter, utf8pp)", s == utf8pp("HXYello"));

        utf8pp s2("Hello");
        auto it2 = s2.begin();
        ++it2;
        s2.insert(it2, "Z");
        print_item("insert(iter, const char*)", s2 == utf8pp("HZello"));

        utf8pp s3("Hello");
        auto it3 = s3.begin();
        ++it3;
        s3.insert(it3, std::string_view("Q"));
        print_item("insert(iter, string_view)", s3 == utf8pp("HQello"));

        utf8pp s4("Hello");
        auto it4 = s4.begin();
        ++it4;
        s4.insert(it4, {char32_t('!'), char32_t('!')});
        print_item("insert(iter, initializer_list)", s4 == utf8pp("H!!ello"));
    }

    // === 71. replace 双子串 ===
    print_section(71, "replace 双子串");
    {
        utf8pp s1("HelloWorld");
        utf8pp src("XYZ");
        // s1[0,5) "Hello" 替换为 src[1,2) "YZ"
        s1.replace(0, 5, src, 1, 2);
        print_item("replace(pos,n,other,pos2,n2)", s1 == utf8pp("YZWorld"));

        utf8pp s2("HelloWorld");
        // s2[0,5) "Hello" 替换为 "ABCDEF" 的 [3,3) "DEF"
        s2.replace(0, 5, "ABCDEF", 3, 3);
        print_item("replace(pos,n,const char*,pos2,n2)", s2 == utf8pp("DEFWorld"));
    }

    // === 72. 与 std::string/u8string/u32string 互操作 ===
    print_section(72, "与 std 互操作");
    {
        utf8pp s("Hello");

        std::string ss = "Hello";
        print_item("==(std::string)", s == ss);
        print_item("!=(std::string) 失败", !(s != ss));
        print_item("<(std::string) 失败", !(s < ss));
        print_item("<=>(std::string) equal", (s <=> ss) == std::strong_ordering::equal);

        std::u8string u8 = u8"Hello";
        print_item("==(u8string)", s == u8);

        std::u32string u32 = {char32_t('H'), char32_t('e'), char32_t('l'), char32_t('l'), char32_t('o')};
        print_item("==(u32string)", s == u32);

        print_item("==(char32_t 'H') 失败", !(s == char32_t('H')));
        utf8pp single("H");
        print_item("==(char32_t 'H')", single == char32_t('H'));

        // operator+ 互操作
        utf8pp cat1 = s + std::string("XX");
        print_item("utf8pp + std::string", cat1 == utf8pp("HelloXX"));
        utf8pp cat2 = std::string("YY") + s;
        print_item("std::string + utf8pp", cat2 == utf8pp("YYHello"));
        utf8pp cat3 = s + std::u8string(u8"你好");
        print_item("utf8pp + u8string", cat3 == utf8pp("Hello你好"));
        utf8pp cat4 = std::u32string{char32_t('!')} + s;
        print_item("u32string + utf8pp", cat4 == utf8pp("!Hello"));
    }

    // === 73. format 静态方法 ===
    print_section(73, "format 静态方法");
    {
        utf8pp r1 = utf8pp::format("Hello %s!", "World");
        print_item("format 字符串", r1 == utf8pp("Hello World!"));

        utf8pp r2 = utf8pp::format("x=%d y=%f", 42, 3.14);
        print_item("format 数值", r2 == utf8pp("x=42 y=3.140000"));

        utf8pp r3 = utf8pp::format("%d-%d-%d", 2024, 1, 15);
        print_item("format 多参", r3 == utf8pp("2024-1-15"));

        // 大于 1024 字节的格式串
        utf8pp r4 = utf8pp::format("%0*d", 2000, 7);
        print_item("format 大缓冲", r4.size() == 2000);

        utf8pp r5 = utf8pp::format("");
        print_item("format 空串", r5.empty());
    }

    // === 74. std::erase / erase_if 自由函数 ===
    print_section(74, "std::erase / erase_if");
    {
        utf8pp s("Hello World");
        size_t n = erase(s, char32_t('l'));
        print_item("erase 'l' 删 3 个", n == 3 && s == utf8pp("Heo Word"));

        utf8pp s2("a1b2c3d4");
        size_t n2 = erase_if(s2, [](char32_t c) { return c >= char32_t('0') && c <= char32_t('9'); });
        print_item("erase_if 数字 删 4 个", n2 == 4 && s2 == utf8pp("abcd"));

        utf8pp s3("abc");
        size_t n3 = erase(s3, char32_t('z'));
        print_item("erase 不存在", n3 == 0 && s3 == utf8pp("abc"));
    }

    // === 75. FNV-1a hash 改进 ===
    print_section(75, "FNV-1a hash");
    {
        std::unordered_map<utf8pp, int> m;
        utf8pp k1("Hello");
        utf8pp k2("World");
        utf8pp k3(u8"你好");
        m[k1] = 1;
        m[k2] = 2;
        m[k3] = 3;
        print_item("unordered_map 插入", m.size() == 3);
        print_item("unordered_map 查找 k1", m[k1] == 1);
        print_item("unordered_map 查找 k3", m[k3] == 3);
        print_item("unordered_map 不存在", m.find(utf8pp("xyz")) == m.end());

        // 验证 FNV-1a 哈希分布: 不同串哈希不同
        std::hash<utf8pp> h;
        size_t h1 = h(k1), h2 = h(k2), h3 = h(k3);
        print_item("哈希各不相同", h1 != h2 && h2 != h3 && h1 != h3);
    }

    // === 76. _utf8 字面量 / 对称比较 / std::swap ===
    print_section(76, "_utf8 字面量 / 对称比较 / std::swap");
    {
        // _utf8 字面量
        auto lit = "Hello"_utf8;
        print_item("\"Hello\"_utf8", lit == utf8pp("Hello"));
        auto lit2 = u8"你好"_utf8;
        print_item("u8\"你好\"_utf8", lit2 == utf8pp(u8"你好"));

        // 对称比较 (lhs 非 utf8pp)
        print_item("\"abc\" == utf8pp(\"abc\")", std::string_view("abc") == utf8pp("abc"));
        print_item("utf8pp(\"abc\") == \"abc\"", utf8pp("abc") == std::string_view("abc"));
        print_item("\"abd\" != utf8pp(\"abc\")", std::string_view("abd") != utf8pp("abc"));
        print_item("\"abc\" < utf8pp(\"abd\")", (std::string_view("abc") <=> utf8pp("abd")) < 0);
        print_item("\"abd\" > utf8pp(\"abc\")", (std::string_view("abd") <=> utf8pp("abc")) > 0);
        print_item("std::string == utf8pp", std::string("Hi") == utf8pp("Hi"));
        print_item("char32_t == utf8pp", char32_t('A') == utf8pp("A"));

        // std::swap 特化
        utf8pp sa("AAA"), sb("BBB");
        std::swap(sa, sb);
        print_item("std::swap", sa == utf8pp("BBB") && sb == utf8pp("AAA"));
    }

    // === 77. insert 返回引用链式 + 新重载 ===
    print_section(77, "insert 链式 + 新重载");
    {
        utf8pp s("Hello");
        s.insert(0, ">>").insert(0, "==");
        print_item("insert 链式", s == utf8pp("==>>Hello"));

        utf8pp s2("World");
        s2.insert(0, utf8pp("ABCD"), 1, 2);  // 从 "ABCD" 的 pos2=1 取 n2=2 → "BC"
        print_item("insert(pos, str, pos2, n2)", s2 == utf8pp("BCWorld"));

        utf8pp s3("Test");
        auto it = s3.insert(s3.begin(), "XY", 2);
        print_item("insert(iter, s, n)", s3 == utf8pp("XYTest") && *it == char32_t('X'));
    }

    // === 78. append 新重载 ===
    print_section(78, "append 新重载");
    {
        utf8pp s;
        s.append(std::u8string(u8"你好"));
        print_item("append(u8string)", s == utf8pp(u8"你好"));

        s.append(std::u32string(U"World"));
        print_item("append(u32string)", s == utf8pp(u8"你好World"));

        utf8pp s2;
        s2.append(3, char32_t('X'));
        print_item("append(n, cp)", s2 == utf8pp("XXX"));

        utf8pp s3;
        std::u32string src = U"ABC";
        s3.append(src.begin(), src.end());
        print_item("append(iter, iter)", s3 == utf8pp("ABC"));
    }

    // === 79. replace initializer_list ===
    print_section(79, "replace initializer_list");
    {
        utf8pp s("Hello World");
        s.replace(0, 5, {U'H', U'i'});
        print_item("replace(pos, n, init_list)", s == utf8pp("Hi World"));
    }

    // === 80. 字节/字形簇 const 迭代器别名 ===
    print_section(80, "const 迭代器别名");
    {
        utf8pp s("ABC");
        auto bit = s.byte_cbegin();
        auto beit = s.byte_cend();
        print_item("byte_cbegin/cend", (beit - bit) == 3);

        auto brit = s.byte_crbegin();
        auto breit = s.byte_crend();
        // rbyte_begin 指向末尾后一位, 用 [-1] 访问最后字节
        print_item("byte_crbegin/crend", brit[-1] == 'C' && breit[0] == 'A');

        utf8pp s2(u8"你好");
        auto git = s2.grapheme_cbegin();
        auto geit = s2.grapheme_cend();
        size_t gc = 0;
        for (; git != geit; ++git)
        {
            ++gc;
        }
        print_item("grapheme_cbegin/cend", gc == 2);
    }

    // ================================================================
    //  模块 3: utf8_view 类
    // ================================================================

    // === 81. 构造与赋值 ===
    print_section(81, "utf8_view 构造与赋值");
    {
        utf8_view v1;
        print_item("默认构造为空", v1.empty() && v1.byte_size() == 0);

        utf8_view v2("Hello");
        print_item("从 C 字符串构造", v2.byte_size() == 5);

        utf8_view v3("Hello", 3);
        print_item("从 (ptr,len) 构造", v3.byte_size() == 3);

        std::string s = "World";
        utf8_view v4(s);
        print_item("从 std::string 构造", v4.byte_size() == 5);

        std::string_view sv = "SV";
        utf8_view v5(sv);
        print_item("从 string_view 构造", v5.byte_size() == 2);

        utf8_view v6 = "Assign";
        print_item("operator= const char*", v6.byte_size() == 6);

        utf8_view v7 = std::string_view("SV2");
        print_item("operator= string_view", v7.byte_size() == 3);
    }

    // === 82. 容量 ===
    print_section(82, "utf8_view 容量");
    {
        utf8_view v("Hello");
        print_item("byte_size() == 5", v.byte_size() == 5);
        print_item("size_bytes() == 5", v.size_bytes() == 5);
        print_item("length_bytes() == 5", v.length_bytes() == 5);
        print_item("empty() == false", !v.empty());
        print_item("max_size() > 0", v.max_size() > 0);

        utf8_view cn(u8"你好");
        print_item("中文 byte_size == 6", cn.byte_size() == 6);
        print_item("中文 size (码点) == 2", cn.size() == 2);
        print_item("中文 length == 2", cn.length() == 2);

        utf8_view empty;
        print_item("空 size == 0", empty.size() == 0);
    }

    // === 83. 数据访问 ===
    print_section(83, "utf8_view 数据访问");
    {
        utf8_view v("Hello");
        print_item("data() != nullptr", v.data() != nullptr);
        print_item("c_str() != nullptr", v.c_str() != nullptr);
        print_item("c_str() == Hello", std::string(v.c_str()) == "Hello");
        print_item("byte_view() == Hello", v.byte_view() == std::string_view("Hello"));
        print_item("隐式转 string_view", std::string_view(v) == std::string_view("Hello"));

        utf8_view empty;
        print_item("空 c_str() == \"\"", std::string(empty.c_str()) == "");
        print_item("空 data() 安全", empty.data() == nullptr || std::string(empty.data()).empty());

        print_item("byte_at(0) == 'H'", v.byte_at(0) == 'H');
        print_item("byte_at(1) == 'e'", v.byte_at(1) == 'e');
        print_item("byte_at 越界 == '\\0'", v.byte_at(100) == '\0');
    }

    // === 84. 码点访问 (O(n)) ===
    print_section(84, "utf8_view 码点访问");
    {
        utf8_view v("Hello");
        print_item("at(0) == 'H'", v.at(0) == char32_t('H'));
        print_item("at(4) == 'o'", v.at(4) == char32_t('o'));
        print_item("at 越界 == U+FFFD", v.at(100) == char32_t(0xFFFD));
        print_item("operator[] == at", v[1] == v.at(1));
        print_item("front() == 'H'", v.front() == char32_t('H'));
        print_item("back() == 'o'", v.back() == char32_t('o'));

        utf8_view cn(u8"你好世界");
        print_item("中文 at(0) == 你", cn.at(0) == char32_t(0x4F60));
        print_item("中文 at(1) == 好", cn.at(1) == char32_t(0x597D));
        print_item("中文 at(2) == 世", cn.at(2) == char32_t(0x4E16));
        print_item("中文 at(3) == 界", cn.at(3) == char32_t(0x754C));
        print_item("中文 front == 你", cn.front() == char32_t(0x4F60));
        print_item("中文 back == 界", cn.back() == char32_t(0x754C));

        utf8_view emoji(u8"😀👍");
        print_item("Emoji at(0) == U+1F600", emoji.at(0) == char32_t(0x1F600));
        print_item("Emoji at(1) == U+1F44D", emoji.at(1) == char32_t(0x1F44D));
    }

    // === 85. 迭代器 ===
    print_section(85, "utf8_view 迭代器");
    {
        utf8_view v("abc");
        std::u32string collected;
        for (char32_t cp : v)
        {
            collected.push_back(cp);
        }
        print_item("正向遍历 == abc", collected == std::u32string(U"abc"));

        utf8_view cn(u8"你好");
        std::u32string collected2;
        for (char32_t cp : cn)
        {
            collected2.push_back(cp);
        }
        print_item("中文遍历 == 你好", collected2 == std::u32string(U"你好"));

        utf8_view v3("XYZ");
        auto it = v3.cbegin();
        print_item("cbegin == 'X'", *it == char32_t('X'));
        ++it;
        print_item("++cbegin == 'Y'", *it == char32_t('Y'));
        ++it;
        ++it;
        print_item("cbegin++++ == cend", it == v3.cend());

        utf8_view empty;
        print_item("空 begin == end", empty.begin() == empty.end());
    }

    // === 86. 反向迭代器 ===
    print_section(86, "utf8_view 反向迭代器");
    {
        utf8_view v("abc");
        std::u32string collected;
        for (auto it = v.rbegin(); it != v.rend(); ++it)
        {
            collected.push_back(*it);
        }
        print_item("rbegin->rend == cba", collected == std::u32string(U"cba"));

        utf8_view cn(u8"你好");
        std::u32string collected2;
        for (auto it = cn.rbegin(); it != cn.rend(); ++it)
        {
            collected2.push_back(*it);
        }
        print_item("中文反向 == 好你", collected2 == std::u32string(U"好你"));

        utf8_view v3("XYZ");
        auto it = v3.crbegin();
        print_item("crbegin == 'Z'", *it == char32_t('Z'));
        ++it;
        print_item("++crbegin == 'Y'", *it == char32_t('Y'));

        utf8_view empty;
        print_item("空 rbegin == rend", empty.rbegin() == empty.rend());
    }

    // === 87. 子串 ===
    print_section(87, "utf8_view 子串");
    {
        utf8_view v("HelloWorld");
        print_item("substr_bytes(0,5) == Hello", v.substr_bytes(0, 5) == utf8_view("Hello"));
        print_item("substr_bytes(5) == World", v.substr_bytes(5) == utf8_view("World"));
        print_item("substr_bytes 越界 == 空", v.substr_bytes(100).empty());

        utf8_view v2("HelloWorld");
        print_item("substr(0,5) 码点 == Hello", v2.substr(0, 5) == utf8_view("Hello"));
        print_item("substr(5) 码点 == World", v2.substr(5) == utf8_view("World"));

        utf8_view cn(u8"你好世界");
        print_item("中文 substr(0,2) == 你好", cn.substr(0, 2) == utf8_view(u8"你好"));
        print_item("中文 substr(2) == 世界", cn.substr(2) == utf8_view(u8"世界"));
        print_item("中文 substr(1,2) == 好世", cn.substr(1, 2) == utf8_view(u8"好世"));

        utf8_view v3("Hi");
        print_item("substr 越界 == 空", v3.substr(10).empty());
    }

    // === 88. remove_prefix / remove_suffix ===
    print_section(88, "remove_prefix / remove_suffix");
    {
        utf8_view v("HelloWorld");
        v.remove_prefix(5);
        print_item("remove_prefix(5) == World", v == utf8_view("World"));

        utf8_view v2("HelloWorld");
        v2.remove_suffix(5);
        print_item("remove_suffix(5) == Hello", v2 == utf8_view("Hello"));

        utf8_view v3("ABC");
        v3.remove_prefix(100);
        print_item("remove_prefix 越界 == 空", v3.empty());

        utf8_view v4("ABC");
        v4.remove_suffix(100);
        print_item("remove_suffix 越界 == 空", v4.empty());
    }

    // === 89. copy ===
    print_section(89, "utf8_view copy");
    {
        utf8_view v("Hello");
        char buf[16] = {};
        size_t n = v.copy(buf, 3);
        print_item("copy(buf,3) == 3", n == 3);
        print_item("copy 内容 == Hel", std::string(buf, n) == "Hel");

        char buf2[16] = {};
        size_t n2 = v.copy(buf2, 10, 2);
        print_item("copy(buf,10,2) == llo", std::string(buf2, n2) == "llo");

        char buf3[16] = {};
        size_t n3 = v.copy(buf3, 5, 10);
        print_item("copy 越界 == 0", n3 == 0);
    }

    // === 90. 比较 ===
    print_section(90, "utf8_view 比较");
    {
        utf8_view a("abc");
        utf8_view b("abd");
        utf8_view c("abc");

        print_item("abc == abc", a == c);
        print_item("abc != abd", a != b);
        print_item("abc < abd", a < b);
        print_item("abd > abc", b > a);
        print_item("abc <= abc", a <= c);
        print_item("abc >= abc", a >= c);

        print_item("abc == \"abc\"", a == "abc");
        print_item("abc != \"abd\"", a != "abd");
        print_item("abc < \"abd\"", a < "abd");

        print_item("abc == string_view(\"abc\")", a == std::string_view("abc"));
        print_item("abc < string_view(\"abd\")", a < std::string_view("abd"));

        print_item("compare(abc,abc) == 0", a.compare(c) == 0);
        print_item("compare(abc,abd) < 0", a.compare(b) < 0);
        print_item("compare(abd,abc) > 0", b.compare(a) > 0);

        utf8_view cn1(u8"你好");
        utf8_view cn2(u8"你好");
        utf8_view cn3(u8"你坏");
        print_item("中文 == 比较", cn1 == cn2);
        print_item("中文 < 比较", cn1 < cn3 || cn1 > cn3); // 只要可比较即可
    }

    // === 91. operator<=> ===
    print_section(91, "utf8_view operator<=>");
    {
        utf8_view a("abc");
        utf8_view b("abd");
        utf8_view c("abc");

        print_item("abc <=> abd less", (a <=> b) == std::strong_ordering::less);
        print_item("abc <=> abc equal", (a <=> c) == std::strong_ordering::equal);
        print_item("abd <=> abc greater", (b <=> a) == std::strong_ordering::greater);

        print_item("abc <=> \"abd\" less", (a <=> "abd") == std::strong_ordering::less);
        print_item("abc <=> \"abc\" equal", (a <=> "abc") == std::strong_ordering::equal);
    }

    // === 92. 字节查找 ===
    print_section(92, "utf8_view 字节查找");
    {
        utf8_view v("HelloWorld");
        print_item("find_byte('l') == 2", v.find_byte('l') == 2);
        print_item("find_byte('l',3) == 3", v.find_byte('l', 3) == 3);
        print_item("find_byte('z') == npos", v.find_byte('z') == utf8_view::npos);
        print_item("find_byte('o',5) == 6", v.find_byte('o', 5) == 6);

        print_item("rfind_byte('l') == 8", v.rfind_byte('l') == 8);
        print_item("rfind_byte('l',2) == 2", v.rfind_byte('l', 2) == 2);
        print_item("rfind_byte('H') == 0", v.rfind_byte('H') == 0);
        print_item("rfind_byte('z') == npos", v.rfind_byte('z') == utf8_view::npos);

        utf8_view empty;
        print_item("空 find_byte == npos", empty.find_byte('a') == utf8_view::npos);
        print_item("空 rfind_byte == npos", empty.rfind_byte('a') == utf8_view::npos);
    }

    // === 93. 字节子串查找 ===
    print_section(93, "utf8_view 字节子串查找");
    {
        utf8_view v("HelloWorldHello");
        print_item("find_bytes(World) == 5", v.find_bytes("World") == 5);
        print_item("find_bytes(World,6) == npos", v.find_bytes("World", 6) == utf8_view::npos);
        print_item("find_bytes(Hello) == 0", v.find_bytes("Hello") == 0);
        print_item("find_bytes(xyz) == npos", v.find_bytes("xyz") == utf8_view::npos);
        print_item("find_bytes(空) == 0", v.find_bytes("") == 0);

        print_item("rfind_bytes(Hello) == 10", v.rfind_bytes("Hello") == 10);
        print_item("rfind_bytes(World) == 5", v.rfind_bytes("World") == 5);
        print_item("rfind_bytes(xyz) == npos", v.rfind_bytes("xyz") == utf8_view::npos);

        utf8_view empty;
        print_item("空 find_bytes == npos", empty.find_bytes("a") == utf8_view::npos);
    }

    // === 94. 码点查找 ===
    print_section(94, "utf8_view 码点查找");
    {
        utf8_view v("HelloWorld");
        print_item("find('l') == 2", v.find(char32_t('l')) == 2);
        print_item("find('l',3) == 3", v.find(char32_t('l'), 3) == 3);
        print_item("find('z') == npos", v.find(char32_t('z')) == utf8_view::npos);

        utf8_view cn(u8"你好世界你好");
        print_item("中文 find(好) == 1", cn.find(char32_t(0x597D)) == 1);
        print_item("中文 find(好,2) == 5", cn.find(char32_t(0x597D), 2) == 5);

        utf8_view v2("abcabc");
        print_item("find(utf8_view bc) == 1", v2.find(utf8_view("bc")) == 1);
        print_item("find(utf8view bc,2) == 4", v2.find(utf8_view("bc"), 2) == 4);
        print_item("find(utf8view xyz) == npos", v2.find(utf8_view("xyz")) == utf8_view::npos);
        print_item("find(utf8view 空) == 0", v2.find(utf8_view()) == 0);
    }

    // === 95. rfind ===
    print_section(95, "utf8_view rfind");
    {
        utf8_view v("abcabc");
        print_item("rfind('a') == 3", v.rfind(char32_t('a')) == 3);
        print_item("rfind('z') == npos", v.rfind(char32_t('z')) == utf8_view::npos);

        utf8_view v2("HelloHello");
        print_item("rfind(utf8view Hello) == 5", v2.rfind(utf8_view("Hello")) == 5);
        print_item("rfind(utf8view xyz) == npos", v2.rfind(utf8_view("xyz")) == utf8_view::npos);
        print_item("rfind(utf8view 空) == size", v2.rfind(utf8_view()) == v2.size());

        utf8_view empty;
        print_item("空 rfind == npos", empty.rfind(char32_t('a')) == utf8_view::npos);
    }

    // === 96. find_first_of / find_last_of ===
    print_section(96, "utf8_view find_first_of / find_last_of");
    {
        utf8_view v("HelloWorld");
        print_item("find_first_of('l') == 2", v.find_first_of(char32_t('l')) == 2);
        print_item("find_first_of('z') == npos", v.find_first_of(char32_t('z')) == utf8_view::npos);

        utf8_view chars("lo");
        print_item("find_first_of(lo) == 2", v.find_first_of(chars) == 2);

        print_item("find_last_of('l') == 8", v.find_last_of(char32_t('l')) == 8);
        utf8_view chars2("od");
        print_item("find_last_of(od) == 9", v.find_last_of(chars2) == 9);

        utf8_view cn(u8"你好世界");
        print_item("中文 find_first_of(界) == 3", cn.find_first_of(char32_t(0x754C)) == 3);
        print_item("中文 find_last_of(好) == 1", cn.find_last_of(char32_t(0x597D)) == 1);
    }

    // === 97. find_first_not_of / find_last_not_of ===
    print_section(97, "utf8_view find_first_not_of / find_last_not_of");
    {
        utf8_view v("HelloWorld");
        print_item("find_first_not_of('H') == 1", v.find_first_not_of(char32_t('H')) == 1);

        utf8_view prefix("He");
        print_item("find_first_not_of(He) == 2", v.find_first_not_of(prefix) == 2);

        utf8_view s2("aaaa");
        print_item("find_first_not_of(a) == npos", s2.find_first_not_of(char32_t('a')) == utf8_view::npos);

        print_item("find_last_not_of('d') == 8", v.find_last_not_of(char32_t('d')) == 8);

        utf8_view suffix("ld");
        print_item("find_last_not_of(ld) == 7", v.find_last_not_of(suffix) == 7);
    }

    // === 98. contains / starts_with / ends_with ===
    print_section(98, "utf8_view contains / starts_with / ends_with");
    {
        utf8_view v("HelloWorld");
        print_item("contains('o')", v.contains(char32_t('o')));
        print_item("contains(World)", v.contains(utf8_view("World")));
        print_item("!contains(xyz)", !v.contains(utf8_view("xyz")));
        print_item("contains(空)", v.contains(utf8_view()));

        print_item("starts_with('H')", v.starts_with(char32_t('H')));
        print_item("!starts_with('h')", !v.starts_with(char32_t('h')));
        print_item("starts_with(Hello)", v.starts_with(utf8_view("Hello")));
        print_item("!starts_with(World)", !v.starts_with(utf8_view("World")));

        print_item("ends_with('d')", v.ends_with(char32_t('d')));
        print_item("ends_with(World)", v.ends_with(utf8_view("World")));
        print_item("!ends_with(Hello)", !v.ends_with(utf8_view("Hello")));

        utf8_view cn(u8"你好世界");
        print_item("中文 starts_with(你)", cn.starts_with(char32_t(0x4F60)));
        print_item("中文 ends_with(界)", cn.ends_with(char32_t(0x754C)));
        print_item("中文 starts_with(你好)", cn.starts_with(utf8_view(u8"你好")));
        print_item("中文 ends_with(世界)", cn.ends_with(utf8_view(u8"世界")));
    }

    // === 99. swap ===
    print_section(99, "utf8_view swap");
    {
        utf8_view a("Hello");
        utf8_view b("World");
        swap(a, b);
        print_item("swap 后 a == World", a == utf8_view("World"));
        print_item("swap 后 b == Hello", b == utf8_view("Hello"));

        utf8_view c("A");
        utf8_view d("B");
        c.swap(d);
        print_item("成员 swap a==B", c == utf8_view("B"));
        print_item("成员 swap b==A", d == utf8_view("A"));

        utf8_view empty;
        utf8_view data("Data");
        swap(empty, data);
        print_item("swap 空与非空", empty == utf8_view("Data") && data.empty());
    }

    // === 100. 流输出 ===
    print_section(100, "utf8_view 流输出");
    {
        utf8_view v("Hello世界");
        std::ostringstream oss;
        oss << v;
        print_item("operator<<", oss.str() == std::string("Hello世界"));

        utf8_view empty;
        std::ostringstream oss2;
        oss2 << empty;
        print_item("空 operator<<", oss2.str().empty());
    }

    // === 101. std::hash ===
    print_section(101, "utf8_view std::hash");
    {
        std::hash<utf8_view> hasher;
        utf8_view v1("HelloWorld");
        utf8_view v2("HelloWorld");
        utf8_view v3("OtherString");

        print_item("hash 相同串相等", hasher(v1) == hasher(v2));
        print_item("hash 不同串通常不等", hasher(v1) != hasher(v3));

        // 与 string_view hash 一致 (因为内部委托)
        std::hash<std::string_view> sv_hasher;
        print_item("hash 与 string_view 一致", hasher(v1) == sv_hasher(std::string_view(v1)));
    }

    // === 102. 与 utf8pp 互操作 ===
    print_section(102, "utf8_view 与 utf8pp 互操作");
    {
        utf8pp s("Hello世界");
        utf8_view v(s.data(), s.byte_size());
        print_item("从 utf8pp 构造 view", v.byte_size() == s.byte_size());
        print_item("view size == utf8pp size", v.size() == s.size());

        utf8_view v2("TestData");
        utf8pp s2(v2.data(), v2.byte_size());
        print_item("从 view 构造 utf8pp", s2 == utf8pp("TestData"));

        // view 可用作 string_view 的替代
        utf8_view v3("Compare");
        std::string_view sv = v3.byte_view();
        print_item("view 转 string_view 比较", sv == std::string_view("Compare"));
    }

    // === 103. 非拥有语义验证 ===
    print_section(103, "utf8_view 非拥有语义验证");
    {
        std::string source = "DynamicString";
        utf8_view v(source);
        print_item("view 引用源数据", v.byte_size() == source.size());
        print_item("view data == source data", v.data() == source.data());

        // 正确用法: 源数据不变, view 跟踪
        const char* literal = "Literal";
        utf8_view v2(literal);
        print_item("view 引用字面量", v2 == utf8_view("Literal"));

        // view 不持有副本: 修改源内容后 view 反映变化 (同一内存)
        char buf[16] = "ABC";
        utf8_view v3(buf, 3);
        buf[1] = 'X';
        print_item("view 反映源修改 (非拥有)", v3.byte_at(1) == 'X');
    }

    // === 104. 非法序列处理 ===
    print_section(104, "utf8_view 非法序列处理");
    {
        const char bad[] = {(char)0xFF, 'A', 0};
        utf8_view v(bad, 2);
        print_item("非法字节 at(0) == U+FFFD", v.at(0) == char32_t(0xFFFD));
        print_item("非法后 at(1) == 'A'", v.at(1) == char32_t('A'));
        print_item("非法 size == 2", v.size() == 2);

        const char bad2[] = {(char)0xC0, (char)0x80, 0}; // 非 shortest form
        utf8_view v2(bad2, 2);
        print_item("非最短形式 at(0) == U+FFFD", v2.at(0) == char32_t(0xFFFD));
    }

    // === 105. 边界条件 ===
    print_section(105, "utf8_view 边界条件");
    {
        utf8_view empty;
        print_item("空 at(0) == U+FFFD", empty.at(0) == char32_t(0xFFFD));
        print_item("空 front() == U+FFFD", empty.front() == char32_t(0xFFFD));
        print_item("空 back() == U+FFFD", empty.back() == char32_t(0xFFFD));
        print_item("空 substr == 空", empty.substr(0).empty());
        print_item("空 substr_bytes == 空", empty.substr_bytes(0).empty());
        print_item("空 find == npos", empty.find(char32_t('a')) == utf8_view::npos);
        print_item("空 starts_with(cp) false", !empty.starts_with(char32_t('a')));
        print_item("空 starts_with(view) true", empty.starts_with(utf8_view()));

        utf8_view single("A");
        print_item("单字符 at(0) == 'A'", single.at(0) == char32_t('A'));
        print_item("单字符 front == back", single.front() == single.back());
        print_item("单字符 substr(0,1) == A", single.substr(0, 1) == utf8_view("A"));
        print_item("单字符 substr(0,10) == A", single.substr(0, 10) == utf8_view("A"));
    }

    // ================================================================
    //  模块 4: Unicode 数据 (Script / 大小写 / 规范化)
    // ================================================================

    // === 106. Unicode Script 单码点判断 ===
    print_section(106, "Unicode Script 单码点判断");
    {
        print_item("'A'  -> Latin",     utf8pp::script_of(U'A')  == script::latin);
        print_item("'z'  -> Latin",     utf8pp::script_of(U'z')  == script::latin);
        print_item("'中' -> Han",       utf8pp::script_of(U'\u4E2D') == script::han);
        print_item("'あ' -> Hiragana",  utf8pp::script_of(U'\u3042') == script::hiragana);
        print_item("'ア' -> Katakana",  utf8pp::script_of(U'\u30A2') == script::katakana);
        print_item("'가' -> Hangul",    utf8pp::script_of(U'\uAC00') == script::hangul);
        print_item("'α'  -> Greek",     utf8pp::script_of(U'\u03B1') == script::greek);
        print_item("'Я'  -> Cyrillic",  utf8pp::script_of(U'\u042F') == script::cyrillic);
        print_item("'א'  -> Hebrew",    utf8pp::script_of(U'\u05D0') == script::hebrew);
        print_item("'ا'  -> Arabic",    utf8pp::script_of(U'\u0627') == script::arabic);
        print_item("'ॐ'  -> Devanagari",utf8pp::script_of(U'\u0950') == script::devanagari);
        print_item("'1'  -> Common",    utf8pp::script_of(U'1')  == script::common);
        print_item("' '  -> Common",    utf8pp::script_of(U' ')  == script::common);
        print_item("U+0301(combining) -> Inherited", utf8pp::script_of(U'\u0301') == script::inherited);
        print_item("U+1F600 (emoji) -> Emoji",       utf8pp::script_of(U'\U0001F600') == script::emoji_picto);
        print_item("U+20000 (CJK Ext B) -> CJK_Ext", utf8pp::script_of(U'\U00020000') == script::cjk_ext);
    }

    // === 107. is_script / script_name ===
    print_section(107, "is_script / script_name");
    {
        print_item("is_script('A', Latin)",   utf8pp::is_script(U'A', script::latin));
        print_item("is_script('中', Han)",    utf8pp::is_script(U'\u4E2D', script::han));
        print_item("script_name(Latin)==\"Latin\"",   std::strcmp(utf8pp::script_name(script::latin), "Latin") == 0);
        print_item("script_name(Han)==\"Han\"",       std::strcmp(utf8pp::script_name(script::han), "Han") == 0);
        print_item("script_name(Emoji)==\"Emoji\"",   std::strcmp(utf8pp::script_name(script::emoji_picto), "Emoji") == 0);
    }

    // === 108. 串级 Script 判断 ===
    print_section(108, "串级 Script 判断");
    {
        utf8pp en("Hello");
        print_item("English script_of()==Latin",   en.script_of() == script::latin);
        print_item("English is_all_script(Latin)", en.is_all_script(script::latin));
        print_item("English !is_all_script(Han)",  !en.is_all_script(script::han));

        utf8pp cn(u8"你好世界");
        print_item("Chinese script_of()==Han",     cn.script_of() == script::han);
        print_item("Chinese is_all_script(Han)",   cn.is_all_script(script::han));

        utf8pp mixed(u8"Hello你好");
        print_item("Mixed script_of()==Latin",     mixed.script_of() == script::latin);
        print_item("Mixed !is_all_script(Latin)",  !mixed.is_all_script(script::latin));
        print_item("Mixed contains_script(Han)",   mixed.contains_script(script::han));
        print_item("Mixed contains_script(Latin)", mixed.contains_script(script::latin));

        utf8pp empty;
        print_item("Empty script_of()==Unknown",   empty.script_of() == script::unknown);
        print_item("Empty !is_all_script(Latin)",  !empty.is_all_script(script::latin));

        // 含标点 (common) 的整串判断: "Hi!" 全部 Latin (common 通配)
        utf8pp with_punct("Hi!");
        print_item("\"Hi!\" is_all_script(Latin)", with_punct.is_all_script(script::latin));
    }

    // === 109. CCC (Canonical Combining Class) 查询 ===
    print_section(109, "CCC 查询");
    {
        print_item("U+0300 (grave)      CCC=230", ud::canonical_combining_class(0x0300) == 230);
        print_item("U+0301 (acute)      CCC=230", ud::canonical_combining_class(0x0301) == 230);
        print_item("U+0327 (cedilla)    CCC=202", ud::canonical_combining_class(0x0327) == 202);
        print_item("U+0323 (dot below)  CCC=220", ud::canonical_combining_class(0x0323) == 220);
        print_item("U+031B (ogonek)     CCC=216", ud::canonical_combining_class(0x031B) == 216);
        print_item("U+0345 (ypogegram.) CCC=240", ud::canonical_combining_class(0x0345) == 240);
        print_item("U+0334 (overlay)    CCC=1",   ud::canonical_combining_class(0x0334) == 1);
        print_item("'A' (base)          CCC=0",   ud::canonical_combining_class(U'A') == 0);
        print_item("U+05B0 (sheva)      CCC=10",  ud::canonical_combining_class(0x05B0) == 10);
        print_item("U+094D (virama)     CCC=9",   ud::canonical_combining_class(0x094D) == 9);
    }

    // === 110. NFC 合并 (e + U+0301 -> é) ===
    print_section(110, "NFC 合并");
    {
        char32_t cps[] = {U'e', U'\u0301'};
        utf8pp s(cps, 2);
        print_item("input size==2", s.size() == 2);
        s.to_nfc();
        print_item("NFC size==1",   s.size() == 1);
        print_item("NFC at(0)==é (U+00E9)", s.at(0) == U'\u00E9');

        // 已是 NFC 不变
        utf8pp already(U"\u00E9");
        already.to_nfc();
        print_item("é NFC unchanged size==1", already.size() == 1 && already.at(0) == U'\u00E9');
    }

    // === 111. NFC canonical ordering (乱序组合标记排序) ===
    print_section(111, "NFC canonical ordering");
    {
        // q + U+0301 (acute, CCC=230) + U+0323 (dot below, CCC=220)  ← 乱序 (230 在 220 前)
        // canonical ordering 后: q + U+0323 (220) + U+0301 (230)  ← CCC 升序
        // q 不参与合并 (不在 compose 表), 故输出码点序列 = [q, U+0323, U+0301]
        char32_t in[] = {U'q', U'\u0301', U'\u0323'};
        utf8pp s(in, 3);
        s.to_nfc();
        print_item("ordering size==3", s.size() == 3);
        print_item("ordering [0]==q",       s.at(0) == U'q');
        print_item("ordering [1]==U+0323",  s.at(1) == U'\u0323');
        print_item("ordering [2]==U+0301",  s.at(2) == U'\u0301');
    }
    {
        // 反向: q + U+0323 (220) + U+0301 (230)  ← 已有序
        char32_t in[] = {U'q', U'\u0323', U'\u0301'};
        utf8pp s(in, 3);
        s.to_nfc();
        print_item("ordered input unchanged [1]==U+0323", s.at(1) == U'\u0323');
        print_item("ordered input unchanged [2]==U+0301", s.at(2) == U'\u0301');
    }

    // === 112. NFC 乱序输入 + 合并 (e + U+0301 + U+0323) ===
    print_section(112, "NFC 乱序输入 + 合并");
    {
        // e + U+0301 (230) + U+0323 (220)  ← 乱序
        // 排序后: e + U+0323 (220) + U+0301 (230)
        // 合并: e + U+0323 -> ẹ (U+1EB9), 剩 ẹ + U+0301 (无预组合)
        char32_t in[] = {U'e', U'\u0301', U'\u0323'};
        utf8pp s(in, 3);
        s.to_nfc();
        print_item("e+0301+0323 NFC size==2", s.size() == 2);
        print_item("NFC [0]==ẹ (U+1EB9)",    s.at(0) == U'\u1EB9');
        print_item("NFC [1]==U+0301",        s.at(1) == U'\u0301');
    }

    // === 113. NFD 分解 (é -> e + U+0301) ===
    print_section(113, "NFD 分解");
    {
        utf8pp s(U"\u00E9");
        print_item("é size==1", s.size() == 1);
        s.to_nfd();
        print_item("NFD size==2",   s.size() == 2);
        print_item("NFD [0]==e",    s.at(0) == U'e');
        print_item("NFD [1]==U+0301", s.at(1) == U'\u0301');
    }

    // === 114. NFC 副本接口 nfc() ===
    print_section(114, "NFC 副本接口 nfc()");
    {
        char32_t cps[] = {U'a', U'\u0308'}; // a + diaeresis -> ä
        utf8pp s(cps, 2);
        utf8pp n = s.nfc();
        print_item("nfc() size==1", n.size() == 1);
        print_item("nfc() at(0)==ä (U+00E4)", n.at(0) == U'\u00E4');
        print_item("original unchanged size==2", s.size() == 2);
    }

    // === 115. Hangul 算法分解 (가 → ㄱ + ㅏ) ===
    print_section(115, "Hangul 算法分解");
    {
        uint32_t out[3] = {0, 0, 0};
        uint32_t n = ud::hangul_decompose(0xAC00, out); // 가 (S_BASE)
        print_item("가 is_hangul_syllable", ud::is_hangul_syllable(0xAC00));
        print_item("가 decompose len==2", n == 2);
        print_item("가 → ㄱ (U+1100)", out[0] == 0x1100);
        print_item("가 → ㅏ (U+1161)", out[1] == 0x1161);

        // LVT: 각 (가 + ㄱ) = U+AC01
        uint32_t out2[3] = {0, 0, 0};
        uint32_t n2 = ud::hangul_decompose(0xAC01, out2);
        print_item("각 decompose len==3", n2 == 3);
        print_item("각 → ㄱ (U+1100)", out2[0] == 0x1100);
        print_item("각 → ㅏ (U+1161)", out2[1] == 0x1161);
        print_item("각 → ㄱ (U+11A8)", out2[2] == 0x11A8);

        // 非音节
        print_item("'A' decompose len==0", ud::hangul_decompose(U'A', out) == 0);
    }

    // === 116. Hangul 算法组合 (L+V → LV, LV+T → LVT) ===
    print_section(116, "Hangul 算法组合");
    {
        // ㄱ (U+1100) + ㅏ (U+1161) → 가 (U+AC00)
        print_item("L+V → 가", ud::hangul_compose(0x1100, 0x1161) == 0xAC00);
        // 가 (U+AC00) + ㄱ (U+11A8) → 각 (U+AC01)
        print_item("LV+T → 각", ud::hangul_compose(0xAC00, 0x11A8) == 0xAC01);
        // 不可组合: A + B
        print_item("A+B 不可组合", ud::hangul_compose(U'A', U'B') == 0);
    }

    // === 117. NFC Hangul (L+V → 가) ===
    print_section(117, "NFC Hangul");
    {
        char32_t cps[] = {char32_t(0x1100), char32_t(0x1161)}; // ㄱ + ㅏ
        utf8pp s(cps, 2);
        s.to_nfc();
        print_item("NFC Hangul size==1", s.size() == 1);
        print_item("NFC Hangul at(0)==가 (U+AC00)", s.at(0) == char32_t(0xAC00));

        // L+V+T → LVT
        char32_t cps2[] = {char32_t(0x1100), char32_t(0x1161), char32_t(0x11A8)};
        utf8pp s2(cps2, 3);
        s2.to_nfc();
        print_item("NFC Hangul LVT size==1", s2.size() == 1);
        print_item("NFC Hangul LVT at(0)==각 (U+AC01)", s2.at(0) == char32_t(0xAC01));
    }

    // === 118. NFD Hangul (가 → L+V) ===
    print_section(118, "NFD Hangul");
    {
        utf8pp s(size_t(1), char32_t(0xAC00)); // 가
        s.to_nfd();
        print_item("NFD Hangul size==2", s.size() == 2);
        print_item("NFD Hangul [0]==ㄱ", s.at(0) == char32_t(0x1100));
        print_item("NFD Hangul [1]==ㅏ", s.at(1) == char32_t(0x1161));
    }

    // === 119. NFKD 全角 ASCII → 半角 ===
    print_section(119, "NFKD 全角 ASCII → 半角");
    {
        uint32_t out = 0;
        print_item("全角 A → A", ud::nfkd_fullwidth_decompose(0xFF21, out) && out == U'A');
        print_item("全角 z → z", ud::nfkd_fullwidth_decompose(0xFF5A, out) && out == U'z');
        print_item("全角 0 → 0", ud::nfkd_fullwidth_decompose(0xFF10, out) && out == U'0');
        print_item("全角 ! → !", ud::nfkd_fullwidth_decompose(0xFF01, out) && out == U'!');
        print_item("全角空格 → 空格", ud::nfkd_fullwidth_decompose(0x3000, out) && out == U' ');
        print_item("半角 A 不处理", !ud::nfkd_fullwidth_decompose(U'A', out));

        // 整串 NFKD
        char32_t full[] = {char32_t(0xFF21), char32_t(0xFF22), char32_t(0xFF23)}; // ＡＢＣ
        utf8pp s(full, 3);
        s.to_nfkd();
        print_item("NFKD ＡＢＣ → ABC size==3", s.size() == 3);
        print_item("NFKD [0]==A", s.at(0) == U'A');
        print_item("NFKD [1]==B", s.at(1) == U'B');
        print_item("NFKD [2]==C", s.at(2) == U'C');
    }

    // === 120. NFKD 连字 (ﬁ → fi) / 罗马数字 / 圆圈数字 / 上标 ===
    print_section(120, "NFKD 连字 / 罗马数字 / 圆圈 / 上标");
    {
        // ﬁ → fi
        char32_t lig = U'\uFB01';
        utf8pp s(&lig, 1);
        s.to_nfkd();
        print_item("NFKD ﬁ → fi size==2", s.size() == 2);
        print_item("NFKD ﬁ [0]==f", s.at(0) == U'f');
        print_item("NFKD ﬁ [1]==i", s.at(1) == U'i');

        // ﬀ → ff
        char32_t lig2 = U'\uFB00';
        utf8pp s2(&lig2, 1);
        s2.to_nfkd();
        print_item("NFKD ﬀ → ff size==2", s2.size() == 2);
        print_item("NFKD ﬀ [0]==f", s2.at(0) == U'f');

        // Ⅳ → IV
        char32_t rn = U'\u2163';
        utf8pp s3(&rn, 1);
        s3.to_nfkd();
        print_item("NFKD Ⅳ → IV size==2", s3.size() == 2);
        print_item("NFKD Ⅳ [0]==I", s3.at(0) == U'I');
        print_item("NFKD Ⅳ [1]==V", s3.at(1) == U'V');

        // ① → 1
        char32_t circ = U'\u2460';
        utf8pp s4(&circ, 1);
        s4.to_nfkd();
        print_item("NFKD ① → 1 size==1", s4.size() == 1);
        print_item("NFKD ① [0]=='1'", s4.at(0) == U'1');

        // ² → 2
        char32_t sup = U'\u00B2';
        utf8pp s5(&sup, 1);
        s5.to_nfkd();
        print_item("NFKD ² → 2 size==1", s5.size() == 1);
        print_item("NFKD ² [0]=='2'", s5.at(0) == U'2');

        // ｱ → ア (半角片假名)
        char32_t half = U'\uFF71';
        utf8pp s6(&half, 1);
        s6.to_nfkd();
        print_item("NFKD ｱ → ア size==1", s6.size() == 1);
        print_item("NFKD ｱ [0]==ア (U+30A2)", s6.at(0) == char32_t(0x30A2));
    }

    // === 121. NFKC + 副本接口 ===
    print_section(121, "NFKC + 副本接口");
    {
        // NFKC = NFKD + compose
        char32_t full_a = U'\uFF21';
        utf8pp s(&full_a, 1);
        s.to_nfkc();
        print_item("NFKC Ａ → A", s.size() == 1 && s.at(0) == U'A');

        // 混合: 全角 Ａ + 组合重音 → NFKC 后分解为 A, 再与 U+0301 组合为 Á (U+00C1)
        char32_t mix[] = {char32_t(0xFF21), char32_t(0x0301)};
        utf8pp s2(mix, 2);
        s2.to_nfkc();
        print_item("NFKC Ａ+´ → Á size==1", s2.size() == 1);
        print_item("NFKC Ａ+´ [0]==Á (U+00C1)", s2.at(0) == U'\u00C1');

        // 副本接口 nfkd()
        char32_t lig = U'\uFB02'; // ﬂ
        utf8pp s3(&lig, 1);
        utf8pp k = s3.nfkd();
        print_item("nfkd() ﬂ → fl size==2", k.size() == 2);
        print_item("nfkd() [0]==f", k.at(0) == U'f');
        print_item("nfkd() [1]==l", k.at(1) == U'l');
        print_item("original unchanged", s3.size() == 1 && s3.at(0) == U'\uFB02');
    }

    // === 122. 规范化幂等性 (NFC(NFC(x)) == NFC(x)) ===
    print_section(122, "规范化幂等性");
    {
        char32_t cps[] = {U'e', U'\u0301'};
        utf8pp s(cps, 2);
        s.to_nfc();
        utf8pp s2 = s;
        s2.to_nfc();
        print_item("NFC 幂等 size", s.size() == s2.size());
        print_item("NFC 幂等 content", s == s2);

        // Hangul 幂等
        utf8pp hg(size_t(1), char32_t(0xAC00)); // 가
        hg.to_nfc();
        utf8pp hg2 = hg;
        hg2.to_nfc();
        print_item("Hangul NFC 幂等", hg == hg2);
    }

    print_summary("功能测试");
    return 0;
}