// test_utf8_unicode.cpp - Unicode Script / CCC / NFC 升级验证 (独立, 不依赖 test_common.hpp)
#include "include/part/utf8pp.hpp"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

static int g_pass = 0, g_fail = 0;

#define CHECK(name, cond) do { \
    bool _r = (cond); \
    if (_r) ++g_pass; else ++g_fail; \
    std::printf("  %-48s : %s\n", name, _r ? "PASS" : "FAIL"); \
} while(0)

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
#endif

    using script = utf8pp::script;

    // ============================================================
    std::printf("[1] Unicode Script 单码点判断\n");
    // ============================================================
    CHECK("'A'  -> Latin",     utf8pp::script_of(U'A')  == script::latin);
    CHECK("'z'  -> Latin",     utf8pp::script_of(U'z')  == script::latin);
    CHECK("'中' -> Han",       utf8pp::script_of(U'\u4E2D') == script::han);
    CHECK("'あ' -> Hiragana",  utf8pp::script_of(U'\u3042') == script::hiragana);
    CHECK("'ア' -> Katakana",  utf8pp::script_of(U'\u30A2') == script::katakana);
    CHECK("'가' -> Hangul",    utf8pp::script_of(U'\uAC00') == script::hangul);
    CHECK("'α'  -> Greek",     utf8pp::script_of(U'\u03B1') == script::greek);
    CHECK("'Я'  -> Cyrillic",  utf8pp::script_of(U'\u042F') == script::cyrillic);
    CHECK("'א'  -> Hebrew",    utf8pp::script_of(U'\u05D0') == script::hebrew);
    CHECK("'ا'  -> Arabic",    utf8pp::script_of(U'\u0627') == script::arabic);
    CHECK("'ॐ'  -> Devanagari",utf8pp::script_of(U'\u0950') == script::devanagari);
    CHECK("'1'  -> Common",    utf8pp::script_of(U'1')  == script::common);
    CHECK("' '  -> Common",    utf8pp::script_of(U' ')  == script::common);
    CHECK("U+0301(combining) -> Inherited", utf8pp::script_of(U'\u0301') == script::inherited);
    CHECK("U+1F600 (emoji) -> Emoji",       utf8pp::script_of(U'\U0001F600') == script::emoji_picto);
    CHECK("U+20000 (CJK Ext B) -> CJK_Ext", utf8pp::script_of(U'\U00020000') == script::cjk_ext);

    // ============================================================
    std::printf("\n[2] is_script / script_name\n");
    // ============================================================
    CHECK("is_script('A', Latin)",   utf8pp::is_script(U'A', script::latin));
    CHECK("is_script('中', Han)",    utf8pp::is_script(U'\u4E2D', script::han));
    CHECK("script_name(Latin)==\"Latin\"",   std::strcmp(utf8pp::script_name(script::latin), "Latin") == 0);
    CHECK("script_name(Han)==\"Han\"",       std::strcmp(utf8pp::script_name(script::han), "Han") == 0);
    CHECK("script_name(Emoji)==\"Emoji\"",   std::strcmp(utf8pp::script_name(script::emoji_picto), "Emoji") == 0);

    // ============================================================
    std::printf("\n[3] 串级 Script 判断\n");
    // ============================================================
    {
        utf8pp en("Hello");
        CHECK("English script_of()==Latin",   en.script_of() == script::latin);
        CHECK("English is_all_script(Latin)", en.is_all_script(script::latin));
        CHECK("English !is_all_script(Han)",  !en.is_all_script(script::han));

        utf8pp cn(u8"你好世界");
        CHECK("Chinese script_of()==Han",     cn.script_of() == script::han);
        CHECK("Chinese is_all_script(Han)",   cn.is_all_script(script::han));

        utf8pp mixed(u8"Hello你好");
        CHECK("Mixed script_of()==Latin",     mixed.script_of() == script::latin);
        CHECK("Mixed !is_all_script(Latin)",  !mixed.is_all_script(script::latin));
        CHECK("Mixed contains_script(Han)",   mixed.contains_script(script::han));
        CHECK("Mixed contains_script(Latin)", mixed.contains_script(script::latin));

        utf8pp empty;
        CHECK("Empty script_of()==Unknown",   empty.script_of() == script::unknown);
        CHECK("Empty !is_all_script(Latin)",  !empty.is_all_script(script::latin));

        // 含标点 (common) 的整串判断: "Hi!" 全部 Latin (common 通配)
        utf8pp with_punct("Hi!");
        CHECK("\"Hi!\" is_all_script(Latin)", with_punct.is_all_script(script::latin));
    }

    // ============================================================
    std::printf("\n[4] CCC (Canonical Combining Class) 查询\n");
    // ============================================================
    {
        namespace ud = unicode_data;
        CHECK("U+0300 (grave)      CCC=230", ud::canonical_combining_class(0x0300) == 230);
        CHECK("U+0301 (acute)      CCC=230", ud::canonical_combining_class(0x0301) == 230);
        CHECK("U+0327 (cedilla)    CCC=202", ud::canonical_combining_class(0x0327) == 202);
        CHECK("U+0323 (dot below)  CCC=220", ud::canonical_combining_class(0x0323) == 220);
        CHECK("U+031B (ogonek)     CCC=216", ud::canonical_combining_class(0x031B) == 216);
        CHECK("U+0345 (ypogegram.) CCC=240", ud::canonical_combining_class(0x0345) == 240);
        CHECK("U+0334 (overlay)    CCC=1",   ud::canonical_combining_class(0x0334) == 1);
        CHECK("'A' (base)          CCC=0",   ud::canonical_combining_class(U'A') == 0);
        CHECK("U+05B0 (sheva)      CCC=10",  ud::canonical_combining_class(0x05B0) == 10);
        CHECK("U+094D (virama)     CCC=9",   ud::canonical_combining_class(0x094D) == 9);
    }

    // ============================================================
    std::printf("\n[5] NFC 合并 (e + U+0301 -> é)\n");
    // ============================================================
    {
        char32_t cps[] = {U'e', U'\u0301'};
        utf8pp s(cps, 2);
        CHECK("input size==2", s.size() == 2);
        s.to_nfc();
        CHECK("NFC size==1",   s.size() == 1);
        CHECK("NFC at(0)==é (U+00E9)", s.at(0) == U'\u00E9');

        // 已是 NFC 不变
        utf8pp already(U"\u00E9");
        already.to_nfc();
        CHECK("é NFC unchanged size==1", already.size() == 1 && already.at(0) == U'\u00E9');
    }

    // ============================================================
    std::printf("\n[6] NFC canonical ordering (乱序组合标记排序)\n");
    // ============================================================
    {
        // q + U+0301 (acute, CCC=230) + U+0323 (dot below, CCC=220)  ← 乱序 (230 在 220 前)
        // canonical ordering 后: q + U+0323 (220) + U+0301 (230)  ← CCC 升序
        // q 不参与合并 (不在 compose 表), 故输出码点序列 = [q, U+0323, U+0301]
        char32_t in[] = {U'q', U'\u0301', U'\u0323'};
        utf8pp s(in, 3);
        s.to_nfc();
        CHECK("ordering size==3", s.size() == 3);
        CHECK("ordering [0]==q",       s.at(0) == U'q');
        CHECK("ordering [1]==U+0323",  s.at(1) == U'\u0323');
        CHECK("ordering [2]==U+0301",  s.at(2) == U'\u0301');
    }
    {
        // 反向: q + U+0323 (220) + U+0301 (230)  ← 已有序
        char32_t in[] = {U'q', U'\u0323', U'\u0301'};
        utf8pp s(in, 3);
        s.to_nfc();
        CHECK("ordered input unchanged [1]==U+0323", s.at(1) == U'\u0323');
        CHECK("ordered input unchanged [2]==U+0301", s.at(2) == U'\u0301');
    }

    // ============================================================
    std::printf("\n[7] NFC 乱序输入 + 合并 (e + U+0301 + U+0323)\n");
    // ============================================================
    {
        // e + U+0301 (230) + U+0323 (220)  ← 乱序
        // 排序后: e + U+0323 (220) + U+0301 (230)
        // 合并: e + U+0301 -> é, 剩 é + U+0323
        char32_t in[] = {U'e', U'\u0301', U'\u0323'};
        utf8pp s(in, 3);
        s.to_nfc();
        CHECK("e+0301+0323 NFC size==2", s.size() == 2);
        CHECK("NFC [0]==é (U+00E9)",     s.at(0) == U'\u00E9');
        CHECK("NFC [1]==U+0323",         s.at(1) == U'\u0323');
    }

    // ============================================================
    std::printf("\n[8] NFD 分解 (é -> e + U+0301)\n");
    // ============================================================
    {
        utf8pp s(U"\u00E9");
        CHECK("é size==1", s.size() == 1);
        s.to_nfd();
        CHECK("NFD size==2",   s.size() == 2);
        CHECK("NFD [0]==e",    s.at(0) == U'e');
        CHECK("NFD [1]==U+0301", s.at(1) == U'\u0301');
    }

    // ============================================================
    std::printf("\n[9] NFC 副本接口 nfc()\n");
    // ============================================================
    {
        char32_t cps[] = {U'a', U'\u0308'}; // a + diaeresis -> ä
        utf8pp s(cps, 2);
        utf8pp n = s.nfc();
        CHECK("nfc() size==1", n.size() == 1);
        CHECK("nfc() at(0)==ä (U+00E4)", n.at(0) == U'\u00E4');
        CHECK("original unchanged size==2", s.size() == 2);
    }

    // ============================================================
    std::printf("\n[10] Hangul 算法分解 (가 → ㄱ + ㅏ)\n");
    // ============================================================
    {
        namespace ud = unicode_data;
        uint32_t out[3] = {0, 0, 0};
        uint32_t n = ud::hangul_decompose(0xAC00, out); // 가 (S_BASE)
        CHECK("가 is_hangul_syllable", ud::is_hangul_syllable(0xAC00));
        CHECK("가 decompose len==2", n == 2);
        CHECK("가 → ㄱ (U+1100)", out[0] == 0x1100);
        CHECK("가 → ㅏ (U+1161)", out[1] == 0x1161);

        // LVT: 각 (가 + ㄱ) = U+AC01
        uint32_t out2[3] = {0, 0, 0};
        uint32_t n2 = ud::hangul_decompose(0xAC01, out2);
        CHECK("각 decompose len==3", n2 == 3);
        CHECK("각 → ㄱ (U+1100)", out2[0] == 0x1100);
        CHECK("각 → ㅏ (U+1161)", out2[1] == 0x1161);
        CHECK("각 → ㄱ (U+11A8)", out2[2] == 0x11A8);

        // 非音节
        CHECK("'A' decompose len==0", ud::hangul_decompose(U'A', out) == 0);
    }

    // ============================================================
    std::printf("\n[11] Hangul 算法组合 (L+V → LV, LV+T → LVT)\n");
    // ============================================================
    {
        namespace ud = unicode_data;
        // ㄱ (U+1100) + ㅏ (U+1161) → 가 (U+AC00)
        CHECK("L+V → 가", ud::hangul_compose(0x1100, 0x1161) == 0xAC00);
        // 가 (U+AC00) + ㄱ (U+11A8) → 각 (U+AC01)
        CHECK("LV+T → 각", ud::hangul_compose(0xAC00, 0x11A8) == 0xAC01);
        // 不可组合: A + B
        CHECK("A+B 不可组合", ud::hangul_compose(U'A', U'B') == 0);
    }

    // ============================================================
    std::printf("\n[12] NFC Hangul (L+V → 가)\n");
    // ============================================================
    {
        char32_t cps[] = {char32_t(0x1100), char32_t(0x1161)}; // ㄱ + ㅏ
        utf8pp s(cps, 2);
        s.to_nfc();
        CHECK("NFC Hangul size==1", s.size() == 1);
        CHECK("NFC Hangul at(0)==가 (U+AC00)", s.at(0) == char32_t(0xAC00));

        // L+V+T → LVT
        char32_t cps2[] = {char32_t(0x1100), char32_t(0x1161), char32_t(0x11A8)};
        utf8pp s2(cps2, 3);
        s2.to_nfc();
        CHECK("NFC Hangul LVT size==1", s2.size() == 1);
        CHECK("NFC Hangul LVT at(0)==각 (U+AC01)", s2.at(0) == char32_t(0xAC01));
    }

    // ============================================================
    std::printf("\n[13] NFD Hangul (가 → L+V)\n");
    // ============================================================
    {
        utf8pp s(size_t(1), char32_t(0xAC00)); // 가
        s.to_nfd();
        CHECK("NFD Hangul size==2", s.size() == 2);
        CHECK("NFD Hangul [0]==ㄱ", s.at(0) == char32_t(0x1100));
        CHECK("NFD Hangul [1]==ㅏ", s.at(1) == char32_t(0x1161));
    }

    // ============================================================
    std::printf("\n[14] NFKD 全角 ASCII → 半角\n");
    // ============================================================
    {
        namespace ud = unicode_data;
        uint32_t out = 0;
        CHECK("全角 A → A", ud::nfkd_fullwidth_decompose(0xFF21, out) && out == U'A');
        CHECK("全角 z → z", ud::nfkd_fullwidth_decompose(0xFF5A, out) && out == U'z');
        CHECK("全角 0 → 0", ud::nfkd_fullwidth_decompose(0xFF10, out) && out == U'0');
        CHECK("全角 ! → !", ud::nfkd_fullwidth_decompose(0xFF01, out) && out == U'!');
        CHECK("全角空格 → 空格", ud::nfkd_fullwidth_decompose(0x3000, out) && out == U' ');
        CHECK("半角 A 不处理", !ud::nfkd_fullwidth_decompose(U'A', out));

        // 整串 NFKD
        char32_t full[] = {char32_t(0xFF21), char32_t(0xFF22), char32_t(0xFF23)}; // ＡＢＣ
        utf8pp s(full, 3);
        s.to_nfkd();
        CHECK("NFKD ＡＢＣ → ABC size==3", s.size() == 3);
        CHECK("NFKD [0]==A", s.at(0) == U'A');
        CHECK("NFKD [1]==B", s.at(1) == U'B');
        CHECK("NFKD [2]==C", s.at(2) == U'C');
    }

    // ============================================================
    std::printf("\n[15] NFKD 连字 (ﬁ → fi)\n");
    // ============================================================
    {
        char32_t lig = U'\uFB01'; // ﬁ
        utf8pp s(&lig, 1);
        s.to_nfkd();
        CHECK("NFKD ﬁ → fi size==2", s.size() == 2);
        CHECK("NFKD ﬁ [0]==f", s.at(0) == U'f');
        CHECK("NFKD ﬁ [1]==i", s.at(1) == U'i');

        char32_t lig2 = U'\uFB00'; // ﬀ
        utf8pp s2(&lig2, 1);
        s2.to_nfkd();
        CHECK("NFKD ﬀ → ff size==2", s2.size() == 2);
        CHECK("NFKD ﬀ [0]==f", s2.at(0) == U'f');
    }

    // ============================================================
    std::printf("\n[16] NFKD 罗马数字 (Ⅳ → IV)\n");
    // ============================================================
    {
        char32_t rn = U'\u2163'; // Ⅳ
        utf8pp s(&rn, 1);
        s.to_nfkd();
        CHECK("NFKD Ⅳ → IV size==2", s.size() == 2);
        CHECK("NFKD Ⅳ [0]==I", s.at(0) == U'I');
        CHECK("NFKD Ⅳ [1]==V", s.at(1) == U'V');
    }

    // ============================================================
    std::printf("\n[17] NFKD 圆圈数字 (① → 1)\n");
    // ============================================================
    {
        char32_t circ = U'\u2460'; // ①
        utf8pp s(&circ, 1);
        s.to_nfkd();
        CHECK("NFKD ① → 1 size==1", s.size() == 1);
        CHECK("NFKD ① [0]=='1'", s.at(0) == U'1');
    }

    // ============================================================
    std::printf("\n[18] NFKD 上标数字 (² → 2)\n");
    // ============================================================
    {
        char32_t sup = U'\u00B2'; // ²
        utf8pp s(&sup, 1);
        s.to_nfkd();
        CHECK("NFKD ² → 2 size==1", s.size() == 1);
        CHECK("NFKD ² [0]=='2'", s.at(0) == U'2');
    }

    // ============================================================
    std::printf("\n[19] NFKD 半角片假名 (ｱ → ア)\n");
    // ============================================================
    {
        char32_t half = U'\uFF71'; // ｱ
        utf8pp s(&half, 1);
        s.to_nfkd();
        CHECK("NFKD ｱ → ア size==1", s.size() == 1);
        CHECK("NFKD ｱ [0]==ア (U+30A2)", s.at(0) == char32_t(0x30A2));
    }

    // ============================================================
    std::printf("\n[20] NFKC = NFKD + compose\n");
    // ============================================================
    {
        // 全角 é (U+FF29? 不, 用 全角 e + 组合重音 测试)
        // 简单: 全角 Ａ (U+FF21) NFKC → A (单字符, 无需 compose)
        char32_t full_a = U'\uFF21';
        utf8pp s(&full_a, 1);
        s.to_nfkc();
        CHECK("NFKC Ａ → A", s.size() == 1 && s.at(0) == U'A');

        // 混合: 全角 Ａ + 组合重音 → NFKC 后分解为 A, 再与 U+0301 组合为 Á (U+00C1)
        char32_t mix[] = {char32_t(0xFF21), char32_t(0x0301)};
        utf8pp s2(mix, 2);
        s2.to_nfkc();
        CHECK("NFKC Ａ+´ → Á size==1", s2.size() == 1);
        CHECK("NFKC Ａ+´ [0]==Á (U+00C1)", s2.at(0) == U'\u00C1');
    }

    // ============================================================
    std::printf("\n[21] 副本接口 nfkc()/nfkd()\n");
    // ============================================================
    {
        char32_t lig = U'\uFB02'; // ﬂ
        utf8pp s(&lig, 1);
        utf8pp k = s.nfkd();
        CHECK("nfkd() ﬂ → fl size==2", k.size() == 2);
        CHECK("nfkd() [0]==f", k.at(0) == U'f');
        CHECK("nfkd() [1]==l", k.at(1) == U'l');
        CHECK("original unchanged", s.size() == 1 && s.at(0) == U'\uFB02');
    }

    // ============================================================
    std::printf("\n[22] 规范化幂等性 (NFC(NFC(x)) == NFC(x))\n");
    // ============================================================
    {
        char32_t cps[] = {U'e', U'\u0301'};
        utf8pp s(cps, 2);
        s.to_nfc();
        utf8pp s2 = s;
        s2.to_nfc();
        CHECK("NFC 幂等 size", s.size() == s2.size());
        CHECK("NFC 幂等 content", s == s2);

        // Hangul 幂等
        utf8pp hg(size_t(1), char32_t(0xAC00)); // 가
        hg.to_nfc();
        utf8pp hg2 = hg;
        hg2.to_nfc();
        CHECK("Hangul NFC 幂等", hg == hg2);
    }

    std::printf("\n========================================\n");
    std::printf("PASS: %d, FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
