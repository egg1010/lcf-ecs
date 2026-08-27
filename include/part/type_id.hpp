#pragma once
#include <atomic>
#include <cassert>
#include <cstring>
#include <new>
#include <string_view>
#include "id_.hpp"
#include "force_inline.hpp"

// 自定义类型 (运行期按名字注册) 的存储语义描述
struct type_def
{
    size_t size{0};                     // 元素字节大小 (>0)
    size_t alignment{0};                // 对齐 (2 的幂)
    bool trivially_copyable{false};    // true: memcpy 搬运, 不调构造析构
    void (*construct)(void* p) noexcept{nullptr};  // 非平凡时必填
    void (*destruct)(void* p) noexcept{nullptr};    // 非平凡时必填
};

class type_id
{
private:
    inline static id_allocation<int> type_id_allocator{};

    // ---- 运行期名字注册表 ----
    // 编码同 string_to_code::code_value::encode_inline (字节级单射, 非哈希);
    // 因 type_id 位于核心包含图, 此处本地实现避免拖入 utf8pp 依赖
    // 短名(<=8B): 编码值 + 长度即身份, 槽内直存 id, 查询单 cache line
    // 长名(>8B):  前 8 字节为槽内 key, 注册表条目存全名 memcmp 验证
    // 写侧自旋锁 + 发布序 (payload/length 先于 key); 读侧无锁
    static constexpr size_t def_table_size_ = size_t{1} << 12;  // 4096 槽
    static constexpr size_t def_registry_cap_ = 4096;

    struct def_slot
    {
        std::atomic<uint64_t> key;   // 短名: 全串编码; 长名: 前 8 字节
        uint32_t length;             // 全名长度; 0 = 空槽
        int32_t payload;             // 短名: type_id; 长名: 注册表索引
    };

    struct def_entry
    {
        type_def def;                // 存储语义 (绑定条目无效)
        char* name_data;             // 全名字节堆副本 (进程存续期有效)
        uint64_t key;                // 同槽 key (短名即全串编码)
        int32_t id;                  // 分配的类型 id
        uint32_t length;             // 全名长度
        bool name_binding;           // true: 名字→既有 id 绑定 (无 def 语义, 模板类型稳定名/别名)
    };

    inline static def_slot def_table_[def_table_size_]{};
    inline static def_entry def_registry_[def_registry_cap_]{};
    inline static std::atomic<uint32_t> def_registry_size_{0};
    inline static std::atomic_flag def_lock_{};

    // 槽位散布: 尾部块折入 + 乘法混位, 仅决定分布, 不参与身份判定
    // 长名若只按前 8 字节散布, 同前缀家族会挤入同一槽形成长链
    [[nodiscard]] static FORCE_INLINE
    size_t def_slot_index_(const char* p, size_t n, uint64_t key) noexcept
    {
        uint64_t fold = key;
        for (size_t i = 8; i < n; i += 8)
        {
            uint64_t chunk = 0;
            const size_t m = (n - i < 8) ? n - i : 8;
            std::memcpy(&chunk, p + i, m);
            fold ^= chunk;
        }
        return static_cast<size_t>((fold * 0x9E3779B97F4A7C15ULL)
            ^ (uint64_t(n) * 0xFF51AFD7ED558CCDULL)) >> 52;
    }

    // 名字编码: 短名全串 (<=8B), 长名取前 8 字节
    [[nodiscard]] static FORCE_INLINE
    uint64_t def_key_of_(const char* p, size_t n) noexcept
    {
        if (n > 8)
        {
            n = 8;
        }
        uint64_t v = 0;
        std::memcpy(&v, p, n);
        return v;
    }

    [[nodiscard]] static
    bool def_equal_(const type_def& a, const type_def& b) noexcept
    {
        return a.size == b.size && a.alignment == b.alignment
            && a.trivially_copyable == b.trivially_copyable
            && a.construct == b.construct && a.destruct == b.destruct;
    }

public:
    type_id() noexcept = default;

    template<typename T>
    [[nodiscard]] static FORCE_INLINE
    int get_type_id() noexcept
    {
        static int id = type_id_allocator.get_id();
        return id;
    }

    [[nodiscard]] static FORCE_INLINE
    int current_max_id() noexcept
    {
        return type_id_allocator.maximum_id();
    }

    // type id → multi_block_bitmask 布局 (位块/块内偏移), 全库唯一出处
    //   component_meta / group / runtime_query 的掩码构建共用
    [[nodiscard]] static constexpr uint32_t mask_block_of(int tid) noexcept
    {
        return static_cast<uint32_t>(tid - 1) / 64;
    }

