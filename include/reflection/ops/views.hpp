#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "config/reflect_config.hpp"
#include "part/dense.hpp"
#include "part/fnv1a.hpp"
#include "part/force_inline.hpp"
#include "part/operating_message.hpp"
#include "part/type/type_id.hpp"
#include "part/type/void_any.hpp"
#include "reflection/core/query.hpp"
#include "reflection/base/type_ops.hpp"

// views.hpp - 比较 / 构造 / 容器 / 转换 / 哈希 / 继承 / 虚派发 / 属性
// 命名空间: reflect

// ============================================================================
// attributes.hpp - 字段属性/注解查询接口 (#4)
// 命名空间: reflect
// 基于 field_meta::attrs 的 dense<attr_entry> 实现


namespace reflect {

class attribute_view
{
    const type_meta* meta_{nullptr};

public:
    attribute_view() noexcept = default;
    explicit attribute_view(const type_meta* m) noexcept : meta_(m) {}

    // 检查字段是否有指定属性
    [[nodiscard]] bool has_attr(size_t field_idx, const char* key) const noexcept
    {
        if (!meta_ || field_idx >= meta_->field_count.load(std::memory_order_acquire))
        {
            return false;
        }
        uint64_t kh = fnv1a_runtime(key);
        const auto& attrs = meta_->fields[field_idx].attrs;
        size_t n = attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (attrs[i].key_hash == kh)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool has_attr(const char* field_name, const char* key) const noexcept
    {
        const field_meta* f = meta_ ? field_by_name_impl(field_name) : nullptr;
        if (!f)
        {
            return false;
        }
        uint64_t kh = fnv1a_runtime(key);
        size_t n = f->attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (f->attrs[i].key_hash == kh)
            {
                return true;
            }
        }
        return false;
    }

    // 获取属性值 (void_any 指针, 失败返回 nullptr)
    [[nodiscard]] const void_any* get_attr(size_t field_idx, const char* key) const noexcept
    {
        if (!meta_ || field_idx >= meta_->field_count.load(std::memory_order_acquire))
        {
            return nullptr;
        }
        uint64_t kh = fnv1a_runtime(key);
        const auto& attrs = meta_->fields[field_idx].attrs;
        size_t n = attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (attrs[i].key_hash == kh)
            {
                return &attrs[i].value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const void_any* get_attr(const char* field_name, const char* key) const noexcept
    {
        const field_meta* f = meta_ ? field_by_name_impl(field_name) : nullptr;
        if (!f)
        {
            return nullptr;
        }
        uint64_t kh = fnv1a_runtime(key);
        size_t n = f->attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (f->attrs[i].key_hash == kh)
            {
                return &f->attrs[i].value;
            }
        }
        return nullptr;
    }

    // 类型安全获取
    template<typename V>
    [[nodiscard]] const V* get_attr_as(size_t field_idx, const char* key) const noexcept
    {
        const void_any* any = get_attr(field_idx, key);
        return any ? any->get_ptr<V>() : nullptr;
    }

    template<typename V>
    [[nodiscard]] const V* get_attr_as(const char* field_name, const char* key) const noexcept
    {
        const void_any* any = get_attr(field_name, key);
        return any ? any->get_ptr<V>() : nullptr;
    }

    // 遍历字段所有属性
    template<typename F>
    void for_each_attr(size_t field_idx, F&& f) const noexcept
    {
        if (!meta_ || field_idx >= meta_->field_count.load(std::memory_order_acquire))
        {
            return;
        }
        const auto& attrs = meta_->fields[field_idx].attrs;
        size_t n = attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            f(attrs[i].key_hash, attrs[i].value);
        }
    }

private:
    [[nodiscard]] const field_meta* field_by_name_impl(const char* name) const noexcept
    {
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            if (std::strcmp(meta_->fields[i].name, name) == 0)
            {
                return &meta_->fields[i];
            }
        }
        return nullptr;
    }
};

