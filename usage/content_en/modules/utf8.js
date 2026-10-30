window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['utf8pp'] = {
  title: "utf8pp — UTF-8 encoding and decoding",
  content: `## 35. utf8_codec / utf8pp — UTF-8 encoding and decoding

Global namespace. Core interfaces are \`noexcept\`; mutating operations (which \`std::abort\` on allocation failure) are also \`noexcept\`.

The module is split into three layers:

- **Encoding/decoding**: UTF-8 encode/decode functions.
- **\`utf8pp\` class**: an owning UTF-8 string class with code point-level and byte-level access.
- **\`utf8pp\` non-member interfaces**: \`operator+\` / stream operators / \`swap\` / literals / \`std::hash\` specialization.

Only Unicode encodings (UTF-8 / UTF-32 code points) are supported; other encodings (GBK / UTF-16 etc.) are not. Illegal sequences are replaced with the U+FFFD replacement character. Full Unicode character processing is supported: code point level + grapheme cluster level + East Asian Width display width + NFC/NFKC normalization (including the Hangul algorithm) + Script detection.

### 35.1 Macro configuration

| Macro | Description |
|------|------|
| \`UTF8PP_USE_LAYERED_ALLOCATOR\` | Enables the layered allocator; the heap path uses \`layered_allocator\` (slab buckets + big_pool) |
| \`UTF8PP_LAYERED_ALLOCATOR_NOT_ENABLED\` | Disables the layered allocator; the heap path falls back to \`std::malloc\` / \`std::free\` |

> Choose one; \`UTF8PP_USE_LAYERED_ALLOCATOR\` is enabled by default.

### 35.2 Encoding and decoding interfaces

| Interface | Description |
|------|------|
| \`to_char(uint32_t cp)\` | Numeric value to Unicode character (\`char32_t\`), constexpr |
| \`to_int(char32_t ch)\` | Unicode character to numeric value (\`uint32_t\`), constexpr |
| \`utf8_to_codepoints(src, src_len, out, out_cap, out_has_err)\` | UTF-8 string to code point array (\`uint32_t*\`) |
| \`codepoints_to_char32(cps, cp_count, out, out_cap, out_has_err)\` | Code point array to \`char32_t\` string |
| \`char32_to_utf8(src, src_len, out, out_cap, out_has_err)\` | \`char32_t\` string to UTF-8 byte sequence |

\`out_has_err\` is an optional output parameter (\`bool*\`); passing \`nullptr\` means the error flag is not received. The return value is the total count that needs to be written: \`<= out_cap\` means everything was written; \`> out_cap\` means the required capacity (the caller retries after growing).

### 35.3 utf8pp class interfaces

#### Construction and assignment

| Interface | Description |
|------|------|
| \`utf8pp()\` | Default constructor, empty string (SSO mode) |
| \`utf8pp(const char* s)\` / \`utf8pp(const char* s, size_t byte_len)\` | Construct from a UTF-8 C string |
| \`utf8pp(const char8_t* s)\` / \`utf8pp(const char8_t* s, size_t byte_len)\` | Construct from a \`char8_t\` string |
| \`utf8pp(const char32_t* s, size_t cp_count)\` | Construct from a code point array |
| \`utf8pp(size_t n, char32_t cp)\` | Fill constructor with n copies of cp |
| \`utf8pp(std::string_view sv)\` | Construct from a string_view |
| \`utf8pp(const std::string& s)\` / \`utf8pp(const std::u8string& s)\` / \`utf8pp(const std::u32string& s)\` | Construct from std strings |
| \`utf8pp(const utf8_view& v)\` | Construct from a \`utf8_view\` |
| \`utf8pp(std::initializer_list<char32_t> il)\` | Construct from a code point initializer list |
| \`utf8pp(InputIt first, InputIt last)\` | Construct from an iterator range (SFINAE excludes integer types) |
| \`explicit utf8pp(const std::array<utf8pp, N>& parts)\` / \`explicit utf8pp(const std::vector<utf8pp>& parts)\` | Construct by joining a range |
| \`utf8pp(const utf8pp&)\` / \`utf8pp(utf8pp&&) noexcept\` | Copy/move constructor |
| \`utf8pp(std::nullptr_t) = delete\` | Construction from nullptr is forbidden |
| \`operator=(const utf8pp&)\` / \`operator=(utf8pp&&) noexcept\` | Copy/move assignment |
| \`operator=(const char*)\` / \`operator=(std::string_view)\` / \`operator=(char32_t)\` | Assign from common types |
| \`operator=(const char8_t*)\` / \`operator=(std::initializer_list<char32_t>)\` | Assign from char8_t / initializer list |
| \`operator=(const std::string&)\` / \`operator=(const std::u8string&)\` | Assign from std strings |
| \`operator=(const utf8_view& v)\` | Assign from \`utf8_view\` |
| \`assign(const char* s, size_t byte_len)\` / \`assign(const char* s)\` / \`assign(const utf8pp&)\` | Reassign; passing itself (\`s.assign(s)\`) is safe and yields the same result as assigning a copy |
| \`assign(std::string_view)\` / \`assign(const std::string&)\` / \`assign(const char8_t*)\` / \`assign(const utf8_view&)\` | Reassign |
| \`assign(std::initializer_list<char32_t>)\` / \`assign(InputIt first, InputIt last)\` | Assign from an initializer list / iterator range |
| \`assign(size_t n, char32_t cp)\` | Reassign to n copies of cp |
| \`assign(const std::u8string&)\` / \`assign(const std::u32string&)\` | Assign from a u8/u32 string |
| \`assign(const utf8pp& other, size_t pos, size_t n = npos)\` | Assign from the substring [pos, pos+n) of other |
| \`assign(const std::array<utf8pp, N>&)\` / \`assign(const std::vector<utf8pp>&)\` | Reassign from a range |
| \`swap(utf8pp&) noexcept\` / \`swap(utf8pp&, utf8pp&) noexcept\` | Swap |

#### Capacity

| Interface | Description |
|------|------|
| \`size()\` / \`length()\` | Number of code points |
| \`byte_size()\` | Number of bytes |
| \`capacity()\` | Current byte capacity |
| \`cp_capacity()\` | Current code point capacity (decoupled from byte capacity) |
| \`max_size()\` | Upper bound on bytes/code points held (constant \`0xFFFFFFFF\`) |
| \`empty()\` | Whether empty |
| \`is_sso()\` | Whether in SSO mode (constexpr) |
| \`sso_capacity()\` | SSO capacity (constexpr, = 103) |
| \`reserve(n)\` | Reserve byte capacity (grown according to the growth policy) |
| \`reserve_exact(n)\` | Reserve byte capacity exactly (force growth to n) |
| \`reserve_cp(n)\` | Reserve code point capacity |
| \`increase_capacity(new_cap)\` | Grow the byte buffer and the code point offset array together |
| \`shrink_to_fit()\` | Release excess capacity (falls back to SSO if possible) |
| \`clear() noexcept\` | Clear contents (does not release memory) |
| \`rebuild_cp_offsets() noexcept\` | Rebuild the code point offset cache after modifying \`data()\` directly |
| \`rebuild(new_byte_size) noexcept\` | Set a new byte size and rebuild the code point offset cache (convenience after modifying \`data()\`) |

#### Access

| Interface | Description |
|------|------|
| \`at(cp_idx)\` / \`operator[](cp_idx)\` | Code point index access (out of range returns U+FFFD) |
| \`front()\` / \`back()\` | First/last code point (empty string returns U+FFFD) |
| \`c_str()\` / \`data()\` (const) | C string (**an empty object returns \`""\`, not nullptr**) |
| \`data()\` (non-const) | Writable byte pointer (must call \`rebuild_cp_offsets()\` after modification to rebuild the cache) |
| \`view()\` / \`binary_view()\` | \`std::string_view\` (equivalent; \`binary_view\` emphasizes byte semantics) |
| \`u8view()\` | \`std::u8string_view\` |

#### Iterators

| Interface | Description |
|------|------|
| \`begin()\` / \`end()\` / \`cbegin()\` / \`cend()\` | Code point-level forward iterator (\`contiguous_iterator_tag\`, dereference returns \`char32_t\`) |
| \`rbegin()\` / \`rend()\` / \`crbegin()\` / \`crend()\` | Code point-level reverse iterator (\`bidirectional_iterator_tag\`) |
| \`make_iterator(cp_idx)\` | Build an iterator pointing at the given code point index (use after insert/erase) |
| \`byte_begin()\` / \`byte_end()\` / \`byte_cbegin()\` / \`byte_cend()\` | Byte-level forward iterator (\`contiguous_iterator_tag\`, dereference returns \`char\`) |
| \`rbyte_begin()\` / \`rbyte_end()\` / \`byte_crbegin()\` / \`byte_crend()\` | Byte-level reverse iterator (\`contiguous_iterator_tag\`) |
| \`const_iterator::ptr()\` / \`const_iterator::cp_ptr()\` | Expose \`const char*\` / \`const char32_t*\` (for interacting with C APIs) |
| \`const_byte_iterator::ptr()\` | Expose \`const char*\` |

#### Grapheme cluster iterators

A grapheme cluster = a user-perceived character (combining marks / ZWJ sequences are not split), following simplified UAX #29 rules. Dereferencing returns \`utf8_view\`, covering cases such as combining marks, Hangul LVT sequences and Emoji ZWJ sequences.

| Interface | Description |
|------|------|
| \`grapheme_begin()\` / \`grapheme_end()\` | Grapheme cluster-level forward iterator (\`forward_iterator_tag\`) |
| \`grapheme_cbegin()\` / \`grapheme_cend()\` | Constant grapheme cluster-level forward iterator |
| \`grapheme_count()\` | Number of grapheme clusters |
| \`grapheme_clusters()\` | Split into grapheme clusters, returns \`dense<utf8_view>\` |
| \`const_grapheme_iterator::ptr()\` | End position of the current grapheme cluster as \`const char*\` |
| \`const_grapheme_iterator::start()\` | Start position of the current grapheme cluster as \`const char*\` |

#### Modifying operations

| Interface | Description |
|------|------|
| \`push_back(char32_t cp)\` | Append a single code point |
| \`append(const char*)\` / \`append(const char*, size_t)\` / \`append(const char8_t*)\` | Append a UTF-8 string |
| \`append(const char32_t*, size_t)\` / \`append(const utf8pp&)\` / \`append(std::string_view)\` | Append other forms |
| \`append(const std::string&)\` / \`append(const std::u8string&)\` / \`append(const std::u32string&)\` | Append std strings |
| \`append(std::initializer_list<char32_t>)\` / \`append(size_t n, char32_t cp)\` | Append an initializer list / n identical code points |
| \`append(InputIt first, InputIt last)\` | Append an iterator range (SFINAE excludes integer types) |
| \`append(std::array<utf8pp, N>&)\` / \`append(std::vector<utf8pp>&)\` / \`append(const utf8pp*, size_t)\` / \`append(std::span<const utf8pp>)\` | Append in bulk |
| \`operator+=(char32_t)\` / \`operator+=(const char*)\` / \`operator+=(const utf8pp&)\` | Append operators |
| \`operator+=(std::string_view)\` / \`operator+=(const char8_t*)\` / \`operator+=(std::initializer_list<char32_t>)\` | Append operator extensions |
| \`insert(cp_idx, char32_t)\` | Insert a single code point at a code point index |
| \`insert(cp_idx, const utf8pp&)\` / \`insert(cp_idx, const char*)\` / \`insert(cp_idx, std::string_view)\` | Insert a string at a code point index |
| \`insert(cp_idx, const char* s, size_t n)\` / \`insert(cp_idx, size_t n, char32_t cp)\` / \`insert(cp_idx, std::initializer_list<char32_t>)\` | Insert at a code point index (explicit length / fill / initializer list) |
| \`insert(cp_idx, const utf8pp& str, size_t pos2, size_t n2)\` | Substring insertion: insert [pos2, pos2+n2) from str |
| \`insert(const_iterator pos, char32_t)\` / \`insert(pos, size_t n, char32_t cp)\` | Iterator-based insertion |
| \`insert(const_iterator pos, InputIt first, InputIt last)\` | Iterator-based insertion (template iterator range) |
| \`insert(const_iterator pos, const utf8pp&)\` / \`insert(pos, const char*)\` / \`insert(pos, const char*, size_t byte_len)\` / \`insert(pos, std::string_view)\` / \`insert(pos, std::initializer_list<char32_t>)\` | Iterator-based string insertion |
| \`erase(cp_idx, n=1)\` | Erase n code points starting at a code point index |
| \`erase(const_iterator pos)\` / \`erase(first, last)\` | Iterator-based erase |
| \`pop_back()\` | Remove the last code point |
| \`substr(pos, cp_count=npos)\` | Substring by code point index (returns utf8pp) |
| \`append_cp(n, cp)\` | Append n identical code points |
| \`assign_cp(n, cp)\` | Clear then assign n identical code points |
| \`resize_cp(n, cp=U'\\0')\` / \`resize(n)\` / \`resize(n, cp)\` | Adjust the number of code points (truncate if smaller, pad with cp if larger; \`resize\` is a \`std::string\` alias) |
| \`replace(pos, n, const utf8pp&)\` / \`replace(pos, n, const char*)\` / \`replace(pos, n, std::string_view)\` | Replace [pos, pos+n) |
| \`replace(pos, n, const char* s, size_t n2)\` / \`replace(pos, n, size_t n2, char32_t cp)\` / \`replace(pos, n, std::initializer_list<char32_t>)\` | Replace (explicit length / fill / initializer list) |
| \`replace(pos1, n1, const utf8pp& other, pos2, n2)\` / \`replace(pos1, n1, const char* s, pos2, n2)\` | Two-range replace (this string [pos1,pos1+n1) ← other [pos2,pos2+n2)) |
| \`replace(const_iterator first, last, const utf8pp&)\` / \`replace(first, last, const char*)\` / \`replace(first, last, std::string_view)\` | Iterator-range replace |
| \`replace(const_iterator first, last, const char* s, size_t n2)\` / \`replace(first, last, size_t n2, char32_t cp)\` | Iterator-range replace (explicit length / fill) |
| \`replace(const_iterator first, last, InputIt ifirst, InputIt ilast)\` | Iterator-range replace (template iterator range) |
| \`replace_all(const utf8pp& old, const utf8pp& new)\` / \`replace_all(const char*, const char*)\` / \`replace_all(std::string_view, std::string_view)\` | Replace all occurrences of a substring |
| \`replace_all(const char* old, const utf8pp& new)\` / \`replace_all(const utf8pp& old, const char* new)\` | Replace all occurrences of a substring (mixed overloads) |
| \`replace_all(char32_t old_cp, char32_t new_cp)\` | Replace all occurrences of a single code point |
| \`trim_left()\` / \`trim_right()\` / \`trim()\` | Strip leading/trailing/both-side whitespace (Unicode whitespace) |
| \`trim_left(Pred pred)\` / \`trim_right(Pred pred)\` / \`trim(Pred pred)\` | Predicate-based strip (Pred: \`bool(char32_t)\`, SFINAE-constrained) |
| \`trim(const utf8pp& chars)\` / \`trim_left(const utf8pp& chars)\` / \`trim_right(const utf8pp& chars)\` | Character-set-based strip |
| \`trim(const char* chars)\` / \`trim_left(const char* chars)\` / \`trim_right(const char* chars)\` | Character-set-based strip (C string) |
| \`trimmed()\` / \`trimmed_left()\` / \`trimmed_right()\` | Return a trimmed copy (includes predicate / character-set overloads) |
| \`to_lower()\` / \`to_upper()\` | Case conversion (full Unicode, in place) |
| \`to_title()\` / \`swapcase()\` | Title case / case swap (full Unicode, in place) |
| \`lowered()\` / \`uppered()\` / \`titled()\` / \`swapcased()\` | Return a converted copy |
| \`reverse()\` | Code point-level reversal (in place) |
| \`reversed()\` | Return a reversed copy |
| \`strip_bom()\` | Strip the UTF-8 BOM |
| \`copy(char* buf, size_t n, pos=0)\` | Copy to an external buffer (returns the number of bytes copied) |
| \`pad_left(width, fill=U' ')\` / \`pad_right(width, fill=U' ')\` / \`center(width, fill=U' ')\` | Display-width-aware padding (East Asian Width aware, fullwidth = 2, in place) |
| \`padded_left(width, fill=U' ')\` / \`padded_right(width, fill=U' ')\` / \`centered(width, fill=U' ')\` | Return a padded copy |
| \`display_width()\` | Compute display width (East Asian Width: fullwidth = 2, zero width = 0, ASCII = 1) |
| \`to_nfc()\` / \`to_nfd()\` | NFC normalization / NFD decomposition (in place, includes Hangul algorithmic decomposition) |
| \`to_nfkc()\` / \`to_nfkd()\` | NFKC normalization / NFKD decomposition (in place, includes compatibility decomposition: fullwidth→halfwidth, ligatures, superscripts, Roman numerals, etc.) |
| \`nfc()\` / \`nfd()\` / \`nfkc()\` / \`nfkd()\` | Return a normalized copy |

#### Byte-level access

| Interface | Description |
|------|------|
| \`byte_at(byte_idx)\` | Byte index access (out of range returns \`'\\0'\`) |
| \`at_byte(byte_idx)\` | Byte index access (out of range \`std::abort\`) |
| \`byte_substr(byte_pos, byte_len=npos)\` | Byte-level substring (returns utf8pp, does not validate UTF-8 boundaries) |
| \`byte_to_cp_idx(byte_idx)\` | Byte index → code point index (returns \`npos\` if not at a code point start) |
| \`cp_to_byte_idx(cp_idx)\` | Code point index → byte index (out of range returns \`byte_size()\`) |

#### Character classification API (static methods + string-level checks)

| Interface | Description |
|------|------|
| \`static is_alpha(cp)\` / \`static is_digit(cp)\` / \`static is_alnum(cp)\` | Letter / digit / alphanumeric (full Unicode) |
| \`static is_space(cp)\` / \`static is_punct(cp)\` | Whitespace / punctuation (Unicode) |
| \`static is_lower(cp)\` / \`static is_upper(cp)\` | Lowercase / uppercase (full Unicode) |
| \`static is_xdigit(cp)\` / \`static is_cntrl(cp)\` / \`static is_printable(cp)\` | Hexadecimal / control character / printable |
| \`static is_combining(cp)\` / \`static is_wide(cp)\` / \`static is_zero_width(cp)\` / \`static is_emoji(cp)\` | Combining mark / wide character / zero width / Emoji |
| \`static cp_width(cp)\` | Display width of a single code point (0/1/2) |
| \`static to_lower_cp(cp)\` / \`static to_upper_cp(cp)\` / \`static to_title_cp(cp)\` | Single code point case conversion (full Unicode) |
| \`static script_of(cp)\` | Script of a single code point (\`utf8pp::script\` enum: Latin/Han/Hiragana/Arabic/Emoji etc.) |
| \`static is_script(cp, s)\` | Whether a single code point belongs to the given script |
| \`static script_name(s)\` | Script enum → name string |
| \`is_all_alpha()\` / \`is_all_digit()\` / \`is_all_alnum()\` | Whether the whole string is all letters/digits/alphanumeric |
| \`is_all_space()\` / \`is_all_xdigit()\` / \`is_all_printable()\` | Whether the whole string is all whitespace/hexadecimal/printable |
| \`script_of()\` | Script of the first character of the whole string (empty string returns \`script::unknown\`) |
| \`is_all_script(s)\` | Whether the whole string belongs entirely to the given script (empty string returns false) |
| \`contains_script(s)\` | Whether at least one code point of the given script is present |

#### Number conversion

| Interface | Description |
|------|------|
| \`to_int(pos*, base=10)\` / \`to_long(pos*, base=10)\` / \`to_ll(pos*, base=10)\` | String → int/long/long long (returns 0 on failure) |
| \`to_ulong(pos*, base=10)\` / \`to_ull(pos*, base=10)\` | String → unsigned long/unsigned long long |
| \`to_float(pos*)\` / \`to_double(pos*)\` / \`to_long_double(pos*)\` | String → float/double/long double |
| \`stoi/stol/stoll/stoul/stoull/stof/stod/stold(...)\` | std-style aliases (same parameters as above) |
| \`parse_int(out, base=10) noexcept\` / \`parse_long(out, base=10) noexcept\` / \`parse_ll(out, base=10) noexcept\` | Strict parse to int/long/long long (returns bool, allows leading +/- and surrounding whitespace) |
| \`parse_ulong(out, base=10) noexcept\` / \`parse_ull(out, base=10) noexcept\` | Strict parse to unsigned long/unsigned long long |
| \`parse_float(out) noexcept\` / \`parse_double(out) noexcept\` / \`parse_long_double(out) noexcept\` | Strict parse to floating point (returns bool) |
| \`is_integer(base=10) noexcept\` / \`is_float() noexcept\` / \`is_number() noexcept\` | Content checks: integer / floating point / number |
| \`is_hex() noexcept\` / \`is_binary() noexcept\` / \`is_octal() noexcept\` | Convenience aliases for radix checks |

#### format / vformat

| Interface | Description |
|------|------|
| \`static format(const char* fmt, ...)\` | printf-style formatting (returns utf8pp, grows automatically) |
| \`static vformat(const char* fmt, std::va_list ap)\` | va_list version |

#### Lookup

| Interface | Description |
|------|------|
| \`find(char32_t, pos=0)\` / \`find(const utf8pp&, pos=0)\` | Forward search for a code point/substring |
| \`find(const char* s, pos=0)\` / \`find(std::string_view sv, pos=0)\` | Forward search for a C string/string_view |
| \`find(const char* s, pos, n)\` | Forward search the first n bytes of a C string (three-arg, aligned with \`std::string\`) |
| \`rfind(char32_t, pos=npos)\` / \`rfind(const utf8pp&, pos=npos)\` | Reverse search: returns the last match whose start position is not greater than pos, npos if none |
| \`rfind(const char* s, pos=npos)\` / \`rfind(std::string_view sv, pos=npos)\` | Reverse search for a C string/string_view: returns the last match whose start position is not greater than pos, npos if none |
| \`rfind(const char* s, pos, n)\` | Reverse search the first n bytes of a C string (three-arg); pos semantics as above |
| \`find_first_of(char32_t, pos=0)\` / \`find_first_of(const utf8pp&, pos=0)\` | First match |
| \`find_first_of(const char* s, pos=0)\` / \`find_first_of(std::string_view sv, pos=0)\` | First match (C string/string_view) |
| \`find_first_of(const char* s, pos, n)\` | First match (three-arg) |
| \`find_last_of(char32_t, pos=npos)\` / \`find_last_of(const utf8pp&, pos=npos)\` | Last match |
| \`find_last_of(const char* s, pos=npos)\` / \`find_last_of(std::string_view sv, pos=npos)\` | Last match (C string/string_view) |
| \`find_last_of(const char* s, pos, n)\` | Last match (three-arg) |
| \`find_first_not_of(char32_t, pos=0)\` / \`find_first_not_of(const utf8pp&, pos=0)\` | First non-match |
| \`find_first_not_of(const char* s, pos=0)\` / \`find_first_not_of(std::string_view sv, pos=0)\` | First non-match (C string/string_view) |
| \`find_first_not_of(const char* s, pos, n)\` | First non-match (three-arg) |
| \`find_last_not_of(char32_t, pos=npos)\` / \`find_last_not_of(const utf8pp&, pos=npos)\` | Last non-match |
| \`find_last_not_of(const char* s, pos=npos)\` / \`find_last_not_of(std::string_view sv, pos=npos)\` | Last non-match (C string/string_view) |
| \`find_last_not_of(const char* s, pos, n)\` | Last non-match (three-arg) |
| \`count(char32_t)\` / \`count(const utf8pp&)\` / \`count(const char*)\` / \`count(std::string_view)\` | Count occurrences |
| \`contains(char32_t)\` / \`contains(const utf8pp&)\` / \`contains(const char*)\` / \`contains(std::string_view)\` | Contains check |
| \`starts_with(char32_t)\` / \`starts_with(const utf8pp&)\` / \`starts_with(const char*)\` / \`starts_with(std::string_view)\` | Prefix check |
| \`ends_with(char32_t)\` / \`ends_with(const utf8pp&)\` / \`ends_with(const char*)\` / \`ends_with(std::string_view)\` | Suffix check |

#### Comparison and conversion

| Interface | Description |
|------|------|
| \`compare(const utf8pp&)\` / \`compare(const char*)\` / \`compare(std::string_view)\` | Three-way comparison (byte order = code point order) |
| \`compare(const std::string&)\` / \`compare(const std::u8string&)\` / \`compare(const std::u32string&)\` / \`compare(char32_t cp)\` | Compare with std strings / a single code point |
| \`compare(pos, n, const utf8pp&)\` / \`compare(pos, n, const char*)\` / \`compare(pos, n, std::string_view)\` | Substring comparison (this string [pos, pos+n)) |
| \`compare(pos1, n1, const utf8pp& s, pos2, n2)\` / \`compare(pos1, n1, const char* s, n2)\` | Two-range comparison |
| \`operator==/!=/</>/<=/>=\` | Compare with \`utf8pp\` / \`const char*\` / \`std::string_view\` / \`std::string\` / \`std::u8string\` / \`std::u32string\` / \`char32_t\` |
| \`operator<=>(const utf8pp&)\` / \`operator<=>(const char*)\` / \`operator<=>(std::string_view)\` | C++20 three-way comparison |
| \`operator<=>(const std::string&)\` / \`operator<=>(const std::u8string&)\` / \`operator<=>(const std::u32string&)\` / \`operator<=>(char32_t)\` | C++20 three-way comparison extensions |
| \`to_std_string()\` | Convert to \`std::string\` |
| \`to_u32string()\` | Convert to \`std::u32string\` |
| \`to_u8string()\` | Convert to \`std::u8string\` |
| \`to_utf8_view()\` | Zero-copy conversion to \`utf8_view\` (points into the internal buffer; lifetime is bounded by \`*this\`) |

#### BOM / validation

| Interface | Description |
|------|------|
| \`has_bom()\` | Whether it starts with a UTF-8 BOM |
| \`strip_bom()\` | Strip the UTF-8 BOM |
| \`valid()\` | Whether the whole string is valid UTF-8 |
| \`validate()\` | Returns the index of the first invalid code point (\`npos\` if all valid) |

#### split / join / split_view

| Interface | Description |
|------|------|
| \`split(char32_t delim)\` | Split on a single code point, returns \`dense<utf8pp>\` |
| \`split(const utf8pp& delim)\` / \`split(const char*)\` / \`split(std::string_view)\` | Split on a substring |
| \`split_view(char32_t delim)\` / \`split_view(const utf8pp& delim)\` | Zero-copy split, returns \`dense<utf8_view>\` (reuses the original string's memory; the original string must outlive the views) |
| \`split_view(const char*)\` / \`split_view(std::string_view)\` | Zero-copy split (C string/string_view) |
| \`split_to(char32_t, std::vector<utf8pp>&)\` / \`split_to(const utf8pp&, std::vector<utf8pp>&)\` | Split into a std container |
| \`split_to(const char*, std::vector<utf8pp>&)\` / \`split_to(std::string_view, std::vector<utf8pp>&)\` | Split into a std container (C string/string_view) |
| \`split_to(const utf8pp&, utf8pp* out, size_t cap)\` / \`split_to(const char*, utf8pp* out, size_t cap)\` / \`split_to(std::string_view, utf8pp* out, size_t cap)\` | Split into a raw pointer buffer |
| \`static join(const dense<utf8pp>&, const utf8pp&)\` / \`static join(..., char32_t)\` | Join |
| \`static join(const std::array<utf8pp, N>&, const utf8pp&)\` / \`static join(const std::vector<utf8pp>&, ...)\` | Join std containers |
| \`static join(const utf8pp* parts, size_t count, const utf8pp&)\` | Join raw pointers |

### 35.4 utf8pp non-member interfaces

| Interface | Description |
|------|------|
| \`operator+(const utf8pp&, const utf8pp&)\` / \`operator+(const utf8pp&, char32_t)\` / \`operator+(char32_t, const utf8pp&)\` | Concatenation |
| \`operator+(const utf8pp&, const char*)\` / \`operator+(const char*, const utf8pp&)\` | Concatenation with a C string |
| \`operator+(const utf8pp&, std::string_view)\` / \`operator+(std::string_view, const utf8pp&)\` | Concatenation with a string_view |
| \`operator+(const utf8pp&, const char8_t*)\` / \`operator+(const char8_t*, const utf8pp&)\` | Concatenation with a char8_t string |
| \`operator+(const utf8pp&, const std::string&)\` / \`operator+(const std::string&, const utf8pp&)\` | Concatenation with std::string |
| \`operator+(const utf8pp&, const std::u8string&)\` / \`operator+(const std::u8string&, const utf8pp&)\` | Concatenation with std::u8string |
| \`operator+(const utf8pp&, const std::u32string&)\` / \`operator+(const std::u32string&, const utf8pp&)\` | Concatenation with std::u32string |
| \`operator<<(std::ostream&, const utf8pp&)\` | Stream output (writes byte contents) |
| \`operator>>(std::istream&, utf8pp&)\` | Stream input (appends read data) |
| \`getline(std::istream&, utf8pp&, char delim='\\n')\` | Read one line up to a delimiter (free function) |
| \`swap(utf8pp&, utf8pp&) noexcept\` | Non-member swap |
| \`to_utf8pp(int/long/long long/unsigned/unsigned long/unsigned long long)\` | Number → utf8pp (similar to \`std::to_string\`) |
| \`to_utf8pp(float/double/long double)\` | Floating point → utf8pp |
| \`utf8pp_format(const char* fmt, ...)\` / \`utf8pp_vformat(const char* fmt, std::va_list ap)\` | Free-function formatting (coexists with the static members \`format\`/\`vformat\`) |
| \`"..."_u8\` (const char*) / \`"..."_u8\` (const char8_t*) | Literal operator (returns utf8pp, two overloads) |
| \`"..."_utf8\` (const char*) / \`"..."_utf8\` (const char8_t*) | Literal operator aliases (returns utf8pp, two overloads) |
| \`std::hash<utf8pp>\` | hash specialization (**FNV-1a byte hash**, more uniform distribution) |
| \`std::swap<utf8pp>\` | std::swap specialization (delegates to the member \`swap\`) |
| \`erase(utf8pp&, char32_t cp)\` / \`erase_if(utf8pp&, Pred pred)\` | Globally erase all matching code points (C++20 style, returns the number removed) |
| \`std::formatter<utf8pp>\` | C++20 \`std::format\` specialization (guarded by \`__cpp_lib_format >= 201907L\`, supports width/fill/align and case-conversion flags U/L/T/C) |

### Usage

\`\`\`cpp

// === Encoding/decoding functions ===
char32_t ch = to_char(0x4E2D);       // '中'
uint32_t cps[16];
size_t n = utf8_to_codepoints("你好", 6, cps, 16);  // n = 2

// === utf8pp string class ===
utf8pp s("Hello你好");     // Construct from a UTF-8 C string
s.size();                  // 7 (code points)
s.byte_size();             // 11 (bytes: 5 + 6)
s.get(0);                  // 'H'
s.get(5);                  // '你' (U+4F60)

// Code point-level iteration
for (char32_t cp : s) { /* iterate over each code point */ }

// Reverse iteration
for (auto it = s.rbegin(); it != s.rend(); ++it) { /* reverse iteration */ }

// Grapheme cluster iteration (user-perceived characters; combining marks/ZWJ sequences are not split)
utf8pp emoji_str(U"Hello\\U0001F600\\U0001F926\\U0001F631");
for (auto it = emoji_str.grapheme_begin(); it != emoji_str.grapheme_end(); ++it) {
    utf8_view gv = *it;  // byte range covering a single grapheme cluster
}
size_t gc = emoji_str.grapheme_count();  // number of grapheme clusters
dense<utf8_view> clusters = emoji_str.grapheme_clusters();

// Append/insert/erase
s.push_back(char32_t(0x1F600));   // append an emoji
s.insert(0, char32_t('X'));       // insert at the first position
s.erase(0, 1);                    // erase the first code point
s.pop_back();                     // remove the last one
s.append_cp(3, U'-');             // append 3 '-'
s.resize_cp(10, U'.');            // resize to 10 code points, padding with '.'

// Substring/search/comparison
utf8pp sub = s.substr(0, 3);      // take the first 3 code points
s.find(char32_t('H'));            // code point-level search
s.rfind(U'好');                   // reverse search
s.contains("Hello");              // contains check
s.starts_with(U'H');              // prefix check
s.ends_with(U'好');               // suffix check
s.count(U'l');                    // count occurrences
s == utf8pp("Hello你好");

// Search family
s.find_first_of(utf8pp("aeiou"));
s.find_last_not_of(U' ');

// String modification
utf8pp t("  Hello  World  ");
t.trim();                         // "Hello  World"
t.to_lower();                     // "hello  world"  (full Unicode)
t.to_title();                     // title case
t.swapcase();                     // swap case
t.replace(0, 5, utf8pp("Hi"));    // "Hi   World"
t.replace_all(U'l', U'L');        // replace all occurrences of a single code point
t.reverse();                      // code point-level reversal

// trim predicate / character-set versions
utf8pp csv(",,abc,,");
csv.trim(U',');                   // "abc"  (character-set version)
csv.trim([](char32_t c){ return c == U',' || c == U' '; });  // predicate version

// Padding
utf8pp num("42");
num.pad_left(6, U'0');            // "000042"
utf8pp centered = utf8pp("Hi").centered(10, U'-');  // "----Hi----"

// NFC / NFD / NFKC / NFKD normalization
utf8pp e_acute = utf8pp(U"\\u00E9");           // é (precomposed)
e_acute.to_nfd();                  // → e + U+0301 (decomposed, size==2)
utf8pp e_acute2("e");
e_acute2.push_back(U'\\u0301');
e_acute2.to_nfc();                 // → é (composed, size==1)

utf8pp hangul(U"\\uAC00");          // 가 (Hangul syllable)
hangul.to_nfd();                   // → ㄱ + ㅏ (Hangul algorithmic decomposition)

utf8pp fullwidth(u8"\\uFF21\\uFF22\\uFF23");  // ＡＢＣ
fullwidth.to_nfkd();               // → ABC (compatibility decomposition: fullwidth→halfwidth)

utf8pp lig(u8"\\uFB01");            // ﬁ (ligature)
lig.to_nfkd();                     // → fi (ligature decomposition)

// Byte-level access
utf8pp cn(u8"Hi中");              // 5 bytes: H i 中(3)
cn.byte_at(0);                    // 'H'
cn.byte_substr(2, 3);             // "中" (byte slice)
cn.byte_to_cp_idx(2);             // 2 (byte index→code point index)
cn.cp_to_byte_idx(2);             // 2 (code point index→byte index)

// Character classification
utf8pp::is_alpha(U'A');           // true
utf8pp::is_digit(U'5');           // true
utf8pp("12345").is_all_digit();   // true
utf8pp("Hello").is_all_alpha();   // true

// Number conversion
utf8pp num_str("3.14");
num_str.to_double();              // 3.14
num_str.parse_double(d);          // true, d=3.14 (strict parse)
utf8pp("ff").to_int(nullptr, 16); // 255 (hexadecimal)
utf8pp("42").is_integer();        // true

// format static method
utf8pp fmt = utf8pp::format("x=%d, y=%.2f", 10, 3.14);

// split / join
dense<utf8pp> parts = utf8pp("a,b,c,d").split(U',');
utf8pp joined = utf8pp::join(parts, utf8pp("-"));  // "a-b-c-d"

// split_view zero-copy (views reuse the original string's memory; the original string must outlive the views)
utf8pp src("a,b,c");
dense<utf8_view> views = src.split_view(U',');  // 3 utf8_view objects

// Compatible with std::vector / raw pointer output
std::vector<utf8pp> vparts;
utf8pp("x/y/z").split_to(U'/', vparts);

utf8pp arr[8];
utf8pp("1 2 3").split_to(utf8pp(" "), arr, 8);

// Concatenation operators
utf8pp full = utf8pp("Hello") + U',' + utf8pp(" World");
utf8pp cat = utf8pp("a") + std::string("b") + std::u8string(u8"c");

// Literals
auto u = "Hello"_u8;              // returns utf8pp (const char* overload)
auto u2 = u8"你好"_u8;            // returns utf8pp (const char8_t* overload)

// Number → utf8pp
utf8pp n1 = to_utf8pp(42);
utf8pp n2 = to_utf8pp(3.14);

// Global erase / erase_if
utf8pp e("Hello");
erase(e, U'l');                   // "Heo" (remove all 'l')
erase_if(e, [](char32_t c){ return c == U'H'; });  // "eo"

// C++20 std::format
std::string s = std::format("{}", utf8pp("测试"));

// An empty object's c_str() returns "" (not nullptr)
utf8pp empty;
empty.c_str();                    // ""
empty.data();                     // ""
empty.empty();                    // true
empty.is_sso();                   // true (empty strings use SSO)

// BOM handling
utf8pp bom(u8"\\uFEFFHello");
bom.has_bom();                    // true
bom.strip_bom();                  // content is "Hello" after stripping

// Validation
utf8pp bad("abc\\xff\\xfe");
bad.valid();                      // false
bad.validate();                   // returns the first invalid code point index

// Stream I/O
std::ostringstream os;
os << utf8pp("测试");
std::istringstream is("line1\\nline2");
utf8pp line;
getline(is, line);                // read a line
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Input in a non-UTF-8 encoding (e.g. GBK) | Illegal code points are decoded and replaced with U+FFFD | The input must be valid UTF-8 |
| Assuming \`size()\` equals \`byte_size()\` | For multi-byte characters the two differ | Use \`size()\` for the code point count and \`byte_size()\` for the byte count |
| Accessing \`at()\` out of range without checking the return value | Out of range returns U+FFFD, not a crash | Check the index or use \`operator[]\` (which also returns U+FFFD) |
| Accessing \`at_byte()\` out of range | Out of range \`std::abort\`s (unlike \`byte_at\`, which returns \`'\\0'\`) | Use \`byte_at\` for tolerance or ensure the index is valid |
| Frequent \`insert\`/\`erase\` in the middle | Each call must shift the following bytes and the offset array | Consider another data structure for frequent middle modifications |
| \`byte_substr\` cutting through a multi-byte character | Does not validate UTF-8 boundaries; may produce illegal sequences | Use only where boundaries are known, or use the code point-level \`substr\` |
| Expecting Unicode case behavior from \`to_lower()\` / \`to_upper()\` | Full Unicode is already supported | Covers major scripts such as Latin/Greek/Cyrillic/Armenian/CJK |
| Expecting an emoji ZWJ sequence to act as a single character | Code point-level \`reverse\`/\`split\` will split it apart | Iterate by grapheme cluster with \`grapheme_begin\`/\`grapheme_end\` |
| Using \`capacity()\` to determine code point capacity | \`capacity()\` is byte capacity, not code point capacity | Use \`cp_capacity()\` for code point capacity and \`reserve_cp()\` to reserve |
| Dangling views returned by \`split_view\` | Views reuse the original string's memory and become invalid once the original is destroyed | Ensure the original \`utf8pp\` outlives every use of the \`utf8_view\`s |
| \`to_int\` returning 0 on failure cannot be distinguished | 0 may be a legitimate value | In strict cases use \`parse_int(out, base)\`, which returns bool |
| Misusing \`to_nfc()\` on compatibility characters | NFC does not decompose compatibility characters (e.g. fullwidth, ligatures) | Use \`to_nfkc()\`/\`to_nfkd()\` for compatibility cases |
| Misusing \`to_nfd()\` to decompose Hangul syllables | NFD already supports Hangul algorithmic decomposition (가→ㄱ+ㅏ) | Use \`to_nfd()\`/\`to_nfkd()\` directly for Hangul decomposition |
| Expecting NFKC to keep A + combining mark uncomposed | NFKC includes all NFC composition rules, A+U+0301→Á | Use \`to_nfkd()\` when only compatibility decomposition is needed without composition |
| \`script_of\` returning \`inherited\` for combining marks | Combining marks inherit the script of the preceding character | When contextual script is needed, the caller tracks the previous starter |
| Sharing the same \`utf8pp\` across threads (even read-only) | Read-only interfaces may lazily build internal caches, which is not thread-safe | Synchronize externally, or call \`size()\` / \`begin()\` once in a single-threaded phase to warm it up |
| Passing the object itself to \`assign\`/\`append\`/\`insert\`/\`replace\` | Self-reference safety is already guaranteed; behavior matches \`std::string\` for the same input | You can directly use \`s.append(s)\`, \`s.insert(1, s)\`, \`s.replace(0, 2, s)\` |

---
`
};
window.DOCS_DATA_EN['utf8_view'] = {
  title: "utf8_view — UTF-8 string view",
  content: `## 36. utf8_view — UTF-8 string view

Global namespace. All interfaces are \`noexcept\`.

A non-owning lightweight UTF-8 string view, similar to \`std::string_view\`, but with code point-level interfaces. It holds only a \`const char*\` and a byte count, and does not copy data.

Byte-level interfaces operate directly on the pointer; code point-level interfaces must traverse and decode. Conversion with \`utf8pp\`: \`utf8pp\` can be constructed from \`utf8_view\` via \`std::string_view\` (\`utf8_view\` implicitly converts to \`std::string_view\`, and \`utf8pp\` accepts \`std::string_view\`); \`utf8_view\` can be explicitly constructed from \`utf8pp\`'s \`data()\` + \`byte_size()\`.

### 36.1 Construction and assignment

| Interface | Description |
|------|------|
| \`utf8_view()\` | Default constructor, empty view |
| \`utf8_view(const char* s)\` / \`utf8_view(const char* s, size_t byte_len)\` | Construct from a C string |
| \`utf8_view(const char8_t* s)\` / \`utf8_view(const char8_t* s, size_t byte_len)\` | Construct from \`char8_t\` |
| \`utf8_view(std::string_view sv)\` | Construct from \`string_view\` |
| \`utf8_view(std::u8string_view sv)\` | Construct from \`u8string_view\` |
| \`utf8_view(const std::string& s)\` / \`utf8_view(const std::u8string& s)\` | Construct from std strings |
| \`operator=(const char*)\` / \`operator=(std::string_view)\` | Assignment |

### 36.2 Capacity

| Interface | Description |
|------|------|
| \`byte_size()\` / \`size_bytes()\` / \`length_bytes()\` | Number of bytes |
| \`size()\` / \`length()\` | Number of code points |
| \`empty()\` | Whether empty |
| \`max_size()\` | Theoretical maximum (\`size_t(-1)\`) |

### 36.3 Data access

| Interface | Description |
|------|------|
| \`data()\` / \`c_str()\` | Raw byte pointer (\`data()\` returns \`nullptr\` for an empty view, \`c_str()\` returns \`""\`) |
| \`byte_view()\` / \`operator std::string_view()\` | Convert to \`std::string_view\` (implicit conversion) |
| \`byte_at(i)\` | Byte-level access (out of range returns \`'\\0'\`) |
| \`at(cp_idx)\` / \`operator[](cp_idx)\` | Code point-level access (out of range returns U+FFFD) |
| \`front()\` / \`back()\` | First/last code point (empty view returns U+FFFD) |

### 36.4 Iterators

| Interface | Description |
|------|------|
| \`begin()\` / \`end()\` / \`cbegin()\` / \`cend()\` | Code point-level forward iterator (\`forward_iterator_tag\`, supports only \`++\`) |
| \`rbegin()\` / \`rend()\` / \`crbegin()\` / \`crend()\` | Code point-level reverse iterator (\`bidirectional_iterator_tag\`, supports \`++\`/\`--\`) |
| \`iterator\` / \`reverse_iterator\` | Type aliases (equivalent to \`const_iterator\` / \`const_reverse_iterator\`) |
| \`const_iterator::ptr()\` | Expose \`const char*\` (for interacting with C APIs) |

> Note: the forward iterator is forward-only (no \`--\`), and the reverse iterator is bidirectional (supports \`--\`). Both dereference to \`char32_t\` by value (no \`->\` operator).

### 36.5 Substring and modification

| Interface | Description |
|------|------|
| \`substr_bytes(byte_pos, byte_len=npos)\` | Byte-level substring |
| \`substr(cp_pos, cp_count=npos)\` | Code point-level substring |
| \`remove_prefix(byte_n)\` | Remove a prefix (STL semantics, byte-level) |
| \`remove_suffix(byte_n)\` | Remove a suffix (byte-level) |
| \`copy(char* buf, size_t byte_n, byte_pos=0)\` | Copy to an external buffer |
| \`swap(utf8_view&)\` / \`swap(utf8_view&, utf8_view&)\` | Swap |

### 36.6 Lookup

| Interface | Description |
|------|------|
| \`find_byte(char c, byte_pos=0)\` | Byte-level forward search |
| \`rfind_byte(char c, byte_pos=npos)\` | Byte-level reverse search |
| \`find_bytes(std::string_view str, byte_pos=0)\` | Byte-level substring forward search |
| \`rfind_bytes(std::string_view str, byte_pos=npos)\` | Byte-level substring reverse search |
| \`find(char32_t cp, cp_pos=0)\` / \`find(const utf8_view&, cp_pos=0)\` | Code point-level forward search |
| \`rfind(char32_t cp, cp_pos=npos)\` / \`rfind(const utf8_view&, cp_pos=npos)\` | Code point-level reverse search |
| \`find_first_of(char32_t, cp_pos=0)\` / \`find_first_of(const utf8_view&, cp_pos=0)\` | First match |
| \`find_last_of(char32_t, cp_pos=npos)\` / \`find_last_of(const utf8_view&, cp_pos=npos)\` | Last match |
| \`find_first_not_of(char32_t, cp_pos=0)\` / \`find_first_not_of(const utf8_view&, cp_pos=0)\` | First non-match |
| \`find_last_not_of(char32_t, cp_pos=npos)\` / \`find_last_not_of(const utf8_view&, cp_pos=npos)\` | Last non-match |
| \`contains(char32_t)\` / \`contains(const utf8_view&)\` | Contains check |
| \`starts_with(char32_t)\` | Code point-level prefix check |
| \`starts_with(const utf8_view&)\` | Byte-level prefix check |
| \`ends_with(char32_t)\` | Code point-level suffix check |
| \`ends_with(const utf8_view&)\` | Byte-level suffix check |

### 36.7 Comparison

| Interface | Description |
|------|------|
| \`compare(const utf8_view&)\` / \`compare(std::string_view)\` / \`compare(const char*)\` | Three-way comparison (byte order) |
| \`operator==\` / \`!=\` / \`<\` / \`>\` / \`<=\` / \`>=\` | Compare with \`utf8_view\` / \`std::string_view\` / \`const char*\` |
| \`operator<=>(const utf8_view&)\` / \`operator<=>(std::string_view)\` / \`operator<=>(const char*)\` | Three-way comparison operators |

### 36.8 Read-only queries

| Interface | Description |
|------|------|
| \`display_width()\` | Display width (East Asian Width: CJK/fullwidth = 2, zero width = 0, others = 1) |
| \`to_lower_into(out, cap)\` | Convert to lowercase into an external buffer, returns bytes written (if cap is insufficient, returns the required byte count) |
| \`to_upper_into(out, cap)\` | Convert to uppercase into an external buffer, returns bytes written (as above) |
| \`trimmed()\` / \`trimmed_left()\` / \`trimmed_right()\` | Return a sub-view with whitespace stripped (moves the pointer only) |
| \`is_ascii()\` | Whether all ASCII |
| \`is_valid()\` | Valid UTF-8 validation (code point range + shortest form) |
| \`count(char32_t)\` | Count occurrences of a code point |

\`\`\`cpp
// Display width (for alignment/layout)
size_t w = utf8_view("顺序访问").display_width();  // 8 (4 CJK × 2)

// Case conversion (writes into a user buffer)
char buf[64];
size_t n = utf8_view("ABC").to_lower_into(buf, sizeof(buf));  // n=3, buf="abc"

// trim (returns a sub-view, no allocation)
utf8_view t = utf8_view("  hello  ").trimmed();  // "hello"

// Validation
bool ok = utf8_view("Hello你好").is_valid();   // true
bool ascii = utf8_view("Hello").is_ascii();    // true

// Count
size_t n = utf8_view("a,b,c").count(U',');     // 2
\`\`\`

### 36.9 Non-member interfaces

| Interface | Description |
|------|------|
| \`operator<<(std::ostream&, const utf8_view&)\` | Stream output |
| \`std::hash<utf8_view>\` | hash specialization (byte-wise hash) |
| \`utf8_display_width(const char*)\` / \`utf8_display_width(string_view)\` | Compute display width directly, without constructing a view |
| \`utf8_cp_count(const char*)\` / \`utf8_cp_count(string_view)\` | Compute the code point count directly |
| \`utf8_byte_offset(s, cp_idx)\` | Byte offset of the cp_idx-th code point (out of range returns \`npos\`) |
| \`utf8_is_valid(const char*)\` / \`utf8_is_valid(string_view)\` | Valid UTF-8 validation |
| \`utf8_is_ascii(const char*)\` / \`utf8_is_ascii(string_view)\` | Pure ASCII check |
| \`utf8_next_cp(p, end, *consumed=nullptr)\` | Cursor-style decoding of one code point |
| \`utf8_prev_cp(begin, p, *consumed=nullptr)\` | Cursor-style step back one code point |
| \`utf8_next_line(*p, end, *out, delim='\\n')\` | Cursor-style line reading (produces a zero-copy sub-view, advances \`*p\` to the start of the next line; splitting semantics match a \`getline\` loop) |

\`\`\`cpp
// Operate directly on const char* / string_view, no need to construct a view
size_t w = utf8_display_width("顺序访问");      // 8
size_t n = utf8_cp_count("Hello你好");          // 7
bool ok = utf8_is_valid("Hello你好");           // true
bool ascii = utf8_is_ascii("Hello");            // true

// Code point index to byte offset
size_t off = utf8_byte_offset("Hello你好", 5);  // 5 (byte start of the 5th code point)

// Cursor-style iteration (suits hand-written loops)
const char* s = "Hello你好";
const char* p = s;
const char* end = s + std::strlen(s);
while (p < end) {
    size_t len = 0;
    char32_t cp = utf8_next_cp(p, end, &len);
    use(cp);
    p += len;
}

// Cursor-style line-by-line reading (zero-copy, no utf8pp constructed)
const char* text = "one\\ntwo\\nlast";
const char* lp = text;
const char* lend = text + std::strlen(text);
utf8_view line;
while (utf8_next_line(&lp, lend, &line)) {
    use(line);   // yields "one" "two" "last" in turn (without delimiters)
}
\`\`\`

### Usage

\`\`\`cpp

// === Construction ===
utf8_view v1("Hello你好");          // C string
utf8_view v2("Hello你好", 11);      // explicit byte length
utf8_view v3(std::string_view("abc"));
utf8_view v4(u8"中文");

// === Capacity ===
v1.byte_size();                  // 11
v1.size();                       // 7 (code points)
v1.empty();                      // false

// === Access ===
v1.byte_at(0);                   // 'H'
v1.get(0);                       // 'H'
v1.get(5);                       // '你' (U+4F60)
v1.front();                      // 'H'
v1.back();                       // '好'

// === Iterators ===
for (char32_t cp : v1) { /* code point-level traversal */ }
for (auto it = v1.rbegin(); it != v1.rend(); ++it) { /* reverse */ }

// === Substring ===
utf8_view b1 = v1.substr_bytes(0, 5);   // "Hello"
utf8_view b2 = v1.substr(0, 5);         // "Hello你" (take 5 code points)
v1.remove_prefix(6);                    // remove the first 6 bytes → "你好"
v1.remove_suffix(3);                    // remove the last 3 bytes

// === Lookup ===
v1.find_byte('l');                      // 2
v1.rfind_byte('l');                     // 3
v1.find_bytes("ll");                    // byte substring search
v1.find(U'好');                         // code point-level search
v1.rfind(U'好');
v1.find_first_of(utf8_view("aeiou"));
v1.find_last_not_of(U' ');
v1.contains(U'好');
v1.starts_with(U'H');
v1.ends_with(U'好');

// === Comparison ===
v1 == utf8_view("Hello你好");
v1 < utf8_view("World");
auto cmp = v1 <=> utf8_view("Hello");

// === Copy ===
char buf[32];
size_t n = v1.copy(buf, sizeof(buf));

// === Conversion with utf8pp ===
utf8pp owned(v1.byte_view());          // construct utf8pp via a string_view
utf8_view from_owned = utf8_view(owned.data(), owned.byte_size());

// === Stream output ===
std::cout << v1;

// === Note: the view does not own memory; the source must outlive it ===
utf8_view dangling;
{
    std::string temp = "hello";
    dangling = utf8_view(temp);    // dangling becomes invalid after temp is destroyed
}
// dangling.data() is now a dangling pointer
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Assuming the view owns memory | \`utf8_view\` does not own memory; the view dangles after the source object is destroyed | Ensure the source object outlives the view's use |
| Using \`size()\` for byte operations | \`size()\` is the code point count, not the byte count | Use \`byte_size()\` for the byte count |
| Calling \`size()\` / \`at()\` frequently | Each call must traverse and decode | Convert to \`utf8pp\` in hot paths |
| Passing a code point count to \`remove_prefix(n)\` | \`remove_prefix\` is byte-level | Use \`substr(cp_pos, cp_count)\` for code point level |
| Calling \`to_lower()\` / \`split()\` on a view | The view is read-only and has no mutating methods | Use \`utf8pp\` or convert yourself |
| Mixing byte-level \`find_byte\` with code point-level \`find\` | Byte search returns a byte position; code point search returns a code point index | Distinguish by the interface prefix (\`find_byte\` vs \`find\`) |
| Mixing \`starts_with(char32_t)\` and \`starts_with(utf8_view)\` | The former is code point-level and must traverse; the latter is byte-level and compares directly | Choose per case and mind the parameter type |
| \`--it\` on a forward iterator | The forward iterator is \`forward_iterator_tag\` and does not support \`--\` | Use the reverse iterators \`rbegin()\`/\`rend()\` |
| Out-of-range \`at()\` / \`front()\` / \`back()\` | Returns U+FFFD, not a crash | Check the index or use \`operator[]\` |
| Out-of-range \`byte_at()\` | Returns \`'\\0'\`, not a crash | Check the index |
| \`data()\` and \`c_str()\` return different values for an empty view | \`data()\` returns \`nullptr\`, \`c_str()\` returns \`""\` | Use \`c_str()\` when C string semantics are needed |
| Expecting BOM / validation interfaces | \`utf8_view\` provides no BOM / validation | Use \`utf8pp::has_bom()\` / \`utf8pp::valid()\` |
| Expecting \`operator>>\` / literals / \`to_string\` | \`utf8_view\` does not provide these interfaces | Use \`utf8pp\` or go through \`std::string_view\` |
| Expecting comparison in code point order | Comparison is based on byte order (consistent with \`std::string_view\`) | Byte order = code point order (a UTF-8 property), but be aware of it |

---
`
};
