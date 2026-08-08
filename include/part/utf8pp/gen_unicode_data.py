#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 unicode_data.hpp 的脚本 - 从 Unicode UCD (Unicode Character Database) 生成 unicode_data.hpp

用法:
    python include/part/utf8pp/gen_unicode_data.py [--version 15.1.0] [--output include/part/utf8pp/unicode_data.hpp]

数据源:
    - UnicodeData.txt: 字符属性、大小写映射、CCC、canonical/compatibility 分解
    - DerivedCoreProperties.txt: Alphabetic / Lowercase / Uppercase
    - EastAsianWidth.txt: W/F 分类 (全角宽字符)
    - Scripts.txt: Script 分类
    - emoji-data.txt: Extended_Pictographic (在 emoji/ 子目录)

只使用 Python 标准库, 不依赖第三方库.
"""

import sys
import os
import re
import datetime
import urllib.request
import urllib.error
import tempfile

# ============================================================================
# 常量
# ============================================================================

UCD_BASE = "https://www.unicode.org/Public/"

# 需要下载的 UCD 文件 (相对路径)
UCD_FILES = {
    "UnicodeData":            "ucd/UnicodeData.txt",
    "DerivedCoreProperties":  "ucd/DerivedCoreProperties.txt",
    "EastAsianWidth":         "ucd/EastAsianWidth.txt",
    "Scripts":                "ucd/Scripts.txt",
    "EmojiData":              "ucd/emoji/emoji-data.txt",
}

# 脚本名称 → 枚举名 (与现有 script 枚举一致)
SCRIPT_ENUM = {
    "Common":             "common",
    "Inherited":          "inherited",
    "Latin":              "latin",
    "Greek":              "greek",
    "Cyrillic":           "cyrillic",
    "Armenian":           "armenian",
    "Hebrew":             "hebrew",
    "Arabic":             "arabic",
    "Syriac":             "syriac",
    "Thaana":             "thaana",
    "Devanagari":         "devanagari",
    "Bengali":            "bengali",
    "Gurmukhi":           "gurmukhi",
    "Gujarati":           "gujarati",
    "Oriya":              "oriya",
    "Tamil":              "tamil",
    "Telugu":             "telugu",
    "Kannada":            "kannada",
    "Malayalam":          "malayalam",
    "Sinhala":            "sinhala",
    "Thai":               "thai",
    "Lao":                "lao",
    "Tibetan":            "tibetan",
    "Myanmar":            "myanmar",
    "Georgian":           "georgian",
    "Hangul":             "hangul",
    "Hiragana":           "hiragana",
    "Katakana":           "katakana",
    "Han":                "han",
    "Ethiopic":           "ethiopic",
    "Cherokee":           "cherokee",
    "Canadian_Aboriginal":"canadian",
    "Ogham":              "ogham",
    "Runic":              "runic",
    "Tagalog":            "tagalog",
    "Mongolian":          "mongolian",
    # 注: cjk_ext (37) 和 emoji_picto (38) 不是 UCD script, 由代码特殊处理
}

# ============================================================================
# 下载 UCD (带缓存)
# ============================================================================

def download_file(url, dest_path):
    """下载文件到 dest_path, 显示进度."""
    print(f"  下载: {url}")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "gen_unicode_data.py/1.0"})
        with urllib.request.urlopen(req, timeout=180) as resp:
            total = int(resp.headers.get("Content-Length", 0))
            done = 0
            with open(dest_path, "wb") as f:
                while True:
                    chunk = resp.read(65536)
                    if not chunk:
                        break
                    f.write(chunk)
                    done += len(chunk)
                    if total > 0:
                        pct = done * 100 // total
                        sys.stdout.write(f"\r  进度: {pct}% ({done}/{total})")
                        sys.stdout.flush()
            if total > 0:
                sys.stdout.write("\n")
            else:
                print(f"  完成: {done} 字节")
    except urllib.error.URLError as e:
        print(f"\n  错误: 下载失败 - {e}")
        raise

def download_all(version, cache_dir):
    """下载所有 UCD 文件到缓存目录, 返回文件路径字典."""
    if version.lower() == "latest":
        base = UCD_BASE + "UCD/latest/"
    else:
        base = UCD_BASE + version + "/"

    paths = {}
    for name, rel in UCD_FILES.items():
        url = base + rel
        fname = os.path.basename(rel)
        dest = os.path.join(cache_dir, fname)
        if not os.path.exists(dest):
            download_file(url, dest)
        else:
            print(f"  缓存命中: {fname}")
        paths[name] = dest
    return paths

# ============================================================================
# 解析 UCD
# ============================================================================

def parse_hex(s):
    """解析十六进制字符串, 空串返回 None."""
    s = s.strip()
    if not s:
        return None
    return int(s, 16)

def parse_range_field(s):
    """解析 '0041..005A' 或 '0041' → (start, end)."""
    s = s.strip()
    if ".." in s:
        a, b = s.split("..")
        return int(a.strip(), 16), int(b.strip(), 16)
    v = int(s, 16)
    return v, v

def parse_unicode_data(path):
    """解析 UnicodeData.txt.
    返回:
        entries: dict cp -> {gc, ccc, upper, lower, title, decomp_type, decomp_mapping}
        ranges: list of (start, end, entry_dict)  — First/Last 范围条目
    """
    entries = {}
    ranges = []
    first_cp = None
    first_entry = None

    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split(";")
            # 注: UnicodeData.txt 固定 15 个字段 (0-14); 补齐不足字段
            if len(fields) < 15:
                fields = fields + [""] * (15 - len(fields))
            # 跳过字段不足的异常行
            if not fields[0] or not fields[2]:
                continue
            cp = int(fields[0], 16)
            name = fields[1]
            gc = fields[2]
            ccc = int(fields[3]) if fields[3] else 0
            upper = parse_hex(fields[12]) if fields[12] else None
            lower = parse_hex(fields[13]) if fields[13] else None
            title = parse_hex(fields[14]) if fields[14] else None

            # 解析分解字段 (field 5)
            decomp_field = fields[5] if len(fields) > 5 else ""
            decomp_type = None
            decomp_mapping = []
            if decomp_field:
                if decomp_field.startswith("<"):
                    idx = decomp_field.index(">")
                    decomp_type = decomp_field[1:idx]
                    rest = decomp_field[idx + 1:].strip()
                    if rest:
                        decomp_mapping = [int(x, 16) for x in rest.split()]
                else:
                    decomp_mapping = [int(x, 16) for x in decomp_field.split()]

            entry = {
                "gc": gc,
                "ccc": ccc,
                "upper": upper,
                "lower": lower,
                "title": title,
                "decomp_type": decomp_type,
                "decomp_mapping": decomp_mapping,
            }

            if name.endswith(", First"):
                first_cp = cp
                first_entry = entry
            elif name.endswith(", Last"):
                ranges.append((first_cp, cp, first_entry))
                first_cp = None
                first_entry = None
            else:
                entries[cp] = entry

    return entries, ranges

def parse_property_file(path):
    """解析 DerivedCoreProperties.txt / PropList.txt 格式.
    返回: dict property_name -> list of (start, end)
    """
    props = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0].strip()
            if not line:
                continue
            parts = line.split(";")
            if len(parts) < 2:
                continue
            start, end = parse_range_field(parts[0])
            prop = parts[1].strip()
            props.setdefault(prop, []).append((start, end))
    return props

def parse_east_asian_width(path):
    """解析 EastAsianWidth.txt.
    返回: list of (start, end, width)  width ∈ {W,F,H,A,N}
    """
    result = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0].strip()
            if not line:
                continue
            parts = line.split(";")
            if len(parts) < 2:
                continue
            start, end = parse_range_field(parts[0])
            width = parts[1].strip()
            result.append((start, end, width))
    return result

def parse_scripts(path):
    """解析 Scripts.txt.
    返回: list of (start, end, script_name)
    """
    result = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0].strip()
            if not line:
                continue
            parts = line.split(";")
            if len(parts) < 2:
                continue
            start, end = parse_range_field(parts[0])
            sc = parts[1].strip()
            result.append((start, end, sc))
    return result

def parse_emoji_data(path):
    """解析 emoji-data.txt.
    返回: list of (start, end) Extended_Pictographic 范围
    """
    result = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0].strip()
            if not line:
                continue
            parts = line.split(";")
            if len(parts) < 2:
                continue
            prop = parts[1].strip()
            if prop != "Extended_Pictographic":
                continue
            start, end = parse_range_field(parts[0])
            result.append((start, end))
    return result

# ============================================================================
# 范围合并
# ============================================================================

def merge_ranges(cps):
    """将已排序的码点列表合并为 (start, end) 范围列表."""
    if not cps:
        return []
    cps = sorted(cps)
    result = []
    start = end = cps[0]
    for cp in cps[1:]:
        if cp == end + 1:
            end = cp
        else:
            result.append((start, end))
            start = end = cp
    result.append((start, end))
    return result

def merge_grouped_ranges(cp_value_pairs):
    """将 (cp, value) 列表按 value 分组合并连续范围.
    返回: list of (start, end, value)
    """
    if not cp_value_pairs:
        return []
    cp_value_pairs = sorted(cp_value_pairs)
    result = []
    cur_s = cur_e = cp_value_pairs[0][0]
    cur_v = cp_value_pairs[0][1]
    for cp, v in cp_value_pairs[1:]:
        if cp == cur_e + 1 and v == cur_v:
            cur_e = cp
        else:
            result.append((cur_s, cur_e, cur_v))
            cur_s = cur_e = cp
            cur_v = v
    result.append((cur_s, cur_e, cur_v))
    return result

def range_subtract(start, end, sub_ranges):
    """从 [start, end] 中减去 sub_ranges, 返回剩余 (start, end) 列表."""
    result = [(start, end)]
    for (s, e) in sub_ranges:
        new_result = []
        for (rs, re_) in result:
            if e < rs or s > re_:
                new_result.append((rs, re_))
            else:
                if rs < s:
                    new_result.append((rs, s - 1))
                if re_ > e:
                    new_result.append((e + 1, re_))
        result = new_result
    return result

# ============================================================================
# 大小写映射分类
# ============================================================================

def build_case_tables(entries):
    """构建 k_case_range_maps 和 k_case_special.

    返回:
        range_maps: list of (start, end, lower_delta, upper_delta, pair_dist)
        special: list of (cp, lower, upper, title)
    """
    # 收集所有有大小写映射的码点 (lower != cp 或 upper != cp)
    case_cps = []
    for cp in sorted(entries.keys()):
        e = entries[cp]
        lower = e["lower"] if e["lower"] is not None else cp
        upper = e["upper"] if e["upper"] is not None else cp
        title = e["title"] if e["title"] is not None else upper
        if lower != cp or upper != cp:
            case_cps.append((cp, lower, upper, title))

    range_maps = []
    special = []
    special_set = set()
    used = set()

    i = 0
    n = len(case_cps)
    while i < n:
        cp, lower, upper, title = case_cps[i]

        # 尝试单一 delta 范围 (pair_dist=0)
        lower_delta = lower - cp
        upper_delta = upper - cp
        if i + 1 < n:
            ncp, nlow, nupp, _ = case_cps[i + 1]
            if ncp == cp + 1:
                nld = nlow - ncp
                nud = nupp - ncp
                if nld == lower_delta and nud == upper_delta:
                    # 扩展单一 delta 范围
                    start = cp
                    end = ncp
                    j = i + 2
                    while j < n:
                        jcp, jlow, jupp, _ = case_cps[j]
                        if jcp != end + 1:
                            break
                        if (jlow - jcp) != lower_delta or (jupp - jcp) != upper_delta:
                            break
                        end = jcp
                        j += 1
                    range_maps.append((start, end, lower_delta, upper_delta, 0))
                    for k in range(i, j):
                        used.add(case_cps[k][0])
                    i = j
                    continue

        # 尝试交替配对范围 (pair_dist=1)
        # 起始码点必须是大写: lower = cp+1, upper = cp
        if i + 1 < n and lower == cp + 1 and upper == cp:
            ncp, nlow, nupp, _ = case_cps[i + 1]
            if ncp == cp + 1 and nlow == ncp and nupp == cp:
                # 有效配对, 扩展
                start = cp
                end = ncp
                j = i + 2
                while j < n:
                    jcp, jlow, jupp, _ = case_cps[j]
                    if jcp != end + 1:
                        break
                    offset = jcp - start
                    if offset % 2 == 0:
                        # 偶偏移 = 大写: lower = jcp+1, upper = jcp
                        if jlow != jcp + 1 or jupp != jcp:
                            break
                    else:
                        # 奇偏移 = 小写: lower = jcp, upper = jcp-1
                        if jlow != jcp or jupp != jcp - 1:
                            break
                    end = jcp
                    j += 1
                range_maps.append((start, end, 1, -1, 1))
                for k in range(i, j):
                    used.add(case_cps[k][0])
                i = j
                continue

        # 无法形成范围, 放入 special
        special.append((cp, lower, upper, title))
        special_set.add(cp)
        i += 1

    # 验证: 确保范围表中的每个码点都能得到正确结果
    # 如果范围表给出错误结果, 则将该码点移入 special (覆盖)
    for (cp, lower, upper, title) in case_cps:
        if cp < 0x80:
            continue  # ASCII 快速路径, 不查表
        if cp in special_set:
            continue  # 已在 special 中
        # 查找所在范围
        computed_lower = cp
        computed_upper = cp
        for (s, e, ld, ud, pd) in range_maps:
            if s <= cp <= e:
                if pd == 0:
                    computed_lower = cp + ld
                    computed_upper = cp + ud
                else:
                    offset = cp - s
                    if offset % 2 == 0:
                        computed_lower = cp + pd
                        computed_upper = cp
                    else:
                        computed_lower = cp
                        computed_upper = cp - pd
                break
        if computed_lower != lower or computed_upper != upper:
            special.append((cp, lower, upper, title))
            special_set.add(cp)

    special.sort(key=lambda x: x[0])
    return range_maps, special

# ============================================================================
# 组合表 NFC
# ============================================================================

def build_nfc_compose(entries):
    """构建 k_nfc_compose: canonical 分解为恰好 2 个码点的反向表.
    返回: list of (base, combining, composed), 按 (base, combining) 升序
    用于 nfc_compose_lookup (按 base 二分 + combining 顺序查)
    """
    pairs = []
    for cp, e in entries.items():
        dm = e["decomp_mapping"]
        dt = e["decomp_type"]
        # 只处理 canonical 分解 (无类型标签) 且恰好 2 个码点
        if dt is not None:
            continue
        if len(dm) != 2:
            continue
        base, combining = dm[0], dm[1]
        pairs.append((base, combining, cp))

    pairs.sort(key=lambda x: (x[0], x[1]))
    return pairs

def build_nfc_decompose(compose_pairs):
    """构建 k_nfc_decompose: 与 k_nfc_compose 相同数据, 按 composed 升序.
    用于 nfc_decompose_lookup (按 composed 二分)
    全量 UCD 表中, 同 base 条目在 composed 排序下不连续, 无法与 compose 表共用,
    故独立一份按 composed 排序的表.
    """
    return sorted(compose_pairs, key=lambda x: x[2])

# ============================================================================
# 范围表 CCC
# ============================================================================

def build_ccc_ranges(entries, ud_ranges):
    """构建 k_ccc_ranges: CCC > 0 的码点, 按 CCC 值分组合并.
    返回: list of (start, end, ccc), 按 start 升序
    """
    cp_ccc = []
    for cp, e in entries.items():
        if e["ccc"] > 0:
            cp_ccc.append((cp, e["ccc"]))
    for (s, e, entry) in ud_ranges:
        if entry["ccc"] > 0:
            for cp in range(s, e + 1):
                cp_ccc.append((cp, entry["ccc"]))

    return merge_grouped_ranges(cp_ccc)

# ============================================================================
# 脚本范围表
# ============================================================================

def build_script_ranges(script_raw, emoji_picto_ranges):
    """构建 k_script_ranges.
    返回: list of (start, end, script_enum_name), 具体脚本在前, common/inherited 在后
    """
    result = []  # (start, end, enum_name)

    for (s, e, sc_name) in script_raw:
        if sc_name not in SCRIPT_ENUM:
            continue
        enum_name = SCRIPT_ENUM[sc_name]

        # 注: SMP Han → cjk_ext
        if sc_name == "Han" and s >= 0x20000:
            enum_name = "cjk_ext"

        # 注: Common/Inherited 需要减去 Extended_Pictographic 范围
        if sc_name in ("Common", "Inherited"):
            remaining = range_subtract(s, e, emoji_picto_ranges)
            for (rs, re_) in remaining:
                result.append((rs, re_, enum_name))
        else:
            result.append((s, e, enum_name))

    # 添加 Extended_Pictographic 范围为 emoji_picto
    for (s, e) in emoji_picto_ranges:
        result.append((s, e, "emoji_picto"))

    # 合并连续相同脚本的范围
    result.sort(key=lambda x: (x[0], x[2]))
    merged = []
    for (s, e, sc) in result:
        if merged and merged[-1][1] + 1 == s and merged[-1][2] == sc:
            merged[-1] = (merged[-1][0], e, sc)
        else:
            merged.append((s, e, sc))

    # 分区: 具体脚本 → common → inherited
    specific = [(s, e, sc) for (s, e, sc) in merged if sc not in ("common", "inherited")]
    common = [(s, e, sc) for (s, e, sc) in merged if sc == "common"]
    inherited = [(s, e, sc) for (s, e, sc) in merged if sc == "inherited"]

    return specific + common + inherited

# ============================================================================
# 兼容性分解表 NFKD
# ============================================================================

def build_nfkd_table(entries):
    """构建 k_nfkd_table: 兼容性分解, 递归到最终形式.
    返回: list of (cp, len, [d0, d1, d2, d3]), 按 cp 升序
    """
    # 构建 canonical 和 compatibility 分解映射
    canonical_map = {}   # cp -> [components]
    compat_map = {}      # cp -> [components]
    for cp, e in entries.items():
        dm = e["decomp_mapping"]
        dt = e["decomp_type"]
        if not dm:
            continue
        if dt is not None:
            compat_map[cp] = dm
        else:
            canonical_map[cp] = dm

    # 韩文音节范围 (Hangul, U+AC00..U+D7A3), 由算法处理, 不放入 NFKD 表
    HANGUL_S_BASE = 0xAC00
    HANGUL_S_END = 0xAC00 + 11172

    # 全角 ASCII (U+FF01..FF5E) 和全角空格 (U+3000), 由算法处理
    def is_algorithm_handled(cp):
        if HANGUL_S_BASE <= cp < HANGUL_S_END:
            return True
        if 0xFF01 <= cp <= 0xFF5E:
            return True
        if cp == 0x3000:
            return True
        return False

    # 递归 NFKD 分解
    def nfkd_decompose(cp, depth=0):
        if depth > 20:
            return [cp]
        if is_algorithm_handled(cp):
            return [cp]
        if cp in compat_map:
            result = []
            for c in compat_map[cp]:
                result.extend(nfkd_decompose(c, depth + 1))
            return result
        if cp in canonical_map:
            result = []
            for c in canonical_map[cp]:
                result.extend(nfkd_decompose(c, depth + 1))
            return result
        return [cp]

    table = []
    for cp in sorted(compat_map.keys()):
        if is_algorithm_handled(cp):
            continue
        decomp = nfkd_decompose(cp)
        if len(decomp) < 1 or len(decomp) > 4:
            continue
        # 分解结果与原码点相同 → 无需放入表
        if len(decomp) == 1 and decomp[0] == cp:
            continue
        padded = decomp + [0] * (4 - len(decomp))
        table.append((cp, len(decomp), padded))

    return table

# ============================================================================
# 代码格式化
# ============================================================================

def hex_cp(v):
    """格式化码点为 C++ 十六进制字面量."""
    if v <= 0xFFFF:
        return f"0x{v:04X}"
    else:
        return f"0x{v:05X}"

def hex_delta(v):
    """格式化 delta 值 (可为负)."""
    if v < 0:
        return f"-0x{-v:02X}"
    if v == 0:
        return "0"
    return f"0x{v:02X}"

def format_uint32_ranges(name, comment, ranges):
    """格式化 uint32_t 范围数组 (flat: start1,end1,start2,end2,...)."""
    lines = []
    lines.append(f"// ============================================================================")
    lines.append(f"// {comment}")
    lines.append(f"// ============================================================================")
    lines.append(f"inline constexpr uint32_t {name}[] = {{")
    vals = []
    for (s, e) in ranges:
        vals.append(hex_cp(s))
        vals.append(hex_cp(e))
    # 每行 4 对 (8 个值)
    per_line = 8
    for i in range(0, len(vals), per_line):
        chunk = vals[i:i + per_line]
        lines.append("    " + ", ".join(chunk) + ",")
    # 移除最后一行末尾逗号
    if lines[-1].endswith(","):
        lines[-1] = lines[-1][:-1]
    lines.append("};")
    count_name = name.replace("k_", "k_").replace("_ranges", "_range_count")
    if name == "k_alpha_ranges":
        count_name = "k_alpha_range_count"
    elif name == "k_digit_ranges":
        count_name = "k_digit_range_count"
    elif name == "k_lower_ranges":
        count_name = "k_lower_range_count"
    elif name == "k_upper_ranges":
        count_name = "k_upper_range_count"
    elif name == "k_wide_ranges":
        count_name = "k_wide_range_count"
    lines.append(f"constexpr size_t {count_name} = sizeof({name}) / (sizeof(uint32_t) * 2);")
    return "\n".join(lines) + "\n"

def format_case_range_maps(name, comment, entries):
    """格式化 case_range_map 数组."""
    lines = []
    lines.append(f"// ============================================================================")
    lines.append(f"// {comment}")
    lines.append(f"// ============================================================================")
    lines.append(f"inline constexpr case_range_map {name}[] = {{")
    for (s, e, ld, ud, pd) in entries:
        lines.append(f"    {{{hex_cp(s)}, {hex_cp(e)}, {hex_delta(ld)}, {hex_delta(ud)}, {pd}}},")
    if lines[-1].endswith(","):
        lines[-1] = lines[-1][:-1]
    lines.append("};")
    # 算法函数引用 k_case_range_map_count (单数 map)
    count_name = "k_case_range_map_count"
    lines.append(f"constexpr size_t {count_name} = sizeof({name}) / sizeof(case_range_map);")
    return "\n".join(lines) + "\n"

def format_case_special(name, comment, entries):
    """格式化 case_map_entry 数组."""
    lines = []
    lines.append(f"// ============================================================================")
    lines.append(f"// {comment}")
    lines.append(f"// ============================================================================")
    lines.append(f"inline constexpr case_map_entry {name}[] = {{")
    for (cp, lower, upper, title) in entries:
        lines.append(f"    {{{hex_cp(cp)}, {hex_cp(lower)}, {hex_cp(upper)}, {hex_cp(title)}}},")
    if lines[-1].endswith(","):
        lines[-1] = lines[-1][:-1]
    lines.append("};")
    count_name = name + "_count"
    lines.append(f"constexpr size_t {count_name} = sizeof({name}) / sizeof(case_map_entry);")
    return "\n".join(lines) + "\n"

def format_nfc_compose(name, comment, entries):
    """格式化 nfc_compose_entry 数组."""
    lines = []
    lines.append(f"// ============================================================================")
    lines.append(f"// {comment}")
    lines.append(f"// ============================================================================")
    lines.append(f"inline constexpr nfc_compose_entry {name}[] = {{")
    for i, (base, combining, composed) in enumerate(entries):
        line = f"    {{{hex_cp(base)}, {hex_cp(combining)}, {hex_cp(composed)}}}"
        if i < len(entries) - 1:
            line += ","
        lines.append(line)
    lines.append("};")
    count_name = name + "_count"
    lines.append(f"constexpr size_t {count_name} = sizeof({name}) / sizeof(nfc_compose_entry);")
    return "\n".join(lines) + "\n"

def format_ccc_ranges(name, comment, entries):
    """格式化 ccc_range 数组."""
    lines = []
    lines.append(f"// ============================================================================")
    lines.append(f"// {comment}")
    lines.append(f"// ============================================================================")
    lines.append(f"inline constexpr ccc_range {name}[] = {{")
    for i, (s, e, ccc) in enumerate(entries):
        line = f"    {{{hex_cp(s)}, {hex_cp(e)}, {ccc}}}"
        if i < len(entries) - 1:
            line += ","
        lines.append(line)
    lines.append("};")
    # 算法函数引用 k_ccc_range_count (单数 range)
    count_name = "k_ccc_range_count"
    lines.append(f"constexpr size_t {count_name} = sizeof({name}) / sizeof(ccc_range);")
    return "\n".join(lines) + "\n"

def format_script_ranges(name, comment, entries):
    """格式化 script_range 数组."""
    lines = []
    lines.append(f"// ============================================================================")
    lines.append(f"// {comment}")
    lines.append(f"// ============================================================================")
    lines.append(f"inline constexpr script_range {name}[] = {{")
    for i, (s, e, sc) in enumerate(entries):
        line = f"    {{{hex_cp(s)}, {hex_cp(e)}, script::{sc}}}"
        if i < len(entries) - 1:
            line += ","
        lines.append(line)
    lines.append("};")
    # 算法函数引用 k_script_range_count (单数 range)
    count_name = "k_script_range_count"
    lines.append(f"constexpr size_t {count_name} = sizeof({name}) / sizeof(script_range);")
    return "\n".join(lines) + "\n"

def format_nfkd_table(name, comment, entries):
    """格式化 nfkd_entry 数组."""
    lines = []
    lines.append(f"// ============================================================================")
    lines.append(f"// {comment}")
    lines.append(f"// ============================================================================")
    lines.append(f"inline constexpr nfkd_entry {name}[] = {{")
    for i, (cp, length, decomp) in enumerate(entries):
        d = ", ".join(hex_cp(x) for x in decomp)
        line = f"    {{{hex_cp(cp)}, {length}, {{{d}}}}}"
        if i < len(entries) - 1:
            line += ","
        lines.append(line)
    lines.append("};")
    count_name = name + "_count"
    lines.append(f"constexpr size_t {count_name} = sizeof({name}) / sizeof(nfkd_entry);")
    return "\n".join(lines) + "\n"

# ============================================================================
# 算法模板 (范围查找/分类/大小写/规范化/脚本/韩文/NFKD)
# ============================================================================

ALGO_RANGE_LOOKUP = """\
// ============================================================================
// 通用范围表查找 (二分)
// 范围: [start, end], 表项按 start 升序排列
// 返回: 找到返回 true, 否则 false
// ============================================================================
[[nodiscard]] FORCE_INLINE
bool range_lookup(const uint32_t* table, size_t count, uint32_t cp) noexcept
{
    // 表布局: {start1,end1, start2,end2, ...}, count 为表项数 (pair 数)
    size_t lo = 0, hi = count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t s = table[mid * 2];
        uint32_t e = table[mid * 2 + 1];
        if (cp < s) hi = mid;
        else if (cp > e) lo = mid + 1;
        else return true;
    }
    return false;
}
"""

ALGO_CASE_MAP = """\
// ============================================================================
// 大小写映射: 单点映射表 (码点 → 码点)
// 用于不规则映射 (非简单 +/- 偏移)
// ============================================================================
struct case_map_entry
{
    uint32_t cp;
    uint32_t lower;
    uint32_t upper;
    uint32_t title;
};