template<typename T>
[[nodiscard]] attribute_view get_attributes() noexcept
{
    return attribute_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline attribute_view get_attributes(const query_view& q) noexcept
{
    return attribute_view(q.meta());
}

} // namespace reflect

// ============================================================================
// ============================================================================
// compare.hpp - 字段比较/克隆查询接口 (#8)
// 命名空间: reflect
// 复用 meta.hpp 的单份字段遍历骨架, 逐字段/逐元素比较与拷贝


namespace reflect {

class compare_view
{
    const type_meta* meta_{nullptr};

public:
    compare_view() noexcept = default;
    explicit compare_view(const type_meta* m) noexcept : meta_(m) {}

    // 逐字段相等比较 (数组字段逐元素比较)
    [[nodiscard]] bool equal(const void* a, const void* b) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        if (a == b)
        {
            return true;
        }
        if (!a || !b)
        {
            return false;
        }
        return detail::visit_field_slots(a, b, *meta_,
            [](const detail::field_slot& s, const field_meta&) noexcept {
                if (s.ops && s.ops->equal_fn)
                {
                    for (uint32_t e = 0; e < s.count; ++e)
                    {
                        if (!s.ops->equal_fn(s.base + e * s.stride, s.other + e * s.stride))
                        {
                            return false;
                        }
                    }
                    return true;
                }
                if (!s.trivially_copyable)
                {
                    detail::abort_with_location("equal: field type ops missing for non-trivial field");
                }
                return std::memcmp(s.base, s.other,
                                   static_cast<size_t>(s.count) * s.stride) == 0;
            });
    }

    // 逐字段克隆 (copy_construct, dst 必须是未初始化内存)
    void clone(const void* src, void* dst) const noexcept
    {
        if (!meta_ || !src || !dst)
        {
            return;
        }
        detail::visit_field_slots(dst, src, *meta_,
            [](const detail::field_slot& s, const field_meta&) noexcept {
                char* pd = const_cast<char*>(s.base);
                const char* ps = s.other;
                if (s.ops && s.ops->copy_construct_fn)
                {
                    for (uint32_t e = 0; e < s.count; ++e)
                    {
                        s.ops->copy_construct_fn(pd + e * s.stride, ps + e * s.stride);
                    }
                    return true;
                }
                if (!s.trivially_copyable)
                {
                    detail::abort_with_location("clone: field type ops missing for non-trivial field");
                }
                std::memcpy(pd, ps, static_cast<size_t>(s.count) * s.stride);
                return true;
            });
    }

    // 逐字段 copy_assign (dst 已构造)
    void copy_assign(const void* src, void* dst) const noexcept
    {
        if (!meta_ || !src || !dst)
        {
            return;
        }
        detail::visit_field_slots(dst, src, *meta_,
            [](const detail::field_slot& s, const field_meta&) noexcept {
                char* pd = const_cast<char*>(s.base);
                const char* ps = s.other;
                if (s.ops && s.ops->copy_assign_fn)
                {
                    for (uint32_t e = 0; e < s.count; ++e)
                    {
                        s.ops->copy_assign_fn(pd + e * s.stride, ps + e * s.stride);
                    }
                    return true;
                }
                if (!s.trivially_copyable)
                {
                    detail::abort_with_location("copy_assign: field type ops missing for non-trivial field");
                }
                std::memcpy(pd, ps, static_cast<size_t>(s.count) * s.stride);
                return true;
            });
    }

    // 逐字段析构 (对象销毁前调用)
    void destroy_fields(void* obj) const noexcept
    {
        if (!meta_ || !obj)
        {
            return;
        }
        detail::visit_field_slots(obj, nullptr, *meta_,
            [](const detail::field_slot& s, const field_meta&) noexcept {
                if (!s.ops || !s.ops->destroy_fn)
                {
                    return true;
                }
                char* p = const_cast<char*>(s.base);
                for (uint32_t e = 0; e < s.count; ++e)
                {
                    s.ops->destroy_fn(p + e * s.stride);
                }
                return true;
            });
    }
};

