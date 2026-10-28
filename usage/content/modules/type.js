window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['void_any'] = {
  id: 'void_any',
  title: "void_any — 类型擦除存储",
  category: 'type',
  icon: 'A',
  order: 17,
  content: `## 16. void_any — 类型擦除存储

无命名空间。所有接口 \`noexcept\`。类型擦除容器，可存储任意类型对象，运行时通过 \`get_ptr<T>()\` 按类型安全取值。

\`void_any\` 是类模板 \`define_void_any<SsoSize, SsoAlign>\` 的预设别名，SSO 缓冲区大小与对齐由模板参数编译期确定。需要更大内联存储时可直接实例化模板。

**模板参数：**

| 参数 | 类型 | 约束 | 说明 |
|------|------|------|------|
| \`SsoSize\` | \`size_t\` | 8 的倍数 | SSO 缓冲区字节数，决定内联存储上限 |
| \`SsoAlign\` | \`size_t\` | ≥ 8 | SSO 缓冲区对齐值 |

**预设别名：**

| 别名 | 模板实参 | 说明 |
|------|---------|------|
| \`void_any\` | \`define_void_any<56, 8>\` | 默认通用别名 |

**\`SsoSize\` 取值参考（\`SsoAlign\` 保持 8）：**

| \`SsoSize\` | \`sizeof\` | 内联存储上限 | 适用场景 |
|-----------|----------|------------|---------|
| \`8\` | 16B | ≤ 8B | 仅存小标量（int/double/指针） |
| \`24\` | 32B | ≤ 24B | 小对象，内存敏感场景 |
| \`56\` | 64B | ≤ 56B | 默认，通用场景 |
| \`120\` | 128B | ≤ 120B | 需内联存储较大对象（如 \`std::string\`） |
| \`248\` | 256B | ≤ 248B | 大对象内联存储 |

> 注：\`sizeof\` = \`SsoSize\` + 8（类型标记字段）。超出内联上限的对象自动转为堆分配，功能不受影响。

**\`SsoAlign\` 取值参考：**

| \`SsoAlign\` | 说明 |
|------------|------|
| \`8\` | 默认，适配大多数类型 |
| \`16\` | 需要 16 字节对齐的类型（如部分 SIMD 类型） |
| \`32\` | 需要 32 字节对齐的类型（如 AVX 向量） |

> 注：\`SsoAlign\` 过大可能增加 \`sizeof\`（padding 填充），无特殊对齐需求时保持 \`8\`。

### 构造与赋值

| 接口 | 说明 |
|------|------|
| \`void_any()\` | 默认构造，空值 |
| \`void_any(T&&)\` | 从任意类型构造（排除 \`void_any\` 自身） |
| \`void_any(const void_any&)\` | 拷贝构造 |
| \`void_any(void_any&&)\` | 移动构造 |
| \`operator=(const void_any&)\` | 拷贝赋值 |
| \`operator=(void_any&&)\` | 移动赋值 |

### 访问与操作

| 接口 | 说明 |
|------|------|
| \`set(T&&)\` | 设置新值（同类型直接覆盖；不同类型先析构旧值再构造新值） |
| \`type_id()\` | 获取类型 ID（空值返回 -1；仅用于同类型一致性比较，不等于 \`type_id::get_type_id<T>()\`） |
| \`get_ptr<T>()\` | 获取指针（带类型检查，不匹配返回 nullptr） |
| \`get_ptr<T>() const\` | const 版本 |
| \`fast_get_ptr<T>()\` | 获取指针（跳过 type_id 检查） |
| \`fast_get_ptr<T>() const\` | const 版本 |
| \`get_ptr_unchecked<T>()\` | 获取指针（不验证 has_value 和 type_id） |
| \`get_ptr_unchecked<T>() const\` | const 版本 |
| \`get<T>()\` | 获取值副本（空值或类型不匹配返回默认构造） |
| \`get_void()\` | 获取 \`void*\`（空值返回 nullptr） |
| \`get_void() const\` | const 版本 |
| \`copy_from<T>(const T&)\` | 拷贝赋值（编译期已知 T） |
| \`move_from<T>(T&&)\` | 移动赋值（编译期已知 T） |
| \`has_value()\` | 是否有值 |
| \`reset()\` | 清空（析构并置空） |
| \`swap(void_any&)\` | 同布局交换（相同 \`SsoSize\`/\`SsoAlign\` 的两个 \`void_any\` 交换内容） |
| \`cross_layout_swap(define_void_any<SsoSize2, SsoAlign2>&)\` | 跨布局交换（不同 \`SsoSize\`/\`SsoAlign\` 的 \`void_any\` 交换内容，空方接管对方值） |

> 注：判断存储类型是否为 T 时，使用 \`get_ptr<T>() != nullptr\`，不要用 \`type_id() == type_id::get_type_id<T>()\`。

### 使用

\`\`\`cpp

// 基本构造
void_any a(42);                     // 存储 int
void_any b(std::string("hello"));   // 存储 std::string
void_any c;                         // 空值

// 状态查询
a.has_value();                      // true
c.has_value();                      // false
a.type_id();                        // 类型标识 (空值返回 -1)

// 取值 (带类型检查)
int* pi = a.get_ptr<int>();         // 匹配, 返回指针
double* pd = a.get_ptr<double>();   // 不匹配, 返回 nullptr
int val = a.get<int>();             // 获取值副本 (空值或类型不匹配返回默认构造)

// void* 访问
void* vp = a.get_void();            // 返回 void* (空值返回 nullptr)

// 设置新值 (同类型直接覆盖; 不同类型先析构旧值再构造新值)
a.set(99);                          // int 42 → 99 (同类型覆盖)
a.set(std::string("world"));        // int 99 → std::string (不同类型, 析构+构造)

// 清空
a.reset();                          // 析构并置空
a.has_value();                      // false

// 拷贝与移动
void_any src(std::string("data"));
void_any cp(src);                   // 拷贝构造
void_any mv(std::move(src));        // 移动构造 (src 变空)

void_any dst;
dst = cp;                           // 拷贝赋值
dst = std::move(mv);                // 移动赋值 (mv 变空)
\`\`\`

### 编译期已知类型的接口

\`\`\`cpp
void_any a;

// copy_from / move_from: 编译期已知 T, 无需运行时类型推导
a.copy_from(42);                    // 拷贝 int
a.move_from(std::string("x"));      // 移动 string

// fast_get_ptr: 跳过 type_id 检查 (需调用方确保类型正确)
int* p = a.fast_get_ptr<int>();

// get_ptr_unchecked: 不验证 has_value 和 type_id (需调用方确保值存在)
int* pu = a.get_ptr_unchecked<int>();
\`\`\`

### 存储自定义类型

\`\`\`cpp
struct player {
    int hp;
    int mp;
};

void_any a(player{100, 50});
player* p = a.get_ptr<player>();
if (p) {
    p->hp -= 10;
}
\`\`\`

### 在容器中存储

\`\`\`cpp

dense<void_any> bag;
bag.emplace_back(42);
bag.emplace_back(std::string("item"));
bag.emplace_back(3.14);

// 遍历并按类型取值
for (size_t i = 0; i < bag.size(); ++i) {
    if (auto* p = bag[i].get_ptr<int>()) {
        // 处理 int
    } else if (auto* p = bag[i].get_ptr<std::string>()) {
        // 处理 string
    } else if (auto* p = bag[i].get_ptr<double>()) {
        // 处理 double
    }
}
\`\`\`

### 类型一致性比较

\`\`\`cpp
void_any x(1), y(2), z(std::string("a"));
x.type_id() == y.type_id();        // true, 同为 int
x.type_id() == z.type_id();        // false, int 与 string 不同
\`\`\`

> 注：\`type_id()\` 仅用于同类型一致性比较，不等于 \`type_id::get_type_id<T>()\`。判断存储类型是否为 T 时，使用 \`get_ptr<T>() != nullptr\`。

### 自定义 SSO 配置

\`\`\`cpp

// 默认 void_any: 56B SSO, 可内联存储 ≤56B 的对象
void_any a(42);

// 自定义更大 SSO 缓冲, 内联存储更大对象避免堆分配
// 例如需要内联存储 100B 的结构体
struct big_data { char buf[100]; };

using my_void_any = define_void_any<120, 8>;   // 120B SSO
my_void_any b(big_data{});

// 自定义对齐值 (需 ≥ 8)
using aligned_any = define_void_any<56, 16>;   // 16 字节对齐
aligned_any c(3.14);

// 注意: 不同模板实参实例化的类型不兼容, 不能互相赋值
// my_void_any x = a;   // 编译错误
\`\`\`

### 跨布局交换

\`\`\`cpp

// 不同 SsoSize 的 void_any 之间交换内容
void_any a(42);                              // define_void_any<56, 8>
define_void_any<120, 8> b(std::string("hi")); // 120B SSO

// 跨布局交换: a 接收 string, b 接收 int
a.cross_layout_swap(b);

// 双方类型和值都已互换
auto* pa = a.get_ptr<std::string>();   // 匹配
auto* pb = b.get_ptr<int>();           // 匹配

// 空值参与: 空方接管对方的值
void_any empty;
void_any has_val(99);
empty.cross_layout_swap(has_val);     // empty 持有 99, has_val 变空

// 同布局交换
void_any x(1), y(2);
x.swap(y);                             // x=2, y=1
\`\`\`

> 注：\`cross_layout_swap\` 用于不同 \`SsoSize\`/\`SsoAlign\` 实例化的 \`void_any\` 之间交换内容。双方任一为空时，空方接管对方的值。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 使用 \`get_ptr_unchecked\` 前不检查 \`has_value()\` 和类型 | 返回悬垂指针，未定义行为 | 仅在确定类型和值存在时使用，否则用 \`get_ptr<T>()\` |
| 依赖 \`get<T>()\` 返回默认值来判断类型 | 默认构造值可能与实际值相同 | 先用 \`get_ptr<T>()\` 检查指针是否为空 |
| 移动后继续使用 | 移动后源对象为空 | 移动后仅可调用 \`reset()\` 或重新赋值 |
| 在 \`set()\` 之前访问 | \`has_value()\` 为 false，get_ptr 返回 nullptr | 先 \`set()\` 或构造时传值 |
| 用 \`type_id() == type_id::get_type_id<T>()\` 判断类型 | 两者不相等 | 用 \`get_ptr<T>() != nullptr\` 判断类型 |
| \`SsoSize\` 非 8 的倍数 | \`static_assert\` 编译失败 | \`SsoSize\` 必须为 8 的倍数 |
| \`SsoAlign\` 小于 8 | \`static_assert\` 编译失败 | \`SsoAlign\` 必须 ≥ 8 |
| 不同模板实参的 \`define_void_any\` 互相赋值 | 类型不兼容，编译错误 | 同类型间赋值，或通过 \`cross_layout_swap\` 交换内容 |

---
`
};