// 二分查找单点映射
[[nodiscard]] FORCE_INLINE
const case_map_entry* case_map_lookup(const case_map_entry* table, size_t count, uint32_t cp) noexcept
{
    size_t lo = 0, hi = count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (table[mid].cp == cp) return &table[mid];
        if (table[mid].cp < cp) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}
"""

ALGO_CASE_RANGE_MAP = """\
// ============================================================================
// 大小写范围映射: [start, end] 内的码点按固定偏移映射
// 两种语义:
//   1) 单一大小写范围 (pair_dist = 0):
//      - 全大写范围: lower_delta = +offset, upper_delta = 0
//      - 全小写范围: lower_delta = 0, upper_delta = -offset
//   2) 交替配对范围 (pair_dist > 0):
//      - 范围内码点按 pair_dist*2 周期交替: 偶偏移=大写, 奇偏移=小写
//      - 大写 cp: lower = cp + pair_dist, upper = cp
//      - 小写 cp: lower = cp, upper = cp - pair_dist
// ============================================================================
struct case_range_map
{
    uint32_t start;
    uint32_t end;
    int32_t  lower_delta;  // 单一范围: lower = cp + lower_delta
    int32_t  upper_delta;  // 单一范围: upper = cp + upper_delta
    uint32_t pair_dist;    // 0 = 单一范围; >0 = 交替配对 (距离)
};