template<typename T>
[[nodiscard]] compare_view get_compare() noexcept
{
    return compare_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline compare_view get_compare(const query_view& q) noexcept
{
    return compare_view(q.meta());
}

} // namespace reflect

// ============================================================================
// ============================================================================
// construct.hpp - 对象构造/销毁查询接口 (#1)
// 命名空间: reflect
// 通过 type_meta 的 default_construct_/destruct_ 函数指针实现反射构造


namespace reflect {

class construct_view
{
    const type_meta* meta_{nullptr};

public:
    construct_view() noexcept = default;
    explicit construct_view(const type_meta* m) noexcept : meta_(m) {}

    [[nodiscard]] bool has_default_construct() const noexcept
    {
        return meta_ && meta_->has_default_construct;
    }

    // 堆分配构造 (失败返回 nullptr)
    [[nodiscard]] void* create() const noexcept
    {
        if (!meta_ || !meta_->default_construct_)
        {
            return nullptr;
        }
        return meta_->default_construct_(nullptr);
    }

    // 就地构造 (buf 必须足够大且对齐)
    [[nodiscard]] void* create_inplace(void* buf) const noexcept
    {
        if (!meta_ || !meta_->default_construct_)
        {
            return nullptr;
        }
        return meta_->default_construct_(buf);
    }

    // 析构 (不释放内存, 用于就地对象的析构)
    void destroy(void* obj) const noexcept
    {
        if (!meta_ || !meta_->destruct_)
        {
            return;
        }
        meta_->destruct_(obj);
    }

    // 堆分配 + 析构 + 释放 (便利接口)
    void destroy_heap(void* obj) const noexcept
    {
        if (!obj)
        {
            return;
        }
        if (meta_ && meta_->free_heap_)
        {
            meta_->free_heap_(obj);
            return;
        }
        if (meta_ && meta_->destruct_)
        {
            meta_->destruct_(obj);
        }
        ::operator delete(obj);
    }
};

// 便捷工厂函数
template<typename T>
[[nodiscard]] construct_view get_construct() noexcept
{
    return construct_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline construct_view get_construct(const type_meta* m) noexcept
{
    return construct_view(m);
}

[[nodiscard]] inline construct_view get_construct(const query_view& q) noexcept
{
    return construct_view(q.meta());
}

} // namespace reflect

// ============================================================================
// ============================================================================
// container.hpp - 容器反射查询接口 (#5)
// 命名空间: reflect
// 基于 container_ops.hpp 的 container_ops_table 实现容器操作


namespace reflect {

class container_view
{
    void* cont_{nullptr};
    const container_ops_table* ops_{nullptr};

public:
    container_view() noexcept = default;
    container_view(void* cont, const container_ops_table* ops) noexcept
        : cont_(cont), ops_(ops) {}

    [[nodiscard]] bool valid() const noexcept { return cont_ && ops_; }
    [[nodiscard]] bool is_container() const noexcept { return ops_ != nullptr; }

    [[nodiscard]] container_category category() const noexcept
    {
        return ops_ ? ops_->category : container_category::none;
    }

    [[nodiscard]] int element_type_id() const noexcept
    {
        return ops_ ? ops_->element_type_id : -1;
    }

    [[nodiscard]] size_t size() const noexcept
    {
        if (!valid() || !ops_->size_fn)
        {
            return 0;
        }
        return ops_->size_fn(cont_);
    }

    // 越界保护访问: index 越界或容器无效时返回 nullptr
    [[nodiscard]] void* get(size_t index) const noexcept
    {
        if (!valid() || !ops_->get_index_fn)
        {
            return nullptr;
        }
        return ops_->get_index_fn(cont_, index);
    }

    template<typename T>
    [[nodiscard]] T* get_as(size_t index) const noexcept
    {
        void* p = get(index);
        return p ? static_cast<T*>(p) : nullptr;
    }

    void push_back(const void* element) const noexcept
    {
        if (!valid() || !ops_->push_back_fn)
        {
            return;
        }
        ops_->push_back_fn(cont_, element);
    }

