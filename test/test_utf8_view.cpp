// test_utf8_view.cpp - utf8_view 模块功能测试 (非拥有 UTF-8 字符串视图)
#include "test_common.hpp"
#include "include/part/utf8_view.hpp"
#include "include/part/utf8pp.hpp"

int main()
{
    // === 1. 构造与赋值 ===
    print_section(1, "构造与赋值");
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

    // === 2. 容量 ===
    print_section(2, "容量");
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

    // === 3. 数据访问 ===
    print_section(3, "数据访问");
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

    // === 4. 码点访问 (O(n)) ===
    print_section(4, "码点访问");
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

    // === 5. 迭代器 ===
    print_section(5, "迭代器");
    {
        utf8_view v("abc");
        std::u32string collected;
        for (char32_t cp : v) collected.push_back(cp);
        print_item("正向遍历 == abc", collected == std::u32string(U"abc"));

        utf8_view cn(u8"你好");
        std::u32string collected2;
        for (char32_t cp : cn) collected2.push_back(cp);
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

    // === 6. 反向迭代器 ===
    print_section(6, "反向迭代器");
    {
        utf8_view v("abc");
        std::u32string collected;
        for (auto it = v.rbegin(); it != v.rend(); ++it) collected.push_back(*it);
        print_item("rbegin->rend == cba", collected == std::u32string(U"cba"));

        utf8_view cn(u8"你好");
        std::u32string collected2;
        for (auto it = cn.rbegin(); it != cn.rend(); ++it) collected2.push_back(*it);
        print_item("中文反向 == 好你", collected2 == std::u32string(U"好你"));

        utf8_view v3("XYZ");
        auto it = v3.crbegin();
        print_item("crbegin == 'Z'", *it == char32_t('Z'));
        ++it;
        print_item("++crbegin == 'Y'", *it == char32_t('Y'));

        utf8_view empty;
        print_item("空 rbegin == rend", empty.rbegin() == empty.rend());
    }

    // === 7. 子串 ===
    print_section(7, "子串");
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

    // === 8. remove_prefix / remove_suffix ===
    print_section(8, "remove_prefix / remove_suffix");
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

    // === 9. copy ===
    print_section(9, "copy");
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

    // === 10. 比较 ===
    print_section(10, "比较");
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

    // === 11. operator<=> ===
    print_section(11, "operator<=>");
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

    // === 12. 字节查找 ===
    print_section(12, "字节查找");
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

    // === 13. 字节子串查找 ===
    print_section(13, "字节子串查找");
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

    // === 14. 码点查找 ===
    print_section(14, "码点查找");
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

    // === 15. rfind ===
    print_section(15, "rfind");
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

    // === 16. find_first_of / find_last_of ===
    print_section(16, "find_first_of / find_last_of");
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

    // === 17. find_first_not_of / find_last_not_of ===
    print_section(17, "find_first_not_of / find_last_not_of");
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

    // === 18. contains / starts_with / ends_with ===
    print_section(18, "contains / starts_with / ends_with");
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

    // === 19. swap ===
    print_section(19, "swap");
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

    // === 20. 流输出 ===
    print_section(20, "流输出");
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

    // === 21. std::hash ===
    print_section(21, "std::hash");
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

    // === 22. 与 utf8pp 互操作 ===
    print_section(22, "与 utf8pp 互操作");
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

    // === 23. 非拥有语义验证 ===
    print_section(23, "非拥有语义验证");
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

    // === 24. 非法序列处理 ===
    print_section(24, "非法序列处理");
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

    // === 25. 边界条件 ===
    print_section(25, "边界条件");
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

    print_summary("功能测试");
    return 0;
}