window.DOCS_DATA['type_id'] = {
  id: 'type_id',
  title: "type_id — 类型ID",
  category: 'type',
  icon: 'I',
  order: 18,
  content: `## 17. type_id — 类型ID

为每种类型分配唯一整数 ID（编译时确定，线程安全）。
另支持运行期按名字注册自定义类型，注册 ID 与模板类型 ID 互不冲突。

### 接口

| 接口 | 说明 |
|------|------|
| \`type_id::get_type_id<T>()\` | 获取类型 T 的唯一 ID（静态函数，线程安全） |
| \`type_id::current_max_id()\` | 返回当前已分配的最大 type_id（静态函数） |
| \`type_id::mask_block_of(tid)\` | type_id 所在实体掩码块索引 \`(tid-1)/64\`（constexpr） |
| \`type_id::mask_offset_of(tid)\` | type_id 在掩码块内的位偏移 \`(tid-1)%64\`（constexpr） |
| \`type_id::mask_type_of(block, offset)\` | 掩码块/偏移反查 type_id（上两者的逆映射，constexpr） |
| \`type_id::register_type_def(name, def)\` | 按名字注册自定义类型，返回类型 ID；同名重复注册返回已有 ID |
| \`type_id::get_def_type_id(name)\` | 按名字查询类型 ID（含注册类型与绑定名）；未注册返回 -1 |
| \`type_id::bind_def_name(name, id)\` | 将名字绑定到既有类型 ID（模板类型的稳定名/别名）；同名同 ID 幂等，同名异 ID 返回 false |
| \`type_id::get_type_def(id)\` | 按 ID 查询自定义类型的存储语义；非注册 ID 或纯绑定名返回 nullptr |
| \`type_id::get_def_type_name(id)\` | 按 ID 反查名字；非注册 ID 返回空串 |

### type_def 字段

| 字段 | 说明 |
|------|------|
| \`size\` | 元素字节大小（必须 > 0） |
| \`alignment\` | 对齐要求（必须为 2 的幂） |
| \`trivially_copyable\` | true 时按 memcpy 方式搬运数据 |
| \`construct\` / \`destruct\` | 非平凡类型的构造/析构函数指针（\`void(*)(void*) noexcept\`） |

### 使用

\`\`\`cpp
// 模板类型 ID
int id1 = type_id::get_type_id<int>();
int id2 = type_id::get_type_id<double>();
assert(type_id::get_type_id<int>() == id1);  // 同类型 ID 相同

int max_id = type_id::current_max_id();  // 已分配的最大 ID
\`\`\`

\`\`\`cpp
// 注册 trivially copyable 自定义类型
type_def def;
def.size = 12;
def.alignment = 4;
def.trivially_copyable = true;

int id = type_id::register_type_def("Velocity", def);
assert(type_id::get_def_type_id("Velocity") == id);        // 按名查询
assert(type_id::register_type_def("Velocity", def) == id); // 重复注册幂等

// 非平凡类型需提供构造/析构
type_def nontrivial;
nontrivial.size = 16;
nontrivial.alignment = 8;
nontrivial.trivially_copyable = false;
nontrivial.construct = my_construct;  // void(*)(void*) noexcept
nontrivial.destruct = my_destruct;    // void(*)(void*) noexcept
int id2x = type_id::register_type_def("Matrix", nontrivial);

// 按 ID 反查
const type_def* q = type_id::get_type_def(id);
std::string_view name = type_id::get_def_type_name(id);  // "Velocity"
\`\`\`

---
`
};