[[nodiscard]] FORCE_INLINE
const case_range_map* case_range_lookup(const case_range_map* table, size_t count, uint32_t cp) noexcept
{
    size_t lo = 0, hi = count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < table[mid].start) hi = mid;
        else if (cp > table[mid].end) lo = mid + 1;
        else return &table[mid];
    }
    return nullptr;
}
"""

ALGO_IS_UNICODE_SPACE = """\
// ============================================================================
// 空白字符 Unicode (White_Space = Yes)
// ============================================================================
[[nodiscard]] FORCE_INLINE
bool is_unicode_space(uint32_t cp) noexcept
{
    // 空白 ASCII whitespace
    if (cp == 0x09 || cp == 0x0A || cp == 0x0B || cp == 0x0C || cp == 0x0D ||
        cp == 0x20) return true;
    // 换行 Next Line (NEL)
    if (cp == 0x85) return true;
    // 空格 NBSP
    if (cp == 0xA0) return true;
    // 空格 Ogham Space Mark
    if (cp == 0x1680) return true;
    // 各类空格 Various spaces (En/Em/.../Hair Space)
    if (cp >= 0x2000 && cp <= 0x200A) return true;
    // 分隔符 Line Separator / Paragraph Separator
    if (cp == 0x2028 || cp == 0x2029) return true;
    // 空格 Narrow No-Break Space / Medium Mathematical Space
    if (cp == 0x202F || cp == 0x205F) return true;
    // 全角空格 Ideographic Space (全角空格)
    if (cp == 0x3000) return true;
    return false;
}
"""

ALGO_IS_WIDE = """\
// 全角字符 (Wide) → 显示宽度 2
[[nodiscard]] FORCE_INLINE
bool is_wide(uint32_t cp) noexcept
{
    return range_lookup(k_wide_ranges, k_wide_range_count, cp);
}
"""

ALGO_IS_ZERO_WIDTH = """\
// 零宽字符 (Zero Width) → 显示宽度 0
[[nodiscard]] FORCE_INLINE
bool is_zero_width(uint32_t cp) noexcept
{
    // 组合标记 (Mn/Me) 零宽
    if (cp >= 0x0300 && cp <= 0x036F) return true;   // Combining Diacritical Marks
    if (cp >= 0x0483 && cp <= 0x0489) return true;
    if (cp >= 0x0591 && cp <= 0x05BD) return true;
    if (cp == 0x05BF) return true;
    if (cp == 0x05C1 || cp == 0x05C2 || cp == 0x05C4 || cp == 0x05C5 || cp == 0x05C7) return true;
    if (cp >= 0x0610 && cp <= 0x061A) return true;
    if (cp >= 0x064B && cp <= 0x065F) return true;
    if (cp == 0x0670) return true;
    if (cp >= 0x06D6 && cp <= 0x06DC) return true;
    if (cp >= 0x06DF && cp <= 0x06E4) return true;
    if (cp >= 0x06E7 && cp <= 0x06E8) return true;
    if (cp >= 0x06EA && cp <= 0x06ED) return true;
    if (cp == 0x0711) return true;
    if (cp >= 0x0730 && cp <= 0x074A) return true;
    if (cp >= 0x07A6 && cp <= 0x07B0) return true;
    if (cp >= 0x07EB && cp <= 0x07F3) return true;
    if (cp == 0x07FD) return true;
    if (cp >= 0x0816 && cp <= 0x0819) return true;
    if (cp >= 0x081B && cp <= 0x0823) return true;
    if (cp >= 0x0825 && cp <= 0x0827) return true;
    if (cp >= 0x0829 && cp <= 0x082D) return true;
    if (cp >= 0x0859 && cp <= 0x085B) return true;
    if (cp >= 0x08D3 && cp <= 0x08E1) return true;
    if (cp >= 0x08E3 && cp <= 0x0903) return true;
    if (cp >= 0x093A && cp <= 0x093C) return true;
    if (cp >= 0x093E && cp <= 0x094F) return true;
    if (cp >= 0x0951 && cp <= 0x0957) return true;
    if (cp >= 0x0962 && cp <= 0x0963) return true;
    if (cp >= 0x0981 && cp <= 0x0983) return true;
    // 零宽 ZWJ / ZWNJ / BOM
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D) return true;  // Zero Width Space/J/Joiner
    if (cp == 0xFEFF) return true;  // BOM
    // 选择器 Variation Selectors
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;
    if (cp >= 0xE0100 && cp <= 0xE01EF) return true;
    // 简化: 0x09BC 以上的其他组合标记 (Mn/Me) 范围省略, 实际完整表需 UCD
    return false;
}
"""

ALGO_CP_DISPLAY_WIDTH = """\
// 计算码点的显示宽度 (0/1/2)
[[nodiscard]] FORCE_INLINE
int cp_display_width(uint32_t cp) noexcept
{
    if (is_zero_width(cp)) return 0;
    if (is_wide(cp)) return 2;
    return 1;
}
"""

ALGO_IS_COMBINING_MARK = """\
// ============================================================================
// 组合标记判断 (用于字形簇 UAX #29)
// 标记 Mn (Nonspacing Mark) / Me (Enclosing Mark) 范围
// ============================================================================
[[nodiscard]] FORCE_INLINE
bool is_combining_mark(uint32_t cp) noexcept
{
    // 简化范围 (覆盖主要 Mn/Me 范围)
    if (cp >= 0x0300 && cp <= 0x036F) return true;
    if (cp >= 0x0483 && cp <= 0x0489) return true;
    if (cp >= 0x0591 && cp <= 0x05BD) return true;
    if (cp >= 0x05BF && cp <= 0x05C7) return true;  // 部分有间隙
    if (cp >= 0x0610 && cp <= 0x061A) return true;
    if (cp >= 0x064B && cp <= 0x065F) return true;
    if (cp >= 0x06D6 && cp <= 0x06ED) return true;
    if (cp >= 0x0711 && cp <= 0x074A) return true;
    if (cp >= 0x07A6 && cp <= 0x07B0) return true;
    if (cp >= 0x07EB && cp <= 0x07F3) return true;
    if (cp >= 0x0816 && cp <= 0x082D) return true;
    if (cp >= 0x0859 && cp <= 0x085B) return true;
    if (cp >= 0x08D3 && cp <= 0x0903) return true;
    if (cp >= 0x093A && cp <= 0x094F) return true;
    if (cp >= 0x0951 && cp <= 0x0963) return true;
    if (cp >= 0x0981 && cp <= 0x09BC) return true;
    if (cp >= 0x09BE && cp <= 0x09C4) return true;
    if (cp >= 0x09C7 && cp <= 0x09C8) return true;
    if (cp >= 0x09CB && cp <= 0x09CD) return true;
    if (cp >= 0x20D0 && cp <= 0x20FF) return true;  // Combining Diacritical Marks for Symbols
    if (cp >= 0xFE20 && cp <= 0xFE2F) return true;  // Combining Half Marks
    // 选择器 Variation Selectors
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;
    if (cp >= 0xE0100 && cp <= 0xE01EF) return true;
    // 简化: 其他高位组合标记 (Mn/Me) 范围省略, 实际完整表需 UCD
    return false;
}
"""

ALGO_IS_EXTENDED_PICTOGRAPHIC = """\
// 扩展图像 Extended_Pictographic (用于 ZWJ 序列判断)
[[nodiscard]] FORCE_INLINE
bool is_extended_pictographic(uint32_t cp) noexcept
{
    // 表情 Emoji 主要范围
    if (cp >= 0x1F300 && cp <= 0x1F5FF) return true;  // Misc Symbols and Pictographs
    if (cp >= 0x1F600 && cp <= 0x1F64F) return true;  // Emoticons
    if (cp >= 0x1F680 && cp <= 0x1F6FF) return true;  // Transport
    if (cp >= 0x1F900 && cp <= 0x1F9FF) return true;  // Supplemental Symbols
    if (cp >= 0x1FA70 && cp <= 0x1FAFF) return true;  // Symbols and Pictographs Extended-A
    if (cp >= 0x2600 && cp <= 0x26FF) return true;    // Misc Symbols
    if (cp >= 0x2700 && cp <= 0x27BF) return true;    // Dingbats
    return false;
}
"""

ALGO_CLASSIFICATION = """\
// ============================================================================
// 字符分类查询入口
// ============================================================================
[[nodiscard]] FORCE_INLINE
bool is_alpha_cp(uint32_t cp) noexcept
{
    // 纯 ASCII 快速路径
    if (cp < 0x80) [[likely]]
    {
        return (cp >= 0x41 && cp <= 0x5A) || (cp >= 0x61 && cp <= 0x7A);
    }
    return range_lookup(k_alpha_ranges, k_alpha_range_count, cp);
}

