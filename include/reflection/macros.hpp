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

// === 注册类型 (聚合类型自动遍历公有字段) ===
// 用法: REGISTER(Vec3);
#define REGISTER(Cls) \
    inline int REFLECT_UNIQUE(_reflect_reg_) = []{ ::reflect::global().register_type<Cls>(#Cls); return 0; }()

// === 注册类型 (聚合类型, 自定义字段名) ===
// 用法: REGISTER_NAMED(Vec3, "x", "y", "z");
#define REGISTER_NAMED(Cls, ...) \
    inline int REFLECT_UNIQUE(_reflect_reg_named_) = []{ ::reflect::global().register_type_named<Cls>(#Cls, { __VA_ARGS__ }); return 0; }()

// === 私有成员描述辅助宏 ===
// 用法: PRIV_FIELD("name_", 0, type_id::get_type_id<std::string>())
#define PRIV_FIELD(field_name, offset, type_id_val) \
    ::offset_desc{ field_name, offset, type_id_val }

// === 注册私有成员 (手填偏移量) ===
// 用法:
//   REGISTER_PRIVATE_OFFSETS(Account,
//       PRIV_FIELD("name_",    0,  type_id::get_type_id<std::string>()),
//       PRIV_FIELD("balance_", 32, type_id::get_type_id<int>()))
#define REGISTER_PRIVATE_OFFSETS(Cls, ...) \
    inline int REFLECT_UNIQUE(_reflect_reg_priv_) = []{ \
        static const ::offset_desc _descs[] = { __VA_ARGS__ }; \
        ::reflect::global().register_private_offsets<Cls>( \
            _descs, sizeof(_descs) / sizeof(_descs[0])); \
        return 0; \
    }()

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
#define REFLECT_PRIVATE(Cls, ...) \
    template<> struct reflect_access<Cls> { \
        static void reg(::reflect::storage& s) { \
            REFLECT_FOR_EACH(Cls, __VA_ARGS__) \
        } \
    }; \
    inline int REFLECT_UNIQUE(_reflect_priv_auto_) = []{ \
        reflect_access<Cls>::reg(::reflect::global()); \
        return 0; \
    }()

// 可变参数遍历 (支持 1~20 个字段)
#define REFLECT_FOR_EACH(Cls, ...) REFLECT_CAT(REFLECT_FE_, REFLECT_NARG(__VA_ARGS__))(Cls, __VA_ARGS__)

// 参数计数
#define REFLECT_NARG(...) \
    REFLECT_NARG_(__VA_ARGS__, 20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1)
#define REFLECT_NARG_(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,a20,N,...) N

// 遍历展开 (1~20 个字段)
#define REFLECT_FE_1(Cls, f1) \
    REFLECT_FIELD_ONE(Cls, f1)
#define REFLECT_FE_2(Cls, f1, f2) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2)
#define REFLECT_FE_3(Cls, f1, f2, f3) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3)
#define REFLECT_FE_4(Cls, f1, f2, f3, f4) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4)
#define REFLECT_FE_5(Cls, f1, f2, f3, f4, f5) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5)
#define REFLECT_FE_6(Cls, f1, f2, f3, f4, f5, f6) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6)
#define REFLECT_FE_7(Cls, f1, f2, f3, f4, f5, f6, f7) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7)
#define REFLECT_FE_8(Cls, f1, f2, f3, f4, f5, f6, f7, f8) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8)
#define REFLECT_FE_9(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9)
#define REFLECT_FE_10(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10)
#define REFLECT_FE_11(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11)
#define REFLECT_FE_12(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12)
#define REFLECT_FE_13(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12) REFLECT_FIELD_ONE(Cls, f13)
#define REFLECT_FE_14(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12) REFLECT_FIELD_ONE(Cls, f13) REFLECT_FIELD_ONE(Cls, f14)
#define REFLECT_FE_15(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12) REFLECT_FIELD_ONE(Cls, f13) REFLECT_FIELD_ONE(Cls, f14) REFLECT_FIELD_ONE(Cls, f15)
#define REFLECT_FE_16(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12) REFLECT_FIELD_ONE(Cls, f13) REFLECT_FIELD_ONE(Cls, f14) REFLECT_FIELD_ONE(Cls, f15) REFLECT_FIELD_ONE(Cls, f16)
#define REFLECT_FE_17(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12) REFLECT_FIELD_ONE(Cls, f13) REFLECT_FIELD_ONE(Cls, f14) REFLECT_FIELD_ONE(Cls, f15) REFLECT_FIELD_ONE(Cls, f16) REFLECT_FIELD_ONE(Cls, f17)
#define REFLECT_FE_18(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12) REFLECT_FIELD_ONE(Cls, f13) REFLECT_FIELD_ONE(Cls, f14) REFLECT_FIELD_ONE(Cls, f15) REFLECT_FIELD_ONE(Cls, f16) REFLECT_FIELD_ONE(Cls, f17) REFLECT_FIELD_ONE(Cls, f18)
#define REFLECT_FE_19(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12) REFLECT_FIELD_ONE(Cls, f13) REFLECT_FIELD_ONE(Cls, f14) REFLECT_FIELD_ONE(Cls, f15) REFLECT_FIELD_ONE(Cls, f16) REFLECT_FIELD_ONE(Cls, f17) REFLECT_FIELD_ONE(Cls, f18) REFLECT_FIELD_ONE(Cls, f19)
#define REFLECT_FE_20(Cls, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20) \
    REFLECT_FIELD_ONE(Cls, f1) REFLECT_FIELD_ONE(Cls, f2) REFLECT_FIELD_ONE(Cls, f3) REFLECT_FIELD_ONE(Cls, f4) REFLECT_FIELD_ONE(Cls, f5) REFLECT_FIELD_ONE(Cls, f6) REFLECT_FIELD_ONE(Cls, f7) REFLECT_FIELD_ONE(Cls, f8) REFLECT_FIELD_ONE(Cls, f9) REFLECT_FIELD_ONE(Cls, f10) REFLECT_FIELD_ONE(Cls, f11) REFLECT_FIELD_ONE(Cls, f12) REFLECT_FIELD_ONE(Cls, f13) REFLECT_FIELD_ONE(Cls, f14) REFLECT_FIELD_ONE(Cls, f15) REFLECT_FIELD_ONE(Cls, f16) REFLECT_FIELD_ONE(Cls, f17) REFLECT_FIELD_ONE(Cls, f18) REFLECT_FIELD_ONE(Cls, f19) REFLECT_FIELD_ONE(Cls, f20)

// === 注册成员方法 (普通/const 方法统一入口) ===
// 用法: REGISTER_METHOD(Calculator, add)
#define REGISTER_METHOD(Cls, method) \
    inline int REFLECT_UNIQUE(_reflect_reg_m_) = []{ ::reflect::global().register_method<&Cls::method>(#method); return 0; }()

// === 注册静态方法 ===
// 用法: REGISTER_STATIC_METHOD(Calculator, multiply)
#define REGISTER_STATIC_METHOD(Cls, method) \
    inline int REFLECT_UNIQUE(_reflect_reg_sm_) = []{ ::reflect::global().register_static_method<Cls, &Cls::method>(#method); return 0; }()

// === 注册重载成员方法 (显式指定方法指针) ===
// 用法: REGISTER_METHOD_OVERLOAD(Calculator, add,
//        static_cast<int(Calculator::*)(int,int)>(&Calculator::add))
#define REGISTER_METHOD_OVERLOAD(Cls, method, ptr) \
    inline int REFLECT_UNIQUE(_reflect_reg_m_ov_) = []{ ::reflect::global().register_method<ptr>(#method); return 0; }()

// === 注册重载静态方法 (显式指定方法指针) ===
// 用法: REGISTER_STATIC_METHOD_OVERLOAD(Calculator, multiply,
//        static_cast<int(*)(int,int)>(&Calculator::multiply))
#define REGISTER_STATIC_METHOD_OVERLOAD(Cls, method, ptr) \
    inline int REFLECT_UNIQUE(_reflect_reg_sm_ov_) = []{ ::reflect::global().register_static_method<Cls, ptr>(#method); return 0; }()