    [[nodiscard]] static constexpr uint32_t mask_offset_of(int tid) noexcept
    {
        return static_cast<uint32_t>(tid - 1) % 64;
    }

    // 掩码位 → type id (mask_block_of/mask_offset_of 的逆映射)
    [[nodiscard]] static constexpr int mask_type_of(uint32_t block, uint32_t offset) noexcept
    {
        return static_cast<int>(block) * 64 + static_cast<int>(offset) + 1;
    }

    // 槽探测 (写侧, 调用方须持有 def_lock_):
    //   返回槽索引; found=true 表示名字已存在 (id 经 out_id 返回)
    //   短名槽内直存 id; 长名经注册表条目 memcmp 精确比对
    [[nodiscard]] static size_t def_probe_(const char* p, size_t n, uint64_t key,
                                            bool& found, int& out_id) noexcept
    {
        size_t slot = def_slot_index_(p, n, key) & (def_table_size_ - 1);
        for (size_t probe = 0; probe < def_table_size_; ++probe)
        {
            const def_slot& s = def_table_[slot];
            const uint32_t len = s.length;
            if (len == 0)
            {
                found = false;
                out_id = -1;
                return slot;
            }
            if (s.key.load(std::memory_order_relaxed) == key && len == n)
            {
                if (n <= 8)
                {
                    found = true;
                    out_id = s.payload;
                    return slot;
                }
                const def_entry& e = def_registry_[static_cast<uint32_t>(s.payload)];
                if (e.length == n && std::memcmp(e.name_data, p, n) == 0)
                {
                    found = true;
                    out_id = e.id;
                    return slot;
                }
            }
            slot = (slot + 1) & (def_table_size_ - 1);
        }
        found = false;
        out_id = -1;
        return def_table_size_;  // 表满
    }

    // 条目发布 (写侧, 调用方须持有 def_lock_ 且名字未注册):
    //   def 为 nullptr 时创建名字绑定条目 (无存储语义)
    //   发布序: payload/length 先于 key(release), 读侧 acquire 依赖此序
    [[nodiscard]] static bool def_publish_(size_t insert_pos, const char* p, size_t n,
                                            uint64_t key, int id, const type_def* def) noexcept
    {
        const uint32_t reg_size = def_registry_size_.load(std::memory_order_relaxed);
        if (insert_pos >= def_table_size_ || reg_size >= def_registry_cap_) [[unlikely]]
        {
            return false;
        }
        char* name_copy = static_cast<char*>(::operator new(n, std::nothrow));
        if (name_copy == nullptr) [[unlikely]]
        {
            return false;
        }
        std::memcpy(name_copy, p, n);

        def_entry& e = def_registry_[reg_size];
        e.def = def ? *def : type_def{};
        e.name_data = name_copy;
        e.key = key;
        e.id = id;
        e.length = static_cast<uint32_t>(n);
        e.name_binding = (def == nullptr);

        def_slot& s = def_table_[insert_pos];
        s.payload = (n <= 8) ? id : static_cast<int32_t>(reg_size);
        s.length = static_cast<uint32_t>(n);
        s.key.store(key, std::memory_order_release);

        def_registry_size_.store(reg_size + 1, std::memory_order_release);
        return true;
    }

    // 按名字注册自定义类型: 幂等 (同名返回既有 id, 语义须一致), 非法参数/表满返回 -1
    // 冷路径; 与 get_type_id<T>() 共用 id 分配器, 两条轨道 id 天然互斥
    static int register_type_def(std::string_view name, const type_def& def) noexcept
    {
        if (def.size == 0 || def.alignment == 0
            || (def.alignment & (def.alignment - 1)) != 0) [[unlikely]]
        {
            return -1;
        }
        if (!def.trivially_copyable && (!def.construct || !def.destruct)) [[unlikely]]
        {
            return -1;
        }
        const size_t n = name.size();
        if (n == 0 || n > 0xFFFFFFFFu) [[unlikely]]
        {
            return -1;
        }
        const char* p = name.data();
        const uint64_t key = def_key_of_(p, n);
        if (key == 0) [[unlikely]]
        {
            return -1;  // 前 8 字节全 NUL, 与空槽初值混淆, 拒绝
        }

        while (def_lock_.test_and_set(std::memory_order_acquire)) {}

        const uint32_t reg_size = def_registry_size_.load(std::memory_order_relaxed);
        bool found = false;
        int existing = -1;
        const size_t slot = def_probe_(p, n, key, found, existing);
        int result = -1;
        if (found)
        {
            result = existing;
            // 语义一致性断言 (定位 def 条目; 绑定条目跳过)
            for (uint32_t j = 0; j < reg_size; ++j)
            {
                const def_entry& e = def_registry_[j];
                if (!e.name_binding && e.key == key && e.length == n)
                {
                    assert(def_equal_(e.def, def)
                        && "register_type_def: 同名重复注册语义不一致");
                    break;
                }
            }
        }
        else
        {
            const int id = type_id_allocator.get_id();
            if (def_publish_(slot, p, n, key, id, &def))
            {
                result = id;
            }
            else
            {
                type_id_allocator.free_id(id);
            }
        }

        def_lock_.clear(std::memory_order_release);
        return result;
    }

