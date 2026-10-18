// model.hpp - 存档模型: 安全限制 / 存档头 / 实体重映射 / 元数据
#pragma once

#include "../part/dense.hpp"
#include "../part/safety.hpp"
#include "../ecs/entity.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace serialize {

// ECS 序列化安全限制 (扩展通用 safety_limits, 增加实体数量上限)
struct serialize_limits : safety_limits {
    size_t max_entity_count = 10 * 1000 * 1000;
};

enum class archive_format : uint8_t {
    json,
    binary,
    protobuf,
    flatbuffer
};

enum class load_mode : uint8_t {
    replace,
    append,
    merge
};

enum class load_policy : uint8_t {
    strict,
    best_effort
};

namespace detail {

// 存档头信息
struct archive_header {
    uint32_t archive_version = 1;
    uint32_t engine_version  = 0;
};

// 实体重映射表 (稀疏: 旧ID→新实体, 内存按实体数而非ID上限)
struct entity_remap {
    dense<uint8_t> slot_used;
    dense<uint32_t> slot_keys;      // 旧ID
    dense<ecs::entity> slot_ents;
    dense<uint32_t> slot_vers;
    dense<uint8_t> new_mark;        // 下标=新实体 index
    size_t count = 0;

    [[nodiscard]] size_t size() const noexcept { return count; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }

    static uint32_t mix(uint32_t x) noexcept {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    void ensure_capacity(size_t need) noexcept {
        if (need <= slot_keys.size()) { return; }
        size_t cap = 8;
        while (cap < need * 2) { cap <<= 1; }
        dense<uint8_t> nu;
        dense<uint32_t> nk;
        dense<ecs::entity> ne;
        dense<uint32_t> nv;
        nu.resize_fill(cap, 0);
        nk.resize_fill(cap, 0);
        ne.resize_fill(cap, ecs::entity{});
        nv.resize_fill(cap, 0);
        const size_t mask = cap - 1;
        for (size_t i = 0; i < slot_keys.size(); ++i) {
            if (slot_used[i] == 0) { continue; }
            size_t j = mix(slot_keys[i]) & mask;
            while (nu[j] != 0) { j = (j + 1) & mask; }
            nu[j] = 1;
            nk[j] = slot_keys[i];
            ne[j] = slot_ents[i];
            nv[j] = slot_vers[i];
        }
        slot_used = std::move(nu);
        slot_keys = std::move(nk);
        slot_ents = std::move(ne);
        slot_vers = std::move(nv);
    }

    void set(uint32_t old_id, ecs::entity e, uint32_t ver) noexcept {
        ensure_capacity(count + 1);
        const size_t mask = slot_keys.size() - 1;
        size_t j = mix(old_id) & mask;
        while (slot_used[j] != 0 && slot_keys[j] != old_id) {
            j = (j + 1) & mask;
        }
        if (slot_used[j] == 0) {
            slot_used[j] = 1;
            slot_keys[j] = old_id;
            ++count;
        }
        slot_ents[j] = e;
        slot_vers[j] = ver;
        const uint32_t nidx = e.parts_.index_;
        if (new_mark.size() <= nidx) { new_mark.resize_fill(static_cast<size_t>(nidx) + 1, 0); }
        new_mark[nidx] = 1;
    }

    [[nodiscard]] bool find(uint32_t old_id, ecs::entity& out) const noexcept {
        if (slot_keys.size() == 0) { return false; }
        const size_t mask = slot_keys.size() - 1;
        size_t j = mix(old_id) & mask;
        while (slot_used[j] != 0) {
            if (slot_keys[j] == old_id) { out = slot_ents[j]; return true; }
            j = (j + 1) & mask;
        }
        return false;
    }

    [[nodiscard]] uint32_t version(uint32_t old_id) const noexcept {
        if (slot_keys.size() == 0) { return 0; }
        const size_t mask = slot_keys.size() - 1;
        size_t j = mix(old_id) & mask;
        while (slot_used[j] != 0) {
            if (slot_keys[j] == old_id) { return slot_vers[j]; }
            j = (j + 1) & mask;
        }
        return 0;
    }

    [[nodiscard]] bool is_new_entity(uint32_t new_idx) const noexcept {
        return new_idx < new_mark.size() && new_mark[new_idx] != 0;
    }
};

// 元数据键值对
struct metadata_entry {
    std::string key;
    std::string value;
};

} // namespace detail
} // namespace serialize