    template<typename T>
    void push_back(const T& value) const noexcept
    {
        push_back(static_cast<const void*>(&value));
    }

    void clear() const noexcept
    {
        if (!valid() || !ops_->clear_fn)
        {
            return;
        }
        ops_->clear_fn(cont_);
    }

    void reserve(size_t n) const noexcept
    {
        if (!valid() || !ops_->reserve_fn)
        {
            return;
        }
        ops_->reserve_fn(cont_, n);
    }

    // 遍历元素 (回调签名: void(void* element, size_t index))
    template<typename F>
    void for_each(F&& f) const noexcept
    {
        if (!valid() || !ops_->get_index_fn)
        {
            return;
        }
        size_t n = size();
        for (size_t i = 0; i < n; ++i)
        {
            void* elem = ops_->get_index_fn(cont_, i);
            if (elem) f(elem, i);
        }
    }
};

// 从对象指针构造容器视图
[[nodiscard]] inline container_view as_container(void* obj, int type_id) noexcept
{
    const container_ops_table* ops = detail::global_ops().get_container(type_id);
    return container_view(obj, ops);
}

// 从 query_view 构造容器视图 (用于字段类型为容器)
[[nodiscard]] inline container_view as_container(const query_view& q, void* obj) noexcept
{
    // 此处 q 是字段类型的 query_view, obj 是字段值指针
    // 但容器的 ops 存在 type_meta::container_ops 中
    const type_meta* m = q.meta();
    if (!m)
    {
        return container_view{};
    }
    return container_view(obj, m->container_ops);
}

} // namespace reflect

// ============================================================================
// ============================================================================
// convert.hpp - 类型转换查询接口 (#7)
// 命名空间: reflect
// 基于 type_meta 的 converters 列表实现类型转换


namespace reflect {

class convert_view
{
    const type_meta* meta_{nullptr};

public:
    convert_view() noexcept = default;
    explicit convert_view(const type_meta* m) noexcept : meta_(m) {}

    // 检查是否可转换到目标类型
    [[nodiscard]] bool can_convert_to(int target_type_id) const noexcept
    {
        return find_converter(target_type_id) != nullptr;
    }

    template<typename U>
    [[nodiscard]] bool can_convert_to() const noexcept
    {
        return can_convert_to(type_id::get_type_id<U>());
    }

    // 转换到目标类型 (失败返回 false)
    [[nodiscard]] bool convert_to(const void* src, int target_type_id, void* dst) const noexcept
    {
        if (!src || !dst)
        {
            return false;
        }
        const convert_entry* e = find_converter(target_type_id);
        if (e == nullptr)
        {
            return false;
        }
        e->convert_fn(src, dst);
        return true;
    }

    // 类型安全的转换 (返回 optional)
    template<typename U>
    [[nodiscard]] std::optional<U> convert_to(const void* src) const noexcept
    {
        if (!src)
        {
            return std::nullopt;
        }
        const convert_entry* e = find_converter(type_id::get_type_id<U>());
        if (e == nullptr)
        {
            return std::nullopt;
        }
        U result{};
        e->convert_fn(src, &result);
        return result;
    }

private:
    // 单份转换目标查找
    [[nodiscard]] const convert_entry* find_converter(int target_type_id) const noexcept
    {
        if (!meta_)
        {
            return nullptr;
        }
        size_t n = meta_->converters.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (meta_->converters[i].target_type_id == target_type_id)
            {
                return &meta_->converters[i];
            }
        }
        return nullptr;
    }
};

template<typename T>
[[nodiscard]] convert_view get_convert() noexcept
{
    return convert_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline convert_view get_convert(const query_view& q) noexcept
{
    return convert_view(q.meta());
}

} // namespace reflect

// ============================================================================
// ============================================================================
// hash.hpp - 字段哈希查询接口 (#9)
// 命名空间: reflect
// 复用 meta.hpp 的单份字段遍历骨架, 逐字段/逐元素组合 FNV-1a 哈希


namespace reflect {

class hash_view
{
    const type_meta* meta_{nullptr};

public:
    hash_view() noexcept = default;
    explicit hash_view(const type_meta* m) noexcept : meta_(m) {}

