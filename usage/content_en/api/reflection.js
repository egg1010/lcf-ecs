window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['reflection_usage'] = {
  title: "reflection — Reflection module",
  content: `## 11. reflection — Reflection module usage

Namespace \`reflect\`. \`noexcept\`.

Provides registration and query of type metadata: field access, method invocation, enum conversion in both directions, inheritance upcasting, comparison, hashing, construction, container iteration. Both registration and query go through a \`reflect::manager\` instance, and fallible interfaces return \`operating_message\`.

### Quick start

Register fields and read/write them:

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
q.get_by_name<float>(&v, "y") = 20.0f;      // read/write a field by name
\`\`\`

Register and invoke a method:

\`\`\`cpp
struct Calc { int base; int add(int a, int b) { return a + b + base; } };

(void)m.add_type<Calc>();
(void)m.add_method<&Calc::add>("add");

Calc c{10};
int sum = 0;
(void)m.invoke(sum, c, "add", 1, 2);         // sum == 13
\`\`\`

### Find the interface by task

| I want to… | Go to |
|------|------|
| Register types, fields, methods | [11.1 manager registration interface](#111-manager-registration-interface) |
| Query types and invoke methods | [11.2 manager query interface](#112-manager-query-interface) |
| Read/write fields by name or index | [11.3 query_view](#113-query_view--type-view) |
| Iterate field or method lists | [11.4 field_view / method_view](#114-field_view--method_view--iteration-views) |
| Convert enum values to names | [11.7 enum_view](#117-enum_view--enum-reflection) |
| Inheritance relations and upcasting | [11.6 inheritance_view](#116-inheritance_view--inheritance-relations) |
| Field attributes | [11.8 attribute_view](#118-attribute_view--field-attributes) |
| Comparison, cloning, hashing | [11.12 compare_view](#1112-compare_view--field-comparisoncloning) / [11.13 hash_view](#1113-hash_view--field-hashing) |

### 11.1 manager registration interface

\`\`\`cpp
reflect::manager m;
\`\`\`

| Interface | Description |
|------|------|
| \`add_type<T>()\` | Register type metadata without traversing fields |
| \`add_type_as<T>(name)\` | Register a type and explicitly specify the type name |
| \`add_aggregate_type<T>()\` | Register a type and automatically traverse aggregate fields; field names are \`field_0\`, \`field_1\`; up to 256 fields |
| \`add_field<T>(name, &T::field)\` | Register a field; scalars and arrays are dispatched automatically |
| \`add_fields<T>(tuple)\` | Register fields in batch; accepts \`{name, member pointer}\` tuples |
| \`add_method<&T::f>(name)\` | Register a member method; the pointer must be a compile-time constant |
| \`add_method(name, &T::f)\` | Register a member method; the pointer may be a runtime value |
| \`add_methods(tuple)\` | Register member methods in batch |
| \`add_static_method<C, &C::f>(name)\` | Register a static method |
| \`add_virtual_method<&T::f>(name, vtable_offset)\` | Register a virtual method; the vtable offset is supplied by the caller |
| \`add_enum<E>(name, {{E::v, "v"}})\` | Register enum name/value pairs |
| \`add_base<D, B>()\` | Register an inheritance relation; pointer adjustment is performed by the type conversion |
| \`add_base_with_offset<D, B>(offset)\` | Register an inheritance relation and specify the pointer adjustment amount |
| \`add_base_with_adjuster<D, B>(fn)\` | Register an inheritance relation and specify an adjustment function, for private or virtual inheritance |
| \`add_convert<From, To>()\` | Register a type conversion; requires \`From\` to be implicitly convertible to \`To\` |
| \`add_field_attr<T>(field, key, value)\` | Register a field attribute; overwrites when the key already exists |
| \`set_construct_destruct<T>(create, destroy)\` | Specify the construction and destruction functions, for types with private constructors |
| \`add_accessed_members<T>()\` | Register all granted members through the \`reflect_access<T>\` specialization |
| \`freeze()\` / \`unfreeze()\` | After freezing, write interfaces return \`om_err_reflect_frozen\` |

### 11.2 manager query interface

| Interface | Description |
|------|------|
| \`get<T>()\` | Get a \`query_view\` by type; returns an invalid view if not registered |
| \`find(type_name)\` | Get a \`query_view\` by type name |
| \`has<T>()\` | Whether the type is registered |
| \`field<T>(name)\` | Get \`const field_meta*\` by name; returns nullptr if not found |
| \`method<T>(name)\` | Get \`const method_meta*\` by name; returns nullptr if not found |
| \`invoke<R>(out, obj, name, args...)\` | Invoke a method that returns a value; the result is written to \`out\` |
| \`invoke_void(obj, name, args...)\` | Invoke a method that returns nothing |
| \`try_invoke<R>(obj, name, args...)\` | Soft-failure invocation; \`R=void\` returns \`bool\`, otherwise returns \`std::optional<R>\` |
| \`get_fields<T>()\` | Field iteration view; supports range-for |
| \`get_methods<T>()\` | Method iteration view; supports range-for |
| \`type_count()\` | Number of registered types |
| \`frozen()\` | Whether it is frozen |
| \`store()\` | Get the underlying storage object, for scenarios such as serialization |

### 11.3 query_view — Type view

The return type of \`get<T>()\` and \`find()\`.

| Interface | Description |
|------|------|
| \`valid()\` | Whether valid metadata is bound |
| \`name()\` | Type name |
| \`size()\` / \`align()\` | \`sizeof(T)\` / \`alignof(T)\` |
| \`field_count()\` / \`method_count()\` | Field count / method count |
| \`type_id_value()\` | Type id |
| \`meta()\` | Underlying \`const type_meta*\` |
| \`field(i)\` / \`field_by_name(name)\` | Get field metadata by index / by name |
| \`method(i)\` / \`method_by_name(name)\` | Get method metadata by index / by name |
| \`get<R>(obj, i)\` | Get a field reference by index |
| \`get_by_name<R>(obj, name)\` | Get a field reference by name; terminates if the field does not exist |
| \`try_get_by_name<R>(obj, name)\` | Get a field pointer by name; returns nullptr if not found |
| \`get_ptr(obj, name)\` | Get a field \`void*\` by name |
| \`invoke<R>(obj, name, args...)\` | Invoke a method; terminates on failure |
| \`try_invoke<R>(obj, name, args...)\` | Soft-failure invocation; fails if arguments or return type do not match |
| \`find_overload(name, ids, n_args)\` | Match an overload exactly by argument type ids |
| \`get_by_path(obj, path)\` | Get a field \`void*\` by an \`a.b.c\` path |
| \`get_by_path_as<T>(obj, path)\` | Access by path and cast to \`T*\` |

Field and array access:

| Interface | Description |
|------|------|
| \`is_array(i)\` / \`is_array_by_name(name)\` | Whether the field is an array |
| \`array_rank(i)\` | Array rank (0 for scalar, 1~4 for arrays) |
| \`array_total_elements(i)\` | Total number of array elements |
| \`array_element_stride(i)\` | Element stride (bytes) |
| \`array_extent(field_idx, dim)\` | Number of elements in dimension \`dim\` |
| \`array_element_ptr(obj, field_idx, element_idx)\` | Get an array element pointer; returns nullptr when out of range |
| \`array_element_ptr_by_name(obj, name, element_idx)\` | Get an array element pointer by name |
| \`for_each_array_element(obj, field_idx, f)\` | Iterate array elements, \`f(ptr, idx, type_id)\` |
| \`array_get<T>(obj, field_idx, element_idx)\` | Type-safe read; terminates when out of range |
| \`array_get_by_name<T>(obj, name, element_idx)\` | Read by name |
| \`try_array_get<T>(obj, field_idx, element_idx)\` | Safe read; returns nullptr when out of range |
| \`array_set<T>(obj, field_idx, element_idx, value)\` | Type-safe write |
| \`array_set_by_name<T>(obj, name, element_idx, value)\` | Write by name |

### 11.4 field_view / method_view — Iteration views

| Interface | Description |
|------|------|
| \`begin()\` / \`end()\` | range-for iterators; dereferencing yields \`const field_meta&\` / \`const method_meta&\` |
| \`size()\` / \`empty()\` | Number of elements |
| \`operator[](i)\` | Access by index |

### 11.5 construct_view — Object construction/destruction

\`m.get_construct<T>()\`.

| Interface | Description |
|------|------|
| \`has_default_construct()\` | Whether the type is default-constructible |
| \`create()\` | Heap-allocate and default-construct; returns nullptr on failure |
| \`create_inplace(buf)\` | Default-construct in place; \`buf\` must be large enough and aligned |
| \`destroy(obj)\` | Destroy the object without releasing memory |
| \`destroy_heap(obj)\` | Destroy and release; pairs with \`create()\` |

### 11.6 inheritance_view — Inheritance relations

\`m.get_inheritance<T>()\`.

| Interface | Description |
|------|------|
| \`is_derived_from(type_id)\` / \`is_derived_from<Base>()\` | Whether it derives from the given type, including indirect inheritance |
| \`is_base_of(type_id)\` / \`is_base_of<Derived>()\` | Whether it is a base class of the given type |
| \`upcast(derived_obj, base_type_id)\` | Upcast; returns nullptr on failure |
| \`upcast<Base>(derived_obj)\` | Template version; returns \`Base*\` |
| \`base_count()\` / \`derived_count()\` | Number of direct base classes / derived classes |
| \`base_type_id_at(idx)\` / \`derived_type_id_at(idx)\` | Get a type_id by index; returns -1 when out of range |
| \`for_each_base(f)\` | Iterate direct base classes, \`f(base_type_id, offset)\` |
| \`for_each_derived(f)\` | Iterate direct derived classes, \`f(derived_type_id)\` |

### 11.7 enum_view — Enum reflection

\`m.get_enum<E>()\`.

| Interface | Description |
|------|------|
| \`valid()\` | Whether valid enum metadata is bound |
| \`name()\` / \`type_id_value()\` / \`underlying_type_id()\` | Enum name / type id / underlying type id |
| \`value_count()\` | Number of enum values |
| \`value_to_name(value)\` | Convert a value to a name; returns nullptr on failure |
| \`name_to_value(name, out)\` | Convert a name to a value; returns false on failure |
| \`for_each_value(f)\` | Iterate enum values, \`f(uint64_t value, const char* name)\` |

### 11.8 attribute_view — Field attributes

\`m.get_attributes<T>()\`.

| Interface | Description |
|------|------|
| \`has_attr(field, key)\` | Whether the field has the given attribute; \`field\` may be an index or a name |
| \`get_attr(field, key)\` | Get the attribute value as \`const void_any*\`; returns nullptr on failure |
| \`get_attr_as<V>(field, key)\` | Type-safe value retrieval; returns \`const V*\` |
| \`for_each_attr(field_idx, f)\` | Iterate field attributes, \`f(key_hash, void_any&)\` |

### 11.9 container_view — Container reflection

\`m.get_container<T>(obj)\`.

| Interface | Description |
|------|------|
| \`valid()\` / \`is_container()\` | Whether a container is bound |
| \`category()\` | Container category (\`sequential\` / \`none\`) |
| \`element_type_id()\` | Element type id |
| \`size()\` | Number of elements |
| \`get(index)\` / \`get_as<T>(index)\` | Get an element pointer / type-safe get |
| \`push_back(element)\` / \`push_back<T>(value)\` | Append an element |
| \`clear()\` / \`reserve(n)\` | Clear / reserve capacity |
| \`for_each(f)\` | Iterate elements, \`f(void* element, size_t index)\` |

### 11.10 virtual_dispatch_view — Dynamic dispatch

\`m.get_virtual_dispatch<T>()\`.

| Interface | Description |
|------|------|
| \`is_virtual(method_name)\` | Whether the method is virtual |
| \`vtable_offset(method_name)\` | vtable offset of the virtual function; returns -1 for non-virtual |

### 11.11 convert_view — Type conversion

\`m.get_convert<T>()\`.

| Interface | Description |
|------|------|
| \`can_convert_to(type_id)\` / \`can_convert_to<U>()\` | Whether conversion to the target type is possible |
| \`convert_to(src, target_type_id, dst)\` | Perform the conversion and write it to \`dst\`; returns false on failure |
| \`convert_to<U>(src)\` | Returns \`std::optional<U>\` |

### 11.12 compare_view — Field comparison/cloning

\`m.get_compare<T>()\`.

| Interface | Description |
|------|------|
| \`equal(a, b)\` | Field-by-field equality comparison; arrays are compared element by element |
| \`clone(src, dst)\` | Field-by-field copy construction; \`dst\` must be uninitialized memory |
| \`copy_assign(src, dst)\` | Field-by-field assignment; \`dst\` must already be constructed |
| \`destroy_fields(obj)\` | Field-by-field destruction |

### 11.13 hash_view — Field hashing

\`m.get_hash<T>()\`.

| Interface | Description |
|------|------|
| \`hash(obj)\` | Returns \`uint64_t\`; returns 0 when unbound or \`obj\` is null |

### 11.14 Usage example

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
q.get_by_name<float>(&v, "y") = 20.0f;  // modify the field

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

// private members
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

// enums
enum class Color { Red = 1, Green = 2 };
(void)m.add_enum<Color>("Color", {{Color::Red, "Red"}, {Color::Green, "Green"}});
auto ev = m.get_enum<Color>();
ev.value_to_name(Color::Green);                // "Green"

// inheritance
struct Base { int a; };
struct Derived : Base { int b; };
(void)m.add_type<Base>();
(void)m.add_field<Base>("a", &Base::a);
(void)m.add_type<Derived>();
(void)m.add_field<Derived>("b", &Derived::b);
(void)m.add_base<Derived, Base>();

Derived d{};
Base* base = m.get_inheritance<Derived>().upcast<Base>(&d);

// error handling
auto e = m.add_field<Vec3>("x", &Vec3::x);     // duplicate field
if (e.is_code(om_err_reflect_duplicate)) {
    std::cerr << "duplicate field\\n";
}

operating_message chain;                        // keeps only the first error
chain += m.add_field<Vec3>("x", &Vec3::x);
chain += m.add_field<Vec3>("y", &Vec3::y);
\`\`\`

Registering explicitly when the field count exceeds 256:

\`\`\`cpp
struct Wide {
    int a;
    int b;
    // remaining fields are listed in the same form
};

(void)m.add_type<Wide>();                       // create the type only, no automatic field traversal
(void)m.add_fields<Wide>(std::tuple{
    std::pair{"a", &Wide::a},
    std::pair{"b", &Wide::b},
    // one entry per member above
});
\`\`\`

### 11.15 Notes

| Practice | Description |
|------|------|
| Calling \`get<T>()\` on an unregistered type | Returns an invalid view with \`valid()\` false |
| Registering a field or method on an unregistered type | Returns \`om_err_reflect_type_not_found\`; call \`add_type<T>()\` first |
| \`invoke\` argument or return type mismatch | Returns \`om_err_reflect_method_not_found\` |
| Registering a field with a duplicate name | Returns \`om_err_reflect_duplicate\` |
| Empty field name | Returns \`om_err_reflect_invalid_name\` |
| More than 256 fields | Use \`add_type<T>()\` and \`add_fields<T>(...)\` to register all fields explicitly |
| Field and method counts | Grow with actual registrations; no fixed upper limit |
| Storage allocation failure | Returns \`om_err_out_of_memory\` |
| Calling write interfaces after freezing | Returns \`om_err_reflect_frozen\` |
| \`purge_type\` before retiring | Returns \`om_err_reflect_busy\` |
| \`array_element_ptr\` out of range | Returns nullptr |
| \`clone\` with an already constructed \`dst\` | Use \`copy_assign\` instead |
| \`create_inplace\` with an unaligned \`buf\` | Use \`alignas(T) char buf[sizeof(T)]\` |
| Type names, field names, method names, enum item names | The strings passed by the caller must have static storage duration |

### 11.16 Ways to register private members

| Approach | Applicable scenario | Caller input |
|------|---------|-----------|
| \`reflect_access<T>\` specialization + \`add_accessed_members<T>()\` | Types whose class definition can be modified | One friend line inside the class; the field and method lists are written outside the class |
| \`add_field_raw\` / \`add_private_offsets\` | Third-party types that cannot be modified | Fill in field names, offsets, and types manually |
| \`add_base_with_adjuster<D, B>(fn)\` | Private or virtual inheritance | Provide a pointer adjustment function inside the class |
| \`set_construct_destruct<T>(create, destroy)\` | Private constructors or destructors | Provide factory and destroy functions inside the class |

The list functions of \`reflect_access<T>\` return \`{name, member pointer}\` tuples: fields use the same form as \`add_fields\`, and methods use \`add_methods\`. When none of the three exist, \`add_accessed_members\` returns \`om_err_unsupported_operation\`.

---
`
};