    // 名字 → 既有类型 id 绑定 (模板类型的稳定名/别名, 无 def 存储语义)
    //   幂等: 同名同 id 返回 true; 同名异 id 冲突返回 false; 表满/内存失败返回 false
    //   绑定后 get_def_type_id(name) 返回该 id, 序列化存档名反查经此统一入口
    static bool bind_def_name(std::string_view name, int id) noexcept
    {
        const size_t n = name.size();
        if (n == 0 || n > 0xFFFFFFFFu || id <= 0) [[unlikely]]
        {
            return false;
        }
        const char* p = name.data();
        const uint64_t key = def_key_of_(p, n);
        if (key == 0) [[unlikely]]
        {
            return false;
        }

        while (def_lock_.test_and_set(std::memory_order_acquire)) {}

        bool found = false;
        int existing = -1;
        const size_t slot = def_probe_(p, n, key, found, existing);
        bool ok;
        if (found)
        {
            ok = (existing == id);  // 幂等或冲突
        }
        else
        {
            ok = def_publish_(slot, p, n, key, id, nullptr);
        }

        def_lock_.clear(std::memory_order_release);
        return ok;
    }

    // 按名查询自定义类型 id (纯查询, 未注册返回 -1); 读侧无锁
    [[nodiscard]] static int get_def_type_id(std::string_view name) noexcept
    {
        const size_t n = name.size();
        if (n == 0 || n > 0xFFFFFFFFu) [[unlikely]]
        {
            return -1;
        }
        const char* p = name.data();
        const uint64_t key = def_key_of_(p, n);
        if (key == 0) [[unlikely]]
        {
            return -1;
        }
        size_t slot = def_slot_index_(p, n, key) & (def_table_size_ - 1);
        for (size_t probe = 0; probe < def_table_size_; ++probe)
        {
            const def_slot& s = def_table_[slot];
            const uint64_t k = s.key.load(std::memory_order_acquire);  // 先 key, 序依赖
            const uint32_t len = s.length;
            if (len == 0) [[unlikely]]
            {
                return -1;
            }
            if (k == key && len == n)
            {
                if (n <= 8)
                {
                    return s.payload;  // 短名单射: key+length 即身份
                }
                const def_entry& e = def_registry_[static_cast<uint32_t>(s.payload)];
                if (e.length == n && std::memcmp(e.name_data, p, n) == 0)
                {
                    return e.id;
                }
            }
            slot = (slot + 1) & (def_table_size_ - 1);
        }
        return -1;
    }

    // 按 id 反查存储语义 (非注册 id / 纯绑定条目返回 nullptr); 线性扫描, 冷路径
    [[nodiscard]] static const type_def* get_type_def(int def_id) noexcept
    {
        if (def_id <= 0)
        {
            return nullptr;
        }
        const uint32_t cnt = def_registry_size_.load(std::memory_order_acquire);
        for (uint32_t j = 0; j < cnt; ++j)
        {
            if (!def_registry_[j].name_binding && def_registry_[j].id == def_id)
            {
                return &def_registry_[j].def;
            }
        }
        return nullptr;
    }

    // 按 id 反查名字 (非注册 id 返回空串)
    [[nodiscard]] static std::string_view get_def_type_name(int def_id) noexcept
    {
        if (def_id <= 0)
        {
            return {};
        }
        const uint32_t cnt = def_registry_size_.load(std::memory_order_acquire);
        for (uint32_t j = 0; j < cnt; ++j)
        {
            const def_entry& e = def_registry_[j];
            if (e.id == def_id)
            {
                return std::string_view(e.name_data, e.length);
            }
        }
        return {};
    }
};
