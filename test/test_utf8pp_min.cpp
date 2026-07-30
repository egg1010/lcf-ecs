// test_utf8pp_min.cpp - utf8pp 最小化测试 (独立, 不依赖 test_common.hpp)
#include "include/part/utf8pp.hpp"
#include <cstdio>
#include <cstring>

static int g_pass = 0, g_fail = 0;

#define CHECK(name, cond) do { \
    bool _r = (cond); \
    if (_r) ++g_pass; else ++g_fail; \
    std::printf("  %-42s : %s\n", name, _r ? "PASS" : "FAIL"); \
} while(0)

int main()
{
    std::printf("sizeof(utf8pp) = %zu, SSO_CAPACITY = %zu\n\n", sizeof(utf8pp), utf8pp::SSO_CAPACITY);

    // 1. 基本构造
    CHECK("default ctor empty", utf8pp().empty());
    CHECK("const char* ctor", utf8pp("Hello").size() == 5);
    CHECK("u8 string ctor", utf8pp(u8"你好").size() == 2);

    // 2. SSO 边界
    {
        utf8pp s("0123456789012345678901"); // 22 bytes
        CHECK("SSO full 22 bytes", s.size() == 22 && s.is_sso());
        utf8pp s2 = s;
        CHECK("SSO full copy", s2 == s);
        utf8pp s3 = std::move(s2);
        CHECK("SSO full move", s3 == s && s2.empty());
    }

    // 3. swap SSO ↔ SSO
    {
        utf8pp a("Hello");
        utf8pp b("World");
        swap(a, b);
        CHECK("swap SSO a==World", a == utf8pp("World"));
        CHECK("swap SSO b==Hello", b == utf8pp("Hello"));
    }

    // 4. swap 空与非空
    {
        utf8pp empty1;
        utf8pp empty2("Data");
        swap(empty1, empty2);
        CHECK("swap empty↔data", empty1 == utf8pp("Data") && empty2.empty());
    }

    // 5. hash
    {
        std::hash<utf8pp> hasher;
        utf8pp s1("Hello"), s2("Hello"), s3("World");
        CHECK("hash equal strings", hasher(s1) == hasher(s2));
        CHECK("hash different strings", hasher(s1) != hasher(s3));
        CHECK("hash empty strings", hasher(utf8pp()) == hasher(utf8pp()));
    }

    // 6. find/rfind
    {
        utf8pp s("Hello世界World");
        CHECK("find(const char*) == 5", s.find("世界") == 5);
        CHECK("find(string_view) == 5", s.find(std::string_view("世界")) == 5);
        CHECK("find(const char*, 6) == npos", s.find("世界", 6) == utf8pp::npos);
        CHECK("find not found == npos", s.find("xyz") == utf8pp::npos);

        utf8pp s2("abcabcabc");
        CHECK("rfind(const char*) == 6", s2.rfind("abc") == 6);
        CHECK("rfind(string_view) == 6", s2.rfind(std::string_view("abc")) == 6);
        CHECK("rfind(const char*, 3) == 3", s2.rfind("abc", 3) == 3);
        CHECK("rfind not found == npos", s2.rfind("xyz") == utf8pp::npos);

        utf8pp empty;
        CHECK("empty find == npos", empty.find("a") == utf8pp::npos);
    }

    // 7. find_first_of / find_last_of
    {
        utf8pp s("Hello世界World");
        CHECK("find_first_of(const char*) == 2", s.find_first_of("l") == 2);
        CHECK("find_first_of(string_view) == 2", s.find_first_of(std::string_view("lo")) == 2);
        CHECK("find_last_of(const char*) == 10", s.find_last_of("l") == 10);
        CHECK("find_last_of(string_view) == 11", s.find_last_of(std::string_view("od")) == 11);
        CHECK("find_first_not_of(const char*) == 1", s.find_first_not_of("H") == 1);
        CHECK("find_first_not_of(string_view) == 2", s.find_first_not_of(std::string_view("He")) == 2);
        CHECK("find_last_not_of(const char*) == 10", s.find_last_not_of("d") == 10);
        CHECK("find_last_not_of(string_view) == 9", s.find_last_not_of(std::string_view("ld")) == 9);
    }

    // 8. insert
    {
        utf8pp s("Hd");
        s.insert(1, "el");
        CHECK("insert(const char*) == Held", s == utf8pp("Held"));

        utf8pp s2("Hd");
        s2.insert(1, std::string_view("el"));
        CHECK("insert(string_view) == Held", s2 == utf8pp("Held"));

        utf8pp s3("Hd");
        s3.insert(1, utf8pp("el"));
        CHECK("insert(utf8pp) == Held", s3 == utf8pp("Held"));

        utf8pp s4("Hd");
        s4.insert(1, "el", 1);
        CHECK("insert(const char*, 1) == Hed", s4 == utf8pp("Hed"));

        utf8pp s5(u8"你好");
        s5.insert(1, "X");
        CHECK("中文 insert(const char*)", s5.size() == 3 && s5.at(1) == char32_t('X'));
    }

    // 9. replace
    {
        utf8pp s("Hello世界World");
        s.replace(0, 5, "Hi");
        CHECK("replace(const char*) == Hi世界World", s == utf8pp("Hi世界World"));

        utf8pp s3("abc abc abc");
        s3.replace_all("abc", "XY");
        CHECK("replace_all(const char*) == XY XY XY", s3 == utf8pp("XY XY XY"));
    }

    // 10. compare 子串
    {
        utf8pp s("Hello");
        CHECK("compare(0,3, Hel) == 0", s.compare(0, 3, utf8pp("Hel")) == 0);
        CHECK("compare(0,3, Hi) < 0", s.compare(0, 3, "Hi") < 0);
        CHECK("compare(0,3, Hello) < 0", s.compare(0, 3, std::string_view("Hello")) < 0);
    }

    // 11. operator==/string_view + <=>/string_view
    {
        utf8pp s("Hello");
        CHECK("==(string_view)", s == std::string_view("Hello"));
        CHECK("!=(string_view)", s != std::string_view("World"));
        CHECK("<(string_view)", s < std::string_view("World"));
        CHECK("<=>(string_view) equal", (s <=> std::string_view("Hello")) == std::strong_ordering::equal);
        CHECK("<=>(string_view) less", (s <=> std::string_view("World")) == std::strong_ordering::less);

        utf8pp cn(u8"你好");
        CHECK("中文 == string_view", cn == std::string_view(reinterpret_cast<const char*>(u8"你好")));
    }

    std::printf("\n==== Results: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