    // 逐字段哈希并组合 (FNV-1a 组合)
    [[nodiscard]] uint64_t hash(const void* obj) const noexcept
    {
        if (!meta_ || !obj)
        {
            return 0;
        }
        uint64_t h = FNV_OFFSET_BASIS_64;
        detail::visit_field_slots(obj, nullptr, *meta_,
            [&h](const detail::field_slot& s, const field_meta&) noexcept {
                for (uint32_t e = 0; e < s.count; ++e)
                {
                    const char* p = s.base + e * s.stride;
                    if (s.ops && s.ops->hash_fn)
                    {
                        h = fnv1a_mix(h, s.ops->hash_fn(p));
                    }
                    else
                    {
                        h = fnv1a_mix(h, p, s.stride);
                    }
                }
                return true;
            });
        return h;
    }
};

template<typename T>
[[nodiscard]] hash_view get_hash() noexcept
{
    return hash_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline hash_view get_hash(const query_view& q) noexcept
{
    return hash_view(q.meta());
}

} // namespace reflect

// ============================================================================
// ============================================================================
// inheritance.hpp - 继承关系查询接口 (#2)
// 命名空间: reflect
// 基于 type_meta 的 base_offsets/derived_type_ids 实现向上/向下转型


namespace reflect {

class inheritance_view
{
    const type_meta* meta_{nullptr};
    const storage* store_{nullptr};

    [[nodiscard]] const type_meta* resolve(int tid) const noexcept
    {
        return store_ != nullptr ? store_->get_type(tid) : global().get_type(tid);
    }

public:
    inheritance_view() noexcept = default;
    explicit inheritance_view(const type_meta* m) noexcept : meta_(m) {}
    inheritance_view(const type_meta* m, const storage* s) noexcept : meta_(m), store_(s) {}

    // 检查是否继承自 base_type_id (含间接继承)
    [[nodiscard]] bool is_derived_from(int base_type_id) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        if (meta_->type_id == base_type_id)
        {
            return true;
        }

        // 直接基类检查
        size_t n = meta_->base_offsets.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (meta_->base_offsets[i].base_type_id == base_type_id)
            {
                return true;
            }
        }

        // 递归检查间接基类
        for (size_t i = 0; i < n; ++i)
        {
            const type_meta* base = resolve(meta_->base_offsets[i].base_type_id);
            if (base && inheritance_view(base, store_).is_derived_from(base_type_id))
            {
                return true;
            }
        }
        return false;
    }

    template<typename Base>
    [[nodiscard]] bool is_derived_from() const noexcept
    {
        return is_derived_from(type_id::get_type_id<Base>());
    }

    // 检查是否是 base_type_id 的基类
    [[nodiscard]] bool is_base_of(int derived_type_id) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        const type_meta* derived = resolve(derived_type_id);
        if (!derived)
        {
            return false;
        }
        return inheritance_view(derived, store_).is_derived_from(meta_->type_id);
    }

    template<typename Derived>
    [[nodiscard]] bool is_base_of() const noexcept
    {
        return is_base_of(type_id::get_type_id<Derived>());
    }

    // 向上转型 (派生类指针 → 基类指针)
    [[nodiscard]] void* upcast(void* derived_obj, int base_type_id) const noexcept
    {
        if (!meta_ || !derived_obj)
        {
            return nullptr;
        }
        if (meta_->type_id == base_type_id)
        {
            return derived_obj;
        }

        size_t n = meta_->base_offsets.size();
        for (size_t i = 0; i < n; ++i)
        {
            const auto& be = meta_->base_offsets[i];
            if (be.base_type_id == base_type_id)
            {
                if (be.adjust_fn != nullptr)
                {
                    return be.adjust_fn(derived_obj);
                }
                return static_cast<char*>(derived_obj) + be.offset;
            }
            // 递归向上转型
            const type_meta* base = resolve(be.base_type_id);
            if (base)
            {
                void* intermediate = be.adjust_fn != nullptr
                    ? be.adjust_fn(derived_obj)
                    : static_cast<char*>(derived_obj) + be.offset;
                void* result = inheritance_view(base, store_).upcast(intermediate, base_type_id);
                if (result)
                {
                    return result;
                }
            }
        }
        return nullptr;
    }