[[nodiscard]] FORCE_INLINE
bool is_digit_cp(uint32_t cp) noexcept
{
    // 纯 ASCII 快速路径
    if (cp < 0x80) [[likely]] return cp >= 0x30 && cp <= 0x39;
    return range_lookup(k_digit_ranges, k_digit_range_count, cp);
}

[[nodiscard]] FORCE_INLINE
bool is_lower_cp(uint32_t cp) noexcept
{
    // 纯 ASCII 快速路径
    if (cp < 0x80) [[likely]] return cp >= 0x61 && cp <= 0x7A;
    return range_lookup(k_lower_ranges, k_lower_range_count, cp);
}

[[nodiscard]] FORCE_INLINE
bool is_upper_cp(uint32_t cp) noexcept
{
    // 纯 ASCII 快速路径
    if (cp < 0x80) [[likely]] return cp >= 0x41 && cp <= 0x5A;
    return range_lookup(k_upper_ranges, k_upper_range_count, cp);
}

[[nodiscard]] FORCE_INLINE
bool is_alnum_cp(uint32_t cp) noexcept
{
    // 纯 ASCII 快速路径
    if (cp < 0x80) [[likely]]
    {
        return (cp >= 0x30 && cp <= 0x39) ||
               (cp >= 0x41 && cp <= 0x5A) ||
               (cp >= 0x61 && cp <= 0x7A);
    }
    return is_alpha_cp(cp) || is_digit_cp(cp);
}
"""

ALGO_CASE_CONVERSION = """\
// ============================================================================
// 大小写转换查询入口
// ============================================================================
[[nodiscard]] FORCE_INLINE
uint32_t to_lower_cp(uint32_t cp) noexcept
{
    // 纯 ASCII 快速路径: A-Z → a-z
    if (cp < 0x80) [[likely]]
    {
        if (cp >= 0x41 && cp <= 0x5A) return cp + 32;
        return cp;
    }
    // 1. 查不规则映射表
    if (const auto* e = case_map_lookup(k_case_special, k_case_special_count, cp))
        return e->lower;
    // 2. 查范围映射表
    if (const auto* r = case_range_lookup(k_case_range_maps, k_case_range_map_count, cp))
    {
        if (r->pair_dist == 0)
            return static_cast<uint32_t>(static_cast<int32_t>(cp) + r->lower_delta);
        // 交替配对范围: 偶偏移=大写 (lower = cp + pair_dist), 奇偏移=小写 (无变化)
        uint32_t offset = cp - r->start;
        if ((offset & 1) == 0)
            return cp + r->pair_dist;
        return cp;
    }
    return cp;
}