window.DOCS_DATA_EN['reflection_meta'] = {
  title: "reflection — Metadata fields and capacity",
  content: `## 30. reflection — Metadata fields and capacity

Namespace \`reflect\`.

The metadata structure fields returned by the query interfaces, plus the module capacity configuration. For the high-level interfaces see [§ 11. reflection — Reflection module usage](#11-reflection--reflection-module-usage).

### 30.1 Metadata fields

\`m.field<T>(name)\` returns \`const field_meta*\`:

| Field | Description |
|------|------|
| \`name\` | Field name |
| \`offset\` | Offset from the start of the object (bytes) |
| \`type_id\` | Field type id; for an array field, the element type id |
| \`flags\` | Bit flags, queried with \`is_const()\` / \`is_private()\` |
| \`array_rank\` | Array rank; 0 for scalar |
| \`total_elements\` | Total number of array elements |
| \`extents[4]\` | Element count per dimension; the first \`array_rank\` entries are valid |
| \`element_stride\` | Element stride (bytes) |
| \`trivially_copyable\` | Whether the field (for arrays, the element) is trivially copyable |
| \`attrs\` | Field attribute list; each element contains \`key_hash\` and \`value\` |

\`m.method<T>(name)\` returns \`const method_meta*\`:

| Field | Description |
|------|------|
| \`name\` | Method name |
| \`arg_count\` | Number of arguments |
| \`return_type_id\` | Return type id; -1 for void |
| \`is_const\` / \`is_static\` | const method / static method |
| \`arg_type_ids\` | Array of argument type ids; nullptr means not set |
| \`vtable_offset\` | vtable offset of the virtual function; -1 for non-virtual |

Callback parameters of \`for_each_value\` from \`m.get_enum<E>()\`:

| Parameter | Description |
|------|------|
| \`value\` | Enum value, stored as the underlying type |
| \`name\` | Enum item name |

### 30.2 Capacity and configuration

| Configuration | Default | Description |
|------|--------|------|
| \`REFLECT_MAX_FIELDS\` | 64 | Upper bound of fields automatically traversed for an aggregate type; choose from 16/32/64/128/256 |

Field and method metadata grows with actual registrations and has no fixed count limit. When a type has more than 256 fields, use \`add_type<T>()\` and \`add_fields<T>(...)\` to register all fields explicitly, which is not constrained by the automatic traversal limit. The process-level by-name type registry has a separate limit that is independent of the per-type field and method counts.

The configuration lives in \`config/reflect_config.hpp\`; define a macro with the same name before including the reflection headers to override it.

---
`
};
