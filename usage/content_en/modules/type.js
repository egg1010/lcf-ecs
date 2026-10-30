window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};
window.DOCS_DATA_EN['void_any'] = {
  title: "void_any — Type-erased value",
  content: `## 16. void_any — Type-erased storage

No namespace. All interfaces are \`noexcept\`. A type-erased container that can store objects of any type; at runtime values are retrieved type-safely via \`get_ptr<T>()\`.

\`void_any\` is a preset alias of the class template \`define_void_any<SsoSize, SsoAlign>\`; the SSO buffer size and alignment are determined at compile time by the template parameters. When larger inline storage is needed, the template can be instantiated directly.

**Template parameters:**

| Parameter | Type | Constraint | Description |
|------|------|------|------|
| \`SsoSize\` | \`size_t\` | multiple of 8 | SSO buffer size in bytes; determines the inline storage limit |
| \`SsoAlign\` | \`size_t\` | ≥ 8 | SSO buffer alignment |

**Preset alias:**

| Alias | Template arguments | Description |
|------|---------|------|
| \`void_any\` | \`define_void_any<56, 8>\` | Default general-purpose alias |

**\`SsoSize\` reference values (\`SsoAlign\` stays 8):**

| \`SsoSize\` | \`sizeof\` | Inline storage limit | Use case |
|-----------|----------|------------|---------|
| \`8\` | 16B | ≤ 8B | Small scalars only (int/double/pointer) |
| \`24\` | 32B | ≤ 24B | Small objects, memory-sensitive scenarios |
| \`56\` | 64B | ≤ 56B | Default, general purpose |
| \`120\` | 128B | ≤ 120B | When larger objects must be stored inline (e.g. \`std::string\`) |
| \`248\` | 256B | ≤ 248B | Inline storage of large objects |

> Note: \`sizeof\` = \`SsoSize\` + 8 (type tag field). Objects exceeding the inline limit are automatically moved to heap allocation; functionality is unaffected.

**\`SsoAlign\` reference values:**

| \`SsoAlign\` | Description |
|------------|------|
| \`8\` | Default, suitable for most types |
| \`16\` | Types requiring 16-byte alignment (e.g. some SIMD types) |
| \`32\` | Types requiring 32-byte alignment (e.g. AVX vectors) |

> Note: A large \`SsoAlign\` may increase \`sizeof\` (padding). Keep it at \`8\` unless special alignment is required.

### Construction and assignment

| Interface | Description |
|------|------|
| \`void_any()\` | Default constructor, empty value |
| \`void_any(T&&)\` | Construct from any type (excluding \`void_any\` itself) |
| \`void_any(const void_any&)\` | Copy constructor |
| \`void_any(void_any&&)\` | Move constructor |
| \`operator=(const void_any&)\` | Copy assignment |
| \`operator=(void_any&&)\` | Move assignment |

### Access and operations

| Interface | Description |
|------|------|
| \`set(T&&)\` | Set a new value (same type: overwrite directly; different type: destruct the old value, then construct the new one) |
| \`type_id()\` | Get the type ID (returns -1 when empty; used only for same-type consistency comparison, and is not equal to \`type_id::get_type_id<T>()\`) |
| \`get_ptr<T>()\` | Get pointer (with type check, returns nullptr on mismatch) |
| \`get_ptr<T>() const\` | const version |
| \`fast_get_ptr<T>()\` | Get pointer (skips the type_id check) |
| \`fast_get_ptr<T>() const\` | const version |
| \`get_ptr_unchecked<T>()\` | Get pointer (does not verify has_value or type_id) |
| \`get_ptr_unchecked<T>() const\` | const version |
| \`get<T>()\` | Get a copy of the value (returns a default-constructed value when empty or on type mismatch) |
| \`get_void()\` | Get \`void*\` (returns nullptr when empty) |
| \`get_void() const\` | const version |
| \`copy_from<T>(const T&)\` | Copy assign (T known at compile time) |
| \`move_from<T>(T&&)\` | Move assign (T known at compile time) |
| \`has_value()\` | Whether a value is held |
| \`reset()\` | Clear (destruct and leave empty) |
| \`swap(void_any&)\` | Same-layout swap (exchanges contents of two \`void_any\` with the same \`SsoSize\`/\`SsoAlign\`) |
| \`cross_layout_swap(define_void_any<SsoSize2, SsoAlign2>&)\` | Cross-layout swap (exchanges contents of \`void_any\` with different \`SsoSize\`/\`SsoAlign\`; the empty side takes over the other side's value) |

> Note: To check whether the stored type is T, use \`get_ptr<T>() != nullptr\`, not \`type_id() == type_id::get_type_id<T>()\`.

### Usage

\`\`\`cpp

// basic construction
void_any a(42);                     // stores int
void_any b(std::string("hello"));   // stores std::string
void_any c;                         // empty value

// state queries
a.has_value();                      // true
c.has_value();                      // false
a.type_id();                        // type identifier (returns -1 when empty)

// value retrieval (with type check)
int* pi = a.get_ptr<int>();         // match, returns pointer
double* pd = a.get_ptr<double>();   // mismatch, returns nullptr
int val = a.get<int>();             // value copy (default-constructed if empty or type mismatch)

// void* access
void* vp = a.get_void();            // returns void* (nullptr when empty)

// set a new value (same type: overwrite; different type: destruct old, then construct new)
a.set(99);                          // int 42 → 99 (same-type overwrite)
a.set(std::string("world"));        // int 99 → std::string (different type, destruct + construct)

// clear
a.reset();                          // destruct and leave empty
a.has_value();                      // false

// copy and move
void_any src(std::string("data"));
void_any cp(src);                   // copy construction
void_any mv(std::move(src));        // move construction (src becomes empty)

void_any dst;
dst = cp;                           // copy assignment
dst = std::move(mv);                // move assignment (mv becomes empty)
\`\`\`

### Interfaces with compile-time known types

\`\`\`cpp
void_any a;

// copy_from / move_from: T known at compile time, no runtime type deduction needed
a.copy_from(42);                    // copy int
a.move_from(std::string("x"));      // move string

// fast_get_ptr: skips the type_id check (caller must ensure the type is correct)
int* p = a.fast_get_ptr<int>();

// get_ptr_unchecked: does not verify has_value and type_id (caller must ensure the value exists)
int* pu = a.get_ptr_unchecked<int>();
\`\`\`

### Storing custom types

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

### Storing in containers

\`\`\`cpp

dense<void_any> bag;
bag.emplace_back(42);
bag.emplace_back(std::string("item"));
bag.emplace_back(3.14);

// iterate and retrieve by type
for (size_t i = 0; i < bag.size(); ++i) {
    if (auto* p = bag[i].get_ptr<int>()) {
        // handle int
    } else if (auto* p = bag[i].get_ptr<std::string>()) {
        // handle string
    } else if (auto* p = bag[i].get_ptr<double>()) {
        // handle double
    }
}
\`\`\`

### Type consistency comparison

\`\`\`cpp
void_any x(1), y(2), z(std::string("a"));
x.type_id() == y.type_id();        // true, both int
x.type_id() == z.type_id();        // false, int differs from string
\`\`\`

> Note: \`type_id()\` is used only for same-type consistency comparison and is not equal to \`type_id::get_type_id<T>()\`. To check whether the stored type is T, use \`get_ptr<T>() != nullptr\`.

### Custom SSO configuration

\`\`\`cpp

// default void_any: 56B SSO, can store objects ≤56B inline
void_any a(42);

// custom larger SSO buffer, store larger objects inline to avoid heap allocation
// for example, a 100B struct needs inline storage
struct big_data { char buf[100]; };

using my_void_any = define_void_any<120, 8>;   // 120B SSO
my_void_any b(big_data{});

// custom alignment value (must be ≥ 8)
using aligned_any = define_void_any<56, 16>;   // 16-byte alignment
aligned_any c(3.14);

// note: types instantiated with different template arguments are incompatible and cannot be assigned to each other
// my_void_any x = a;   // compile error
\`\`\`

### Cross-layout swap

\`\`\`cpp

// exchange contents between void_any with different SsoSize
void_any a(42);                              // define_void_any<56, 8>
define_void_any<120, 8> b(std::string("hi")); // 120B SSO

// cross-layout swap: a receives string, b receives int
a.cross_layout_swap(b);

// both types and values have been exchanged
auto* pa = a.get_ptr<std::string>();   // match
auto* pb = b.get_ptr<int>();           // match

// empty value involved: the empty side takes over the other side's value
void_any empty;
void_any has_val(99);
empty.cross_layout_swap(has_val);     // empty holds 99, has_val becomes empty

// same-layout swap
void_any x(1), y(2);
x.swap(y);                             // x=2, y=1
\`\`\`

> Note: \`cross_layout_swap\` exchanges contents between \`void_any\` instantiated with different \`SsoSize\`/\`SsoAlign\`. When either side is empty, the empty side takes over the other side's value.

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Using \`get_ptr_unchecked\` without first checking \`has_value()\` and the type | Returns a dangling pointer, undefined behavior | Use it only when the type and value are known to exist; otherwise use \`get_ptr<T>()\` |
| Relying on the value returned by \`get<T>()\` being a default value to detect the type | A default-constructed value may equal the actual value | First check whether the pointer from \`get_ptr<T>()\` is null |
| Continuing to use the object after moving | The source object is empty after the move | After a move only \`reset()\` or reassignment is valid |
| Accessing before \`set()\` | \`has_value()\` is false, get_ptr returns nullptr | Call \`set()\` first, or pass a value at construction |
| Using \`type_id() == type_id::get_type_id<T>()\` to detect the type | The two are not equal | Use \`get_ptr<T>() != nullptr\` to detect the type |
| \`SsoSize\` not a multiple of 8 | \`static_assert\` fails to compile | \`SsoSize\` must be a multiple of 8 |
| \`SsoAlign\` smaller than 8 | \`static_assert\` fails to compile | \`SsoAlign\` must be ≥ 8 |
| Assigning between \`define_void_any\` with different template arguments | Types are incompatible, compile error | Assign between the same type, or exchange contents via \`cross_layout_swap\` |

---
`
};