[[nodiscard]] FORCE_INLINE
uint32_t to_upper_cp(uint32_t cp) noexcept
{
    // 纯 ASCII 快速路径: a-z → A-Z
    if (cp < 0x80) [[likely]]
    {
        if (cp >= 0x61 && cp <= 0x7A) return cp - 32;
        return cp;
    }
    if (const auto* e = case_map_lookup(k_case_special, k_case_special_count, cp))
        return e->upper;
    if (const auto* r = case_range_lookup(k_case_range_maps, k_case_range_map_count, cp))
    {
        if (r->pair_dist == 0)
            return static_cast<uint32_t>(static_cast<int32_t>(cp) + r->upper_delta);
        // 交替配对范围: 偶偏移=大写 (无变化), 奇偏移=小写 (upper = cp - pair_dist)
        uint32_t offset = cp - r->start;
        if ((offset & 1) != 0)
            return cp - r->pair_dist;
        return cp;
    }
    return cp;
}

[[nodiscard]] FORCE_INLINE
uint32_t to_title_cp(uint32_t cp) noexcept
{
    if (const auto* e = case_map_lookup(k_case_special, k_case_special_count, cp))
        return e->title;
    // 默认 titlecase = uppercase
    return to_upper_cp(cp);
}
"""

ALGO_NFC_COMPOSE_ENTRY = """\
// ============================================================================
// 规范化 NFC: (base, combining) ↔ precomposed 组合/分解表
// 数据来源: UnicodeData.txt 的 Canonical Decomposition Field (恰好 2 码点)
// 表 k_nfc_compose:   按 (base, combining) 升序, 供 nfc_compose_lookup
// 表 k_nfc_decompose: 按 composed 升序, 供 nfc_decompose_lookup
// ============================================================================
struct nfc_compose_entry
{
    uint32_t base;      // 基础字符
    uint32_t combining; // 组合标记 (canonical)
    uint32_t composed;  // 预组合码点
};
"""

ALGO_NFC_LOOKUP = """\
// 查询: 给定 base 和 combining, 返回预组合码点 (不存在返回 0)
// 表已按 base 升序 (同 base 内 combining 升序), 两次二分: 先定位 base 区间, 再在区间内查 combining
[[nodiscard]] FORCE_INLINE
uint32_t nfc_compose_lookup(uint32_t base, uint32_t combining) noexcept
{
    // 二分查找 base 的下界 (首个 >= base 的项)
    size_t lo = 0, hi = k_nfc_compose_count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (k_nfc_compose[mid].base < base) lo = mid + 1;
        else hi = mid;
    }
    // 在 base 区间内顺序查 combining (同 base 项很少, 通常 1-3 项)
    while (lo < k_nfc_compose_count && k_nfc_compose[lo].base == base)
    {
        if (k_nfc_compose[lo].combining == combining)
            return k_nfc_compose[lo].composed;
        ++lo;
    }
    return 0;
}

