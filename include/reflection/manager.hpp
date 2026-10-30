#pragma once

// manager.hpp - 反射门面: 增删改查一体
// 命名空间: reflect
// 状态全部在 storage, manager 只做接口聚合与错误映射
// 可失败接口返回 operating_message, 成功为空对象

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <tuple>
#include <utility>

#include "reflection/base/foundation.hpp"
#include "reflection/core/storage.hpp"
#include "reflection/core/query.hpp"
#include "reflection/ops/views.hpp"

namespace reflect {

// 字段遍历视图 (range-for)
class field_view
{
public:
    class iterator
    {
    public:
        iterator(const type_meta* meta, size_t index) noexcept
            : meta_(meta), index_(index) {}

        [[nodiscard]] const field_meta& operator*() const noexcept
        {
            return meta_->fields[index_];
        }

        [[nodiscard]] const field_meta* operator->() const noexcept
        {
            return &meta_->fields[index_];
        }

        iterator& operator++() noexcept
        {
            ++index_;
            return *this;
        }

        [[nodiscard]] bool operator!=(const iterator& other) const noexcept
        {
            return index_ != other.index_;
        }

        [[nodiscard]] bool operator==(const iterator& other) const noexcept
        {
            return index_ == other.index_;
        }

    private:
        const type_meta* meta_;
        size_t index_;
    };

    explicit field_view(const type_meta* meta = nullptr) noexcept : meta_(meta) {}

    [[nodiscard]] iterator begin() const noexcept { return iterator(meta_, 0); }
    [[nodiscard]] iterator end() const noexcept { return iterator(meta_, size()); }

    [[nodiscard]] size_t size() const noexcept
    {
        return meta_ ? meta_->field_count.load(std::memory_order_acquire) : 0;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] const field_meta& operator[](size_t i) const noexcept
    {
        return meta_->fields[i];
    }

private:
    const type_meta* meta_;
};

// 方法遍历视图 (range-for)
class method_view
{
public:
    class iterator
    {
    public:
        iterator(const type_meta* meta, size_t index) noexcept
            : meta_(meta), index_(index) {}

        [[nodiscard]] const method_meta& operator*() const noexcept
        {
            return meta_->methods[index_];
        }

        [[nodiscard]] const method_meta* operator->() const noexcept
        {
            return &meta_->methods[index_];
        }

        iterator& operator++() noexcept
        {
            ++index_;
            return *this;
        }

        [[nodiscard]] bool operator!=(const iterator& other) const noexcept
        {
            return index_ != other.index_;
        }

        [[nodiscard]] bool operator==(const iterator& other) const noexcept
        {
            return index_ == other.index_;
        }

    private:
        const type_meta* meta_;
        size_t index_;
    };

    explicit method_view(const type_meta* meta = nullptr) noexcept : meta_(meta) {}

    [[nodiscard]] iterator begin() const noexcept { return iterator(meta_, 0); }
    [[nodiscard]] iterator end() const noexcept { return iterator(meta_, size()); }

    [[nodiscard]] size_t size() const noexcept
    {
        return meta_ ? meta_->method_count.load(std::memory_order_acquire) : 0;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] const method_meta& operator[](size_t i) const noexcept
    {
        return meta_->methods[i];
    }

private:
    const type_meta* meta_;
};

class manager
{
public:
    manager() noexcept = default;
    manager(manager&&) noexcept = delete;
    manager(const manager&) = delete;
    manager& operator=(const manager&) = delete;
    manager& operator=(manager&&) noexcept = delete;
    ~manager() = default;

    // 底层存储: 序列化与 ops 只依赖这一层
    [[nodiscard]] storage& store() noexcept { return store_; }
    [[nodiscard]] const storage& store() const noexcept { return store_; }

    void freeze() noexcept { store_.freeze(); }
    void unfreeze() noexcept { store_.unfreeze(); }
    [[nodiscard]] bool frozen() const noexcept { return store_.frozen(); }

    // === 增: 类型 ===
    template<typename T>
    operating_message add_type() noexcept
    {
        return add_type_as<T>(detail::type_name_cstr<T>());
    }

    template<typename T>
    operating_message add_type_as(const char* name) noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        const type_meta* owner = store_.find_type(name);
        if (owner != nullptr && owner->type_id != type_id::get_type_id<T>())
        {
            return detail::failure(om_err_reflect_duplicate);
        }
        store_.register_type_only<T>(name);
        if (store_.get_type(type_id::get_type_id<T>()) == nullptr)
        {
            return detail::failure(om_err_out_of_memory);
        }
        return operating_message{};
    }

