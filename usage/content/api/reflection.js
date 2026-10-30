window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['reflection_usage'] = {
  id: 'reflection_usage',
  title: "reflection — 反射模块使用",
  category: 'reflection',
  icon: 'R',
  order: 11,
  content: `## 11. reflection — 反射模块使用

命名空间 \`reflect\`。\`noexcept\`。

提供类型元数据的注册与查询：字段访问、方法调用、枚举互转、继承转型、比较、哈希、构造、容器遍历。注册与查询都通过 \`reflect::manager\` 实例进行，可失败接口返回 \`operating_message\`。

### 快速上手

注册字段并读写：

\`\`\`cpp
reflect::manager m;

struct Vec3 { float x, y, z; };

(void)m.add_type<Vec3>();
(void)m.add_fields<Vec3>(std::tuple{
    std::pair{"x", &Vec3::x},
    std::pair{"y", &Vec3::y},
    std::pair{"z", &Vec3::z},
});

Vec3 v{1.0f, 2.0f, 3.0f};
auto q = m.get<Vec3>();
q.get_by_name<float>(&v, "y") = 20.0f;      // 按名读写字段
\`\`\`

注册并调用方法：

\`\`\`cpp
struct Calc { int base; int add(int a, int b) { return a + b + base; } };

(void)m.add_type<Calc>();
(void)m.add_method<&Calc::add>("add");

Calc c{10};
int sum = 0;
(void)m.invoke(sum, c, "add", 1, 2);         // sum == 13
\`\`\`

### 按任务查接口

| 我想… | 去看 |
|------|------|
| 注册类型、字段、方法 | [11.1 manager 注册接口](#111-manager-注册接口) |
| 查询类型与调用方法 | [11.2 manager 查询接口](#112-manager-查询接口) |
| 按名 / 按索引读写字段 | [11.3 query_view](#113-query_view-类型视图) |
| 遍历字段或方法列表 | [11.4 field_view / method_view](#114-field_view--method_view--遍历视图) |
| 枚举值转名称 | [11.7 enum_view](#117-enum_view-枚举反射) |
| 继承关系与向上转型 | [11.6 inheritance_view](#116-inheritance_view-继承关系) |
| 字段属性 | [11.8 attribute_view](#118-attribute_view-字段属性) |
| 比较、克隆、哈希 | [11.12 compare_view](#1112-compare_view-字段比较克隆) / [11.13 hash_view](#1113-hash_view-字段哈希) |

### 11.1 manager 注册接口

\`\`\`cpp
reflect::manager m;
\`\`\`

| 接口 | 说明 |
|------|------|
| \`add_type<T>()\` | 注册类型元信息，不遍历字段 |
| \`add_type_as<T>(name)\` | 注册类型并显式指定类型名 |
| \`add_aggregate_type<T>()\` | 注册类型并自动遍历聚合字段，字段名为 \`field_0\`、\`field_1\`；最多 256 个字段 |
| \`add_field<T>(name, &T::field)\` | 注册字段，标量与数组自动分派 |
| \`add_fields<T>(tuple)\` | 批量注册字段，接受 \`{名字, 成员指针}\` 元组 |
| \`add_method<&T::f>(name)\` | 注册成员方法，指针须为编译期常量 |
| \`add_method(name, &T::f)\` | 注册成员方法，指针可为运行期值 |
| \`add_methods(tuple)\` | 批量注册成员方法 |
| \`add_static_method<C, &C::f>(name)\` | 注册静态方法 |
| \`add_virtual_method<&T::f>(name, vtable_offset)\` | 注册虚方法，vtable 偏移由调用方给出 |
| \`add_enum<E>(name, {{E::v, "v"}})\` | 注册枚举名值对 |
| \`add_base<D, B>()\` | 注册继承关系，指针调整由类型转换完成 |
| \`add_base_with_offset<D, B>(offset)\` | 注册继承关系并指定指针调整量 |
| \`add_base_with_adjuster<D, B>(fn)\` | 注册继承关系并指定调整函数，用于私有或虚拟继承 |
| \`add_convert<From, To>()\` | 注册类型转换，要求 \`From\` 可隐式转换到 \`To\` |
| \`add_field_attr<T>(field, key, value)\` | 注册字段属性，键存在时覆盖 |
| \`set_construct_destruct<T>(create, destroy)\` | 指定构造与析构函数，用于私有构造类型 |
| \`add_accessed_members<T>()\` | 按 \`reflect_access<T>\` 特化注册全部授信成员 |
| \`freeze()\` / \`unfreeze()\` | 冻结后写接口返回 \`om_err_reflect_frozen\` |

### 11.2 manager 查询接口

| 接口 | 说明 |
|------|------|
| \`get<T>()\` | 按类型取 \`query_view\`，未注册返回 invalid 视图 |
| \`find(type_name)\` | 按类型名取 \`query_view\` |
| \`has<T>()\` | 类型是否已注册 |
| \`field<T>(name)\` | 按名取 \`const field_meta*\`，未找到返回 nullptr |
| \`method<T>(name)\` | 按名取 \`const method_meta*\`，未找到返回 nullptr |
| \`invoke<R>(out, obj, name, args...)\` | 调用有返回值方法，结果写入 \`out\` |
| \`invoke_void(obj, name, args...)\` | 调用无返回值方法 |
| \`try_invoke<R>(obj, name, args...)\` | 软失败调用；\`R=void\` 返回 \`bool\`，否则返回 \`std::optional<R>\` |
| \`get_fields<T>()\` | 字段遍历视图，支持 range-for |
| \`get_methods<T>()\` | 方法遍历视图，支持 range-for |
| \`type_count()\` | 已注册类型数 |
| \`frozen()\` | 是否已冻结 |
| \`store()\` | 取底层存储对象，供序列化等场景使用 |

### 11.3 query_view — 类型视图

\`get<T>()\` 与 \`find()\` 的返回类型。

| 接口 | 说明 |
|------|------|
| \`valid()\` | 是否绑定有效元数据 |
| \`name()\` | 类型名 |
| \`size()\` / \`align()\` | \`sizeof(T)\` / \`alignof(T)\` |
| \`field_count()\` / \`method_count()\` | 字段数 / 方法数 |
| \`type_id_value()\` | 类型 id |
| \`meta()\` | 底层 \`const type_meta*\` |
| \`field(i)\` / \`field_by_name(name)\` | 按索引 / 按名取字段元数据 |
| \`method(i)\` / \`method_by_name(name)\` | 按索引 / 按名取方法元数据 |
| \`get<R>(obj, i)\` | 按索引取字段引用 |
| \`get_by_name<R>(obj, name)\` | 按名取字段引用，字段不存在即终止 |
| \`try_get_by_name<R>(obj, name)\` | 按名取字段指针，未找到返回 nullptr |
| \`get_ptr(obj, name)\` | 按名取字段 \`void*\` |
| \`invoke<R>(obj, name, args...)\` | 调用方法，失败即终止 |
| \`try_invoke<R>(obj, name, args...)\` | 软失败调用，参数或返回类型不匹配即失败 |
| \`find_overload(name, ids, n_args)\` | 按参数类型 id 精确匹配重载 |
| \`get_by_path(obj, path)\` | 按 \`a.b.c\` 路径取字段 \`void*\` |
| \`get_by_path_as<T>(obj, path)\` | 路径访问并转型为 \`T*\` |

字段与数组访问：

| 接口 | 说明 |
|------|------|
| \`is_array(i)\` / \`is_array_by_name(name)\` | 字段是否为数组 |
| \`array_rank(i)\` | 数组维度数（0 为标量，1~4 为数组） |
| \`array_total_elements(i)\` | 数组总元素数 |
| \`array_element_stride(i)\` | 元素步长（字节） |
| \`array_extent(field_idx, dim)\` | 第 \`dim\` 维元素数 |
| \`array_element_ptr(obj, field_idx, element_idx)\` | 取数组元素指针，越界返回 nullptr |
| \`array_element_ptr_by_name(obj, name, element_idx)\` | 按名取数组元素指针 |
| \`for_each_array_element(obj, field_idx, f)\` | 遍历数组元素，\`f(ptr, idx, type_id)\` |
| \`array_get<T>(obj, field_idx, element_idx)\` | 类型安全读取，越界即终止 |
| \`array_get_by_name<T>(obj, name, element_idx)\` | 按名读取 |
| \`try_array_get<T>(obj, field_idx, element_idx)\` | 安全读取，越界返回 nullptr |
| \`array_set<T>(obj, field_idx, element_idx, value)\` | 类型安全写入 |
| \`array_set_by_name<T>(obj, name, element_idx, value)\` | 按名写入 |

### 11.4 field_view / method_view — 遍历视图

| 接口 | 说明 |
|------|------|
| \`begin()\` / \`end()\` | range-for 迭代器，解引用得到 \`const field_meta&\` / \`const method_meta&\` |
| \`size()\` / \`empty()\` | 元素数量 |
| \`operator[](i)\` | 按索引访问 |

### 11.5 construct_view — 对象构造/销毁

\`m.get_construct<T>()\`。

| 接口 | 说明 |
|------|------|
| \`has_default_construct()\` | 类型是否可默认构造 |
| \`create()\` | 堆分配默认构造，失败返回 nullptr |
| \`create_inplace(buf)\` | 就地默认构造，\`buf\` 需足够大且对齐 |
| \`destroy(obj)\` | 析构对象，不释放内存 |
| \`destroy_heap(obj)\` | 析构并释放，与 \`create()\` 配对 |

### 11.6 inheritance_view — 继承关系

\`m.get_inheritance<T>()\`。

| 接口 | 说明 |
|------|------|
| \`is_derived_from(type_id)\` / \`is_derived_from<Base>()\` | 是否继承自指定类型，含间接继承 |
| \`is_base_of(type_id)\` / \`is_base_of<Derived>()\` | 是否为指定类型的基类 |
| \`upcast(derived_obj, base_type_id)\` | 向上转型，失败返回 nullptr |
| \`upcast<Base>(derived_obj)\` | 模板版本，返回 \`Base*\` |
| \`base_count()\` / \`derived_count()\` | 直接基类 / 派生类数量 |
| \`base_type_id_at(idx)\` / \`derived_type_id_at(idx)\` | 按索引取 type_id，越界返回 -1 |
| \`for_each_base(f)\` | 遍历直接基类，\`f(base_type_id, offset)\` |
| \`for_each_derived(f)\` | 遍历直接派生类，\`f(derived_type_id)\` |

### 11.7 enum_view — 枚举反射

\`m.get_enum<E>()\`。

| 接口 | 说明 |
|------|------|
| \`valid()\` | 是否绑定有效枚举元数据 |
| \`name()\` / \`type_id_value()\` / \`underlying_type_id()\` | 枚举名 / 类型 id / 底层类型 id |
| \`value_count()\` | 枚举值数量 |
| \`value_to_name(value)\` | 值转名称，失败返回 nullptr |
| \`name_to_value(name, out)\` | 名称转值，失败返回 false |
| \`for_each_value(f)\` | 遍历枚举值，\`f(uint64_t value, const char* name)\` |

### 11.8 attribute_view — 字段属性

\`m.get_attributes<T>()\`。

| 接口 | 说明 |
|------|------|
| \`has_attr(field, key)\` | 字段是否含指定属性，\`field\` 可为索引或名称 |
| \`get_attr(field, key)\` | 取属性值 \`const void_any*\`，失败返回 nullptr |
| \`get_attr_as<V>(field, key)\` | 类型安全取值，返回 \`const V*\` |
| \`for_each_attr(field_idx, f)\` | 遍历字段属性，\`f(key_hash, void_any&)\` |

### 11.9 container_view — 容器反射

\`m.get_container<T>(obj)\`。

| 接口 | 说明 |
|------|------|
| \`valid()\` / \`is_container()\` | 是否绑定容器 |
| \`category()\` | 容器类别（\`sequential\` / \`none\`） |
| \`element_type_id()\` | 元素类型 id |
| \`size()\` | 元素数 |
| \`get(index)\` / \`get_as<T>(index)\` | 取元素指针 / 类型安全取 |
| \`push_back(element)\` / \`push_back<T>(value)\` | 追加元素 |
| \`clear()\` / \`reserve(n)\` | 清空 / 预留容量 |
| \`for_each(f)\` | 遍历元素，\`f(void* element, size_t index)\` |

### 11.10 virtual_dispatch_view — 动态派发

\`m.get_virtual_dispatch<T>()\`。

| 接口 | 说明 |
|------|------|
| \`is_virtual(method_name)\` | 方法是否为虚函数 |
| \`vtable_offset(method_name)\` | 虚函数 vtable 偏移，非虚返回 -1 |

### 11.11 convert_view — 类型转换

\`m.get_convert<T>()\`。

| 接口 | 说明 |
|------|------|
| \`can_convert_to(type_id)\` / \`can_convert_to<U>()\` | 是否可转换到目标类型 |
| \`convert_to(src, target_type_id, dst)\` | 执行转换写入 \`dst\`，失败返回 false |
| \`convert_to<U>(src)\` | 返回 \`std::optional<U>\` |

### 11.12 compare_view — 字段比较/克隆

\`m.get_compare<T>()\`。

| 接口 | 说明 |
|------|------|
| \`equal(a, b)\` | 逐字段相等比较，数组逐元素比较 |
| \`clone(src, dst)\` | 逐字段拷贝构造，\`dst\` 须为未初始化内存 |
| \`copy_assign(src, dst)\` | 逐字段赋值，\`dst\` 须已构造 |
| \`destroy_fields(obj)\` | 逐字段析构 |

### 11.13 hash_view — 字段哈希

\`m.get_hash<T>()\`。

| 接口 | 说明 |
|------|------|
| \`hash(obj)\` | 返回 \`uint64_t\`；未绑定或 \`obj\` 为空返回 0 |

### 11.14 使用示例

\`\`\`cpp
reflect::manager m;

struct Vec3 { float x, y, z; };

(void)m.add_type<Vec3>();
(void)m.add_field<Vec3>("x", &Vec3::x);
(void)m.add_field<Vec3>("y", &Vec3::y);
(void)m.add_field<Vec3>("z", &Vec3::z);

Vec3 v{1.0f, 2.0f, 3.0f};
auto q = m.get<Vec3>();
q.get_by_name<float>(&v, "y");          // 2.0
q.get_by_name<float>(&v, "y") = 20.0f;  // 修改字段

for (const reflect::field_meta& fm : m.get_fields<Vec3>()) {
    std::cout << fm.name << " offset=" << fm.offset << "\\n";
}

struct Calc {
    int base;
    int add(int a, int b) { return a + b + base; }
};

(void)m.add_type<Calc>();
(void)m.add_field<Calc>("base", &Calc::base);
(void)m.add_method<&Calc::add>("add");

Calc c{10};
int sum = 0;
auto call_error = m.invoke(sum, c, "add", 1, 2);   // sum == 13
if (!call_error) {
    std::cerr << "code=" << call_error.code() << "\\n";
}

// 私有成员
class Account {
    int balance_ = 0;
    void deposit(int amt) { balance_ += amt; }
    template<typename> friend struct ::reflect::reflect_access;
public:
    int balance() const { return balance_; }
};

namespace reflect {
template<>
struct reflect_access<Account> {
    static auto fields() noexcept {
        return std::make_tuple(std::pair{"balance_", &Account::balance_});
    }
    static auto methods() noexcept {
        return std::make_tuple(std::pair{"deposit", &Account::deposit});
    }
};
}

(void)m.add_type<Account>();
(void)m.add_accessed_members<Account>();

Account acc;
(void)m.invoke_void(acc, "deposit", 50);      // acc.balance() == 50

// 枚举
enum class Color { Red = 1, Green = 2 };
(void)m.add_enum<Color>("Color", {{Color::Red, "Red"}, {Color::Green, "Green"}});
auto ev = m.get_enum<Color>();
ev.value_to_name(Color::Green);                // "Green"

// 继承
struct Base { int a; };
struct Derived : Base { int b; };
(void)m.add_type<Base>();
(void)m.add_field<Base>("a", &Base::a);
(void)m.add_type<Derived>();
(void)m.add_field<Derived>("b", &Derived::b);
(void)m.add_base<Derived, Base>();

Derived d{};
Base* base = m.get_inheritance<Derived>().upcast<Base>(&d);

// 错误处理
auto e = m.add_field<Vec3>("x", &Vec3::x);     // 重复字段
if (e.is_code(om_err_reflect_duplicate)) {
    std::cerr << "duplicate field\\n";
}

operating_message chain;                        // 只保留首错
chain += m.add_field<Vec3>("x", &Vec3::x);
chain += m.add_field<Vec3>("y", &Vec3::y);
\`\`\`

字段数超过 256 时显式注册：

\`\`\`cpp
struct Wide {
    int a;
    int b;
    // 其余字段按同一形态继续列出
};

(void)m.add_type<Wide>();                       // 只建类型, 不自动遍历字段
(void)m.add_fields<Wide>(std::tuple{
    std::pair{"a", &Wide::a},
    std::pair{"b", &Wide::b},
    // 与上方成员一一对应
});
\`\`\`

### 11.15 注意事项

| 用法 | 说明 |
|------|------|
| 未注册类型调用 \`get<T>()\` | 返回 invalid 视图，\`valid()\` 为 false |
| 未注册类型注册字段或方法 | 返回 \`om_err_reflect_type_not_found\`，先 \`add_type<T>()\` |
| \`invoke\` 参数或返回类型不匹配 | 返回 \`om_err_reflect_method_not_found\` |
| 重复注册同名字段 | 返回 \`om_err_reflect_duplicate\` |
| 字段名为空 | 返回 \`om_err_reflect_invalid_name\` |
| 字段数超过 256 | 改用 \`add_type<T>()\` 与 \`add_fields<T>(...)\` 显式注册全部字段 |
| 字段与方法数量 | 按实际注册量增长，不设固定上限 |
| 存储分配失败 | 返回 \`om_err_out_of_memory\` |
| 冻结后调用写接口 | 返回 \`om_err_reflect_frozen\` |
| 未退休就 \`purge_type\` | 返回 \`om_err_reflect_busy\` |
| \`array_element_ptr\` 越界 | 返回 nullptr |
| \`clone\` 的 \`dst\` 已构造 | 用 \`copy_assign\` 替代 |
| \`create_inplace\` 的 \`buf\` 未对齐 | 用 \`alignas(T) char buf[sizeof(T)]\` |
| 类型名、字段名、方法名、枚举项名 | 由调用方传入的字符串必须是静态存储期 |

### 11.16 私有成员注册方式

| 方式 | 适用场景 | 调用方输入 |
|------|---------|-----------|
| \`reflect_access<T>\` 特化 + \`add_accessed_members<T>()\` | 可修改类定义的类型 | 类内 1 行 friend，类外写字段与方法清单 |
| \`add_field_raw\` / \`add_private_offsets\` | 无法修改的第三方类型 | 手填字段名、偏移量、类型 |
| \`add_base_with_adjuster<D, B>(fn)\` | 私有或虚拟继承 | 类内提供指针调整函数 |
| \`set_construct_destruct<T>(create, destroy)\` | 私有构造或析构 | 类内提供工厂与销毁函数 |

\`reflect_access<T>\` 的清单函数返回 \`{名字, 成员指针}\` 元组，字段用 \`add_fields\` 的同一形态，方法用 \`add_methods\`；三者都不存在时 \`add_accessed_members\` 返回 \`om_err_unsupported_operation\`。

---
`
};