    template<typename Base>
    [[nodiscard]] Base* upcast(void* derived_obj) const noexcept
    {
        return static_cast<Base*>(upcast(derived_obj, type_id::get_type_id<Base>()));
    }

    // 直接基类数量
    [[nodiscard]] size_t base_count() const noexcept
    {
        return meta_ ? meta_->base_offsets.size() : 0;
    }

    // 直接派生类数量
    [[nodiscard]] size_t derived_count() const noexcept
    {
        return meta_ ? meta_->derived_type_ids.size() : 0;
    }

    // 获取直接基类 type_id
    [[nodiscard]] int base_type_id_at(size_t idx) const noexcept
    {
        if (!meta_ || idx >= meta_->base_offsets.size())
        {
            return -1;
        }
        return meta_->base_offsets[idx].base_type_id;
    }

    // 获取直接派生类 type_id
    [[nodiscard]] int derived_type_id_at(size_t idx) const noexcept
    {
        if (!meta_ || idx >= meta_->derived_type_ids.size())
        {
            return -1;
        }
        return meta_->derived_type_ids[idx];
    }

    // 遍历所有直接基类
    template<typename F>
    void for_each_base(F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->base_offsets.size();
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->base_offsets[i].base_type_id, meta_->base_offsets[i].offset);
        }
    }

    // 遍历所有直接派生类
    template<typename F>
    void for_each_derived(F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->derived_type_ids.size();
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->derived_type_ids[i]);
        }
    }
};

template<typename T>
[[nodiscard]] inheritance_view get_inheritance() noexcept
{
    return inheritance_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline inheritance_view get_inheritance(const query_view& q) noexcept
{
    return inheritance_view(q.meta());
}

// 绑定具体存储实例: 基类解析走该实例, 不碰全局
[[nodiscard]] inline inheritance_view get_inheritance(const query_view& q, const storage& s) noexcept
{
    return inheritance_view(q.meta(), &s);
}

} // namespace reflect

// ============================================================================
// ============================================================================
// virtual_dispatch.hpp - 动态派发查询接口 (#6)
// 命名空间: reflect
// 基于 method_meta::vtable_offset 实现虚函数调用
// 注意: vtable 布局依赖 ABI (Itanium/MSVC), 单继承下指针无调整


namespace reflect {

class virtual_dispatch_view
{
    const type_meta* meta_{nullptr};

public:
    virtual_dispatch_view() noexcept = default;
    explicit virtual_dispatch_view(const type_meta* m) noexcept : meta_(m) {}

    // 检查方法是否为虚函数
    [[nodiscard]] bool is_virtual(const char* method_name) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        size_t n = meta_->method_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const method_meta& mm = meta_->methods[i];
            if (std::strcmp(mm.name, method_name) == 0)
            {
                return mm.vtable_offset >= 0;
            }
        }
        return false;
    }

    // 获取虚函数 vtable 偏移 (非虚返回 -1)
    [[nodiscard]] int vtable_offset(const char* method_name) const noexcept
    {
        if (!meta_)
        {
            return -1;
        }
        size_t n = meta_->method_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const method_meta& mm = meta_->methods[i];
            if (std::strcmp(mm.name, method_name) == 0)
            {
                return mm.vtable_offset;
            }
        }
        return -1;
    }

};

template<typename T>
[[nodiscard]] virtual_dispatch_view get_virtual_dispatch() noexcept
{
    return virtual_dispatch_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline virtual_dispatch_view get_virtual_dispatch(const query_view& q) noexcept
{
    return virtual_dispatch_view(q.meta());
}

} // namespace reflect