    // 注册类型并自动遍历聚合字段 (字段名退化为 field_N)
    template<typename T>
    operating_message add_aggregate_type() noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        const char* name = detail::type_name_cstr<T>();
        const type_meta* owner = store_.find_type(name);
        if (owner != nullptr && owner->type_id != type_id::get_type_id<T>())
        {
            return detail::failure(om_err_reflect_duplicate);
        }
        store_.register_type<T>(name);
        if (store_.get_type(type_id::get_type_id<T>()) == nullptr)
        {
            return detail::failure(om_err_out_of_memory);
        }
        return operating_message{};
    }

    // === 增: 字段 ===
    template<typename T, typename M>
    operating_message add_field(const char* name, M T::*ptr) noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        query_view q(store_.get_type(type_id::get_type_id<T>()));
        if (!q.valid())
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        if (q.field_by_name(name) != nullptr)
        {
            return detail::failure(om_err_reflect_duplicate);
        }
        store_.register_member_runtime<T, M>(q.name(), name, ptr);
        if (q.field_by_name(name) == nullptr)
        {
            return detail::failure(om_err_out_of_memory);
        }
        return operating_message{};
    }

    // 批量添加: 接受类内 reflect_fields() 返回的 {名字, 成员指针} 元组, 只保留首错
    template<typename T, typename Tuple>
    operating_message add_fields(Tuple&& fields) noexcept
    {
        operating_message result;
        std::apply([&](auto&&... pairs) noexcept
        {
            ((result += add_field<T>(pairs.first, pairs.second)), ...);
        }, std::forward<Tuple>(fields));
        return result;
    }

    // 单字段快捷式: 避免 std::tuple{pair} 把 pair 拆成两个元素的推导
    template<typename T, typename M>
    operating_message add_fields(std::pair<const char*, M T::*> field) noexcept
    {
        return add_field<T>(field.first, field.second);
    }

    // 手填偏移: 用于无法取成员指针的第三方类型
    template<typename T>
    operating_message add_field_raw(const char* name, size_t offset, int field_type_id,
                                    uint32_t size, bool trivially_copyable) noexcept
    {
        return store_.add_field_raw<T>(name, offset, field_type_id, size, trivially_copyable);
    }

    template<typename T>
    operating_message add_private_offsets(const offset_desc* descs, size_t count) noexcept
    {
        return store_.add_private_offsets<T>(descs, count);
    }

    // === 增: 方法 ===
    template<auto Fn>
    operating_message add_method(const char* name) noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        using traits = detail::mfn_traits<decltype(Fn)>;
        using C = typename traits::class_type;
        query_view q(store_.get_type(type_id::get_type_id<C>()));
        if (!q.valid())
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        store_.register_method<Fn>(name);
        return operating_message{};
    }

    // 运行期指针: 私有函数在类内取地址后传入, 不要求 constexpr
    template<typename Fn>
        requires std::is_member_function_pointer_v<Fn>
    operating_message add_method(const char* name, Fn ptr) noexcept
    {
        return store_.register_method_ptr(name, ptr);
    }

    // 批量: 接受类内返回的 {名字, 成员函数指针} 元组
    template<typename Tuple>
    operating_message add_methods(Tuple&& methods) noexcept
    {
        operating_message result;
        std::apply([&](auto&&... pairs) noexcept
        {
            ((result += store_.register_method_ptr(pairs.first, pairs.second)), ...);
        }, std::forward<Tuple>(methods));
        return result;
    }

    template<typename C, typename Tuple>
    operating_message add_static_methods(Tuple&& methods) noexcept
    {
        operating_message result;
        std::apply([&](auto&&... pairs) noexcept
        {
            ((result += store_.register_static_ptr<C>(pairs.first, pairs.second)), ...);
        }, std::forward<Tuple>(methods));
        return result;
    }

    // 一键注册授信成员: 类内 friend 声明 + 类外 reflect_access<T> 特化
    template<typename T>
    operating_message add_accessed_members() noexcept
    {
        operating_message result = writable();
        if (!result)
        {
            return result;
        }
        if constexpr (detail::has_access_fields<T>)
        {
            result += add_fields<T>(reflect_access<T>::fields());
        }
        if constexpr (detail::has_access_methods<T>)
        {
            result += add_methods(reflect_access<T>::methods());
        }
        if constexpr (detail::has_access_static_methods<T>)
        {
            result += add_static_methods<T>(reflect_access<T>::static_methods());
        }
        if constexpr (!detail::has_access_fields<T> && !detail::has_access_methods<T>
                      && !detail::has_access_static_methods<T>)
        {
            result += detail::failure(om_err_unsupported_operation);
        }
        return result;
    }

    template<typename C, typename Fn>
        requires std::is_function_v<std::remove_pointer_t<Fn>>
    operating_message add_static_method(const char* name, Fn ptr) noexcept
    {
        return store_.register_static_ptr<C>(name, ptr);
    }

    template<auto Fn>
    operating_message add_virtual_method(const char* name, int vtable_offset) noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        using traits = detail::mfn_traits<decltype(Fn)>;
        using C = typename traits::class_type;
        query_view q(store_.get_type(type_id::get_type_id<C>()));
        if (!q.valid())
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        store_.register_method<Fn>(name, vtable_offset);
        return operating_message{};
    }

    template<typename C, auto Fn>
    operating_message add_static_method(const char* name) noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        if (store_.get_type(type_id::get_type_id<C>()) == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        store_.register_static_method<C, Fn>(name);
        return operating_message{};
    }

    // === 增: 枚举 ===
    template<typename E>
    operating_message add_enum(const char* type_name,
                               std::initializer_list<std::pair<E, const char*>> items) noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (type_name == nullptr || type_name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        if (store_.find_enum<E>() != nullptr)
        {
            return detail::failure(om_err_reflect_duplicate);
        }
        store_.register_enum<E>(type_name, items);
        if (store_.find_enum<E>() == nullptr)
        {
            return detail::failure(om_err_out_of_memory);
        }
        return operating_message{};
    }

    // === 增: 继承 / 转换 / 属性 ===
    template<typename D, typename B>
    operating_message add_base() noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (!has<D>() || !has<B>())
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        store_.register_base_typed<D, B>();
        return operating_message{};
    }

    template<typename D, typename B>
    operating_message add_base_with_offset(ptrdiff_t offset) noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (!has<D>() || !has<B>())
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        store_.register_base<D, B>(offset);
        return operating_message{};
    }

    // 私有/虚拟继承: 调整函数由类内提供 (类外 static_cast 不可访问)
    template<typename D, typename B>
    operating_message add_base_with_adjuster(void* (*adjust)(void*) noexcept) noexcept
    {
        return store_.add_base_with_adjuster<D, B>(adjust);
    }

    // 私有构造/析构: create 与 destroy 由类内工厂提供
    template<typename T>
    operating_message set_construct_destruct(construct_func create, destruct_func destroy) noexcept
    {
        return store_.set_construct_destruct<T>(create, destroy);
    }

    template<typename From, typename To>
    operating_message add_convert() noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if (!has<From>())
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        store_.register_convert<From, To>();
        return operating_message{};
    }

    template<typename T, typename V>
    operating_message add_field_attr(const char* field, const char* key, V&& value) noexcept
    {
        return store_.set_field_attr<T>(field, key, std::forward<V>(value));
    }

    // === 删 ===
    template<typename T>
    operating_message retire_type() noexcept
    {
        return store_.retire_type<T>();
    }

    template<typename T>
    operating_message purge_type() noexcept
    {
        return store_.purge_type<T>();
    }

    template<typename T>
    operating_message remove_field(const char* name) noexcept
    {
        return store_.remove_field<T>(name);
    }

    template<auto Fn>
    operating_message remove_method(const char* name) noexcept
    {
        return store_.remove_method<Fn>(name);
    }

    template<typename Fn>
        requires std::is_member_function_pointer_v<Fn>
    operating_message remove_method(const char* name, Fn ptr) noexcept
    {
        return store_.remove_method_ptr(name, ptr);
    }

    template<typename C, auto Fn>
    operating_message remove_static_method(const char* name) noexcept
    {
        return store_.remove_static_method<C, Fn>(name);
    }

    template<typename E>
    operating_message remove_enum() noexcept
    {
        return store_.remove_enum<E>();
    }

    template<typename D, typename B>
    operating_message remove_base() noexcept
    {
        return store_.remove_base<D, B>();
    }

    // === 改 ===
    template<typename T>
    operating_message rename_type(const char* new_name) noexcept
    {
        return store_.rename_type<T>(new_name);
    }

    template<typename T>
    operating_message rename_field(const char* old_name, const char* new_name) noexcept
    {
        return store_.rename_field<T>(old_name, new_name);
    }

    template<typename T, typename M>
    operating_message rebind_field(const char* name, M T::*ptr) noexcept
    {
        operating_message gate = writable();
        if (!gate)
        {
            return gate;
        }
        if constexpr (detail::array_field_type<M>)
        {
            return detail::failure(om_err_unsupported_operation);
        }
        else
        {
            return store_.rebind_field_at<T, M>(name, detail::offset_of(ptr));
        }
    }

    template<auto Fn>
    operating_message rename_method(const char* old_name, const char* new_name) noexcept
    {
        return store_.rename_method<Fn>(old_name, new_name);
    }

    template<typename Fn>
        requires std::is_member_function_pointer_v<Fn>
    operating_message rename_method(const char* old_name, const char* new_name, Fn ptr) noexcept
    {
        return store_.rename_method_ptr(old_name, new_name, ptr);
    }

    template<typename T>
    operating_message set_field_flags(const char* name, uint8_t flags) noexcept
    {
        return store_.set_field_flags<T>(name, flags);
    }

    template<typename T, typename V>
    operating_message set_field_attr(const char* field, const char* key, V&& value) noexcept
    {
        return store_.set_field_attr<T>(field, key, std::forward<V>(value));
    }

    // === 查: 纯查询不产生错误 ===
    template<typename T>
    [[nodiscard]] query_view get() noexcept
    {
        return query_view(store_.get_type(type_id::get_type_id<T>()), &store_);
    }

    [[nodiscard]] query_view find(const char* type_name) noexcept
    {
        return query_view(store_.find_type(type_name), &store_);
    }

    template<typename T>
    [[nodiscard]] bool has() noexcept
    {
        return store_.get_type(type_id::get_type_id<T>()) != nullptr;
    }

    template<typename T>
    [[nodiscard]] const field_meta* field(const char* name) noexcept
    {
        return get<T>().field_by_name(name);
    }

    template<typename T>
    [[nodiscard]] const method_meta* method(const char* name) noexcept
    {
        return get<T>().method_by_name(name);
    }

    // 有返回值方法: 结果写入 out
    template<typename R, typename T, typename... Args>
    operating_message invoke(R& out, T& obj, const char* name, Args&&... args) noexcept
    {
        query_view q(store_.get_type(type_id::get_type_id<T>()));
        if (!q.valid())
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        auto value = q.template try_invoke<R>(&obj, name, std::forward<Args>(args)...);
        if (!value)
        {
            return detail::failure(om_err_reflect_method_not_found);
        }
        out = std::move(*value);
        return operating_message{};
    }

    // void 返回方法
    template<typename T, typename... Args>
    operating_message invoke_void(T& obj, const char* name, Args&&... args) noexcept
    {
        query_view q(store_.get_type(type_id::get_type_id<T>()));
        if (!q.valid())
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        if (!q.template try_invoke<void>(&obj, name, std::forward<Args>(args)...))
        {
            return detail::failure(om_err_reflect_method_not_found);
        }
        return operating_message{};
    }

    template<typename R = void, typename T, typename... Args>
    [[nodiscard]] auto try_invoke(T& obj, const char* name, Args&&... args) noexcept
    {
        query_view q(store_.get_type(type_id::get_type_id<T>()));
        if (!q.valid())
        {
            if constexpr (std::is_void_v<R>)
            {
                return false;
            }
            else
            {
                return std::optional<R>{};
            }
        }
        return q.template try_invoke<R>(&obj, name, std::forward<Args>(args)...);
    }

    template<typename T>
    [[nodiscard]] field_view get_fields() noexcept
    {
        return field_view(get<T>().meta());
    }

    template<typename T>
    [[nodiscard]] method_view get_methods() noexcept
    {
        return method_view(get<T>().meta());
    }

    // === 操作视图: 全部绑定本实例存储, 不走全局 ===
    template<typename T>
    [[nodiscard]] compare_view get_compare() noexcept
    {
        return reflect::get_compare(get<T>());
    }

    template<typename T>
    [[nodiscard]] hash_view get_hash() noexcept
    {
        return reflect::get_hash(get<T>());
    }

    template<typename T>
    [[nodiscard]] construct_view get_construct() noexcept
    {
        return reflect::get_construct(get<T>());
    }

    template<typename T>
    [[nodiscard]] convert_view get_convert() noexcept
    {
        return reflect::get_convert(get<T>());
    }

    template<typename T>
    [[nodiscard]] attribute_view get_attributes() noexcept
    {
        return reflect::get_attributes(get<T>());
    }

    template<typename T>
    [[nodiscard]] virtual_dispatch_view get_virtual_dispatch() noexcept
    {
        return reflect::get_virtual_dispatch(get<T>());
    }

    template<typename T>
    [[nodiscard]] inheritance_view get_inheritance() noexcept
    {
        return reflect::get_inheritance(get<T>(), store_);
    }

    template<typename T>
    [[nodiscard]] container_view get_container(void* obj) noexcept
    {
        return as_container(get<T>(), obj);
    }

    template<typename E>
    [[nodiscard]] enum_view get_enum() noexcept
    {
        return enum_view(store_.find_enum<E>());
    }

    [[nodiscard]] size_t type_count() const noexcept { return store_.type_count(); }

private:
    operating_message writable() noexcept
    {
        if (store_.frozen())
        {
            return detail::failure(om_err_reflect_frozen);
        }
        return operating_message{};
    }

    storage store_;
};

} // namespace reflect