window.DOCS_DATA_EN['type_id'] = {
  title: "type_id — Runtime type registry",
  content: `## 17. type_id — Type ID

Assigns each type a process-unique integer ID (determined on first query, thread-safe).
Custom types can also be registered by name at runtime; registered IDs and template type IDs never conflict.

### Interfaces

| Interface | Description |
|------|------|
| \`type_id::get_type_id<T>()\` | Get the unique ID of type T (static function, thread-safe) |
| \`type_id::current_max_id()\` | Return the largest type_id allocated so far (static function) |
| \`type_id::mask_block_of(tid)\` | Index of the entity mask block containing type_id, \`(tid-1)/64\` (constexpr) |
| \`type_id::mask_offset_of(tid)\` | Bit offset of type_id within the mask block, \`(tid-1)%64\` (constexpr) |
| \`type_id::mask_type_of(block, offset)\` | Reverse lookup of type_id from mask block/offset (inverse of the two above, constexpr) |
| \`type_id::register_type_def(name, def)\` | Register a custom type by name, returns the type ID; registering the same name again returns the existing ID |
| \`type_id::get_def_type_id(name)\` | Look up a type ID by name (including registered types and bound names); returns -1 if not registered |
| \`type_id::bind_def_name(name, id)\` | Bind a name to an existing type ID (stable name/alias of a template type); idempotent for the same name and same ID, returns false for the same name with a different ID |
| \`type_id::get_type_def(id)\` | Look up the storage semantics of a custom type by ID; returns nullptr for non-registered IDs or pure bound names |
| \`type_id::get_def_type_name(id)\` | Reverse lookup of the name by ID; returns an empty string for non-registered IDs |

### type_def fields

| Field | Description |
|------|------|
| \`size\` | Element size in bytes (must be > 0 and an integer multiple of \`alignment\`) |
| \`alignment\` | Alignment requirement (must be a power of 2) |
| \`trivially_copyable\` | When true, data is moved via memcpy |
| \`construct\` | Default construction for non-trivial types (optional, \`void(*)(void*) noexcept\`) |
| \`copy_construct\` / \`destruct\` | Required for non-trivial types; copy constructor signature is \`void(*)(void*, const void*) noexcept\`, destructor signature is \`void(*)(void*) noexcept\` |

### Usage

\`\`\`cpp
// template type IDs
int id1 = type_id::get_type_id<int>();
int id2 = type_id::get_type_id<double>();
assert(type_id::get_type_id<int>() == id1);  // same type, same ID

int max_id = type_id::current_max_id();  // the largest ID allocated so far
\`\`\`

\`\`\`cpp
// register a trivially copyable custom type
type_def def;
def.size = 12;
def.alignment = 4;
def.trivially_copyable = true;

int id = type_id::register_type_def("Velocity", def);
assert(type_id::get_def_type_id("Velocity") == id);        // lookup by name
assert(type_id::register_type_def("Velocity", def) == id); // repeated registration is idempotent

// non-trivial types must provide copy construction/destruction
type_def nontrivial;
nontrivial.size = 16;
nontrivial.alignment = 8;
nontrivial.trivially_copyable = false;
nontrivial.construct = my_construct;            // optional
nontrivial.copy_construct = my_copy_construct;  // required for non-trivial
nontrivial.destruct = my_destruct;              // required for non-trivial
int id2x = type_id::register_type_def("Matrix", nontrivial);

// reverse lookup by ID
const type_def* q = type_id::get_type_def(id);
std::string_view name = type_id::get_def_type_name(id);  // "Velocity"
\`\`\`

---
`
};

