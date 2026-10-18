// field_codec.hpp - 反射字段 ↔ archive_writer/archive_reader (唯一字段实现)
#pragma once

#include "../part/codec/archive_codec.hpp"
#include "../part/type_id.hpp"
#include "../reflection/query.hpp"
#include "../reflection/meta.hpp"
#include "type_registry.hpp"
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace serialize {

namespace field_codec {

template<typename T>
concept has_json_serialize = requires(const T& v) {
    { v.to_json() } -> std::convertible_to<std::string>;
};

template<typename T>
concept has_json_deserialize = requires(T& v, std::string_view s) {
    v.from_json(s);
};

template<typename T>
[[nodiscard]] inline bool is_reflected() noexcept
{
    return reflect::try_get<T>().valid();
}

[[nodiscard]] inline bool is_type_reflected(int tid) noexcept
{
    return reflect::global().get_type(tid) != nullptr;
}

inline bool write_as_int(archive_writer& w, const void* p, int int_tid) noexcept
{
    if (int_tid == type_id::get_type_id<int32_t>())
    {
        w.write_i32(*static_cast<const int32_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<uint32_t>())
    {
        w.write_u32(*static_cast<const uint32_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<int64_t>())
    {
        w.write_i64(*static_cast<const int64_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<uint64_t>())
    {
        w.write_u64(*static_cast<const uint64_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<int16_t>())
    {
        w.write_i32(static_cast<int32_t>(*static_cast<const int16_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<uint16_t>())
    {
        w.write_u32(static_cast<uint32_t>(*static_cast<const uint16_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<int8_t>())
    {
        w.write_i32(static_cast<int32_t>(*static_cast<const int8_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<uint8_t>())
    {
        w.write_u32(static_cast<uint32_t>(*static_cast<const uint8_t*>(p)));
    }
    else
    {
        return false;
    }
    return true;
}

inline bool read_as_int(archive_reader& r, void* p, int int_tid) noexcept
{
    if (int_tid == type_id::get_type_id<int32_t>())
    {
        *static_cast<int32_t*>(p) = r.read_i32();
    }
    else if (int_tid == type_id::get_type_id<uint32_t>())
    {
        *static_cast<uint32_t*>(p) = r.read_u32();
    }
    else if (int_tid == type_id::get_type_id<int64_t>())
    {
        *static_cast<int64_t*>(p) = r.read_i64();
    }
    else if (int_tid == type_id::get_type_id<uint64_t>())
    {
        *static_cast<uint64_t*>(p) = r.read_u64();
    }
    else if (int_tid == type_id::get_type_id<int16_t>())
    {
        *static_cast<int16_t*>(p) = static_cast<int16_t>(r.read_i32());
    }
    else if (int_tid == type_id::get_type_id<uint16_t>())
    {
        *static_cast<uint16_t*>(p) = static_cast<uint16_t>(r.read_u32());
    }
    else if (int_tid == type_id::get_type_id<int8_t>())
    {
        *static_cast<int8_t*>(p) = static_cast<int8_t>(r.read_i32());
    }
    else if (int_tid == type_id::get_type_id<uint8_t>())
    {
        *static_cast<uint8_t*>(p) = static_cast<uint8_t>(r.read_u32());
    }
    else
    {
        r.skip_value();
        return false;
    }
    return true;
}

inline bool write_basic(archive_writer& w, const void* p, int tid) noexcept
{
    if (tid == type_id::get_type_id<int32_t>())
    {
        w.write_i32(*static_cast<const int32_t*>(p));
    }
    else if (tid == type_id::get_type_id<uint32_t>())
    {
        w.write_u32(*static_cast<const uint32_t*>(p));
    }
    else if (tid == type_id::get_type_id<int64_t>())
    {
        w.write_i64(*static_cast<const int64_t*>(p));
    }
    else if (tid == type_id::get_type_id<uint64_t>())
    {
        w.write_u64(*static_cast<const uint64_t*>(p));
    }
    else if (tid == type_id::get_type_id<int16_t>())
    {
        w.write_i32(static_cast<int32_t>(*static_cast<const int16_t*>(p)));
    }
    else if (tid == type_id::get_type_id<uint16_t>())
    {
        w.write_u32(static_cast<uint32_t>(*static_cast<const uint16_t*>(p)));
    }
    else if (tid == type_id::get_type_id<int8_t>())
    {
        w.write_i32(static_cast<int32_t>(*static_cast<const int8_t*>(p)));
    }
    else if (tid == type_id::get_type_id<uint8_t>())
    {
        w.write_u32(static_cast<uint32_t>(*static_cast<const uint8_t*>(p)));
    }
    else if (tid == type_id::get_type_id<float>())
    {
        w.write_f32(*static_cast<const float*>(p));
    }
    else if (tid == type_id::get_type_id<double>())
    {
        w.write_f64(*static_cast<const double*>(p));
    }
    else if (tid == type_id::get_type_id<bool>())
    {
        w.write_bool(*static_cast<const bool*>(p));
    }
    else if (tid == type_id::get_type_id<std::string>())
    {
        w.write_string(*static_cast<const std::string*>(p));
    }
    else
    {
        int enum_tid = lookup_enum_underlying(tid);
        if (enum_tid < 0)
        {
            return false;
        }
        return write_as_int(w, p, enum_tid);
    }
    return true;
}

inline bool read_basic(archive_reader& r, void* p, int tid) noexcept
{
    if (tid == type_id::get_type_id<int32_t>())
    {
        *static_cast<int32_t*>(p) = r.read_i32();
    }
    else if (tid == type_id::get_type_id<uint32_t>())
    {
        *static_cast<uint32_t*>(p) = r.read_u32();
    }
    else if (tid == type_id::get_type_id<int64_t>())
    {
        *static_cast<int64_t*>(p) = r.read_i64();
    }
    else if (tid == type_id::get_type_id<uint64_t>())
    {
        *static_cast<uint64_t*>(p) = r.read_u64();
    }
    else if (tid == type_id::get_type_id<int16_t>())
    {
        *static_cast<int16_t*>(p) = static_cast<int16_t>(r.read_i32());
    }
    else if (tid == type_id::get_type_id<uint16_t>())
    {
        *static_cast<uint16_t*>(p) = static_cast<uint16_t>(r.read_u32());
    }
    else if (tid == type_id::get_type_id<int8_t>())
    {
        *static_cast<int8_t*>(p) = static_cast<int8_t>(r.read_i32());
    }
    else if (tid == type_id::get_type_id<uint8_t>())
    {
        *static_cast<uint8_t*>(p) = static_cast<uint8_t>(r.read_u32());
    }
    else if (tid == type_id::get_type_id<float>())
    {
        *static_cast<float*>(p) = r.read_f32();
    }
    else if (tid == type_id::get_type_id<double>())
    {
        *static_cast<double*>(p) = r.read_f64();
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
        if (enum_tid < 0)
        {
            r.skip_value();
            return false;
        }
        return read_as_int(r, p, enum_tid);
    }
    return true;
}

// flatbuffer 向量元素编码宽度: 窄整型经 4 字节写出, 宽类型 8 字节, bool 1 字节
[[nodiscard]] inline size_t element_encode_size(int tid) noexcept
{
    int use = tid;
    int enum_tid = lookup_enum_underlying(tid);
    if (enum_tid >= 0)
    {
        use = enum_tid;
    }
    if (use == type_id::get_type_id<int64_t>() || use == type_id::get_type_id<uint64_t>()
        || use == type_id::get_type_id<double>())
    {
        return 8;
    }
    if (use == type_id::get_type_id<bool>())
    {
        return 1;
    }
    return 4;
}

inline bool write_field_meta(archive_writer& w, const char* name, const char* base,
                             const reflect::field_meta& fm) noexcept
{
    w.key(name);
    const char* field_ptr = base + fm.offset;

    if (fm.array_rank > 0)
    {
        int elem_enum = lookup_enum_underlying(fm.type_id);
        bool is_elem_object = (elem_enum < 0) && is_type_reflected(fm.type_id);
        reflect::query_view elem_qv;
        if (is_elem_object)
        {
            elem_qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        }
        bool ok = true;
        w.begin_array(fm.total_elements);
        for (uint32_t i = 0; i < fm.total_elements; ++i)
        {
            const char* elem = field_ptr + i * fm.element_stride;
            if (is_elem_object && elem_qv.valid())
            {
                w.begin_object();
                size_t n = elem_qv.field_count();
                for (size_t j = 0; j < n; ++j)
                {
                    const reflect::field_meta& sub_fm = elem_qv.field(j);
                    if (!write_field_meta(w, sub_fm.name, elem, sub_fm))
                    {
                        ok = false;
                    }
                }
                w.end_object();
            }
            else if (!write_basic(w, elem, fm.type_id))
            {
                ok = false;
            }
        }
        w.end_array();
        return ok;
    }

    int enum_tid = lookup_enum_underlying(fm.type_id);
    if (enum_tid >= 0)
    {
        return write_as_int(w, field_ptr, enum_tid);
    }

    if (is_type_reflected(fm.type_id))
    {
        auto qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        if (qv.valid())
        {
            bool ok = true;
            w.begin_object();
            size_t n = qv.field_count();
            for (size_t i = 0; i < n; ++i)
            {
                const reflect::field_meta& sub_fm = qv.field(i);
                if (!write_field_meta(w, sub_fm.name, field_ptr, sub_fm))
                {
                    ok = false;
                }
            }
            w.end_object();
            return ok;
        }
    }

    return write_basic(w, field_ptr, fm.type_id);
}

inline bool read_field_meta(archive_reader& r, char* base, const reflect::field_meta& fm) noexcept;

inline bool read_object(archive_reader& r, char* base, reflect::query_view qv) noexcept
{
    if (!r.enter_object())
    {
        return false;
    }
    bool ok = true;
    const size_t n = qv.field_count();
    for (size_t i = 0; i < n; ++i)
    {
        if (r.next_key().empty())
        {
            break;
        }
        if (!read_field_meta(r, base, qv.field(i)))
        {
            ok = false;
        }
    }
    r.leave_object();
    return ok;
}

inline bool read_field_meta(archive_reader& r, char* base, const reflect::field_meta& fm) noexcept
{
    char* field_ptr = base + fm.offset;

    if (fm.array_rank > 0)
    {
        if (!r.enter_array())
        {
            return false;
        }
        r.set_element_size(element_encode_size(fm.type_id));
        int elem_enum = lookup_enum_underlying(fm.type_id);
        bool is_elem_object = (elem_enum < 0) && is_type_reflected(fm.type_id);
        reflect::query_view elem_qv;
        if (is_elem_object)
        {
            elem_qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        }
        bool ok = true;
        uint32_t i = 0;
        while (r.next_element())
        {
            if (i < fm.total_elements)
            {
                char* elem = field_ptr + i * fm.element_stride;
                if (is_elem_object && elem_qv.valid())
                {
                    if (!read_object(r, elem, elem_qv))
                    {
                        ok = false;
                    }
                }
                else if (!read_basic(r, elem, fm.type_id))
                {
                    ok = false;
                }
                ++i;
            }
            else
            {
                r.skip_value();
            }
            r.end_element();
        }
        r.leave_array();
        return ok;
    }

    int enum_tid = lookup_enum_underlying(fm.type_id);
    if (enum_tid >= 0)
    {
        return read_as_int(r, field_ptr, enum_tid);
    }

    if (is_type_reflected(fm.type_id))
    {
        auto qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        if (qv.valid())
        {
            return read_object(r, field_ptr, qv);
        }
    }

    return read_basic(r, field_ptr, fm.type_id);
}

template<typename T>
bool to_archive(archive_writer& w, const T& obj) noexcept
{
    auto qv = reflect::try_get<T>();
    if (!qv.valid())
    {
        return false;
    }
    bool ok = true;
    w.begin_object();
    const char* base = static_cast<const char*>(static_cast<const void*>(&obj));
    size_t n = qv.field_count();
    for (size_t i = 0; i < n; ++i)
    {
        const reflect::field_meta& fm = qv.field(i);
        if (!write_field_meta(w, fm.name, base, fm))
        {
            ok = false;
        }
    }
    w.end_object();
    return ok;
}

template<typename T>
bool from_archive(archive_reader& r, T& obj) noexcept
{
    auto qv = reflect::try_get<T>();
    if (!qv.valid())
    {
        return false;
    }
    return read_object(r, reinterpret_cast<char*>(&obj), qv) && !r.has_error();
}

} // namespace field_codec

} // namespace serialize