window.DOCS_DATA['reflection_meta'] = {
  id: 'reflection_meta',
  title: "reflection — 元数据字段与容量",
  category: 'reflection',
  icon: 'R',
  order: 31,
  content: `## 30. reflection — 元数据字段与容量

命名空间 \`reflect\`。

查询接口返回的元数据结构字段，以及模块容量配置。高层接口见 [§ 11. reflection — 反射模块使用](#11-reflection--反射模块使用)。

### 30.1 元数据字段

\`m.field<T>(name)\` 返回 \`const field_meta*\`：

| 字段 | 说明 |
|------|------|
| \`name\` | 字段名 |
| \`offset\` | 相对对象起始的偏移量（字节） |
| \`type_id\` | 字段类型 id，数组字段为元素类型 id |
| \`flags\` | 位标志，用 \`is_const()\` / \`is_private()\` 查询 |
| \`array_rank\` | 数组维度数，0 为标量 |
| \`total_elements\` | 数组总元素数 |
| \`extents[4]\` | 各维元素数，前 \`array_rank\` 个有效 |
| \`element_stride\` | 元素步长（字节） |
| \`trivially_copyable\` | 字段（数组为元素）是否可平凡复制 |
| \`attrs\` | 字段属性列表，元素含 \`key_hash\` 与 \`value\` |

\`m.method<T>(name)\` 返回 \`const method_meta*\`：

| 字段 | 说明 |
|------|------|
| \`name\` | 方法名 |
| \`arg_count\` | 参数数量 |
| \`return_type_id\` | 返回类型 id，void 为 -1 |
| \`is_const\` / \`is_static\` | const 方法 / 静态方法 |
| \`arg_type_ids\` | 参数类型 id 数组，nullptr 表示未设置 |
| \`vtable_offset\` | 虚函数 vtable 偏移，-1 为非虚 |

\`m.get_enum<E>()\` 的 \`for_each_value\` 回调参数：

| 参数 | 说明 |
|------|------|
| \`value\` | 枚举值，按底层类型存储 |
| \`name\` | 枚举项名 |

### 30.2 容量与配置

| 配置 | 默认值 | 说明 |
|------|--------|------|
| \`REFLECT_MAX_FIELDS\` | 64 | 聚合类型自动遍历的字段数上限，取 16/32/64/128/256 |

字段与方法元数据按实际注册量增长，不设固定数量上限。字段数超过 256 时，用 \`add_type<T>()\` 与 \`add_fields<T>(...)\` 显式注册全部字段，不受自动遍历上限约束。进程级按名类型注册表另有限制，与单类型字段、方法数量无关。

配置在 \`config/reflect_config.hpp\`，可在包含反射头文件前定义同名宏覆盖。

---
`
};