window.DOCS_DATA_EN['t_fun'] = {
  title: "t_fun — Function wrappers",
  content: `## 34. t_fun — Deferred function invoker

Global namespace. \`noexcept\`.

A deferred invoker whose function type is deduced at compile time; supports function pointers and member function pointers (const/non-const). The user does not need to specify template arguments — CTAD deduces them automatically. void return values are specialized so that \`result_ptr()\` returns \`nullptr\`.

### Interfaces

| Interface | Description |
|------|------|
| \`t_fun v{f, args...}\` | Construct, binding the function and arguments (deduced via CTAD) |
| \`v()\` | Call with the bound arguments |
| \`v(a, b)\` | Call with the supplied arguments, without modifying the bound arguments |
| \`v.fun()\` / \`v.fun(a, b)\` | Equivalent to operator() |
| \`v.result_ptr()\` | For non-void returns \`R*\`; for void returns \`nullptr\` |
| \`v.result_reset()\` | For non-void resets to the default value; for void has no effect |
| \`v.target()\` | The original callable object (function pointer / member function pointer) |
| \`v.bound_arg<I>()\` | Reference to the I-th bound argument (const/non-const overloads) |
| \`v.set_arg<I>(v)\` | Modify the I-th bound argument |
| \`v.object()\` | For member function pointer versions, the object pointer |
| \`v.arity\` | Compile-time constant, number of arguments |
| \`v.return_type\` | Compile-time type alias, return value type |
| \`v.args_tuple\` | Compile-time type alias, tuple of arguments |
| \`v.then_call(g)\` | Chained call, \`g(v())\`; void version \`v(); g();\` |
| \`v.compose(g, more...)\` | Multi-level composition, calls g, more... in sequence |
| \`v.bind_front(args...)\` | Set the first N bound arguments |
| \`v.apply_n(count)\` | Call repeatedly N times, returns the last result |
| \`v.apply_range(data, n)\` | Batch call over an array (arity==1) |
| \`v.swap(other)\` | O(1) swap |
| \`v == other\` / \`v != other\` | Compare target and bound arguments |
| \`v.hash()\` | Hash value |
| \`v.empty()\` | Whether it has been released |
| \`v.release()\` | Release ownership |
| \`v.reset(args...)\` | Reset all bound arguments |
| \`os << v\` | Stream diagnostic information |

### Usage

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

// === function pointer: calling with bound arguments ===
t_fun v1{free_add, 10, 20};
v1();                  // 30, uses the bound 10,20
v1.fun();              // 30, equivalent
v1(3, 4);              // 7,  uses supplied arguments, bound arguments unchanged
v1();                  // still 30, bound arguments were not overwritten

// === result_ptr / result_reset ===
int* p = v1.result_ptr();   // &result_, *p == 30
v1.result_reset();          // *p becomes 0
v1.result_ptr();            // &result_, *p == 0

// === target / bound_arg / set_arg ===
v1.target()(10, 20);        // 30, calls the original function pointer directly
v1.bound_arg<0>();          // 10
v1.set_arg<0>(100);
v1.set_arg<1>(200);
v1();                       // 300

// === compile-time metadata ===
static_assert(decltype(v1)::arity == 2);
static_assert(std::is_same_v<decltype(v1)::return_type, int>);

// === void return value ===
t_fun v2{free_void, 42};
v2();                       // call, no return value
void* p2 = v2.result_ptr(); // nullptr
v2.result_reset();          // no effect

t_fun v3{free_noop};        // function without arguments
v3();

// === std::string return value ===
t_fun v4{free_make_str, "abc"};
std::string s = v4();       // "abc"
std::string* p4 = v4.result_ptr();  // &result_, *p4 == "abc"
v4.result_reset();          // empty string

// === member function pointer ===
Calculator calc;
t_fun m1{&Calculator::add, &calc, 10, 20};
m1();                       // 30, binds object and arguments
m1(3, 4);                   // 7,  call with arguments
m1.object();                // &calc
m1.target();                // &Calculator::add

// const member function
t_fun m2{&Calculator::is_positive, &calc, 5};
m2();                       // true

// void member function
t_fun m3{&Calculator::no_return, &calc, 0};
m3();

// === move and copy ===
t_fun v5{std::move(v1)};    // move construction
t_fun v6{v5};                // copy construction
v5 = std::move(v6);          // move assignment

// === then_call chained call ===
t_fun v7{free_add, 10, 20};
int doubled = v7.then_call([](int x){ return x * 2; });  // 60
// void version: v(); g();

// === compose multi-level composition ===
int composed = v7.compose(
    [](int x){ return x * 2; },
    [](int x){ return x + 1; }
);  // (30*2)+1 = 61

// === bind_front sets the first N arguments ===
t_fun v8{free_add, 0, 0};
v8.bind_front(100);          // sets argument 0 to 100
v8.set_arg<1>(200);
v8();                        // 300

// === apply_n batch call ===
t_fun v9{free_add, 1, 2};
int last = v9.apply_n(1000); // calls 1000 times, returns the last result (3)

// === apply_range range application (arity==1) ===
t_fun v10{free_identity};
int data[] = {1, 2, 3, 4, 5};
int last_r = v10.apply_range(data, 5);  // calls for each element, returns the last (5)

// === swap ===
t_fun a{free_add, 1, 2};
t_fun b{free_add, 10, 20};
a.swap(b);
a();                         // 30 (after swap)
b();                         // 3

// === operator== / != ===
t_fun c1{free_add, 10, 20};
t_fun c2{free_add, 10, 20};
t_fun c3{free_add, 30, 40};
c1 == c2;                    // true (same target and bound arguments)
c1 != c3;                    // true

// === hash ===
size_t h = c1.hash();        // hash value

// === empty / release ===
t_fun v11{free_add, 10, 20};
v11.empty();                 // false
v11.release();               // release ownership
v11.empty();                 // true

// === reset all bound arguments ===
t_fun v12{free_add, 0, 0};
v12.reset(5, 6);
v12();                       // 11

// === operator<< stream output ===
t_fun v13{free_add, 10, 20};
std::cout << v13;            // [t_fun arity=2 target=set]
v13.release();
std::cout << v13;            // [t_fun arity=2 target=null]
\`\`\`

### Caveats

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Return type is not default-constructible | Compile-time error | Ensure \`R\` is default-constructible |
| \`set_arg<I>\` out of range | Compile-time error | I < arity |
| \`bound_arg<I>\` out of range | Compile-time error | I < arity |
| Number of arguments in a parameterized call differs from the signature | Compile-time error | Number of arguments matches the function signature |
| Member function pointer version without an object | Compile-time error | Pass \`C*\` as the second constructor argument |
| Passing a non-const object pointer to a const member function | Compile-time error | Pass \`const C*\` |
| Dereferencing \`result_ptr()\` before calling | Reads the default-constructed value | Call \`v()\` first |
| Forgetting \`result_reset()\` after use | Reads the previous call's result | Call it when a reset is needed |
| \`apply_range\` used with arity != 1 | Compile-time error | Only usable when arity == 1 |
| Calling after \`release\` | Dereferences a null pointer | Check \`empty()\` before calling |
| \`bind_front\` with more arguments than arity | Compile-time error | Number of arguments <= arity |
| \`reset\` argument count differs from arity | Compile-time error | Number of arguments == arity |
| \`then_call\` g parameter type mismatch | Compile-time error | g accepts an argument of type R |
`
};