// 查询: 给定预组合码点, 返回其 (base, combining) 分解 (不存在返回 false)
// 表 k_nfc_decompose 已按 composed 升序, 直接二分
// (全量 UCD 表中, 同 base 条目在 composed 排序下不连续, 故独立一份按 composed 排序的表)
[[nodiscard]] FORCE_INLINE
bool nfc_decompose_lookup(uint32_t composed, uint32_t& base, uint32_t& combining) noexcept
{
    size_t lo = 0, hi = k_nfc_decompose_count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t c = k_nfc_decompose[mid].composed;
        if (c == composed)
        {
            base = k_nfc_decompose[mid].base;
            combining = k_nfc_decompose[mid].combining;
            return true;
        }
        if (c < composed) lo = mid + 1;
        else hi = mid;
    }
    return false;
}
"""

ALGO_CCC_RANGE = """\
// ============================================================================
// 规范 Canonical Combining Class (CCC, UAX #44 / UnicodeData.txt)
// 用于 NFC 规范化的 canonical ordering: 组合标记按 CCC 升序稳定排序
//   值 CCC=0: 基础字符 (不参与排序)
//   值 CCC=220: Below (下方)  CCC=230: Above (上方)
// 表布局: {start, end, ccc}, 按 start 升序, 二分查找
// 覆盖: 0x0300-0x036F (Latin 组合标记, 详细) + 主要 Indic/Arabic/Hebrew/
//        含 Tibetan/Myanmar 等组合标记范围 (近似值); 未列出范围默认 CCC=230
// ============================================================================
struct ccc_range
{
    uint32_t start;
    uint32_t end;
    uint8_t  ccc;
};
"""

ALGO_CANONICAL_CCC = """\
// 查询: 给定码点, 返回其 CCC (0 表示基础字符或不在表中, 默认 0)
[[nodiscard]] FORCE_INLINE
uint8_t canonical_combining_class(uint32_t cp) noexcept
{
    size_t lo = 0, hi = k_ccc_range_count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < k_ccc_ranges[mid].start) hi = mid;
        else if (cp > k_ccc_ranges[mid].end) lo = mid + 1;
        else return k_ccc_ranges[mid].ccc;
    }
    return 0;
}
"""

ALGO_SCRIPT_ENUM = """\
// ============================================================================
// 脚本 Unicode Script (脚本) 判断, UAX #24
// 枚举主要脚本; 覆盖 BMP 主要脚本 + 常用 SMP (CJK Ext / Emoji 等)
// 数据来源: Scripts.txt; 表布局: {start, end, script_id}, 二分查找
// ============================================================================
enum class script : uint8_t
{
    unknown     = 0,
    common      = 1,   // 通用 (数字/标点等, 多脚本共用)
    inherited   = 2,   // 继承 (组合标记继承前字符脚本)
    latin       = 3,
    greek       = 4,
    cyrillic    = 5,
    armenian    = 6,
    hebrew      = 7,
    arabic      = 8,
    syriac      = 9,
    thaana      = 10,
    devanagari  = 11,
    bengali     = 12,
    gurmukhi    = 13,
    gujarati    = 14,
    oriya       = 15,
    tamil       = 16,
    telugu      = 17,
    kannada     = 18,
    malayalam   = 19,
    sinhala     = 20,
    thai        = 21,
    lao         = 22,
    tibetan     = 23,
    myanmar     = 24,
    georgian    = 25,
    hangul      = 26,  // Hangul Jamo/Syllables/Compatibility
    hiragana    = 27,
    katakana    = 28,
    han         = 29,  // CJK Unified Ideographs (中文/日文汉字)
    ethiopic    = 30,
    cherokee    = 31,
    canadian    = 32,  // Canadian Aboriginal
    ogham       = 33,
    runic       = 34,
    tagalog     = 35,
    mongolian   = 36,
    // 常用 SMP
    cjk_ext     = 37,  // CJK Ext B/C/D/E/F (历史汉字扩展)
    emoji_picto = 38,  // Emoji / Extended Pictographic 主要范围
};

struct script_range
{
    uint32_t    start;
    uint32_t    end;
    script      sc;
};
"""

ALGO_SCRIPT_OF = """\
// 查询: 给定码点, 返回其 Script (未匹配返回 common, 因多数未列出码点为 common/符号)
// 注意: 表项可能重叠 (inherited/common 与具体脚本范围), 故按"先具体后通用"顺序遍历匹配
// 表已按"具体脚本在前, common/inherited 在后"排列, 故线性返回首个命中即可
// 优化: ASCII fast-path (覆盖最常见的 Latin/数字/标点), 非 ASCII 才查表
[[nodiscard]] FORCE_INLINE
script script_of(uint32_t cp) noexcept
{
    // 纯 ASCII fast-path: A-Z/a-z → latin, 数字/标点 → common, 控制字符 → unknown
    if (cp < 0x80) [[likely]]
    {
        if ((cp >= 0x41 && cp <= 0x5A) || (cp >= 0x61 && cp <= 0x7A)) return script::latin;
        if (cp >= 0x20) return script::common;
        return script::unknown;
    }
    // 优先匹配具体脚本 (跳过 common/inherited); 再回退 common/inherited
    for (size_t i = 0; i < k_script_range_count; ++i)
    {
        if (cp >= k_script_ranges[i].start && cp <= k_script_ranges[i].end)
            return k_script_ranges[i].sc;
    }
    return script::unknown;
}

// 查询: 给定码点是否属于指定脚本
[[nodiscard]] FORCE_INLINE
bool is_script(uint32_t cp, script sc) noexcept
{
    return script_of(cp) == sc;
}
"""

ALGO_HANGUL = """\
// ============================================================================
// 韩文 Hangul 规范化算法 (UAX #15 L3, 纯算法无查表)
// 预组合音节范围: U+AC00..U+D7A3 (共 11172 个)
// 音节 L (Lead) × V (Vowel) × T (Trail) = 19 × 21 × 28 = 11172
// ============================================================================
inline constexpr uint32_t HANGUL_S_BASE  = 0xAC00;
inline constexpr uint32_t HANGUL_L_BASE  = 0x1100;
inline constexpr uint32_t HANGUL_V_BASE  = 0x1161;
inline constexpr uint32_t HANGUL_T_BASE  = 0x11A7;
inline constexpr uint32_t HANGUL_L_COUNT = 19;
inline constexpr uint32_t HANGUL_V_COUNT = 21;
inline constexpr uint32_t HANGUL_T_COUNT = 28;
inline constexpr uint32_t HANGUL_N_COUNT = HANGUL_V_COUNT * HANGUL_T_COUNT; // 588
inline constexpr uint32_t HANGUL_S_COUNT = HANGUL_L_COUNT * HANGUL_N_COUNT; // 11172

// 判断是否为预组合 Hangul 音节
[[nodiscard]] FORCE_INLINE
bool is_hangul_syllable(uint32_t cp) noexcept
{
    return cp >= HANGUL_S_BASE && cp < HANGUL_S_BASE + HANGUL_S_COUNT;
}

// 分解 Hangul 音节 → L + V [+ T]
// 返回: 分解后码点数 (0=非音节; 2=LV; 3=LVT)
[[nodiscard]] FORCE_INLINE
uint32_t hangul_decompose(uint32_t cp, uint32_t out[3]) noexcept
{
    if (cp < HANGUL_S_BASE || cp >= HANGUL_S_BASE + HANGUL_S_COUNT) return 0;
    uint32_t s = cp - HANGUL_S_BASE;
    out[0] = HANGUL_L_BASE + s / HANGUL_N_COUNT;
    out[1] = HANGUL_V_BASE + (s % HANGUL_N_COUNT) / HANGUL_T_COUNT;
    uint32_t t = s % HANGUL_T_COUNT;
    if (t != 0)
    {
        out[2] = HANGUL_T_BASE + t;
        return 3;
    }
    return 2;
}

