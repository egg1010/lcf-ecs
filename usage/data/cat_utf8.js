window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['utf8pp'] = {
  id: 'utf8pp',
  title: "utf8_codec / utf8pp — UTF-8 编解码与拥有型字符串",
  category: 'utf8',
  icon: 'U',
  order: 36,
  content: `## 35. utf8_codec / utf8pp — UTF-8 编解码与拥有型字符串

\`#include "part/utf8pp/utf8pp.hpp"\`（自动包含 \`utf8_codec.hpp\` 与 \`dense.hpp\`），全局命名空间。核心接口 \`noexcept\`，修改操作（需扩容时失败 \`std::abort\`）亦为 \`noexcept\`。

模块分三层：

- **\`utf8_codec.hpp\`**：UTF-8 编解码函数。
- **\`utf8pp\` 类**：拥有内存的 UTF-8 字符串类，码点级访问与字节级访问。
- **\`utf8pp\` 非成员接口**：\`operator+\` / 流操作符 / \`swap\` / 字面量 / \`std::hash\` 特化。

仅支持 Unicode 编码（UTF-8 / UTF-32 码点），不支持其他编码（GBK / UTF-16 等）。非法序列替换为 U+FFFD 替换字符。支持完整 Unicode 字符处理：码点级 + 字形簇级 + East Asian Width 显示宽度 + NFC/NFKC 规范化（含 Hangul 算法） + Script 脚本判断。

### 35.1 宏配置（utf8pp_config.hpp）

| 宏 | 说明 |
|------|------|
| \`UTF8PP_USE_LAYERED_ALLOCATOR\` | 启用分层分配器，堆路径接入 \`layered_allocator\`（slab 分桶 + big_pool） |
| \`UTF8PP_LAYERED_ALLOCATOR_NOT_ENABLED\` | 禁用分层分配器，堆路径回退 \`std::malloc\` / \`std::free\` |

> 二选一，默认启用 \`UTF8PP_USE_LAYERED_ALLOCATOR\`。

### 35.2 编解码接口（utf8_codec.hpp）

| 接口 | 说明 |
|------|------|
| \`to_char(uint32_t cp)\` | 数值转 Unicode 字符（\`char32_t\`），constexpr |
| \`to_int(char32_t ch)\` | Unicode 字符转数值（\`uint32_t\`），constexpr |
| \`utf8_to_codepoints(src, src_len, out, out_cap, out_has_err)\` | UTF-8 字符串转码点数组（\`uint32_t*\`） |
| \`codepoints_to_char32(cps, cp_count, out, out_cap, out_has_err)\` | 码点数组转 \`char32_t\` 字符串 |
| \`char32_to_utf8(src, src_len, out, out_cap, out_has_err)\` | \`char32_t\` 字符串转 UTF-8 字节序列 |

\`out_has_err\` 为可选输出参数（\`bool*\`），传入 \`nullptr\` 表示不接收错误标志。返回值为需要写入的总数：\`<= out_cap\` 表示已全部写入；\`> out_cap\` 表示所需容量（调用方扩容后重试）。

### 35.3 utf8pp 类接口

#### 构造与赋值

| 接口 | 说明 |
|------|------|
| \`utf8pp()\` | 默认构造，空字符串（SSO 模式） |
| \`utf8pp(const char* s)\` / \`utf8pp(const char* s, size_t byte_len)\` | 从 UTF-8 C 字符串构造 |
| \`utf8pp(const char8_t* s)\` / \`utf8pp(const char8_t* s, size_t byte_len)\` | 从 \`char8_t\` 字符串构造 |
| \`utf8pp(const char32_t* s, size_t cp_count)\` | 从码点数组构造 |
| \`utf8pp(size_t n, char32_t cp)\` | 用 n 个 cp 填充构造 |
| \`utf8pp(std::string_view sv)\` | 从 string_view 构造 |
| \`utf8pp(const std::string& s)\` / \`utf8pp(const std::u8string& s)\` / \`utf8pp(const std::u32string& s)\` | 从 std 字符串构造 |
| \`utf8pp(const utf8_view& v)\` | 从 \`utf8_view\` 构造 |
| \`utf8pp(std::initializer_list<char32_t> il)\` | 从码点初始化列表构造 |
| \`utf8pp(InputIt first, InputIt last)\` | 从迭代器范围构造（SFINAE 排除整数类型） |
| \`explicit utf8pp(const std::array<utf8pp, N>& parts)\` / \`explicit utf8pp(const std::vector<utf8pp>& parts)\` | 从范围拼接构造 |
| \`utf8pp(const utf8pp&)\` / \`utf8pp(utf8pp&&) noexcept\` | 拷贝/移动构造 |
| \`utf8pp(std::nullptr_t) = delete\` | 禁止从 nullptr 构造 |
| \`operator=(const utf8pp&)\` / \`operator=(utf8pp&&) noexcept\` | 拷贝/移动赋值 |
| \`operator=(const char*)\` / \`operator=(std::string_view)\` / \`operator=(char32_t)\` | 从常见类型赋值 |
| \`operator=(const char8_t*)\` / \`operator=(std::initializer_list<char32_t>)\` | 从 char8_t/初始化列表赋值 |
| \`operator=(const std::string&)\` / \`operator=(const std::u8string&)\` | 从 std 字符串赋值 |
| \`operator=(const utf8_view& v)\` | 从 \`utf8_view\` 赋值 |
| \`assign(const char* s, size_t byte_len)\` / \`assign(const char* s)\` / \`assign(const utf8pp&)\` | 重新赋值 |
| \`assign(std::string_view)\` / \`assign(const std::string&)\` / \`assign(const char8_t*)\` / \`assign(const utf8_view&)\` | 重新赋值 |
| \`assign(std::initializer_list<char32_t>)\` / \`assign(InputIt first, InputIt last)\` | 从初始化列表/迭代器范围赋值 |
| \`assign(size_t n, char32_t cp)\` | 重新赋值为 n 个 cp |
| \`assign(const std::u8string&)\` / \`assign(const std::u32string&)\` | 从 u8/u32 字符串赋值 |
| \`assign(const utf8pp& other, size_t pos, size_t n = npos)\` | 从 other 的子串 [pos, pos+n) 赋值 |
| \`assign(const std::array<utf8pp, N>&)\` / \`assign(const std::vector<utf8pp>&)\` | 从范围重新赋值 |
| \`swap(utf8pp&) noexcept\` / \`swap(utf8pp&, utf8pp&) noexcept\` | 交换 |

#### 容量

| 接口 | 说明 |
|------|------|
| \`size()\` / \`length()\` | 码点数 |
| \`byte_size()\` | 字节数 |
| \`capacity()\` | 当前字节容量 |
| \`cp_capacity()\` | 当前码点容量（与字节容量解耦） |
| \`max_size()\` | 理论最大字节数 |
| \`empty()\` | 是否为空 |
| \`is_sso()\` | 是否处于 SSO 模式（constexpr） |
| \`sso_capacity()\` | SSO 容量（constexpr，= 103） |
| \`reserve(n)\` | 预留字节容量（按增长策略放大） |
| \`reserve_exact(n)\` | 精确预留字节容量（强制增长到 n） |
| \`reserve_cp(n)\` | 预留码点容量 |
| \`increase_capacity(new_cap)\` | 同时扩容字节缓冲与码点偏移数组 |
| \`shrink_to_fit()\` | 释放多余容量（若可回退到 SSO 则回退） |
| \`clear() noexcept\` | 清空内容（不释放内存） |
| \`rebuild_cp_offsets() noexcept\` | 直接修改 \`data()\` 后重建码点偏移缓存 |
| \`rebuild(new_byte_size) noexcept\` | 设置新字节大小并重建码点偏移缓存（\`data()\` 修改后的便捷接口） |

#### 访问

| 接口 | 说明 |
|------|------|
| \`at(cp_idx)\` / \`operator[](cp_idx)\` | 码点索引访问（越界返回 U+FFFD） |
| \`front()\` / \`back()\` | 首尾码点（空串返回 U+FFFD） |
| \`c_str()\` / \`data()\` (const) | C 字符串（**空对象返回 \`""\`，非 nullptr**） |
| \`data()\` (非 const) | 可写字节指针（修改后须调用 \`rebuild_cp_offsets()\` 重建缓存） |
| \`view()\` / \`binary_view()\` | \`std::string_view\`（两者等价，\`binary_view\` 强调字节语义） |
| \`u8view()\` | \`std::u8string_view\` |

#### 迭代器

| 接口 | 说明 |
|------|------|
| \`begin()\` / \`end()\` / \`cbegin()\` / \`cend()\` | 码点级前向迭代器（\`contiguous_iterator_tag\`，解引用返回 \`char32_t\`） |
| \`rbegin()\` / \`rend()\` / \`crbegin()\` / \`crend()\` | 码点级反向迭代器（\`bidirectional_iterator_tag\`） |
| \`make_iterator(cp_idx)\` | 构建指向指定码点索引的迭代器（insert/erase 后使用） |
| \`byte_begin()\` / \`byte_end()\` / \`byte_cbegin()\` / \`byte_cend()\` | 字节级前向迭代器（\`contiguous_iterator_tag\`，解引用返回 \`char\`） |
| \`rbyte_begin()\` / \`rbyte_end()\` / \`byte_crbegin()\` / \`byte_crend()\` | 字节级反向迭代器（\`contiguous_iterator_tag\`） |
| \`const_iterator::ptr()\` / \`const_iterator::cp_ptr()\` | 暴露 \`const char*\` / \`const char32_t*\`（用于与 C API 交互） |
| \`const_byte_iterator::ptr()\` | 暴露 \`const char*\` |

#### 字形簇迭代器

字形簇 = 用户感知字符（组合标记 / ZWJ 序列不分割），遵循 UAX #29 简化规则。解引用返回 \`utf8_view\`，覆盖组合标记、Hangul LVT 序列、Emoji ZWJ 序列等场景。

| 接口 | 说明 |
|------|------|
| \`grapheme_begin()\` / \`grapheme_end()\` | 字形簇级前向迭代器（\`forward_iterator_tag\`） |
| \`grapheme_cbegin()\` / \`grapheme_cend()\` | 常量字形簇级前向迭代器 |
| \`grapheme_count()\` | 字形簇数量 |
| \`grapheme_clusters()\` | 按字形簇分割，返回 \`dense<utf8_view>\` |
| \`const_grapheme_iterator::ptr()\` | 当前字形簇结束位置 \`const char*\` |
| \`const_grapheme_iterator::start()\` | 当前字形簇起始位置 \`const char*\` |

#### 修改操作

| 接口 | 说明 |
|------|------|
| \`push_back(char32_t cp)\` | 追加单个码点 |
| \`append(const char*)\` / \`append(const char*, size_t)\` / \`append(const char8_t*)\` | 追加 UTF-8 字符串 |
| \`append(const char32_t*, size_t)\` / \`append(const utf8pp&)\` / \`append(std::string_view)\` | 追加其他形式 |
| \`append(const std::string&)\` / \`append(const std::u8string&)\` / \`append(const std::u32string&)\` | 追加 std 字符串 |
| \`append(std::initializer_list<char32_t>)\` / \`append(size_t n, char32_t cp)\` | 追加初始化列表 / n 个相同码点 |
| \`append(InputIt first, InputIt last)\` | 追加迭代器范围（SFINAE 排除整数类型） |
| \`append(std::array<utf8pp, N>&)\` / \`append(std::vector<utf8pp>&)\` / \`append(const utf8pp*, size_t)\` / \`append(std::span<const utf8pp>)\` | 批量追加 |
| \`operator+=(char32_t)\` / \`operator+=(const char*)\` / \`operator+=(const utf8pp&)\` | 追加运算符 |
| \`operator+=(std::string_view)\` / \`operator+=(const char8_t*)\` / \`operator+=(std::initializer_list<char32_t>)\` | 追加运算符扩展 |
| \`insert(cp_idx, char32_t)\` | 按码点索引插入单码点 |
| \`insert(cp_idx, const utf8pp&)\` / \`insert(cp_idx, const char*)\` / \`insert(cp_idx, std::string_view)\` | 按码点索引插入字符串 |
| \`insert(cp_idx, const char* s, size_t n)\` / \`insert(cp_idx, size_t n, char32_t cp)\` / \`insert(cp_idx, std::initializer_list<char32_t>)\` | 按码点索引插入（指定长度/填充/初始化列表） |
| \`insert(cp_idx, const utf8pp& str, size_t pos2, size_t n2)\` | 子串插入：从 str 的 [pos2, pos2+n2) 插入 |
| \`insert(const_iterator pos, char32_t)\` / \`insert(pos, size_t n, char32_t cp)\` | 迭代器版插入 |
| \`insert(const_iterator pos, InputIt first, InputIt last)\` | 迭代器版插入（模板迭代器范围） |
| \`insert(const_iterator pos, const utf8pp&)\` / \`insert(pos, const char*)\` / \`insert(pos, const char*, size_t byte_len)\` / \`insert(pos, std::string_view)\` / \`insert(pos, std::initializer_list<char32_t>)\` | 迭代器版插入字符串 |
| \`erase(cp_idx, n=1)\` | 按码点索引删除 n 个码点 |
| \`erase(const_iterator pos)\` / \`erase(first, last)\` | 迭代器版删除 |
| \`pop_back()\` | 删除末尾码点 |
| \`substr(pos, cp_count=npos)\` | 按码点索引取子串（返回 utf8pp） |
| \`append_cp(n, cp)\` | 追加 n 个相同码点 |
| \`assign_cp(n, cp)\` | 清空后赋 n 个相同码点 |
| \`resize_cp(n, cp=U'\\0')\` / \`resize(n)\` / \`resize(n, cp)\` | 调整码点数（小于则截断，大于则补 cp；\`resize\` 为 \`std::string\` 别名） |
| \`replace(pos, n, const utf8pp&)\` / \`replace(pos, n, const char*)\` / \`replace(pos, n, std::string_view)\` | 替换 [pos, pos+n) |
| \`replace(pos, n, const char* s, size_t n2)\` / \`replace(pos, n, size_t n2, char32_t cp)\` / \`replace(pos, n, std::initializer_list<char32_t>)\` | 替换（指定长度/填充/初始化列表） |
| \`replace(pos1, n1, const utf8pp& other, pos2, n2)\` / \`replace(pos1, n1, const char* s, pos2, n2)\` | 双区间替换（本串 [pos1,pos1+n1) ← other [pos2,pos2+n2)） |
| \`replace(const_iterator first, last, const utf8pp&)\` / \`replace(first, last, const char*)\` / \`replace(first, last, std::string_view)\` | 迭代器范围替换 |
| \`replace(const_iterator first, last, const char* s, size_t n2)\` / \`replace(first, last, size_t n2, char32_t cp)\` | 迭代器范围替换（指定长度/填充） |
| \`replace(const_iterator first, last, InputIt ifirst, InputIt ilast)\` | 迭代器范围替换（模板迭代器范围） |
| \`replace_all(const utf8pp& old, const utf8pp& new)\` / \`replace_all(const char*, const char*)\` / \`replace_all(std::string_view, std::string_view)\` | 全局替换子串 |
| \`replace_all(const char* old, const utf8pp& new)\` / \`replace_all(const utf8pp& old, const char* new)\` | 全局替换子串（混合重载） |
| \`replace_all(char32_t old_cp, char32_t new_cp)\` | 全局替换单码点 |
| \`trim_left()\` / \`trim_right()\` / \`trim()\` | 去除首/尾/两端空白（Unicode 空白） |
| \`trim_left(Pred pred)\` / \`trim_right(Pred pred)\` / \`trim(Pred pred)\` | 谓词版去除（Pred: \`bool(char32_t)\`，SFINAE 约束） |
| \`trim(const utf8pp& chars)\` / \`trim_left(const utf8pp& chars)\` / \`trim_right(const utf8pp& chars)\` | 字符集版去除 |
| \`trim(const char* chars)\` / \`trim_left(const char* chars)\` / \`trim_right(const char* chars)\` | 字符集版去除（C 字符串） |
| \`trimmed()\` / \`trimmed_left()\` / \`trimmed_right()\` | 返回去除后的副本（含谓词/字符集重载） |
| \`to_lower()\` / \`to_upper()\` | 大小写转换（完整 Unicode，原地修改） |
| \`to_title()\` / \`swapcase()\` | 首字母大写 / 大小写互换（完整 Unicode，原地修改） |
| \`lowered()\` / \`uppered()\` / \`titled()\` / \`swapcased()\` | 返回转换后的副本 |
| \`reverse()\` | 码点级反转（原地修改） |
| \`reversed()\` | 返回反转后的副本 |
| \`strip_bom()\` | 剥离 UTF-8 BOM |
| \`copy(char* buf, size_t n, pos=0)\` | 拷贝到外部缓冲区（返回拷贝字节数） |
| \`pad_left(width, fill=U' ')\` / \`pad_right(width, fill=U' ')\` / \`center(width, fill=U' ')\` | 显示宽度对齐填充（East Asian Width 感知，全角=2，原地修改） |
| \`padded_left(width, fill=U' ')\` / \`padded_right(width, fill=U' ')\` / \`centered(width, fill=U' ')\` | 返回对齐填充后的副本 |
| \`display_width()\` | 计算显示宽度（East Asian Width: 全角=2, 零宽=0, ASCII=1） |
| \`to_nfc()\` / \`to_nfd()\` | NFC 规范化 / NFD 分解（原地修改，含 Hangul 算法分解） |
| \`to_nfkc()\` / \`to_nfkd()\` | NFKC 规范化 / NFKD 分解（原地修改，含兼容性字符分解：全角→半角、连字、上标、罗马数字等） |
| \`nfc()\` / \`nfd()\` / \`nfkc()\` / \`nfkd()\` | 返回规范化后的副本 |

#### 字节级访问

| 接口 | 说明 |
|------|------|
| \`byte_at(byte_idx)\` | 字节索引访问（越界返回 \`'\\0'\`） |
| \`at_byte(byte_idx)\` | 字节索引访问（越界 \`std::abort\`） |
| \`byte_substr(byte_pos, byte_len=npos)\` | 字节级子串（返回 utf8pp，不校验 UTF-8 边界） |
| \`byte_to_cp_idx(byte_idx)\` | 字节索引 → 码点索引（非码点起点返回 \`npos\`） |
| \`cp_to_byte_idx(cp_idx)\` | 码点索引 → 字节索引（越界返回 \`byte_size()\`） |

#### 字符分类 API（静态方法 + 串级判断）

| 接口 | 说明 |
|------|------|
| \`static is_alpha(cp)\` / \`static is_digit(cp)\` / \`static is_alnum(cp)\` | 字母 / 数字 / 字母数字（完整 Unicode） |
| \`static is_space(cp)\` / \`static is_punct(cp)\` | 空白 / 标点（Unicode） |
| \`static is_lower(cp)\` / \`static is_upper(cp)\` | 小写 / 大写（完整 Unicode） |
| \`static is_xdigit(cp)\` / \`static is_cntrl(cp)\` / \`static is_printable(cp)\` | 十六进制 / 控制字符 / 可打印 |
| \`static is_combining(cp)\` / \`static is_wide(cp)\` / \`static is_zero_width(cp)\` / \`static is_emoji(cp)\` | 组合标记 / 宽字符 / 零宽 / Emoji |
| \`static cp_width(cp)\` | 单码点显示宽度（0/1/2） |
| \`static to_lower_cp(cp)\` / \`static to_upper_cp(cp)\` / \`static to_title_cp(cp)\` | 单码点大小写转换（完整 Unicode） |
| \`static script_of(cp)\` | 单码点所属脚本（\`utf8pp::script\` 枚举：Latin/Han/Hiragana/Arabic/Emoji 等） |
| \`static is_script(cp, s)\` | 单码点是否属于指定脚本 |
| \`static script_name(s)\` | 脚本枚举 → 名称字符串 |
| \`is_all_alpha()\` / \`is_all_digit()\` / \`is_all_alnum()\` | 整串是否全为字母/数字/字母数字 |
| \`is_all_space()\` / \`is_all_xdigit()\` / \`is_all_printable()\` | 整串是否全为空白/十六进制/可打印 |
| \`script_of()\` | 整串首字符所属脚本（空串返回 \`script::unknown\`） |
| \`is_all_script(s)\` | 整串是否全属指定脚本（空串返回 false） |
| \`contains_script(s)\` | 是否包含至少一个指定脚本的码点 |

#### 数字转换

| 接口 | 说明 |
|------|------|
| \`to_int(pos*, base=10)\` / \`to_long(pos*, base=10)\` / \`to_ll(pos*, base=10)\` | 字符串 → int/long/long long（失败返回 0） |
| \`to_ulong(pos*, base=10)\` / \`to_ull(pos*, base=10)\` | 字符串 → unsigned long/unsigned long long |
| \`to_float(pos*)\` / \`to_double(pos*)\` / \`to_long_double(pos*)\` | 字符串 → float/double/long double |
| \`stoi/stol/stoll/stoul/stoull/stof/stod/stold(...)\` | std 风格别名（参数同上） |
| \`parse_int(out, base=10) noexcept\` / \`parse_long(out, base=10) noexcept\` / \`parse_ll(out, base=10) noexcept\` | 严格解析为 int/long/long long（返回 bool，允许前导 +/- 与首尾空白） |
| \`parse_ulong(out, base=10) noexcept\` / \`parse_ull(out, base=10) noexcept\` | 严格解析为 unsigned long/unsigned long long |
| \`parse_float(out) noexcept\` / \`parse_double(out) noexcept\` / \`parse_long_double(out) noexcept\` | 严格解析为浮点（返回 bool） |
| \`is_integer(base=10) noexcept\` / \`is_float() noexcept\` / \`is_number() noexcept\` | 内容判断：整数 / 浮点 / 数字 |
| \`is_hex() noexcept\` / \`is_binary() noexcept\` / \`is_octal() noexcept\` | 进制判断便捷别名 |

#### format / vformat

| 接口 | 说明 |
|------|------|
| \`static format(const char* fmt, ...)\` | printf 风格格式化（返回 utf8pp，自动扩容） |
| \`static vformat(const char* fmt, std::va_list ap)\` | va_list 版本 |

#### 查找

| 接口 | 说明 |
|------|------|
| \`find(char32_t, pos=0)\` / \`find(const utf8pp&, pos=0)\` | 正向查找码点/子串 |
| \`find(const char* s, pos=0)\` / \`find(std::string_view sv, pos=0)\` | 正向查找 C 字符串/string_view |
| \`find(const char* s, pos, n)\` | 正向查找 C 字符串前 n 字节（三参，与 \`std::string\` 对齐） |
| \`rfind(char32_t, pos=npos)\` / \`rfind(const utf8pp&, pos=npos)\` | 逆向查找 |
| \`rfind(const char* s, pos=npos)\` / \`rfind(std::string_view sv, pos=npos)\` | 逆向查找 C 字符串/string_view |
| \`rfind(const char* s, pos, n)\` | 逆向查找 C 字符串前 n 字节（三参） |
| \`find_first_of(char32_t, pos=0)\` / \`find_first_of(const utf8pp&, pos=0)\` | 首个匹配 |
| \`find_first_of(const char* s, pos=0)\` / \`find_first_of(std::string_view sv, pos=0)\` | 首个匹配（C 字符串/string_view） |
| \`find_first_of(const char* s, pos, n)\` | 首个匹配（三参） |
| \`find_last_of(char32_t, pos=npos)\` / \`find_last_of(const utf8pp&, pos=npos)\` | 末个匹配 |
| \`find_last_of(const char* s, pos=npos)\` / \`find_last_of(std::string_view sv, pos=npos)\` | 末个匹配（C 字符串/string_view） |
| \`find_last_of(const char* s, pos, n)\` | 末个匹配（三参） |
| \`find_first_not_of(char32_t, pos=0)\` / \`find_first_not_of(const utf8pp&, pos=0)\` | 首个不匹配 |
| \`find_first_not_of(const char* s, pos=0)\` / \`find_first_not_of(std::string_view sv, pos=0)\` | 首个不匹配（C 字符串/string_view） |
| \`find_first_not_of(const char* s, pos, n)\` | 首个不匹配（三参） |
| \`find_last_not_of(char32_t, pos=npos)\` / \`find_last_not_of(const utf8pp&, pos=npos)\` | 末个不匹配 |
| \`find_last_not_of(const char* s, pos=npos)\` / \`find_last_not_of(std::string_view sv, pos=npos)\` | 末个不匹配（C 字符串/string_view） |
| \`find_last_not_of(const char* s, pos, n)\` | 末个不匹配（三参） |
| \`count(char32_t)\` / \`count(const utf8pp&)\` / \`count(const char*)\` / \`count(std::string_view)\` | 统计出现次数 |
| \`contains(char32_t)\` / \`contains(const utf8pp&)\` / \`contains(const char*)\` / \`contains(std::string_view)\` | 包含判断 |
| \`starts_with(char32_t)\` / \`starts_with(const utf8pp&)\` / \`starts_with(const char*)\` / \`starts_with(std::string_view)\` | 前缀判断 |
| \`ends_with(char32_t)\` / \`ends_with(const utf8pp&)\` / \`ends_with(const char*)\` / \`ends_with(std::string_view)\` | 后缀判断 |

#### 比较与转换

| 接口 | 说明 |
|------|------|
| \`compare(const utf8pp&)\` / \`compare(const char*)\` / \`compare(std::string_view)\` | 三态比较（字节序 = 码点序） |
| \`compare(const std::string&)\` / \`compare(const std::u8string&)\` / \`compare(const std::u32string&)\` / \`compare(char32_t cp)\` | 与 std 字符串/单码点比较 |
| \`compare(pos, n, const utf8pp&)\` / \`compare(pos, n, const char*)\` / \`compare(pos, n, std::string_view)\` | 子串比较（本串 [pos, pos+n)） |
| \`compare(pos1, n1, const utf8pp& s, pos2, n2)\` / \`compare(pos1, n1, const char* s, n2)\` | 双区间比较 |
| \`operator==/!=/</>/<=/>=\` | 与 \`utf8pp\` / \`const char*\` / \`std::string_view\` / \`std::string\` / \`std::u8string\` / \`std::u32string\` / \`char32_t\` 比较 |
| \`operator<=>(const utf8pp&)\` / \`operator<=>(const char*)\` / \`operator<=>(std::string_view)\` | C++20 三路比较 |
| \`operator<=>(const std::string&)\` / \`operator<=>(const std::u8string&)\` / \`operator<=>(const std::u32string&)\` / \`operator<=>(char32_t)\` | C++20 三路比较扩展 |
| \`to_std_string()\` | 转 \`std::string\` |
| \`to_u32string()\` | 转 \`std::u32string\` |
| \`to_u8string()\` | 转 \`std::u8string\` |
| \`to_utf8_view()\` | 零拷贝转 \`utf8_view\`（指向内部缓冲区，生命周期受 \`*this\` 限制） |

#### BOM / 校验

| 接口 | 说明 |
|------|------|
| \`has_bom()\` | 是否以 UTF-8 BOM 开头 |
| \`strip_bom()\` | 剥离 UTF-8 BOM |
| \`valid()\` | 整串是否为合法 UTF-8 |
| \`validate()\` | 返回首个非法码点索引（全部合法返回 \`npos\`） |

#### split / join / split_view

| 接口 | 说明 |
|------|------|
| \`split(char32_t delim)\` | 按单码点分割，返回 \`dense<utf8pp>\` |
| \`split(const utf8pp& delim)\` / \`split(const char*)\` / \`split(std::string_view)\` | 按子串分割 |
| \`split_view(char32_t delim)\` / \`split_view(const utf8pp& delim)\` | 零拷贝分割，返回 \`dense<utf8_view>\`（复用原字符串内存，原串生命周期需覆盖视图使用） |
| \`split_view(const char*)\` / \`split_view(std::string_view)\` | 零拷贝分割（C 字符串/string_view） |
| \`split_to(char32_t, std::vector<utf8pp>&)\` / \`split_to(const utf8pp&, std::vector<utf8pp>&)\` | 分割到 std 容器 |
| \`split_to(const char*, std::vector<utf8pp>&)\` / \`split_to(std::string_view, std::vector<utf8pp>&)\` | 分割到 std 容器（C 字符串/string_view） |
| \`split_to(const utf8pp&, utf8pp* out, size_t cap)\` / \`split_to(const char*, utf8pp* out, size_t cap)\` / \`split_to(std::string_view, utf8pp* out, size_t cap)\` | 分割到裸指针缓冲 |
| \`static join(const dense<utf8pp>&, const utf8pp&)\` / \`static join(..., char32_t)\` | 拼接 |
| \`static join(const std::array<utf8pp, N>&, const utf8pp&)\` / \`static join(const std::vector<utf8pp>&, ...)\` | 拼接 std 容器 |
| \`static join(const utf8pp* parts, size_t count, const utf8pp&)\` | 拼接裸指针 |

### 35.4 utf8pp 非成员接口

| 接口 | 说明 |
|------|------|
| \`operator+(const utf8pp&, const utf8pp&)\` / \`operator+(const utf8pp&, char32_t)\` / \`operator+(char32_t, const utf8pp&)\` | 拼接 |
| \`operator+(const utf8pp&, const char*)\` / \`operator+(const char*, const utf8pp&)\` | 与 C 字符串拼接 |
| \`operator+(const utf8pp&, std::string_view)\` / \`operator+(std::string_view, const utf8pp&)\` | 与 string_view 拼接 |
| \`operator+(const utf8pp&, const char8_t*)\` / \`operator+(const char8_t*, const utf8pp&)\` | 与 char8_t 字符串拼接 |
| \`operator+(const utf8pp&, const std::string&)\` / \`operator+(const std::string&, const utf8pp&)\` | 与 std::string 拼接 |
| \`operator+(const utf8pp&, const std::u8string&)\` / \`operator+(const std::u8string&, const utf8pp&)\` | 与 std::u8string 拼接 |
| \`operator+(const utf8pp&, const std::u32string&)\` / \`operator+(const std::u32string&, const utf8pp&)\` | 与 std::u32string 拼接 |
| \`operator<<(std::ostream&, const utf8pp&)\` | 流输出（写字节内容） |
| \`operator>>(std::istream&, utf8pp&)\` | 流输入（追加读取） |
| \`getline(std::istream&, utf8pp&, char delim='\\n')\` | 按分隔符读取一行（自由函数） |
| \`swap(utf8pp&, utf8pp&) noexcept\` | 非成员 swap |
| \`to_utf8pp(int/long/long long/unsigned/unsigned long/unsigned long long)\` | 数字 → utf8pp（类似 \`std::to_string\`） |
| \`to_utf8pp(float/double/long double)\` | 浮点 → utf8pp |
| \`utf8pp_format(const char* fmt, ...)\` / \`utf8pp_vformat(const char* fmt, std::va_list ap)\` | 自由函数版格式化（与静态成员 \`format\`/\`vformat\` 并存） |
| \`"..."_u8\` (const char*) / \`"..."_u8\` (const char8_t*) | 字面量运算符（返回 utf8pp，两个重载） |
| \`"..."_utf8\` (const char*) / \`"..."_utf8\` (const char8_t*) | 字面量运算符别名（返回 utf8pp，两个重载） |
| \`std::hash<utf8pp>\` | hash 特化（**FNV-1a 字节哈希**，分布更均匀） |
| \`std::swap<utf8pp>\` | std::swap 特化（委托成员 \`swap\`） |
| \`erase(utf8pp&, char32_t cp)\` / \`erase_if(utf8pp&, Pred pred)\` | 全局删除所有匹配码点（C++20 风格，返回移除数量） |
| \`std::formatter<utf8pp>\` | C++20 \`std::format\` 特化（受 \`__cpp_lib_format >= 201907L\` 保护，支持 width/fill/align 及大小写转换标志 U/L/T/C） |

### 使用

\`\`\`cpp
#include "part/utf8pp/utf8pp.hpp"

// === 编解码函数 ===
char32_t ch = to_char(0x4E2D);       // '中'
uint32_t cps[16];
size_t n = utf8_to_codepoints("你好", 6, cps, 16);  // n = 2

// === utf8pp 字符串类 ===
utf8pp s("Hello你好");     // 从 UTF-8 C 字符串构造
s.size();                  // 7 (码点数)
s.byte_size();             // 11 (字节数: 5 + 6)
s.at(0);                   // 'H'
s.at(5);                   // '你' (U+4F60)

// 码点级迭代
for (char32_t cp : s) { /* 遍历每个码点 */ }

// 反向迭代
for (auto it = s.rbegin(); it != s.rend(); ++it) { /* 反向遍历 */ }

// 字形簇迭代 (用户感知字符, 组合标记/ZWJ 序列不分割)
utf8pp emoji_str(U"Hello\\U0001F600\\U0001F926\\U0001F631");
for (auto it = emoji_str.grapheme_begin(); it != emoji_str.grapheme_end(); ++it) {
    utf8_view gv = *it;  // 覆盖单个字形簇的字节范围
}
size_t gc = emoji_str.grapheme_count();  // 字形簇数量
dense<utf8_view> clusters = emoji_str.grapheme_clusters();

// 追加/插入/删除
s.push_back(char32_t(0x1F600));   // 追加 emoji
s.insert(0, char32_t('X'));       // 在首位插入
s.erase(0, 1);                    // 删除首位码点
s.pop_back();                     // 删除末尾
s.append_cp(3, U'-');             // 追加 3 个 '-'
s.resize_cp(10, U'.');            // 调整到 10 码点，补 '.'

// 子串/查找/比较
utf8pp sub = s.substr(0, 3);      // 取前 3 个码点
s.find(char32_t('H'));            // 码点级查找
s.rfind(U'好');                   // 逆向查找
s.contains("Hello");              // 包含判断
s.starts_with(U'H');              // 前缀判断
s.ends_with(U'好');               // 后缀判断
s.count(U'l');                    // 统计出现次数
s == utf8pp("Hello你好");

// 查找系列
s.find_first_of(utf8pp("aeiou"));
s.find_last_not_of(U' ');

// 字符串修改
utf8pp t("  Hello  World  ");
t.trim();                         // "Hello  World"
t.to_lower();                     // "hello  world"  (完整 Unicode)
t.to_title();                     // 首字母大写
t.swapcase();                     // 大小写互换
t.replace(0, 5, utf8pp("Hi"));    // "Hi   World"
t.replace_all(U'l', U'L');        // 全局替换单码点
t.reverse();                      // 码点级反转

// trim 谓词版 / 字符集版
utf8pp csv(",,abc,,");
csv.trim(U',');                   // "abc"  (字符集版)
csv.trim([](char32_t c){ return c == U',' || c == U' '; });  // 谓词版

// 对齐填充
utf8pp num("42");
num.pad_left(6, U'0');            // "000042"
utf8pp centered = utf8pp("Hi").centered(10, U'-');  // "----Hi----"

// NFC / NFD / NFKC / NFKD 规范化
utf8pp e_acute = utf8pp(U"\\u00E9");           // é (预组合)
e_acute.to_nfd();                  // → e + U+0301 (分解, size==2)
utf8pp e_acute2("e");
e_acute2.push_back(U'\\u0301');
e_acute2.to_nfc();                 // → é (组合, size==1)

utf8pp hangul(U"\\uAC00");          // 가 (Hangul 音节)
hangul.to_nfd();                   // → ㄱ + ㅏ (Hangul 算法分解)

utf8pp fullwidth(u8"\\uFF21\\uFF22\\uFF23");  // ＡＢＣ
fullwidth.to_nfkd();               // → ABC (兼容性分解: 全角→半角)

utf8pp lig(u8"\\uFB01");            // ﬁ (连字)
lig.to_nfkd();                     // → fi (连字分解)

// 字节级访问
utf8pp cn(u8"Hi中");              // 5 字节: H i 中(3)
cn.byte_at(0);                    // 'H'
cn.byte_substr(2, 3);             // "中" (字节切片)
cn.byte_to_cp_idx(2);             // 2 (字节索引→码点索引)
cn.cp_to_byte_idx(2);             // 2 (码点索引→字节索引)

// 字符分类
utf8pp::is_alpha(U'A');           // true
utf8pp::is_digit(U'5');           // true
utf8pp("12345").is_all_digit();   // true
utf8pp("Hello").is_all_alpha();   // true

// 数字转换
utf8pp num_str("3.14");
num_str.to_double();              // 3.14
num_str.parse_double(d);          // true, d=3.14 (严格解析)
utf8pp("ff").to_int(nullptr, 16); // 255 (十六进制)
utf8pp("42").is_integer();        // true

// format 静态方法
utf8pp fmt = utf8pp::format("x=%d, y=%.2f", 10, 3.14);

// split / join
dense<utf8pp> parts = utf8pp("a,b,c,d").split(U',');
utf8pp joined = utf8pp::join(parts, utf8pp("-"));  // "a-b-c-d"

// split_view 零拷贝（视图复用原串内存，原串生命周期需覆盖视图使用）
utf8pp src("a,b,c");
dense<utf8_view> views = src.split_view(U',');  // 3 个 utf8_view

// 兼容 std::vector / 裸指针输出
std::vector<utf8pp> vparts;
utf8pp("x/y/z").split_to(U'/', vparts);

utf8pp arr[8];
utf8pp("1 2 3").split_to(utf8pp(" "), arr, 8);

// 拼接运算符
utf8pp full = utf8pp("Hello") + U',' + utf8pp(" World");
utf8pp cat = utf8pp("a") + std::string("b") + std::u8string(u8"c");

// 字面量
auto u = "Hello"_u8;              // 返回 utf8pp (const char* 重载)
auto u2 = u8"你好"_u8;            // 返回 utf8pp (const char8_t* 重载)

// 数字 → utf8pp
utf8pp n1 = to_utf8pp(42);
utf8pp n2 = to_utf8pp(3.14);

// 全局 erase / erase_if
utf8pp e("Hello");
erase(e, U'l');                   // "Heo" (移除所有 'l')
erase_if(e, [](char32_t c){ return c == U'H'; });  // "eo"

// C++20 std::format
std::string s = std::format("{}", utf8pp("测试"));

// 空对象 c_str() 返回 ""（非 nullptr）
utf8pp empty;
empty.c_str();                    // ""
empty.data();                     // ""
empty.empty();                    // true
empty.is_sso();                   // true (空串走 SSO)

// BOM 处理
utf8pp bom(u8"\\uFEFFHello");
bom.has_bom();                    // true
bom.strip_bom();                  // 剥离后内容为 "Hello"

// 校验
utf8pp bad("abc\\xff\\xfe");
bad.valid();                      // false
bad.validate();                   // 返回首个非法码点索引

// 流 I/O
std::ostringstream os;
os << utf8pp("测试");
std::istringstream is("line1\\nline2");
utf8pp line;
getline(is, line);                // 读取一行
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 输入非 UTF-8 编码（如 GBK） | 解码出非法码点，被替换为 U+FFFD | 输入必须是合法 UTF-8 |
| 假设 \`size()\` 等于 \`byte_size()\` | 多字节字符两者不等 | 用 \`size()\` 取码点数，\`byte_size()\` 取字节数 |
| 越界访问 \`at()\` 不检查返回值 | 越界返回 U+FFFD，非崩溃 | 检查索引或用 \`operator[]\`（同样返回 U+FFFD） |
| 越界访问 \`at_byte()\` | 越界 \`std::abort\`（与 \`byte_at\` 返回 \`'\\0'\` 不同） | 用 \`byte_at\` 容错或确保索引合法 |
| 频繁 \`insert\`/\`erase\` 中间位置 | 每次需移动后续字节和偏移数组 | 高频中间修改考虑用其他数据结构 |
| \`byte_substr\` 切断多字节字符 | 不校验 UTF-8 边界，可能产生非法序列 | 仅用于已知边界的场景，或用码点级 \`substr\` |
| 对 \`to_lower()\` / \`to_upper()\` 期望 Unicode 大小写 | 已支持完整 Unicode | 覆盖 Latin/Greek/Cyrillic/Armenian/CJK 等主要脚本 |
| 期望 emoji ZWJ 序列作为单字符 | 码点级 \`reverse\`/\`split\` 会拆散 | 用 \`grapheme_begin\`/\`grapheme_end\` 按字形簇迭代 |
| 用 \`capacity()\` 判断码点容量 | \`capacity()\` 是字节容量，非码点容量 | 用 \`cp_capacity()\` 取码点容量，\`reserve_cp()\` 预留 |
| \`split_view\` 返回的视图悬垂 | 视图复用原串内存，原串析构后视图失效 | 确保原 \`utf8pp\` 生命周期覆盖所有 \`utf8_view\` 使用 |
| \`to_int\` 失败返回 0 无法区分 | 0 可能是合法值 | 严格场景用 \`parse_int(out, base)\` 返回 bool |
| 误用 \`to_nfc()\` 处理兼容性字符 | NFC 不分解兼容性字符（如全角、连字） | 兼容性场景用 \`to_nfkc()\`/\`to_nfkd()\` |
| 误用 \`to_nfd()\` 分解 Hangul 音节 | NFD 已支持 Hangul 算法分解（가→ㄱ+ㅏ） | Hangul 分解直接用 \`to_nfd()\`/\`to_nfkd()\` |
| 期望 NFKC 保持 A+组合标记不组合 | NFKC 包含 NFC 全部组合规则，A+U+0301→Á | 仅需兼容性分解不需组合时用 \`to_nfkd()\` |
| \`script_of\` 对组合标记返回 \`inherited\` | 组合标记继承前字符脚本 | 需上下文脚本时由调用者跟踪前一个 starter |

---
`
};

