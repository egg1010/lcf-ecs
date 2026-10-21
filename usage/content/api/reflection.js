window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['reflection_usage'] = {
  id: 'reflection_usage',
  title: "reflection — 反射模块使用",
  category: 'reflection',
  icon: 'R',
  order: 11,
  content: `## 11. reflection — 反射模块使用

命名空间 \`reflect\`。\`noexcept\`。

反射模块提供运行期类型元数据查询、字段访问与方法调用。支持聚合类型自动遍历公有字段、手填偏移量注册私有成员、宏注册成员/静态方法。支持运行时动态注册，注册线程安全。库独立，不创建线程，嵌入外部多线程架构。

### 11.1 注册宏

宏只能在命名空间作用域使用（通过 \`inline\` 变量初始化）。聚合类型字段名自动生成 \`field_0\`、\`field_1\`、…（上限由 \`REFLECT_MAX_FIELDS\` 决定，默认 64）。

| 宏 | 说明 |
|------|------|
| \`REGISTER(Cls)\` | 注册类型：聚合类型自动遍历公有字段；非聚合类型仅注册类型元信息（字段需 \`REFLECT_PRIVATE\` 或 \`REGISTER_MEMBERS\`） |
| \`REGISTER_MEMBERS(Cls, f1, f2, ...)\` | 注册成员（标量/数组统一入口），自动判断字段类别；支持 1~20 个字段 |
| \`REGISTER_FNS(Cls, m1, m2, ...)\` | 批量注册方法（自动判断成员/静态），支持 1~20 个方法 |
| \`REGISTER_FN_OVERLOAD(Cls, method, ptr)\` | 注册重载方法，显式指定方法指针 |
| \`REFLECT(Cls)\` | 类内标记宏，声明 friend 授权反射访问私有成员 |
| \`REFLECT_PRIVATE(Cls, ...)\` | 类外注册私有成员，偏移量和类型由成员指针自动推导；自动注册类型元信息（无需先 \`REGISTER\`） |
| \`REGISTER_PRIVATE_OFFSETS(Cls, ...)\` + \`PRIV_FIELD(name, offset, Type)\` | 手填偏移量注册私有成员，用于无法修改的第三方类型 |
| \`REGISTER_TYPE_ONLY(Cls)\` | 只注册类型元信息，不自动遍历字段。用于无字段类型，或配合 \`register_array_field\` 手动注册 |
| \`REGISTER_ENUM(EnumType, v1, v2, ...)\` | 注册枚举类型，自动生成 \`{E::v, #v}\` 名值对，支持 1~10 个枚举值 |
| \`REGISTER_BASE(Derived, Base)\` | 注册继承关系；指针调整由编译期类型转换承担，单/多继承均适用 |
| \`REGISTER_BASE_OFFSET(Derived, Base, off)\` | 显式指定派生类→基类指针调整量（特殊布局时使用） |
| \`REGISTER_FIELD_ATTR(Cls, field, key, value)\` | 为字段注册属性/注解，\`value\` 任意类型（\`void_any\` 存储） |
| \`REGISTER_CONVERT(From, To)\` | 注册类型转换（要求 \`From\` 可隐式转换到 \`To\`） |

### 11.2 查询入口

查询统一入口，无需区分单/多线程。\`query_view\` 持有 \`type_meta*\`，注册后长期有效。

| 接口 | 说明 |
|------|------|
| \`reflect::get<T>()\` | 按类型查询，未注册触发 \`std::abort()\`，返回 \`query_view\` |
| \`reflect::get_by_name(name)\` | 按类型名查询，未注册触发 \`std::abort()\`，返回 \`query_view\` |
| \`reflect::try_get<T>()\` | 软失败版本，未注册返回 \`query_view::valid()==false\`，不 abort |
| \`reflect::try_get_by_name(name)\` | 软失败版本（按名），不 abort |
| \`reflect::global()\` | 全局存储对象 |

### 11.3 query_view 接口

| 接口 | 说明 |
|------|------|
| \`name()\` | 类型名 |
| \`valid()\` | 是否绑定有效元数据（\`try_get\` 后用于判断是否注册） |
| \`meta()\` | 取底层 \`const type_meta*\`，用于访问构造/继承/容器等扩展字段 |
| \`size()\` | \`sizeof(T)\` |
| \`align()\` | \`alignof(T)\` |
| \`field_count()\` | 字段数 |
| \`method_count()\` | 方法数 |
| \`type_id_value()\` | 类型 id |
| \`field(i)\` | 按索引取字段元数据 |
| \`field_by_name(name)\` | 按名取字段元数据，未找到返回 nullptr |
| \`method(i)\` | 按索引取方法元数据 |
| \`method_by_name(name)\` | 按名取方法元数据，未找到返回 nullptr |
| \`get<R>(obj, i)\` | 按索引取字段引用（编译期偏移） |
| \`get<R>(obj, i)\` (const) | const 对象版本 |
| \`get_by_name<R>(obj, name)\` | 按名取字段引用；字段不存在即终止（带诊断） |
| \`get_by_name<R>(obj, name)\` (const) | const 对象版本 |
| \`try_get_by_name<R>(obj, name)\` | 安全取字段指针，未找到返回 \`nullptr\` |
| \`try_get_by_name<R>(obj, name)\` (const) | const 对象版本 |
| \`get_ptr(obj, name)\` | 按名取字段 \`void*\` |
| \`get_ptr(obj, name)\` (const) | const 对象版本 |
| \`invoke<R>(obj, name, args...)\` | 调用方法，返回 R。参数数量/类型不匹配、返回类型不匹配或未注册均触发 \`std::abort()\` |
| \`try_invoke<R>(obj, name, args...)\` | 软失败版本。\`R=void\` 返回 \`bool\`；非 void 返回 \`std::optional<R>\`。参数或返回类型不匹配即失败，不 abort |
| \`find_overload(name, given_ids, n_args)\` | 按参数类型 id 精确匹配重载，返回 \`const method_meta*\`；无精确匹配返回 nullptr |
| \`get_by_path(obj, path)\` | 按 \`a.b.c\` 嵌套路径取字段 \`void*\`，递归查找子类型；任一级未找到返回 nullptr |
| \`get_by_path(obj, path)\` (const) | const 对象版本 |
| \`get_by_path_as<T>(obj, path)\` | 路径访问并转型为 \`T*\` |
| \`get_by_path_as<T>(obj, path)\` (const) | const 对象版本 |
| \`for_each_field(obj, f)\` | 遍历实例字段，\`f(name, ptr, type_id)\` |
| \`for_each_field(obj, f)\` (const) | const 对象版本 |
| \`for_each_field_meta(f)\` | 遍历字段元数据，\`f(field_meta&)\` |
| \`for_each_method(f)\` | 遍历方法元数据，\`f(method_meta&)\` |
| \`is_array(i)\` | 字段 i 是否为数组 |
| \`is_array_by_name(name)\` | 按名判断字段是否为数组 |
| \`array_rank(i)\` | 数组维度数（0=标量，1~4=数组） |
| \`array_total_elements(i)\` | 数组总元素数（非数组为 0） |
| \`array_element_stride(i)\` | 数组元素步长（字节） |
| \`array_extent(field_idx, dim)\` | 第 \`dim\` 维元素数（\`uint32_t\`） |
| \`array_element_ptr(obj, field_idx, element_idx)\` | 取数组元素指针，越界或非数组返回 nullptr |
| \`array_element_ptr(obj, field_idx, element_idx)\` (const) | const 对象版本 |
| \`array_element_ptr_by_name(obj, name, element_idx)\` | 按名取数组元素指针 |
| \`for_each_array_element(obj, field_idx, f)\` | 遍历数组元素，\`f(ptr, idx, type_id)\` |
| \`for_each_array_element(obj, field_idx, f)\` (const) | const 对象版本 |
| **便捷接口** | |
| \`array_info(field_idx)\` | 聚合查询，返回 \`const field_meta*\`，非数组返回 nullptr |
| \`array_info_by_name(name)\` | 按名聚合查询 |
| \`array_get<T>(obj, field_idx, element_idx)\` | 类型安全访问；越界即终止（带诊断） |
| \`array_get<T>(obj, field_idx, element_idx)\` (const) | const 对象版本 |
| \`array_get_by_name<T>(obj, name, element_idx)\` | 按名类型安全访问；越界或字段不存在即终止 |
| \`try_array_get<T>(obj, field_idx, element_idx)\` | 安全取数组元素指针，越界返回 \`nullptr\` |
| \`try_array_get<T>(obj, field_idx, element_idx)\` (const) | const 对象版本 |
| \`array_set<T>(obj, field_idx, element_idx, value)\` | 类型安全写入 |
| \`array_set_by_name<T>(obj, name, element_idx, value)\` | 按名类型安全写入 |

### 11.4 construct_view — 对象构造/销毁

反射式构造与析构，基于 \`type_meta\` 的默认构造/析构函数指针。需类型可默认构造。

| 接口 | 说明 |
|------|------|
| \`construct_view()\` | 默认构造，未绑定 |
| \`has_default_construct()\` | 类型是否可默认构造 |
| \`create()\` | 堆分配默认构造，返回 \`void*\`；不可构造或未绑定返回 nullptr |
| \`create_inplace(buf)\` | 就地默认构造（\`buf\` 需足够大且对齐），返回 \`void*\` |
| \`destroy(obj)\` | 析构对象（不释放内存，用于就地对象） |
| \`destroy_heap(obj)\` | 析构并释放堆内存，与 \`create()\` 配对（含过对齐类型的对齐释放） |
| \`reflect::get_construct<T>()\` | 按类型取 \`construct_view\` |
| \`reflect::get_construct(q)\` | 由 \`query_view\` 构造 \`construct_view\` |

### 11.5 inheritance_view — 继承关系

查询直接/间接基类与派生类，支持向上转型。需先 \`REGISTER_BASE\` 注册关系。

| 接口 | 说明 |
|------|------|
| \`is_derived_from(base_type_id)\` | 是否继承自指定类型（含间接继承） |
| \`is_derived_from<Base>()\` | 模板版本 |
| \`is_base_of(derived_type_id)\` | 是否是指定类型的基类 |
| \`is_base_of<Derived>()\` | 模板版本 |
| \`upcast(derived_obj, base_type_id)\` | 派生类指针向上转型为基类指针，失败返回 nullptr |
| \`upcast<Base>(derived_obj)\` | 模板版本，返回 \`Base*\` |
| \`base_count()\` | 直接基类数量 |
| \`derived_count()\` | 直接派生类数量 |
| \`base_type_id_at(idx)\` | 取第 idx 个直接基类 type_id，越界返回 -1 |
| \`derived_type_id_at(idx)\` | 取第 idx 个直接派生类 type_id，越界返回 -1 |
| \`for_each_base(f)\` | 遍历直接基类，\`f(base_type_id, offset)\` |
| \`for_each_derived(f)\` | 遍历直接派生类，\`f(derived_type_id)\` |
| \`reflect::get_inheritance<T>()\` | 按类型取 \`inheritance_view\` |
| \`reflect::get_inheritance(q)\` | 由 \`query_view\` 构造 |

### 11.6 enum_view — 枚举反射

枚举值↔名称互转与遍历。需先 \`REGISTER_ENUM\` 注册。

| 接口 | 说明 |
|------|------|
| \`valid()\` | 是否绑定有效枚举元数据 |
| \`name()\` | 枚举类型名 |
| \`type_id_value()\` | 枚举类型 id |
| \`underlying_type_id()\` | 底层整数类型 id |
| \`value_count()\` | 枚举值数量 |
| \`value_to_name(uint64_t)\` | 值→名称，失败返回 nullptr |
| \`value_to_name(E value)\` | 模板版本，接受枚举值 |
| \`name_to_value(name, out)\` | 名称→值（\`uint64_t&\`），失败返回 false |
| \`name_to_value<E>(name, out)\` | 模板版本，\`out\` 为 \`E&\` |
| \`for_each_value(f)\` | 遍历所有枚举值，\`f(uint64_t value, const char* name)\` |
| \`reflect::get_enum<E>()\` | 按枚举类型取 \`enum_view\` |
| \`reflect::get_enum(type_id)\` | 按类型 id 取 \`enum_view\` |

### 11.7 attribute_view — 属性/注解

查询字段附加的键值对属性。需先 \`REGISTER_FIELD_ATTR\` 注册。

| 接口 | 说明 |
|------|------|
| \`has_attr(field_idx, key)\` | 按字段索引判断是否含指定属性 |
| \`has_attr(field_name, key)\` | 按字段名判断 |
| \`get_attr(field_idx, key)\` | 取属性值 \`const void_any*\`，失败返回 nullptr |
| \`get_attr(field_name, key)\` | 按字段名取 |
| \`get_attr_as<V>(field_idx, key)\` | 类型安全取属性值，返回 \`const V*\` |
| \`get_attr_as<V>(field_name, key)\` | 按字段名类型安全取 |
| \`for_each_attr(field_idx, f)\` | 遍历字段所有属性，\`f(key_hash, void_any&)\` |
| \`reflect::get_attributes<T>()\` | 按类型取 \`attribute_view\` |
| \`reflect::get_attributes(q)\` | 由 \`query_view\` 构造 |

### 11.8 container_view — 容器反射

统一访问顺序容器（\`dense<T>\`/\`std::vector\` 等）的元素。类型注册时自动登记容器特征，无需额外调用。

| 接口 | 说明 |
|------|------|
| \`valid()\` | 是否绑定有效容器与 ops |
| \`is_container()\` | ops 是否非空 |
| \`category()\` | 容器类别（\`sequential\`/\`none\`） |
| \`element_type_id()\` | 元素类型 id |
| \`size()\` | 元素数 |
| \`get(index)\` | 取元素 \`void*\`，越界返回 nullptr |
| \`get_as<T>(index)\` | 类型安全取元素 |
| \`push_back(element)\` | 追加元素（\`const void*\`） |
| \`push_back<T>(value)\` | 模板版本 |
| \`clear()\` | 清空容器 |
| \`reserve(n)\` | 预留容量 |
| \`for_each(f)\` | 遍历元素，\`f(void* element, size_t index)\` |
| \`reflect::as_container(obj, type_id)\` | 由对象指针与类型 id 构造 \`container_view\` |
| \`reflect::as_container(q, obj)\` | 由 \`query_view\`（字段类型）与对象指针构造 |

### 11.9 virtual_dispatch_view — 动态派发

查询方法是否虚函数及 vtable 偏移。通过 \`register_method\` 注册的虚函数，invoker 已正确处理 vtable，可直接 \`invoke_virtual\` 调用。

| 接口 | 说明 |
|------|------|
| \`is_virtual(method_name)\` | 方法是否为虚函数 |
| \`vtable_offset(method_name)\` | 虚函数 vtable 偏移（非虚返回 -1） |
| \`invoke_virtual<R>(obj, name, args...)\` | 调用虚函数（复用重载解析），未找到触发 \`std::abort()\` |
| \`reflect::get_virtual_dispatch<T>()\` | 按类型取 \`virtual_dispatch_view\` |
| \`reflect::get_virtual_dispatch(q)\` | 由 \`query_view\` 构造 |

### 11.10 convert_view — 类型转换

查询与执行已注册的类型转换。需先 \`REGISTER_CONVERT\` 注册。

| 接口 | 说明 |
|------|------|
| \`can_convert_to(target_type_id)\` | 是否可转换到目标类型 |
| \`can_convert_to<U>()\` | 模板版本 |
| \`convert_to(src, target_type_id, dst)\` | 执行转换写入 \`dst\`，失败返回 false |
| \`convert_to<U>(src)\` | 类型安全转换，返回 \`std::optional<U>\` |
| \`for_each_convertible(f)\` | 遍历所有可转换目标，\`f(target_type_id)\` |
| \`reflect::get_convert<T>()\` | 按类型取 \`convert_view\` |
| \`reflect::get_convert(q)\` | 由 \`query_view\` 构造 |

### 11.11 compare_view — 字段比较/克隆

逐字段相等比较、克隆、赋值与析构。数组字段逐元素处理；未登记字段操作表的字段回退到 \`memcmp\`/\`memcpy\`。

| 接口 | 说明 |
|------|------|
| \`equal(a, b)\` | 逐字段相等比较（\`a==b\` 返回 true；其一为 nullptr 返回 false） |
| \`clone(src, dst)\` | 逐字段拷贝构造（\`dst\` 必须是未初始化内存） |
| \`copy_assign(src, dst)\` | 逐字段赋值（\`dst\` 已构造） |
| \`destroy_fields(obj)\` | 逐字段析构（对象销毁前调用） |
| \`reflect::get_compare<T>()\` | 按类型取 \`compare_view\` |
| \`reflect::get_compare(q)\` | 由 \`query_view\` 构造 |

### 11.12 hash_view — 字段哈希

逐字段 FNV-1a 哈希组合。已登记字段操作表的字段用其哈希函数计算，否则按字节级 FNV-1a。

| 接口 | 说明 |
|------|------|
| \`hash(obj)\` | 计算对象哈希，返回 \`uint64_t\`；未绑定或 \`obj\` 为空返回 0 |
| \`reflect::get_hash<T>()\` | 按类型取 \`hash_view\` |
| \`reflect::get_hash(q)\` | 由 \`query_view\` 构造 |

### 使用

\`\`\`cpp

// === 聚合类型 (自动遍历公有字段) ===
struct Vec3 { float x, y, z; };
struct Pod16 { int a, b, c, d; };

REGISTER(Vec3);
REGISTER(Pod16);

// === 非聚合类型 (侵入式自动推导) ===
class Account {
    std::string name_;
    int balance_;
public:
    REFLECT(Account);  // 类内: friend 授权
    Account() : name_(""), balance_(0) {}
    Account(std::string n, int b) : name_(n), balance_(b) {}
    void deposit(int amt) { balance_ += amt; }
    int get_balance() const { return balance_; }
    static int version() { return 42; }
};

REGISTER(Account);

// 类外: 只写字段名, 偏移量和类型自动推导
REFLECT_PRIVATE(Account, name_, balance_);

REGISTER_FNS(Account, deposit, get_balance, version);  // 成员 + 静态 (自动判断)

// 重载方法示例 (显式指定方法指针)
struct Calc {
    int add(int a, int b) { return a + b; }
    int add(int a, int b, int c) { return a + b + c; }
};
REGISTER(Calc);
REGISTER_FN_OVERLOAD(Calc, add,
    static_cast<int(Calc::*)(int,int)>(&Calc::add));
REGISTER_FN_OVERLOAD(Calc, add,
    static_cast<int(Calc::*)(int,int,int)>(&Calc::add));

// === 字段访问 ===
Vec3 v{1.0f, 2.0f, 3.0f};
auto view = reflect::get<Vec3>();

view.get<float>(&v, 0);                    // 1.0 (按索引)
view.get_by_name<float>(&v, "field_1");    // 2.0 (按名)
view.get<float>(&v, 0) = 10.0f;            // 修改字段

// 私有成员访问
Account acc{"Alice", 100};
auto acc_view = reflect::get<Account>();
int bal = acc_view.get_by_name<int>(&acc, "balance_");  // 100
acc_view.get_by_name<int>(&acc, "balance_") = 200;      // 修改私有成员

// === 方法调用 ===
acc_view.invoke<void>(&acc, "deposit", 50);  // void 返回
int bal2 = acc_view.invoke<int>(&acc, "get_balance");  // 250
int ver  = acc_view.invoke<int>(nullptr, "version");   // 42 (静态方法)

// === 遍历字段 ===
view.for_each_field(&v, [](const char* name, void* ptr, int tid) {
    std::cout << name << " = " << *static_cast<float*>(ptr) << "\\n";
});

view.for_each_field_meta([](const reflect::field_meta& fm) {
    std::cout << fm.name << " offset=" << fm.offset << "\\n";
});

// === 按类型名查询 ===
auto v2 = reflect::get_by_name("Vec3");

// === 运行时动态注册 (线程安全) ===
struct Dynamic { int a, b; };
reflect::global().register_type<Dynamic>("Dynamic");
auto dv = reflect::get<Dynamic>();

// === 注册成员 (REGISTER_MEMBERS 统一入口) ===
struct Path { float points[16]; };
struct Grid { float cells[8][8]; };

REGISTER_MEMBERS(Path, points);    // 数组字段
REGISTER_MEMBERS(Grid, cells);     // 二维数组字段

// 多个成员: REGISTER_MEMBERS 批量注册
struct Mesh { float vertices[8]; int indices[16]; float transform[16]; };
REGISTER_MEMBERS(Mesh, vertices, indices, transform);  // 3 个字段

// 混合类型 (标量 + 数组): REGISTER_MEMBERS 批量
struct Mixed { int id; float pos[3]; };
REGISTER_MEMBERS(Mixed, id, pos);  // id 标量, pos 数组

// 聚合类型自定义字段名: REGISTER_MEMBERS 批量
struct Color { float r, g, b, a; };
REGISTER_MEMBERS(Color, r, g, b, a);  // 4 个字段

// 方法注册: REGISTER_FNS 批量
struct Calculator {
    int add(int a, int b) { return a + b; }
    static int multiply(int a, int b) { return a * b; }
};
REGISTER_FNS(Calculator, add, multiply);  // 成员 + 静态

Path p{};
auto pv = reflect::get<Path>();

// 元数据查询
pv.is_array(0);                       // true
pv.array_rank(0);                     // 1
pv.array_total_elements(0);           // 16
pv.array_element_stride(0);           // 4
pv.array_extent(0, 0);                // 16

// 元素访问 (按索引/按名)
float* e = static_cast<float*>(pv.array_element_ptr(&p, 0, 5));
float* e2 = static_cast<float*>(pv.array_element_ptr_by_name(&p, "points", 10));

// 便捷访问 (类型安全)
float val = pv.array_get<float>(&p, 0, 5);           // 读
pv.array_set<float>(&p, 0, 5, 42.0f);                // 写
float val_n = pv.array_get_by_name<float>(&p, "points", 3);  // 按名读
pv.array_set_by_name<float>(&p, "points", 8, 99.0f);         // 按名写

// 聚合查询
if (const auto* info = pv.array_info(0)) {
    // info->array_rank / info->total_elements / info->element_stride / info->extents[0]
}

// 遍历数组元素
pv.for_each_array_element(&p, 0, [](void* ptr, uint32_t idx, int tid) {
    *static_cast<float*>(ptr) = static_cast<float>(idx);
});

// 二维数组
Grid g{};
auto gv = reflect::get<Grid>();
gv.array_rank(0);                     // 2
gv.array_total_elements(0);           // 64
gv.array_extent(0, 0);                // 8
gv.array_extent(0, 1);                // 8

// === #3 枚举反射 ===
enum class Element { Fire, Water, Earth, Air };
REGISTER_ENUM(Element, Fire, Water, Earth, Air);  // 自动生成名值对

auto ev = reflect::get_enum<Element>();
ev.value_count();                                    // 4
ev.value_to_name(Element::Fire);                     // "Fire"
Element out{};
ev.name_to_value("Air", out);                        // true, out == Element::Air
ev.for_each_value([](uint64_t v, const char* name) {
    std::cout << name << " = " << v << "\\n";
});

// === #2 继承关系 ===
struct Base { int base_val; };
struct Derived : Base { int derived_val; };
REGISTER(Base);
REGISTER_TYPE_ONLY(Derived);
REGISTER_PRIVATE_OFFSETS(Derived,
    PRIV_FIELD("base_val",    offsetof(Derived, base_val),    int),
    PRIV_FIELD("derived_val", offsetof(Derived, derived_val), int));
REGISTER_BASE(Derived, Base);   // 注册继承关系（指针调整自动）

auto iv = reflect::get_inheritance<Derived>();
iv.base_count();                       // 1
iv.is_derived_from<Base>();            // true
Derived d{};
d.base_val = 10;
Base* b = iv.upcast<Base>(&d);         // 向上转型
std::cout << b->base_val;              // 10

auto ivb = reflect::get_inheritance<Base>();
ivb.derived_count();                   // 1
ivb.is_base_of<Derived>();             // true

// === #4 字段属性/注解 ===
struct Player { int hp; int mp; float x; };
REGISTER_TYPE_ONLY(Player);
REGISTER_MEMBERS(Player, hp, mp, x);
REGISTER_FIELD_ATTR(Player, hp, "range_min", 0);       // int 属性
REGISTER_FIELD_ATTR(Player, hp, "range_max", 100);
REGISTER_FIELD_ATTR(Player, hp, "category", "Combat");  // 字符串属性

auto av = reflect::get_attributes<Player>();
av.has_attr("hp", "range_min");                       // true
const int* min_val = av.get_attr_as<int>("hp", "range_min");   // *min_val == 0
const int* max_val = av.get_attr_as<int>("hp", "range_max");   // *max_val == 100

// === #7 类型转换 ===
REGISTER_TYPE_ONLY(int);
REGISTER_TYPE_ONLY(float);
REGISTER_CONVERT(int, int64_t);     // int → int64_t
REGISTER_CONVERT(float, double);    // float → double

auto cv = reflect::get_convert<int>();
cv.can_convert_to<int64_t>();       // true
int src = 42;
int64_t dst{};
cv.convert_to(&src, type_id::get_type_id<int64_t>(), &dst);  // dst == 42
auto result = cv.convert_to<int64_t>(&src);  // std::optional<int64_t>, *result == 42

// === #1 对象构造/销毁 ===
struct Constructable { int a; float b; };
REGISTER(Constructable);

auto ctv = reflect::get_construct<Constructable>();
ctv.has_default_construct();        // true
void* obj = ctv.create();           // 堆分配默认构造
// ... 使用 obj ...
ctv.destroy_heap(obj);              // 析构并释放

alignas(Constructable) char buf[sizeof(Constructable)];
void* obj2 = ctv.create_inplace(buf);  // 就地构造
ctv.destroy(obj2);                     // 仅析构（不释放）

// === #8 字段比较/克隆 ===
struct CompareTest { int a; float b; };
REGISTER(CompareTest);

auto cmpv = reflect::get_compare<CompareTest>();
CompareTest a{1, 2.0f}, b{1, 2.0f}, c{1, 3.0f};
cmpv.equal(&a, &b);                 // true
cmpv.equal(&a, &c);                 // false

alignas(CompareTest) char cbuf[sizeof(CompareTest)];
cmpv.clone(&a, cbuf);               // 拷贝构造到未初始化内存
cmpv.copy_assign(&a, &c);           // 逐字段赋值 (c 已构造)
cmpv.destroy_fields(&c);            // 逐字段析构

// === #9 字段哈希 ===
auto hv = reflect::get_hash<CompareTest>();
uint64_t ha = hv.hash(&a);          // FNV-1a 组合哈希
uint64_t hb = hv.hash(&b);          // ha == hb (字段值相同)

// === #11 字段路径访问 (嵌套) ===
struct Inner { int value; };
struct Outer { Inner inner; };
REGISTER_TYPE_ONLY(Inner);
REGISTER_MEMBERS(Inner, value);
REGISTER_TYPE_ONLY(Outer);
REGISTER_MEMBERS(Outer, inner);

Outer obj{};
obj.inner.value = 42;
auto q = reflect::get<Outer>();
void* p = q.get_by_path(&obj, "inner.value");   // 指向 obj.inner.value
int* ip = q.get_by_path_as<int>(&obj, "inner.value");  // *ip == 42

// === #12 重载按类型匹配 ===
auto q2 = reflect::get<Calculator>();
int ids[] = { type_id::get_type_id<int>(), type_id::get_type_id<int>() };
const reflect::method_meta* m = q2.find_overload("add", ids, 2);  // 精确匹配 add(int,int)

// === #5 容器反射 ===
reflect::global().register_type_only<dense<int>>("dense<int>");  // 类型注册时自动登记容器特征

dense<int> vec;
vec.push_back(10);
vec.push_back(20);
auto ccv = reflect::as_container(&vec, type_id::get_type_id<dense<int>>());
ccv.valid();                // true
ccv.size();                 // 2
int* p0 = ccv.get_as<int>(0);  // *p0 == 10
ccv.for_each([](void* elem, size_t idx) {
    std::cout << idx << ": " << *static_cast<int*>(elem) << "\\n";
});
ccv.push_back<int>(30);     // 追加元素
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 在函数内部使用 \`REGISTER\` 等注册宏 | \`inline\` 变量不能在块作用域声明 | 宏在命名空间作用域使用；函数内直接调 \`reflect::global().register_type<T>(name)\` |
| 未注册类型就查询 | \`std::abort()\` | 先 \`REGISTER(T)\` 再 \`reflect::get<T>()\` |
| 未注册私有成员就访问 | \`field_by_name\` 返回 nullptr，解引用崩溃 | 先 \`REFLECT_PRIVATE\` 或 \`REGISTER_PRIVATE_OFFSETS\` |
| \`REFLECT_PRIVATE\` 字段名写错 | 编译错误（成员不存在） | 字段名与类定义一致 |
| \`REGISTER_PRIVATE_OFFSETS\` 偏移量填错 | 访问错误内存 | 优先用 \`REFLECT_PRIVATE\` 自动推导；第三方类型用 \`offsetof\` 确认 |
| 方法参数数量不匹配 | \`std::abort()\` | 调用 \`invoke\` 时参数数量与方法签名一致 |
| 静态方法传非空 \`obj\` | 被忽略，不影响结果 | 静态方法传 \`nullptr\` |
| 字段数超过 \`MAX_FIELDS_PER_TYPE\`（256） | \`std::abort()\` | 单类型字段数不超过上限 |
| 方法数超过 \`MAX_METHODS_PER_TYPE\`（256） | \`std::abort()\` | 单类型方法数不超过上限 |
| 类型 id 超过 \`MAX_TYPE_ID\`（65536） | 注册被忽略 | 类型总数不超过上限 |
| 含 C 数组字段的类型直接用 \`REGISTER\` | 结构化绑定展开数组为多个标量字段，字段计数错误 | 改用 \`REGISTER_MEMBERS\`（自动判断标量/数组 + 推导数组元数据） |
| \`REGISTER_MEMBERS\` 数组维度数超过 4 | 注册被忽略 | 维度数限制 1~4，更高维度需拆分为结构体嵌套 |
| \`array_element_ptr\` 越界访问 | 返回 nullptr | \`element_idx\` 须 < \`array_total_elements\`；\`array_get\` 越界会带诊断终止 |
| 认为必须先注册类型再注册成员/方法/属性/继承/转换 | 无需如此 | 这些注册会自动创建类型元信息，顺序无关 |
| 未 \`REGISTER_BASE\` 就调用 \`upcast\` | 返回 nullptr | 先 \`REGISTER_BASE(Derived, Base)\` 注册继承关系 |
| 未 \`REGISTER_ENUM\` 就调用 \`get_enum\` | \`enum_view::valid()==false\` | 先 \`REGISTER_ENUM(E, ...)\` 注册枚举 |
| 未 \`REGISTER_FIELD_ATTR\` 就调用 \`get_attr\` | 返回 nullptr | 先 \`REGISTER_FIELD_ATTR\` 注册属性 |
| 未 \`REGISTER_CONVERT\` 就调用 \`convert_to\` | 返回 false/\`nullopt\` | 先 \`REGISTER_CONVERT(From, To)\` 注册转换 |
| \`convert_to\` 的 \`From\` 不可隐式转换到 \`To\` | 编译错误 | \`REGISTER_CONVERT\` 要求 \`is_convertible_v<From, To>\` |
| \`clone\` 的 \`dst\` 已构造 | 内存泄漏/双重析构 | \`clone\` 仅用于未初始化内存；已构造对象用 \`copy_assign\` |
| \`create_inplace\` 的 \`buf\` 未对齐 | placement new 未对齐访问崩溃 | \`alignas(T) char buf[sizeof(T)]\` |
| 容器类型未注册就用 \`as_container\` | \`container_view::valid()==false\` | 先 \`REGISTER\`/\`register_type_only\` 注册类型（容器特征随之登记） |
| \`get_by_path\` 路径中间字段未注册子类型 | 返回 nullptr | 中间字段类型也需 \`REGISTER\`/\`REGISTER_MEMBERS\` |
| \`REGISTER_ENUM\` 枚举值超过 10 个 | 宏展开错误 | 超过 10 个值时直接调 \`reflect::global().register_enum<E>(name, {...})\` |

### 私有成员注册方式选择

| 方式 | 适用场景 | 用户输入 |
|------|---------|---------|
| \`REFLECT\` + \`REFLECT_PRIVATE\` | 可修改类定义的类型 | 类内 1 行 friend，类外只写字段名 |
| \`REGISTER_PRIVATE_OFFSETS\` | 无法修改的第三方类型 | 手填字段名、偏移量、类型 |

\`REFLECT_PRIVATE\` 通过成员指针自动推导偏移量和类型，编译期保证正确性，优先使用。

---
`
};