// 组合两个码点为 Hangul 音节 (L+V → LV, 或 LV+T → LVT)
// 返回: 组合后码点 (0 = 不可组合)
[[nodiscard]] FORCE_INLINE
uint32_t hangul_compose(uint32_t a, uint32_t b) noexcept
{
    // 组合 L + V → LV
    uint32_t l = a - HANGUL_L_BASE;
    if (l < HANGUL_L_COUNT)
    {
        uint32_t v = b - HANGUL_V_BASE;
        if (v < HANGUL_V_COUNT)
        {
            return HANGUL_S_BASE + l * HANGUL_N_COUNT + v * HANGUL_T_COUNT;
        }
        return 0;
    }
    // 组合 LV + T → LVT (仅当 LV 未含 T 时)
    if (a >= HANGUL_S_BASE && a < HANGUL_S_BASE + HANGUL_S_COUNT)
    {
        uint32_t s = a - HANGUL_S_BASE;
        if ((s % HANGUL_T_COUNT) == 0)
        {
            uint32_t t = b - HANGUL_T_BASE;
            if (t > 0 && t < HANGUL_T_COUNT)
            {
                return a + t;
            }
        }
    }
    return 0;
}
"""

ALGO_NFKD_ENTRY = """\
// ============================================================================
// 兼容性分解 NFKD
// 全角 ASCII (U+FF01..FF5E) 与全角空格 (U+3000) 用算法处理 (减偏移)
// 其它兼容字符用查表; 表项已预计算到最终分解形式 (无需再 canonical 分解)
// 表布局: {cp, len, decomp[4]}, 按 cp 升序, 二分查找
// ============================================================================
struct nfkd_entry
{
    uint32_t cp;
    uint8_t  len;        // 分解后码点数 (1..4)
    uint32_t decomp[4];  // 分解后码点序列
};
"""

ALGO_NFKD_LOOKUP = """\
// 查表 NFKD (不含全角 ASCII 算法处理部分)
// 返回: 找到返回 true, out_len/out[4] 输出分解; 否则 false
[[nodiscard]] FORCE_INLINE
bool nfkd_lookup(uint32_t cp, uint32_t out[4], uint8_t& out_len) noexcept
{
    size_t lo = 0, hi = k_nfkd_table_count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (k_nfkd_table[mid].cp == cp)
        {
            out_len = k_nfkd_table[mid].len;
            out[0] = k_nfkd_table[mid].decomp[0];
            out[1] = k_nfkd_table[mid].decomp[1];
            out[2] = k_nfkd_table[mid].decomp[2];
            out[3] = k_nfkd_table[mid].decomp[3];
            return true;
        }
        if (k_nfkd_table[mid].cp < cp) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

// 全角 ASCII / 全角空格算法分解 NFKD (FF01..FF5E, 3000)
// 返回: true 表示是全角 ASCII 并已分解
[[nodiscard]] FORCE_INLINE
bool nfkd_fullwidth_decompose(uint32_t cp, uint32_t& out) noexcept
{
    if (cp >= 0xFF01 && cp <= 0xFF5E)
    {
        out = cp - 0xFEE0;
        return true;
    }
    if (cp == 0x3000)
    {
        out = 0x0020;
        return true;
    }
    return false;
}

// 判断码点是否需要 NFKD 分解 (用于快速跳过)
[[nodiscard]] FORCE_INLINE
bool needs_nfkd_decompose(uint32_t cp) noexcept
{
    if (cp >= 0xFF01 && cp <= 0xFF9F) return true;
    if (cp == 0x3000) return true;
    if (cp >= 0x00B2 && cp <= 0x00B9) return true;
    if (cp >= 0x2070 && cp <= 0x209F) return true;
    if (cp >= 0x2150 && cp <= 0x218F) return true;
    if (cp >= 0x2460 && cp <= 0x24FF) return true;
    if (cp >= 0x2100 && cp <= 0x214F) return true;
    if (cp >= 0xFB00 && cp <= 0xFB06) return true;
    if (cp >= 0x3300 && cp <= 0x33FF) return true;
    return false;
}
"""

# ============================================================================
# 文件生成
# ============================================================================

def generate_header(version, date_str, timestamp):
    """生成文件头."""
    return f"""#pragma once

// 范围表 Unicode 数据与查找算法
// 自动生成, 勿手动修改. 修改请运行: python include/part/utf8pp/gen_unicode_data.py
// 数据来源: UnicodeData.txt / DerivedCoreProperties.txt / EastAsianWidth.txt / Scripts.txt / emoji-data.txt
// 版本 Unicode: {version} ({date_str})
// 生成时间: {timestamp}

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "../force_inline.hpp"

namespace unicode_data {{

"""

def generate_hpp(version, date_str, timestamp, tables):
    """组装完整的 unicode_data.hpp 内容."""
    parts = []
    parts.append(generate_header(version, date_str, timestamp))

    # 算法: range_lookup
    parts.append(ALGO_RANGE_LOOKUP)
    parts.append("\n")

    # 算法: case_map_entry + case_map_lookup
    parts.append(ALGO_CASE_MAP)
    parts.append("\n")

    # 算法: case_range_map + case_range_lookup
    parts.append(ALGO_CASE_RANGE_MAP)
    parts.append("\n")

    # 数据: k_alpha_ranges
    parts.append(tables["alpha_ranges"])
    parts.append("\n")

    # 数据: k_digit_ranges
    parts.append(tables["digit_ranges"])
    parts.append("\n")

    # 数据: k_lower_ranges
    parts.append(tables["lower_ranges"])
    parts.append("\n")

    # 数据: k_upper_ranges
    parts.append(tables["upper_ranges"])
    parts.append("\n")

    # 数据: k_case_range_maps
    parts.append(tables["case_range_maps"])
    parts.append("\n")

    # 数据: k_case_special
    parts.append(tables["case_special"])
    parts.append("\n")

    # 算法: is_unicode_space
    parts.append(ALGO_IS_UNICODE_SPACE)
    parts.append("\n")

    # 数据: k_wide_ranges
    parts.append(tables["wide_ranges"])
    parts.append("\n")

    # 算法: is_wide
    parts.append(ALGO_IS_WIDE)
    parts.append("\n")

    # 算法: is_zero_width
    parts.append(ALGO_IS_ZERO_WIDTH)
    parts.append("\n")

    # 算法: cp_display_width
    parts.append(ALGO_CP_DISPLAY_WIDTH)
    parts.append("\n")

    # 算法: is_combining_mark
    parts.append(ALGO_IS_COMBINING_MARK)
    parts.append("\n")

    # 算法: is_extended_pictographic
    parts.append(ALGO_IS_EXTENDED_PICTOGRAPHIC)
    parts.append("\n")

    # 算法: 字符分类查询入口
    parts.append(ALGO_CLASSIFICATION)
    parts.append("\n")

    # 算法: 大小写转换查询入口
    parts.append(ALGO_CASE_CONVERSION)
    parts.append("\n")

    # 算法: nfc_compose_entry 结构
    parts.append(ALGO_NFC_COMPOSE_ENTRY)
    parts.append("\n")

    # 数据: k_nfc_compose + k_nfc_decompose
    parts.append(tables["nfc_compose"])
    parts.append("\n")
    parts.append(tables["nfc_decompose"])
    parts.append("\n")

    # 算法: nfc_compose_lookup / nfc_decompose_lookup
    parts.append(ALGO_NFC_LOOKUP)
    parts.append("\n")

    # 算法: ccc_range 结构
    parts.append(ALGO_CCC_RANGE)
    parts.append("\n")

    # 数据: k_ccc_ranges
    parts.append(tables["ccc_ranges"])
    parts.append("\n")

    # 算法: canonical_combining_class
    parts.append(ALGO_CANONICAL_CCC)
    parts.append("\n")

    # 算法: script 枚举 + script_range 结构
    parts.append(ALGO_SCRIPT_ENUM)
    parts.append("\n")

    # 数据: k_script_ranges
    parts.append(tables["script_ranges"])
    parts.append("\n")

    # 算法: script_of / is_script
    parts.append(ALGO_SCRIPT_OF)
    parts.append("\n")

    # 算法: Hangul 常量 + 函数
    parts.append(ALGO_HANGUL)
    parts.append("\n")

    # 算法: nfkd_entry 结构
    parts.append(ALGO_NFKD_ENTRY)
    parts.append("\n")

    # 数据: k_nfkd_table
    parts.append(tables["nfkd_table"])
    parts.append("\n")

    # 算法: nfkd_lookup / nfkd_fullwidth_decompose / needs_nfkd_decompose
    parts.append(ALGO_NFKD_LOOKUP)

    # 文件尾
    parts.append("\n} // namespace unicode_data\n")

    return "".join(parts)

# ============================================================================
# 主函数
# ============================================================================

def main():
    # 解析命令行参数
    version = "latest"
    output = None

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--version" and i + 1 < len(args):
            version = args[i + 1]
            i += 2
        elif args[i] == "--output" and i + 1 < len(args):
            output = args[i + 1]
            i += 2
        elif args[i] in ("-h", "--help"):
            print("用法: python include/part/utf8pp/gen_unicode_data.py [--version 15.1.0] [--output include/part/utf8pp/unicode_data.hpp]")
            return 0
        else:
            print(f"未知参数: {args[i]}")
            print("用法: python include/part/utf8pp/gen_unicode_data.py [--version 15.1.0] [--output include/part/utf8pp/unicode_data.hpp]")
            return 1

    # 默认输出路径
    if output is None:
        # 脚本在 include/part/utf8pp/ 目录下, 输出到同级的 unicode_data.hpp
        script_dir = os.path.dirname(os.path.abspath(__file__))
        output = os.path.join(script_dir, "unicode_data.hpp")

    now = datetime.datetime.now()
    date_str = now.strftime("%Y-%m-%d")
    timestamp = now.strftime("%Y-%m-%d %H:%M:%S")

    print(f"Unicode 数据生成脚本")
    print(f"  版本: {version}")
    print(f"  输出: {output}")
    print()

    # 下载 UCD 文件
    cache_dir = os.path.join(tempfile.gettempdir(), "ucd_cache")
    os.makedirs(cache_dir, exist_ok=True)

    print("下载 UCD 文件:")
    try:
        paths = download_all(version, cache_dir)
    except Exception as e:
        print(f"\n错误: 无法下载 UCD 文件 - {e}")
        print("提示: 请检查网络连接, 或手动下载 UCD 文件到缓存目录:")
        print(f"  {cache_dir}")
        return 1
    print()

    # 解析 UCD 文件
    print("解析 UCD 文件...")
    entries, ud_ranges = parse_unicode_data(paths["UnicodeData"])
    print(f"  UnicodeData.txt: {len(entries)} 条目, {len(ud_ranges)} 范围")

    props = parse_property_file(paths["DerivedCoreProperties"])
    alpha_ranges_raw = props.get("Alphabetic", [])
    lower_ranges_raw = props.get("Lowercase", [])
    upper_ranges_raw = props.get("Uppercase", [])
    print(f"  DerivedCoreProperties.txt: Alphabetic={len(alpha_ranges_raw)} 范围, "
          f"Lowercase={len(lower_ranges_raw)} 范围, Uppercase={len(upper_ranges_raw)} 范围")

    eaw = parse_east_asian_width(paths["EastAsianWidth"])
    wide_raw = [(s, e) for (s, e, w) in eaw if w in ("W", "F")]
    print(f"  EastAsianWidth.txt: W/F 范围 {len(wide_raw)} 个")

    scripts_raw = parse_scripts(paths["Scripts"])
    print(f"  Scripts.txt: {len(scripts_raw)} 范围")

    emoji_picto = parse_emoji_data(paths["EmojiData"])
    print(f"  emoji-data.txt: Extended_Pictographic {len(emoji_picto)} 范围")
    print()

    # 构建数据表
    print("构建数据表...")

    # 表 k_alpha_ranges
    alpha_cps = []
    for (s, e) in alpha_ranges_raw:
        alpha_cps.extend(range(s, e + 1))
    alpha_ranges = merge_ranges(alpha_cps)
    print(f"  k_alpha_ranges: {len(alpha_ranges)} 范围")

    # 表 k_digit_ranges
    digit_cps = []
    for cp, e in entries.items():
        if e["gc"] == "Nd":
            digit_cps.append(cp)
    for (s, e, entry) in ud_ranges:
        if entry["gc"] == "Nd":
            digit_cps.extend(range(s, e + 1))
    digit_ranges = merge_ranges(digit_cps)
    print(f"  k_digit_ranges: {len(digit_ranges)} 范围")

    # 表 k_lower_ranges
    lower_cps = []
    for (s, e) in lower_ranges_raw:
        lower_cps.extend(range(s, e + 1))
    lower_ranges = merge_ranges(lower_cps)
    print(f"  k_lower_ranges: {len(lower_ranges)} 范围")

    # 表 k_upper_ranges
    upper_cps = []
    for (s, e) in upper_ranges_raw:
        upper_cps.extend(range(s, e + 1))
    upper_ranges = merge_ranges(upper_cps)
    print(f"  k_upper_ranges: {len(upper_ranges)} 范围")

    # 表 k_case_range_maps / k_case_special
    case_range_maps, case_special = build_case_tables(entries)
    print(f"  k_case_range_maps: {len(case_range_maps)} 条目")
    print(f"  k_case_special: {len(case_special)} 条目")

    # 表 k_wide_ranges
    wide_cps = []
    for (s, e) in wide_raw:
        wide_cps.extend(range(s, e + 1))
    wide_ranges = merge_ranges(wide_cps)
    print(f"  k_wide_ranges: {len(wide_ranges)} 范围")

    # 表 k_nfc_compose (按 base, combining 排序) + k_nfc_decompose (按 composed 排序)
    nfc_compose = build_nfc_compose(entries)
    nfc_decompose = build_nfc_decompose(nfc_compose)
    print(f"  k_nfc_compose: {len(nfc_compose)} 条目")
    print(f"  k_nfc_decompose: {len(nfc_decompose)} 条目")

    # 表 k_ccc_ranges
    ccc_ranges = build_ccc_ranges(entries, ud_ranges)
    print(f"  k_ccc_ranges: {len(ccc_ranges)} 条目")

    # 表 k_script_ranges
    script_ranges = build_script_ranges(scripts_raw, emoji_picto)
    print(f"  k_script_ranges: {len(script_ranges)} 条目")

    # 表 k_nfkd_table
    nfkd_table = build_nfkd_table(entries)
    print(f"  k_nfkd_table: {len(nfkd_table)} 条目")
    print()

    # 格式化数据表
    tables = {
        "alpha_ranges": format_uint32_ranges(
            "k_alpha_ranges",
            "字母范围 (Alphabetic, DerivedCoreProperties.txt)",
            alpha_ranges),
        "digit_ranges": format_uint32_ranges(
            "k_digit_ranges",
            "数字范围 (Nd: Decimal Number)",
            digit_ranges),
        "lower_ranges": format_uint32_ranges(
            "k_lower_ranges",
            "小写字母范围 (Lowercase, DerivedCoreProperties.txt)",
            lower_ranges),
        "upper_ranges": format_uint32_ranges(
            "k_upper_ranges",
            "大写字母范围 (Uppercase, DerivedCoreProperties.txt)",
            upper_ranges),
        "case_range_maps": format_case_range_maps(
            "k_case_range_maps",
            "大小写范围映射 (连续范围 + 固定偏移 + 交替配对)",
            case_range_maps),
        "case_special": format_case_special(
            "k_case_special",
            "不规则大小写映射 (无法用范围+偏移表达的)",
            case_special),
        "wide_ranges": format_uint32_ranges(
            "k_wide_ranges",
            "宽度 East Asian Width: 全角/宽字符 (display_width = 2), 来源: EastAsianWidth.txt 的 W/F 分类",
            wide_ranges),
        "nfc_compose": format_nfc_compose(
            "k_nfc_compose",
            "组合表 NFC: (base, combining) → composed, canonical 分解恰好 2 码点, 按 (base, combining) 升序",
            nfc_compose),
        "nfc_decompose": format_nfc_compose(
            "k_nfc_decompose",
            "分解表 NFC: 同 k_nfc_compose 数据, 按 composed 升序 (供 nfc_decompose_lookup 二分)",
            nfc_decompose),
        "ccc_ranges": format_ccc_ranges(
            "k_ccc_ranges",
            "规范 Canonical Combining Class 范围 (CCC > 0), 来源: UnicodeData.txt field 3",
            ccc_ranges),
        "script_ranges": format_script_ranges(
            "k_script_ranges",
            "脚本 Unicode Script 范围, 来源: Scripts.txt + emoji-data.txt (Extended_Pictographic → emoji_picto, SMP Han → cjk_ext)",
            script_ranges),
        "nfkd_table": format_nfkd_table(
            "k_nfkd_table",
            "兼容性分解表 NFKD (递归到最终形式, 不含全角 ASCII/空格)",
            nfkd_table),
    }

    # 生成完整文件
    print("生成 unicode_data.hpp...")
    content = generate_hpp(version, date_str, timestamp, tables)

    # 写入文件
    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    with open(output, "w", encoding="utf-8") as f:
        f.write(content)

    line_count = content.count("\n")
    print(f"  已写入: {output} ({line_count} 行)")
    print()
    print("完成!")

    return 0

if __name__ == "__main__":
    sys.exit(main())