window.DOCS_DATA['utf8_view'] = {
  id: 'utf8_view',
  title: "utf8_view — UTF-8 字符串视图",
  category: 'utf8',
  icon: 'W',
  order: 37,
  content: `## 36. utf8_view — UTF-8 字符串视图

\`#include "part/utf8pp/utf8_view.hpp"\`（自动包含 \`utf8_codec.hpp\`），全局命名空间。所有接口 \`noexcept\`。

非拥有型轻量级 UTF-8 字符串视图，类似 \`std::string_view\`，但提供码点级接口。内部仅存储 \`const char*\` 与字节数两个字段（16 字节），不分配内存。

字节级接口直接操作指针，码点级接口需遍历解码。与 \`utf8pp\` 互转：\`utf8pp\` 可经 \`std::string_view\` 中转构造自 \`utf8_view\`（\`utf8_view\` 隐式转 \`std::string_view\`，\`utf8pp\` 接收 \`std::string_view\`）；\`utf8_view\` 可由 \`utf8pp\` 的 \`data()\` + \`byte_size()\` 显式构造。

### 36.1 构造与赋值

| 接口 | 说明 |
|------|------|
| \`utf8_view()\` | 默认构造，空视图 |
| \`utf8_view(const char* s)\` / \`utf8_view(const char* s, size_t byte_len)\` | 从 C 字符串构造 |
| \`utf8_view(const char8_t* s)\` / \`utf8_view(const char8_t* s, size_t byte_len)\` | 从 \`char8_t\` 构造 |
| \`utf8_view(std::string_view sv)\` | 从 \`string_view\` 构造 |
| \`utf8_view(std::u8string_view sv)\` | 从 \`u8string_view\` 构造 |
| \`utf8_view(const std::string& s)\` / \`utf8_view(const std::u8string& s)\` | 从 std 字符串构造 |
| \`operator=(const char*)\` / \`operator=(std::string_view)\` | 赋值 |

### 36.2 容量

| 接口 | 说明 |
|------|------|
| \`byte_size()\` / \`size_bytes()\` / \`length_bytes()\` | 字节数 |
| \`size()\` / \`length()\` | 码点数 |
| \`empty()\` | 是否为空 |
| \`max_size()\` | 理论最大值 |

### 36.3 数据访问

| 接口 | 说明 |
|------|------|
| \`data()\` / \`c_str()\` | 原始字节指针（\`data()\` 空视图返回 \`nullptr\`，\`c_str()\` 返回 \`""\`） |
| \`byte_view()\` / \`operator std::string_view()\` | 转 \`std::string_view\`（隐式转换） |
| \`byte_at(i)\` | 字节级访问（越界返回 \`'\\0'\`） |
| \`at(cp_idx)\` / \`operator[](cp_idx)\` | 码点级访问（越界返回 U+FFFD） |
| \`front()\` / \`back()\` | 首/尾码点（空视图返回 U+FFFD） |

### 36.4 迭代器

| 接口 | 说明 |
|------|------|
| \`begin()\` / \`end()\` / \`cbegin()\` / \`cend()\` | 码点级前向迭代器（\`forward_iterator_tag\`，仅支持 \`++\`） |
| \`rbegin()\` / \`rend()\` / \`crbegin()\` / \`crend()\` | 码点级反向迭代器（\`bidirectional_iterator_tag\`，支持 \`++\`/\`--\`） |
| \`iterator\` / \`reverse_iterator\` | 类型别名（等价 \`const_iterator\` / \`const_reverse_iterator\`） |
| \`const_iterator::ptr()\` | 暴露 \`const char*\`（用于与 C API 交互） |

> 注：正向迭代器为 forward（不支持 \`--\`），反向迭代器为 bidirectional（支持 \`--\`）。两者解引用均按值返回 \`char32_t\`（无 \`->\` 运算符）。

### 36.5 子串与修改

| 接口 | 说明 |
|------|------|
| \`substr_bytes(byte_pos, byte_len=npos)\` | 字节级子串 |
| \`substr(cp_pos, cp_count=npos)\` | 码点级子串 |
| \`remove_prefix(byte_n)\` | 移除前缀（STL 语义，字节级） |
| \`remove_suffix(byte_n)\` | 移除后缀（字节级） |
| \`copy(char* buf, size_t byte_n, byte_pos=0)\` | 拷贝到外部缓冲区 |
| \`swap(utf8_view&)\` / \`swap(utf8_view&, utf8_view&)\` | 交换 |

### 36.6 查找

| 接口 | 说明 |
|------|------|
| \`find_byte(char c, byte_pos=0)\` | 字节正向查找 |
| \`rfind_byte(char c, byte_pos=npos)\` | 字节逆向查找 |
| \`find_bytes(std::string_view str, byte_pos=0)\` | 字节子串正向查找 |
| \`rfind_bytes(std::string_view str, byte_pos=npos)\` | 字节子串逆向查找 |
| \`find(char32_t cp, cp_pos=0)\` / \`find(const utf8_view&, cp_pos=0)\` | 码点级正向查找 |
| \`rfind(char32_t cp, cp_pos=npos)\` / \`rfind(const utf8_view&, cp_pos=npos)\` | 码点级逆向查找 |
| \`find_first_of(char32_t, cp_pos=0)\` / \`find_first_of(const utf8_view&, cp_pos=0)\` | 首个匹配 |
| \`find_last_of(char32_t, cp_pos=npos)\` / \`find_last_of(const utf8_view&, cp_pos=npos)\` | 末个匹配 |
| \`find_first_not_of(char32_t, cp_pos=0)\` / \`find_first_not_of(const utf8_view&, cp_pos=0)\` | 首个不匹配 |
| \`find_last_not_of(char32_t, cp_pos=npos)\` / \`find_last_not_of(const utf8_view&, cp_pos=npos)\` | 末个不匹配 |
| \`contains(char32_t)\` / \`contains(const utf8_view&)\` | 包含判断 |
| \`starts_with(char32_t)\` | 码点级前缀判断 |
| \`starts_with(const utf8_view&)\` | 字节级前缀判断 |
| \`ends_with(char32_t)\` | 码点级后缀判断 |
| \`ends_with(const utf8_view&)\` | 字节级后缀判断 |

### 36.7 比较

| 接口 | 说明 |
|------|------|
| \`compare(const utf8_view&)\` / \`compare(std::string_view)\` / \`compare(const char*)\` | 三态比较（字节序） |
| \`operator==\` / \`!=\` / \`<\` / \`>\` / \`<=\` / \`>=\` | 与 \`utf8_view\` / \`std::string_view\` / \`const char*\` 比较 |
| \`operator<=>(const utf8_view&)\` / \`operator<=>(std::string_view)\` / \`operator<=>(const char*)\` | 三态比较运算符 |

### 36.8 只读查询（零分配）

| 接口 | 说明 |
|------|------|
| \`display_width()\` | 显示宽度（East Asian Width: CJK/全角=2, 零宽=0, 其他=1） |
| \`to_lower_into(out, cap)\` | 小写转换写入外部缓冲，返回写入字节数（cap 不足返回所需字节数） |
| \`to_upper_into(out, cap)\` | 大写转换写入外部缓冲，返回写入字节数（同上） |
| \`trimmed()\` / \`trimmed_left()\` / \`trimmed_right()\` | 返回去除空白后的子视图（仅指针移动，零分配） |
| \`is_ascii()\` | 全 ASCII 判断 |
| \`is_valid()\` | 合法 UTF-8 校验（码点范围 + 最短形式） |
| \`count(char32_t)\` | 统计码点出现次数 |

\`\`\`cpp
// 显示宽度 (对齐布局用)
size_t w = utf8_view("顺序访问").display_width();  // 8 (4 CJK × 2)

// 大小写转换 (零分配, 用户提供缓冲)
char buf[64];
size_t n = utf8_view("ABC").to_lower_into(buf, sizeof(buf));  // n=3, buf="abc"

// trim (返回子 view, 不分配)
utf8_view t = utf8_view("  hello  ").trimmed();  // "hello"

// 校验
bool ok = utf8_view("Hello你好").is_valid();   // true
bool ascii = utf8_view("Hello").is_ascii();    // true

// 计数
size_t n = utf8_view("a,b,c").count(U',');     // 2
\`\`\`

### 36.9 非成员接口

| 接口 | 说明 |
|------|------|
| \`operator<<(std::ostream&, const utf8_view&)\` | 流输出 |
| \`std::hash<utf8_view>\` | hash 特化（按字节 hash） |
| \`utf8_display_width(const char*)\` / \`utf8_display_width(string_view)\` | 直接算显示宽度，免构造 view |
| \`utf8_cp_count(const char*)\` / \`utf8_cp_count(string_view)\` | 直接算码点数 |
| \`utf8_byte_offset(s, cp_idx)\` | 第 cp_idx 个码点的字节偏移（越界返回 \`npos\`） |
| \`utf8_is_valid(const char*)\` / \`utf8_is_valid(string_view)\` | 合法 UTF-8 校验 |
| \`utf8_is_ascii(const char*)\` / \`utf8_is_ascii(string_view)\` | 纯 ASCII 判断 |
| \`utf8_next_cp(p, end, *consumed=nullptr)\` | 游标式解码一个码点 |
| \`utf8_prev_cp(begin, p, *consumed=nullptr)\` | 游标式回溯一个码点 |

\`\`\`cpp
// 直接对 const char* / string_view 操作, 不必构造 view
size_t w = utf8_display_width("顺序访问");      // 8
size_t n = utf8_cp_count("Hello你好");          // 7
bool ok = utf8_is_valid("Hello你好");           // true
bool ascii = utf8_is_ascii("Hello");            // true

// 码点索引转字节偏移
size_t off = utf8_byte_offset("Hello你好", 5);  // 5 (第 5 个码点的字节起点)

// 游标式迭代 (适合手写循环)
const char* s = "Hello你好";
const char* p = s;
const char* end = s + std::strlen(s);
while (p < end) {
    size_t len = 0;
    char32_t cp = utf8_next_cp(p, end, &len);
    use(cp);
    p += len;
}
\`\`\`

### 使用

\`\`\`cpp
#include "part/utf8pp/utf8_view.hpp"

// === 构造 ===
utf8_view v1("Hello你好");          // C 字符串
utf8_view v2("Hello你好", 11);      // 显式字节长度
utf8_view v3(std::string_view("abc"));
utf8_view v4(u8"中文");

// === 容量 ===
v1.byte_size();                  // 11
v1.size();                       // 7 (码点数)
v1.empty();                      // false

// === 访问 ===
v1.byte_at(0);                   // 'H'
v1.at(0);                        // 'H'
v1.at(5);                        // '你' (U+4F60)
v1.front();                      // 'H'
v1.back();                       // '好'

// === 迭代器 ===
for (char32_t cp : v1) { /* 码点级遍历 */ }
for (auto it = v1.rbegin(); it != v1.rend(); ++it) { /* 反向 */ }

// === 子串 ===
utf8_view b1 = v1.substr_bytes(0, 5);   // "Hello"
utf8_view b2 = v1.substr(0, 5);         // "Hello你" (取 5 个码点)
v1.remove_prefix(6);                    // 移除前 6 字节 → "你好"
v1.remove_suffix(3);                    // 移除后 3 字节

// === 查找 ===
v1.find_byte('l');                      // 2
v1.rfind_byte('l');                     // 3
v1.find_bytes("ll");                    // 字节子串查找
v1.find(U'好');                         // 码点级查找
v1.rfind(U'好');
v1.find_first_of(utf8_view("aeiou"));
v1.find_last_not_of(U' ');
v1.contains(U'好');
v1.starts_with(U'H');
v1.ends_with(U'好');

// === 比较 ===
v1 == utf8_view("Hello你好");
v1 < utf8_view("World");
auto cmp = v1 <=> utf8_view("Hello");

// === 拷贝 ===
char buf[32];
size_t n = v1.copy(buf, sizeof(buf));

// === 与 utf8pp 互转 ===
utf8pp owned(v1.byte_view());          // 经 string_view 中转构造 utf8pp
utf8_view from_owned = utf8_view(owned.data(), owned.byte_size());

// === 流输出 ===
std::cout << v1;

// === 注意: 视图不持有内存, 源生命周期需保证 ===
utf8_view dangling;
{
    std::string temp = "hello";
    dangling = utf8_view(temp);    // temp 析构后 dangling 失效
}
// dangling.data() 已是悬垂指针
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 视图持有内存的假设 | \`utf8_view\` 不拥有内存，源对象析构后视图悬垂 | 确保源对象生命周期覆盖视图使用范围 |
| 用 \`size()\` 做字节操作 | \`size()\` 是码点数，非字节数 | 用 \`byte_size()\` 取字节数 |
| 高频调用 \`size()\` / \`at()\` | 每次需遍历解码 | 高频场景转 \`utf8pp\` |
| \`remove_prefix(n)\` 传码点数 | \`remove_prefix\` 是字节级 | 码点级用 \`substr(cp_pos, cp_count)\` |
| 对视图调用 \`to_lower()\` / \`split()\` | 视图只读，无修改方法 | 用 \`utf8pp\` 或自行转换 |
| 字节级 \`find_byte\` 与码点级 \`find\` 混用 | 字节查找返回字节位置，码点查找返回码点索引 | 阅读接口前缀区分（\`find_byte\` vs \`find\`） |
| \`starts_with(char32_t)\` 与 \`starts_with(utf8_view)\` 混用 | 前者码点级需遍历，后者字节级直接比较 | 按场景选择，注意参数类型 |
| 正向迭代器 \`--it\` | 正向迭代器为 \`forward_iterator_tag\`，不支持 \`--\` | 用反向迭代器 \`rbegin()\`/\`rend()\` |
| 越界 \`at()\` / \`front()\` / \`back()\` | 返回 U+FFFD，非崩溃 | 检查索引或用 \`operator[]\` |
| 越界 \`byte_at()\` | 返回 \`'\\0'\`，非崩溃 | 检查索引 |
| \`data()\` 与 \`c_str()\` 空视图返回值不同 | \`data()\` 返回 \`nullptr\`，\`c_str()\` 返回 \`""\` | 需要 C 字符串语义时用 \`c_str()\` |
| 期望 BOM / 校验接口 | \`utf8_view\` 不提供 BOM / 校验 | 用 \`utf8pp::has_bom()\` / \`utf8pp::valid()\` |
| 期望 \`operator>>\` / 字面量 / \`to_string\` | \`utf8_view\` 不提供这些接口 | 用 \`utf8pp\` 或经 \`std::string_view\` 中转 |
| 比较运算期望码点序 | 比较基于字节序（与 \`std::string_view\` 一致） | 字节序 = 码点序（UTF-8 特性），但需注意 |

---
`
};

