#pragma once

// macros.hpp - 反射注册宏
// 用户通过宏注册类型, 字段, 方法
// 宏在命名空间作用域使用 (通过 inline 变量初始化)

#include "storage.hpp"
#include "../part/member_offset.hpp"
#include "../part/aggregate_reflect_pp.hpp"

// 反射访问器主模板 (由 REFLECT_PRIVATE 宏特化)
template<typename Cls>
struct reflect_access;

// === 宏辅助: 唯一变量名生成 ===
// MSVC gotcha: _##__COUNTER__ 不展开 __COUNTER__, 需两层宏延迟展开
#define REFLECT_CONCAT_I(a, b) a##b
#define REFLECT_CONCAT(a, b) REFLECT_CONCAT_I(a, b)
#define REFLECT_UNIQUE(prefix) REFLECT_CONCAT(prefix, __COUNTER__)

// === 注册类型 (聚合类型自动遍历公有字段, 非聚合类型仅注册类型元信息) ===
// 用法: REGISTER(Vec3);
// 用法: REGISTER(Account);  // 非聚合类型仅注册类型元信息, 字段需 REFLECT_PRIVATE 或 REGISTER_MEMBERS
#define REGISTER(Cls) \
    inline int REFLECT_UNIQUE(_reflect_reg_) = []{ \
        if constexpr (std::is_aggregate_v<Cls>) \
        { \
            ::reflect::global().register_type<Cls>(#Cls); \
        } \
        else \
        { \
            ::reflect::global().register_type_only<Cls>(#Cls); \
        } \
        return 0; \
    }()

// === 批量注册成员 (标量/数组统一入口) ===
// 用法 (单字段): REGISTER_MEMBERS(Path, points);
// 用法 (多字段): REGISTER_MEMBERS(Color, r, g, b, a);
// 用法 (混合):   REGISTER_MEMBERS(Mixed, id, pos);
// 支持 1~20 个字段
#define REGISTER_MEMBERS(Cls, ...) \
    inline int REFLECT_UNIQUE(_reflect_members_) = []{ \
        REFLECT_FOR_EACH_DATA(REFLECT_MEMBER_ONE, Cls, __VA_ARGS__) \
        return 0; \
    }()

#define REFLECT_MEMBER_ONE(Cls, field) \
    ::reflect::global().register_member_auto<Cls, \
        decltype(std::declval<Cls>().field), &Cls::field>(#Cls, #field);

// === 批量注册方法 ===
// 用法: REGISTER_FNS(Calculator, add, sum5, is_positive, no_return, multiply);
// 支持 1~20 个方法
#define REGISTER_FNS(Cls, ...) \
    inline int REFLECT_UNIQUE(_reflect_fns_) = []{ \
        REFLECT_FOR_EACH_DATA(REFLECT_FN_ONE, Cls, __VA_ARGS__) \
        return 0; \
    }()

#define REFLECT_FN_ONE(Cls, method) \
    if constexpr (std::is_member_function_pointer_v<decltype(&Cls::method)>) { \
        ::reflect::global().register_method<&Cls::method>(#method); \
    } else { \
        ::reflect::global().register_static_method<Cls, &Cls::method>(#method); \
    }

// === 注册私有成员 (手填偏移量, 逃生通道) ===
// 用法:
//   REGISTER_PRIVATE_OFFSETS(Account,
//       PRIV_FIELD("name_",    0,  std::string),
//       PRIV_FIELD("balance_", 32, int));
#define REGISTER_PRIVATE_OFFSETS(Cls, ...) \
    inline int REFLECT_UNIQUE(_reflect_reg_priv_) = []{ \
        static const ::offset_desc _descs[] = { __VA_ARGS__ }; \
        ::reflect::global().register_private_offsets<Cls>( \
            _descs, sizeof(_descs) / sizeof(_descs[0])); \
        return 0; \
    }()

// 私有成员描述
#define PRIV_FIELD(field_name, offset, Type) \
    ::offset_desc{ field_name, offset, ::type_id::get_type_id<Type>() }

// === 侵入式自动推导注册 (类内 friend + 类外字段名列表) ===
// 类内标记宏: 展开为 friend 声明, 授权反射访问器访问私有成员
// 用法 (类内):
//   class Account {
//       std::string name_;
//       int balance_;
//   public:
//       REFLECT(Account);
//   };
#define REFLECT(Cls) \
    template<typename> friend struct reflect_access

// 字段注册辅助宏 (单个字段)
#define REFLECT_FIELD_ONE(Cls, field) \
    s.register_field<Cls, decltype(Cls::field), &Cls::field>(#field);

// 类外字段列表注册宏
// 用法 (类外):
//   REFLECT_PRIVATE(Account, name_, balance_);
// 自动注册类型元信息 (register_type_only 内部去重, 重复调用安全)
#define REFLECT_PRIVATE(Cls, ...) \
    template<> struct reflect_access<Cls> { \
        static void reg(::reflect::storage& s) { \
            REFLECT_FOR_EACH_DATA(REFLECT_FIELD_ONE, Cls, __VA_ARGS__) \
        } \
    }; \
    inline int REFLECT_UNIQUE(_reflect_priv_auto_) = []{ \
        ::reflect::global().register_type_only<Cls>(#Cls); \
        reflect_access<Cls>::reg(::reflect::global()); \
        return 0; \
    }()

// === 注册重载方法 (显式指定方法指针) ===
// 用法 (成员重载): REGISTER_FN_OVERLOAD(Calculator, add, static_cast<int(Calculator::*)(int,int)>(&Calculator::add))
// 用法 (静态重载): REGISTER_FN_OVERLOAD(Calculator, multiply, static_cast<int(*)(int,int)>(&Calculator::multiply))
#define REGISTER_FN_OVERLOAD(Cls, method, ptr) \
    inline int REFLECT_UNIQUE(_reflect_fn_ov_) = []{ \
        if constexpr (std::is_member_function_pointer_v<ptr>) { \
            ::reflect::global().register_method<ptr>(#method); \
        } else { \
            ::reflect::global().register_static_method<Cls, ptr>(#method); \
        } \
        return 0; \
    }()

// === 只注册类型 (不自动遍历字段, 用于无字段类型或配合底层 register_array_field 手动注册) ===
// 用法: REGISTER_TYPE_ONLY(Mixed);
#define REGISTER_TYPE_ONLY(Cls) \
    inline int REFLECT_UNIQUE(_reflect_reg_only_) = []{ ::reflect::global().register_type_only<Cls>(#Cls); return 0; }()