window.DOCS_DATA['t_fun'] = {
  id: 't_fun',
  title: "t_fun — 函数类型延迟调用器",
  category: 'type',
  icon: 'F',
  order: 35,
  content: `## 34. t_fun — 函数类型延迟调用器

全局命名空间。\`noexcept\`。

编译期推导函数类型的延迟调用器，支持函数指针、成员函数指针（const/非 const）。用户无需输入模板参数，通过 CTAD 自动推导。void 返回值特化，\`result_ptr()\` 返回 \`nullptr\`。

### 接口

| 接口 | 说明 |
|------|------|
| \`t_fun v{f, args...}\` | 构造，绑定函数与参数（CTAD 推导） |
| \`v()\` | 用绑定参数调用 |
| \`v(a, b)\` | 用传入参数调用，不修改绑定参数 |
| \`v.fun()\` / \`v.fun(a, b)\` | 等价 operator() |
| \`v.result_ptr()\` | 非 void 返回 \`R*\`；void 返回 \`nullptr\` |
| \`v.result_reset()\` | 非 void 重置为默认值；void 无副作用 |
| \`v.target()\` | 原始可调用对象（函数指针 / 成员函数指针） |
| \`v.bound_arg<I>()\` | 第 I 个绑定参数引用（const/非 const 重载） |
| \`v.set_arg<I>(v)\` | 修改第 I 个绑定参数 |
| \`v.object()\` | 成员函数指针版本，对象指针 |
| \`v.arity\` | 编译期常量，参数数量 |
| \`v.return_type\` | 编译期类型别名，返回值类型 |
| \`v.args_tuple\` | 编译期类型别名，参数 tuple |
| \`v.then_call(g)\` | 链式调用，\`g(v())\`；void 版本 \`v(); g();\` |
| \`v.compose(g, more...)\` | 多级组合，依次调用 g, more... |
| \`v.bind_front(args...)\` | 设置前 N 个绑定参数 |
| \`v.apply_n(count)\` | 重复调用 N 次，返回最后结果 |
| \`v.apply_range(data, n)\` | 对数组批量调用（arity==1） |
| \`v.swap(other)\` | O(1) 交换 |
| \`v == other\` / \`v != other\` | 比较 target 与绑定参数 |
| \`v.hash()\` | 哈希值 |
| \`v.empty()\` | 是否已释放 |
| \`v.release()\` | 释放所有权 |
| \`v.reset(args...)\` | 重置所有绑定参数 |
| \`os << v\` | 流输出诊断信息 |

### 使用

\`\`\`cpp

int  free_add(int a, int b) { return a + b; }
void free_void(int x) { (void)x; }
void free_noop() {}
std::string free_make_str(const char* s) { return std::string(s); }

class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
};

// === 函数指针: 绑定参数调用 ===
t_fun v1{free_add, 10, 20};
v1();                  // 30, 用绑定的 10,20
v1.fun();              // 30, 等价
v1(3, 4);              // 7,  用传入参数, 不修改绑定参数
v1();                  // 仍 30, 绑定参数未被覆盖

// === result_ptr / result_reset ===
int* p = v1.result_ptr();   // &result_, *p == 30
v1.result_reset();          // *p 变为 0
v1.result_ptr();            // &result_, *p == 0

// === target / bound_arg / set_arg ===
v1.target()(10, 20);        // 30, 直接调用原始函数指针
v1.bound_arg<0>();          // 10
v1.set_arg<0>(100);
v1.set_arg<1>(200);
v1();                       // 300

// === 编译期元信息 ===
static_assert(decltype(v1)::arity == 2);
static_assert(std::is_same_v<decltype(v1)::return_type, int>);

// === void 返回值 ===
t_fun v2{free_void, 42};
v2();                       // 调用, 无返回值
void* p2 = v2.result_ptr(); // nullptr
v2.result_reset();          // 无副作用

t_fun v3{free_noop};        // 无参函数
v3();

// === std::string 返回值 ===
t_fun v4{free_make_str, "abc"};
std::string s = v4();       // "abc"
std::string* p4 = v4.result_ptr();  // &result_, *p4 == "abc"
v4.result_reset();          // 空字符串

// === 成员函数指针 ===
Calculator calc;
t_fun m1{&Calculator::add, &calc, 10, 20};
m1();                       // 30, 绑定对象与参数
m1(3, 4);                   // 7,  带参调用
m1.object();                // &calc
m1.target();                // &Calculator::add

// const 成员函数
t_fun m2{&Calculator::is_positive, &calc, 5};
m2();                       // true

// void 成员函数
t_fun m3{&Calculator::no_return, &calc, 0};
m3();

// === 移动与拷贝 ===
t_fun v5{std::move(v1)};    // 移动构造
t_fun v6{v5};                // 拷贝构造
v5 = std::move(v6);          // 移动赋值

// === then_call 链式调用 ===
t_fun v7{free_add, 10, 20};
int doubled = v7.then_call([](int x){ return x * 2; });  // 60
// void 版本: v(); g();

// === compose 多级组合 ===
int composed = v7.compose(
    [](int x){ return x * 2; },
    [](int x){ return x + 1; }
);  // (30*2)+1 = 61

// === bind_front 设置前 N 个参数 ===
t_fun v8{free_add, 0, 0};
v8.bind_front(100);          // 设置第 0 个参数为 100
v8.set_arg<1>(200);
v8();                        // 300

// === apply_n 批量调用 ===
t_fun v9{free_add, 1, 2};
int last = v9.apply_n(1000); // 调用 1000 次, 返回最后结果 (3)

// === apply_range 范围应用 (arity==1) ===
t_fun v10{free_identity};
int data[] = {1, 2, 3, 4, 5};
int last_r = v10.apply_range(data, 5);  // 对每个元素调用, 返回最后 (5)

// === swap 交换 ===
t_fun a{free_add, 1, 2};
t_fun b{free_add, 10, 20};
a.swap(b);
a();                         // 30 (交换后)
b();                         // 3

// === operator== / != ===
t_fun c1{free_add, 10, 20};
t_fun c2{free_add, 10, 20};
t_fun c3{free_add, 30, 40};
c1 == c2;                    // true (target 与绑定参数相同)
c1 != c3;                    // true

// === hash ===
size_t h = c1.hash();        // 哈希值

// === empty / release ===
t_fun v11{free_add, 10, 20};
v11.empty();                 // false
v11.release();               // 释放所有权
v11.empty();                 // true

// === reset 重置所有绑定参数 ===
t_fun v12{free_add, 0, 0};
v12.reset(5, 6);
v12();                       // 11

// === operator<< 流输出 ===
t_fun v13{free_add, 10, 20};
std::cout << v13;            // [t_fun arity=2 target=set]
v13.release();
std::cout << v13;            // [t_fun arity=2 target=null]
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 返回值类型不可默认构造 | 编译期报错 | 确保 \`R\` 可默认构造 |
| \`set_arg<I>\` 越界 | 编译期报错 | I < arity |
| \`bound_arg<I>\` 越界 | 编译期报错 | I < arity |
| 带参调用参数数与签名不符 | 编译期报错 | 参数数量与函数签名一致 |
| 成员函数指针版本不传对象 | 编译期报错 | 构造时第二参数传 \`C*\` |
| const 成员函数传非 const 对象指针 | 编译期报错 | 传 \`const C*\` |
| \`result_ptr()\` 未调用就解引用 | 读到默认构造值 | 先调用 \`v()\` |
| 使用后忘记 \`result_reset()\` | 读到上次调用结果 | 需要重置时调用 |
| \`apply_range\` 用于 arity != 1 | 编译期报错 | 仅 arity == 1 时可用 |
| \`release\` 后再调用 | 解引用空指针 | 调用前检查 \`empty()\` |
| \`bind_front\` 参数数超过 arity | 编译期报错 | 参数数 <= arity |
| \`reset\` 参数数与 arity 不符 | 编译期报错 | 参数数 == arity |
| \`then_call\` 的 g 参数类型不匹配 | 编译期报错 | g 接受 R 类型参数 |
`
};