window.DOCS_DATA['reflection_meta'] = {
  id: 'reflection_meta',
  title: "reflection — 反射元数据与存储",
  category: 'reflection',
  icon: 'R',
  order: 31,
  content: `## 30. reflection — 反射元数据与存储

命名空间 \`reflect\`。\`noexcept\`。

反射模块元数据结构与存储接口。高层使用接口见 [§ 11. reflection — 反射模块使用](#11-reflection--反射模块使用)。

### 30.1 元数据结构

| 结构 | 字段 |
|------|------|
| \`field_meta\` | \`name\`, \`offset\`, \`type_id\`, \`is_const\`, \`is_private\`, \`array_rank\`, \`reserved\`, \`total_elements\`, \`extents[4]\`（\`uint32_t\`）, \`element_stride\`, \`trivially_copyable\`, \`attrs\`（\`dense<attr_entry>\`，#4 属性列表） |
| \`method_meta\` | \`name\`, \`arg_count\`, \`return_type_id\`, \`invoker\`, \`is_const\`, \`is_static\`, \`arg_type_ids\`（\`const int*\`，#12 参数类型 id 数组，nullptr=未设置）, \`vtable_offset\`（#6 虚函数偏移，-1=非虚） |
| \`type_meta\` | \`name\`, \`registered\`, \`field_count\`, \`method_count\`, \`size\`/\`align\`（\`size_t\`）, \`type_id\`, \`fields\`, \`methods\`, \`default_construct_\`/\`destruct_\`/\`free_heap_\`/\`has_default_construct\`（#1）, \`base_offsets\`（#2）, \`derived_type_ids\`（#2）, \`converters\`（#7）, \`container_ops\`（#5） |
| \`attr_entry\` | \`key_hash\`（FNV-1a(key)）, \`value\`（\`void_any\` 类型擦除值） |
| \`base_offset_entry\` | \`base_type_id\`, \`offset\`（派生类→基类指针调整量）, \`adjust_fn\`（类型化指针调整，优先于 \`offset\`） |
| \`convert_entry\` | \`target_type_id\`, \`convert_fn\`（\`void(*)(const void* src, void* dst)\`） |
| \`enum_value_entry\` | \`value\`（\`uint64_t\`，underlying type 存储）, \`name\`, \`name_hash\`（FNV-1a(name)） |
| \`enum_meta\` | \`name\`, \`underlying_type_id\`, \`type_id\`, \`values\`（\`dense<enum_value_entry>\`）, \`registered\` |
| \`MAX_TYPE_ID\` | 类型槽位上限（65536） |
| \`MAX_FIELDS_PER_TYPE\` | 单类型字段上限（256） |
| \`MAX_METHODS_PER_TYPE\` | 单类型方法上限（256） |

\`field_meta\` 数组字段：\`array_rank=1~4\`、\`extents[0..rank-1]\` 为各维元素数、\`total_elements\` 为总元素数、\`element_stride\` 为元素步长（字节）。标量字段 \`array_rank=0\`。

\`type_meta\` 扩展字段说明：
- \`default_construct_\`/\`destruct_\`（#1）：默认构造与析构函数指针，\`has_default_construct\` 标记是否可默认构造。注册类型时若 \`is_default_constructible_v<T>\` 自动填充。
- \`base_offsets\`/\`derived_type_ids\`（#2）：直接基类与派生类列表，通过 \`register_base\` 填充。
- \`converters\`（#7）：已注册的目标类型转换表，通过 \`register_convert\` 填充。
- \`container_ops\`（#5）：容器操作表指针，顺序容器类型注册时自动填充。

### 30.2 storage 接口

| 接口 | 说明 |
|------|------|
| \`storage::register_type<T>(name)\` | 注册类型，聚合类型自动遍历公有字段 |
| \`storage::register_type_only<T>(name)\` | 只注册类型元信息，不自动遍历字段。用于无字段类型或配合 \`register_array_field\` 手动注册 |
| \`storage::register_private_offsets<T>(descs, count)\` | 手填偏移量注册私有成员 |
| \`storage::register_field<T, M, Ptr>(name)\` | 成员指针注册字段，需先 \`register_type_only\` |
| \`storage::register_member_auto<T, M, Ptr>(type_name, field_name)\` | 注册成员（标量/数组统一入口），自动判断字段类别 |
| \`storage::register_array_field<T, M, Ptr>(name, rank, extents, element_type_id)\` | 注册数组字段（C 数组/std::array），\`extents\` 为 \`const uint32_t*\` |
| \`storage::register_array_field_auto<T, M, Ptr>(name)\` | 注册数组字段，自动推导 rank/extents/element_type |
| \`storage::register_method<Fn>(name)\` | 注册成员方法 |
| \`storage::register_static_method<C, Fn>(name)\` | 注册静态方法 |
| \`storage::get_type(tid)\` | 按类型 id 查询 \`type_meta*\` |
| \`storage::find_type(name)\` | 按类型名查询 \`type_meta*\` |
| \`storage::register_enum<E>(name, values)\` | 注册枚举类型（#3），\`values\` 为 \`std::initializer_list<std::pair<E, const char*>>\` |
| \`storage::find_enum(tid)\` | 按类型 id 查询 \`enum_meta*\` |
| \`storage::find_enum<E>()\` | 按枚举类型查询 \`enum_meta*\` |
| \`storage::register_base<Derived, Base>(offset)\` | 注册继承关系（#2），\`offset\` 为派生类→基类指针调整量；另有 \`register_base_typed<Derived, Base>()\` 由编译期类型转换自动调整 |
| \`storage::register_field_attr<T, M, Ptr, V>(field_name, attr_key, value)\` | 注册字段属性（#4），\`value\` 任意类型（\`void_any\` 存储） |
| \`storage::register_convert<T, U>()\` | 注册类型转换（#7），要求 \`is_convertible_v<T, U>\` |
| \`reflect::global()\` | 全局 storage 对象 |

---
`
};
