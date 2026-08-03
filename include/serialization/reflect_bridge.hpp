// reflect_bridge.hpp - 反射桥接: 自动序列化/反序列化 (支持嵌套对象/数组/枚举)
#pragma once

#include "../part/json_writer.hpp"
#include "../part/json_reader.hpp"
#include "../part/type_id.hpp"
#include "../reflection/meta.hpp"
#include "../reflection/query.hpp"
#include "../reflection/storage.hpp"
#include "type_name.hpp"
#include <concepts>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace serialize {

namespace reflect_bridge {

template<typename T>
concept has_json_serialize = requires(const T& v) {
    { v.to_json() } -> std::convertible_to<std::string>;
};

template<typename T>
concept has_json_deserialize = requires(T& v, std::string_view s) {
    v.from_json(s);
};

template<typename T>
[[nodiscard]] inline bool is_reflected() noexcept {
    return reflect::try_get<T>().valid();
}

[[nodiscard]] inline bool is_type_reflected(int tid) noexcept {
    return reflect::global().get_type(tid) != nullptr;
}

// 按底层整数类型写入
inline void write_as_int(json_writer& w, const void* p, int int_tid) noexcept {
    if (int_tid == type_id::get_type_id<int32_t>())
    {
        w.value(*static_cast<const int32_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<uint32_t>())
    {
        w.value(*static_cast<const uint32_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<int64_t>())
    {
        w.value(*static_cast<const int64_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<uint64_t>())
    {
        w.value(*static_cast<const uint64_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<int16_t>())
    {
        w.value(static_cast<int32_t>(*static_cast<const int16_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<uint16_t>())
    {
        w.value(static_cast<uint32_t>(*static_cast<const uint16_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<int8_t>())
    {
        w.value(static_cast<int32_t>(*static_cast<const int8_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<uint8_t>())
    {
        w.value(static_cast<uint32_t>(*static_cast<const uint8_t*>(p)));
    }
    else
    {
        w.null();
    }
}

inline void read_as_int(json_reader& r, void* p, int int_tid) noexcept {
    if (int_tid == type_id::get_type_id<int32_t>())
    {
        *static_cast<int32_t*>(p) = r.read_int32();
    }
    else if (int_tid == type_id::get_type_id<uint32_t>())
    {
        *static_cast<uint32_t*>(p) = r.read_uint32();
    }
    else if (int_tid == type_id::get_type_id<int64_t>())
    {
        *static_cast<int64_t*>(p) = r.read_int64();
    }
    else if (int_tid == type_id::get_type_id<uint64_t>())
    {
        *static_cast<uint64_t*>(p) = r.read_uint64();
    }
    else if (int_tid == type_id::get_type_id<int16_t>())
    {
        *static_cast<int16_t*>(p) = static_cast<int16_t>(r.read_int32());
    }
    else if (int_tid == type_id::get_type_id<uint16_t>())
    {
        *static_cast<uint16_t*>(p) = static_cast<uint16_t>(r.read_uint32());
    }
    else if (int_tid == type_id::get_type_id<int8_t>())
    {
        *static_cast<int8_t*>(p) = static_cast<int8_t>(r.read_int32());
    }
    else if (int_tid == type_id::get_type_id<uint8_t>())
    {
        *static_cast<uint8_t*>(p) = static_cast<uint8_t>(r.read_uint32());
    }
    else
    {
        r.skip_value();
    }
}

// 基本类型写入
inline void write_basic(json_writer& w, const void* p, int tid) noexcept {
    if (tid == type_id::get_type_id<int32_t>())
    {
        w.value(*static_cast<const int32_t*>(p));
    }
    else if (tid == type_id::get_type_id<uint32_t>())
    {
        w.value(*static_cast<const uint32_t*>(p));
    }
    else if (tid == type_id::get_type_id<int64_t>())
    {
        w.value(*static_cast<const int64_t*>(p));
    }
    else if (tid == type_id::get_type_id<uint64_t>())
    {
        w.value(*static_cast<const uint64_t*>(p));
    }
    else if (tid == type_id::get_type_id<float>())
    {
        w.value(*static_cast<const float*>(p));
    }
    else if (tid == type_id::get_type_id<double>())
    {
        w.value(*static_cast<const double*>(p));
    }
    else if (tid == type_id::get_type_id<bool>())
    {
        w.value(*static_cast<const bool*>(p));
    }
    else if (tid == type_id::get_type_id<std::string>())
    {
        w.value(*static_cast<const std::string*>(p));
    }
    else
    {
        int enum_tid = lookup_enum_underlying(tid);
        if (enum_tid >= 0)
        {
            write_as_int(w, p, enum_tid);
        }
        else
        {
            w.null();
        }
    }
}

inline void read_basic(json_reader& r, void* p, int tid) noexcept {
    if (tid == type_id::get_type_id<int32_t>())
    {
        *static_cast<int32_t*>(p) = r.read_int32();
    }
    else if (tid == type_id::get_type_id<uint32_t>())
    {
        *static_cast<uint32_t*>(p) = r.read_uint32();
    }
    else if (tid == type_id::get_type_id<int64_t>())
    {
        *static_cast<int64_t*>(p) = r.read_int64();
    }
    else if (tid == type_id::get_type_id<uint64_t>())
    {
        *static_cast<uint64_t*>(p) = r.read_uint64();
    }
    else if (tid == type_id::get_type_id<float>())
    {
        *static_cast<float*>(p) = static_cast<float>(r.read_double());
    }
    else if (tid == type_id::get_type_id<double>())
    {
        *static_cast<double*>(p) = r.read_double();
    }
    else if (tid == type_id::get_type_id<bool>())
    {
        *static_cast<bool*>(p) = r.read_bool();
    }
    else if (tid == type_id::get_type_id<std::string>())
    {
        *static_cast<std::string*>(p) = r.read_string();
    }
    else
    {
        int enum_tid = lookup_enum_underlying(tid);
        if (enum_tid >= 0)
        {
            read_as_int(r, p, enum_tid);
        }
        else
        {
            r.skip_value();
        }
    }
}

// 带 field_meta 的写入 (支持数组 + 嵌套对象)
inline void write_field_meta(json_writer& w, const char* name,
                              const char* base, const reflect::field_meta& fm) noexcept {
    w.key(name);
    const char* field_ptr = base + fm.offset;

    if (fm.array_rank > 0)
    {
        // 数组字段
        w.begin_array();
        uint32_t total = fm.total_elements;
        for (uint32_t i = 0; i < total; ++i)
        {
            const char* elem = field_ptr + i * fm.element_stride;
            write_basic(w, elem, fm.type_id);
        }
        w.end_array();
        return;
    }

    int enum_tid = lookup_enum_underlying(fm.type_id);
    if (enum_tid >= 0)
    {
        write_as_int(w, field_ptr, enum_tid);
        return;
    }

    if (is_type_reflected(fm.type_id))
    {
        // 嵌套对象
        auto qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        if (qv.valid())
        {
            w.begin_object();
            size_t n = qv.field_count();
            for (size_t i = 0; i < n; ++i)
            {
                const reflect::field_meta& sub_fm = qv.field(i);
                write_field_meta(w, sub_fm.name, field_ptr, sub_fm);
            }
            w.end_object();
            return;
        }
    }

    write_basic(w, field_ptr, fm.type_id);
}

// 带 field_meta 的读取
inline void read_field_meta(json_reader& r, char* base,
                             const reflect::field_meta& fm) noexcept {
    char* field_ptr = base + fm.offset;

    if (fm.array_rank > 0)
    {
        if (!r.enter_array())
        {
            return;
        }
        uint32_t i = 0;
        while (r.next_element() && i < fm.total_elements)
        {
            read_basic(r, field_ptr + i * fm.element_stride, fm.type_id);
            ++i;
            r.end_element();
        }
        return;
    }

    int enum_tid = lookup_enum_underlying(fm.type_id);
    if (enum_tid >= 0)
    {
        read_as_int(r, field_ptr, enum_tid);
        return;
    }

    if (is_type_reflected(fm.type_id))
    {
        auto qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        if (qv.valid())
        {
            if (!r.enter_object())
            {
                return;
            }
            std::string_view key;
            while (!(key = r.next_key()).empty())
            {
                const auto* sub_fm = qv.field_by_name(std::string(key).c_str());
                if (sub_fm)
                {
                    read_field_meta(r, field_ptr, *sub_fm);
                }
                else
                {
                    r.skip_value();
                }
            }
            return;
        }
    }

    read_basic(r, field_ptr, fm.type_id);
}

// 对象写入入口
template<typename T>
void to_json(json_writer& w, const T& obj) noexcept {
    auto qv = reflect::try_get<T>();
    if (!qv.valid())
    {
        return;
    }
    w.begin_object();
    size_t n = qv.field_count();
    const char* base = static_cast<const char*>(static_cast<const void*>(&obj));
    for (size_t i = 0; i < n; ++i)
    {
        const reflect::field_meta& fm = qv.field(i);
        write_field_meta(w, fm.name, base, fm);
    }
    w.end_object();
}

// 对象读取入口
template<typename T>
void from_json(json_reader& r, T& obj) noexcept {
    auto qv = reflect::try_get<T>();
    if (!qv.valid())
    {
        return;
    }
    if (!r.enter_object())
    {
        return;
    }
    char* base = static_cast<char*>(static_cast<void*>(&obj));
    std::string_view key;
    while (!(key = r.next_key()).empty())
    {
        const auto* fm = qv.field_by_name(std::string(key).c_str());
        if (fm)
        {
            read_field_meta(r, base, *fm);
        }
        else
        {
            r.skip_value();
        }
    }
}

} // namespace reflect_bridge

template<typename T>
concept json_serializable = (reflect_bridge::has_json_serialize<T> &&
                             reflect_bridge::has_json_deserialize<T>)
                            || std::is_trivially_copyable_v<T>
                            || std::is_class_v<T>;

} // namespace serialize
