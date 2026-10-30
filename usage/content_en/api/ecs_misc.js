window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['runtime_view'] = {
  title: "runtime_view — Runtime view",
  content: `## 7. runtime_view — Runtime view

Dynamically specify a combination of component types at runtime for queries. There is no upper limit on the number of component types; the first 64 component types automatically maintain entity bitmasks, and component types beyond 64 also participate in all view/group queries.

### 7.1 Entity mask

Each entity automatically maintains multiple component bitmask blocks when components are added/removed; \`get_component_mask\` returns the first block, and other blocks are read via \`get_component_mask_block\`:

\`\`\`cpp
ecs::manager mgr;
auto e = mgr.create_entity();
mgr.add(e, Position{1, 0});
mgr.add(e, Velocity{2, 0});

uint64_t mask = mgr.get_component_mask(e);
// mask contains the bits for Position and Velocity

uint64_t pos_bit = mgr.get_component_bit<Position>();
bool has_pos = (mask & pos_bit) != 0;  // true
\`\`\`

### 7.2 Runtime view

\`\`\`cpp
// Two-component runtime view
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<Position>(),
    type_id::get_type_id<Velocity>()
});

rv.for_each([](entity e) {
    auto* p = rv.get_ptr<Position>(e);
    auto* v = rv.get_ptr<Velocity>(e);
    p->x += v->vx;
});
\`\`\`

### 7.3 Exclusion view

\`\`\`cpp
// Entities that have Position but not Velocity
auto rv = mgr.runtime_view_create(
    { type_id::get_type_id<Position>() },
    { type_id::get_type_id<Velocity>() }
);

rv.for_each([](entity e) {
    // process entities that only have Position
});
\`\`\`

### 7.4 Interfaces

| Interface | Description |
|------|------|
| \`runtime_view_create({ids...})\` | Create a runtime view; pass the list of component type_ids that must be owned |
| \`runtime_view_create({ids}, {exclude_ids})\` | Create an exclusion-style runtime view |
| \`runtime_view_create_from_terms(terms)\` | Create a view via \`runtime_term\`, supporting OR / OPTIONAL / NOT |
| \`for_each(func)\` | Iterate all matching entities; the callback receives an \`entity\` or no arguments |
| \`for_each_typed<Ts...>(func)\` | Iterate and pass component references; the callback receives \`entity, Ts&...\` or \`Ts&...\` |
| \`for_each_paged(offset, limit, func)\` | Paged iteration |
| \`for_each_changed(func)\` | Iterate entities changed since the previous call |
| \`size()\` | Number of hit entities |
| \`count()\` | Exact number of hits |
| \`empty()\` | Whether the view is empty |
| \`contains(entity)\` | Check whether an entity matches the query |
| \`get_ptr<T>(entity)\` | Get the component pointer of an entity (returns \`nullptr\` if the component is absent; does not check whether the entity belongs to this view) |
| \`get_first_entity()\` | Return the first matching entity |
| \`sort_by_component<T>(cmp)\` | Sort by component value; results are stored in \`sorted_entities_\` |
| \`get_sorted_entities()\` | Get the sorted entity list (\`const dense<entity>&\`; requires \`sort_by_component\` to be called first) |
| \`changed()\` | Detect whether the component pool version has changed |
| \`reset_change_tracking()\` | Reset the change-detection baseline |
| \`begin()\` / \`end()\` | Iterators, supporting range-for |
| \`rebuild()\` | Reselect the smallest set (call after the number of components changes) |

> \`get_component_mask(entity)\` and \`get_component_bit<T>()\` are methods of \`manager\`, see [§4](#4-ecsmanager-ecs管理器) and [§7.1](#71-实体掩码).

### 7.5 No upper limit on component types

The number of component types is not limited to 64. \`get_component_bit<T>()\` is only used for the first mask block, and returns 0 when type_id > 64; such components can still be added and queried normally, and participate in matching for all views/groups (\`view\` / \`group\` / \`owning_group\` / \`reorder_group\` / \`runtime_view\` / \`view(without)\`). Use \`get_component_mask_block(entity, block)\` to read other mask blocks.

\`\`\`cpp
ecs::manager mgr;

// Assume more than 64 component types have been registered; CompA has type_id = 64, CompB has type_id = 65
mgr.add(e1, CompA{});
mgr.add(e2, CompB{});
mgr.add(e3, CompA{});
mgr.add(e3, CompB{});

// CompB is beyond 64, get_component_bit returns 0, but queries still work
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<CompA>(),
    type_id::get_type_id<CompB>()
});
rv.for_each([](entity e) { /* hits e3 */ });

// group / view(without) likewise support component types beyond 64
auto g = mgr.group<CompA, CompB>();
mgr.view<CompA>(ecs::without<CompB>).for_each([](CompA&) {});
\`\`\`

### 7.6 for_each_typed — passing component references

\`for_each\` only passes back \`entity\`, so you must call \`get_ptr\` manually to obtain components. \`for_each_typed<Ts...>\` passes component references directly back; the order of \`Ts\` corresponds to the order of the type_ids passed to \`runtime_view_create\`.

\`\`\`cpp
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<Position>(),
    type_id::get_type_id<Velocity>()
});

// The callback receives entity + component references
rv.for_each_typed<Position, Velocity>([](entity e, Position& p, Velocity& v) {
    p.x += v.dx;
});

// The entity argument may also be omitted
rv.for_each_typed<Position>([](Position& p) {
    p.x += 1;
});
\`\`\`

### 7.7 for_each_paged — paged iteration

\`\`\`cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

// 100 per page; process the second page
rv.for_each_paged(100, 100, [](entity e) {
    // process the entity
});
\`\`\`

### 7.8 Change detection

Changes are detected through component pool version numbers. The first call to \`reset_change_tracking\` records the baseline, and \`changed\` compares the current version with the baseline.

\`\`\`cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

rv.reset_change_tracking();      // record baseline
// ... modify components ...
if (rv.changed()) {              // true if the version changed
    rv.for_each_changed([](entity e) {
        // iterate all matching entities (not incremental; full iteration)
    });
    // for_each_changed resets change tracking
}
\`\`\`

### 7.9 sort_by_component — sort by component

\`\`\`cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

rv.sort_by_component<Position>([](const Position& a, const Position& b) {
    return a.x < b.x;  // ascending
});

// The sorted result is cached in the view
for (const auto& e : rv.get_sorted_entities()) {
    auto* p = mgr.get_ptr<Position>(e);
    // process in ascending x order
}
\`\`\`

### 7.10 count — exact number of hits

\`\`\`cpp
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<Position>(),
    type_id::get_type_id<Velocity>()
});

size_t n = rv.count();  // exact match count, computed by iteration
\`\`\`

### 7.11 iterator — iterators

\`\`\`cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

// range-for
for (auto it = rv.begin(); it != rv.end(); ++it) {
    entity e = *it;
    auto* p = mgr.get_ptr<Position>(e);
}

// equivalent to
for (entity e : rv) {
    // process e
}
\`\`\`

### 7.12 runtime_term — OR / OPTIONAL / NOT queries

Construct query conditions via \`runtime_term\`, supporting OR (union), NOT (exclusion), and OPTIONAL operations.

\`\`\`cpp
class_pool<ecs::runtime_term> terms;
// AND: op=0
terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Position>(), 0, ecs::access_mode::read_write});
// OR: op=1
terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Velocity>(), 1, ecs::access_mode::read_only});
// NOT: op=2
terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Health>(), 2, ecs::access_mode::read_only});
// OPTIONAL: op=3
terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Name>(), 3, ecs::access_mode::read_only});

auto rv = mgr.runtime_view_create_from_terms(std::move(terms));
rv.for_each([](entity e) {
    // hit: has Position OR Velocity, and does not have Health
});
\`\`\`

| op value | Semantics | Description |
|-------|------|------|
| 0 | AND | Must be owned |
| 1 | OR | Matches at least one OR term |
| 2 | NOT | Must not be owned |
| 3 | OPTIONAL | Optional; does not affect matching |

### 7.13 access_mode — read/write annotation

\`runtime_term.access\` annotates the component access mode, used for intent declaration.

\`\`\`cpp
class_pool<ecs::runtime_term> terms;
terms.emplace_back(ecs::runtime_term{
    type_id::get_type_id<Position>(), 0, ecs::access_mode::read_only});
terms.emplace_back(ecs::runtime_term{
    type_id::get_type_id<Velocity>(), 0, ecs::access_mode::read_write});

auto rv = mgr.runtime_view_create_from_terms(std::move(terms));
\`\`\`

| Value | Semantics |
|----|------|
| \`access_mode::read_only\` | Read-only access |
| \`access_mode::read_write\` | Read/write access |

### What not to do

| Wrong approach | Problem | Correct approach |
|---------|------|---------|
| Relying on \`size()\` for an exact value | It returns an upper bound on the main-set size, not the exact match count | Use \`count()\` for the exact value, or iterate with \`for_each\` |
| Using \`get_component_bit<T>() != 0\` to test whether a component exists | Components with type_id > 64 always have a bit of 0 | Use \`get_ptr<T>(e) != nullptr\` or a view query instead |
| Forgetting \`rebuild()\` after the number of components changes | The main-set selection may not be optimal | Call \`rebuild()\` after adding or removing component types |
| Relying on \`for_each_changed\` for incremental semantics | It performs a full traversal of matching entities, not only changed ones | Change detection only tells you whether *any* change occurred |
| Modifying components after \`sort_by_component\` without re-sorting | The sort cache becomes inconsistent with the actual data | Call \`sort_by_component\` again after component changes |

---
`
};

window.DOCS_DATA_EN['function_storage'] = {
  title: "Function storage (callbacks as components)",
  content: `## 8. Function storage (callbacks as components)

\`t_fun\` can be used directly as an ECS component with no wrapper struct. Different function signatures are automatically deduced as different component types, and are stored and invoked through the standard component interfaces of \`ecs::manager\`. See [§ 34. t_fun](#34-t_fun--函数类型延迟调用器).

### Usage

\`\`\`cpp

// Target functions for the callback
int  on_add(int a, int b) { return a + b; }
void on_callback(int x) { std::printf("cb: %d\\n", x); }

ecs::manager mgr;
entity e = mgr.create_entity();

// Store t_fun directly as a component (no wrapper struct needed)
mgr.add(e, t_fun{on_add, 10, 20});

// Fetch and invoke
auto* cb = mgr.get_ptr<t_fun<int(int,int)>>(e);
if (cb)
{
    (*cb)();               // 30, invoked with bound arguments
    (*cb)(3, 4);           // 7,  invoked with overriding arguments
    cb->set_arg<0>(100);
    (*cb)();               // 300
}
\`\`\`

### void return value and functions without parameters

\`\`\`cpp
// void callback
entity e2 = mgr.create_entity();
mgr.add(e2, t_fun{on_callback, 42});
auto* v = mgr.get_ptr<t_fun<void(int)>>(e2);
(*v)();                    // prints "cb: 42"
v->result_ptr();           // nullptr

// Function without parameters
void on_noop() {}
entity e3 = mgr.create_entity();
mgr.add(e3, t_fun{on_noop});
auto* n = mgr.get_ptr<t_fun<void()>>(e3);
(*n)();
\`\`\`

### Batch invocation through a View

\`\`\`cpp
// Add a callback to multiple entities
entity e1 = mgr.create_entity();
entity e2 = mgr.create_entity();
entity e3 = mgr.create_entity();
mgr.add(e1, t_fun{on_add, 1, 2});
mgr.add(e2, t_fun{on_add, 10, 20});
mgr.add(e3, t_fun{on_add, 100, 200});

// Iterate all callback components and invoke them
size_t count = 0;
int sum = 0;
mgr.view<t_fun<int(int,int)>>().for_each([&](entity, t_fun<int(int,int)>& c) {
    sum += c();
    ++count;
});
// count == 3, sum == 333
\`\`\`

### Changes inside a component persist

\`\`\`cpp
entity e = mgr.create_entity();
mgr.add(e, t_fun{on_add, 1, 2});

// Modify on the first query
auto* p1 = mgr.get_ptr<t_fun<int(int,int)>>(e);
p1->set_arg<0>(100);
p1->set_arg<1>(200);

// Verify persistence on the second query
auto* p2 = mgr.get_ptr<t_fun<int(int,int)>>(e);
(*p2)();                   // 300, the modification persisted
\`\`\`

---
`
};

window.DOCS_DATA_EN['signals'] = {
  title: "signals — Lifecycle signals",
  content: `## 9. Lifecycle signals

Two-layer architecture: **immediate signals** (function-pointer callbacks) + **deferred signals** (ring buffer + overflow chain, batch processing).

Events with registered immediate callbacks fire synchronously; those without a registered callback are handled by \`flush_*_signals\`.

### 9.1 Entity-level immediate signals

Callbacks fire immediately when an entity is created/destroyed. Context is passed through \`void* user_data\`.

**Interfaces:**

| Interface | Description |
|------|------|
| \`set_on_entity_created(fn, user_data)\` | Bind the entity-created callback: \`void fn(entity, void* user_data)\` |
| \`set_on_entity_destroyed(fn, user_data)\` | Bind the entity-destroyed callback: \`void fn(entity, void* user_data)\` |

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100);

int created = 0, destroyed = 0;

// Captureless lambdas (convertible to function pointers)
auto on_created = [](entity e, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
};
auto on_destroyed = [](entity e, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
};

mgr.set_on_entity_created(+on_created, &created);
mgr.set_on_entity_destroyed(+on_destroyed, &destroyed);

entity e1 = mgr.create_entity();  // created == 1
entity e2 = mgr.create_entity();  // created == 2
mgr.delete_entity(e1);           // destroyed == 1
\`\`\`

> **Note:** The callback must have the signature \`void (*)(entity, void*) noexcept\`. Use \`+\` to convert a captureless lambda into a function pointer.

### 9.2 Component-level immediate signals

Callbacks fire immediately when a component is added/removed. The callback receives the entity, the component pointer, and \`user_data\`, and can modify component data directly inside the callback.

**Interfaces:**

| Interface | Description |
|------|------|
| \`set_on_add<T>(fn, user_data)\` | Bind the add callback for component T: \`void fn(entity, void* component, void* user_data)\` |
| \`set_on_remove<T>(fn, user_data)\` | Bind the remove callback for component T: \`void fn(entity, void* component, void* user_data)\` |

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100);

int add_count = 0, remove_count = 0;

auto on_add = [](entity e, void* comp, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
    auto* pos = static_cast<Position*>(comp);
    pos->x = 100;  // modify the component directly
};
auto on_remove = [](entity e, void* comp, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
};

mgr.set_on_add<Position>(+on_add, &add_count);
mgr.set_on_remove<Position>(+on_remove, &remove_count);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 2});        // add_count == 1, Position.x changed to 100
mgr.add(e, Position{3, 4});        // overwrite: falls back to on_remove(old) + on_add(new) when on_modify is not registered
mgr.hard_remove<Position>(e);      // remove_count == 2 (overwrite 1 + hard_remove 1)
\`\`\`

> **Note:** The component pointer is valid during the callback and can be used to read or modify component data. \`hard_remove\` triggers \`on_remove\`; \`soft_remove\` only logically hides the component (it is not destroyed at that time), and **does not trigger** \`on_remove\` nor enter the deferred queue; the old object is destroyed later when its dead slot is reused and overwritten by \`add\` or reclaimed by \`compact\`.

### 9.3 Overwrite and on_modify

Adding the same component to the same entity again is called an overwrite. The overwrite semantics are determined by whether \`on_modify\` is registered:

- **\`on_modify<T>\` registered**: the overwrite triggers only \`on_modify\`, not \`on_remove\`/\`on_add\`.
- **\`on_modify<T>\` not registered**: the overwrite falls back to \`on_remove\` (old component) + \`on_add\` (new component).

| Interface | Description |
|------|------|
| \`set_on_modify<T>(fn, user_data)\` | Bind the overwrite callback for component T: \`void fn(entity, void* component, void* user_data)\` |

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(10);
int add_cnt = 0, remove_cnt = 0, modify_cnt = 0;
mgr.set_on_add<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &add_cnt);
mgr.set_on_remove<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &remove_cnt);
mgr.set_on_modify<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &modify_cnt);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 0});   // add_cnt == 1
mgr.add(e, Position{2, 0});   // modify_cnt == 1, add_cnt/remove_cnt unchanged
\`\`\`

> **Don't:** use \`on_add\` to distinguish a new add from an overwrite. Once \`on_modify\` is registered, the overwrite path no longer goes through \`on_add\`.

### 9.4 Entity-level deferred signals

Entity create/destroy events are pushed into a ring buffer and processed in batch when \`flush_entity_signals\` is called. Suitable for batch synchronization and scenarios that avoid reentrancy.

**Interfaces:**

| Interface | Description |
|------|------|
| \`flush_entity_signals(handler)\` | Process all pending entity signals in batch: \`handler(uint32_t type, uint32_t entity_idx)\` |
| \`has_pending_entity_signals()\` | Whether there are pending entity signals |

**Signal types:**
- \`type=0\`: entity created
- \`type=1\`: entity destroyed

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100);

entity e1 = mgr.create_entity();  // pushed into the buffer
entity e2 = mgr.create_entity();  // pushed into the buffer
mgr.delete_entity(e1);           // pushed into the buffer

int created = 0, destroyed = 0;

// Batch processing
mgr.flush_entity_signals([&](uint32_t type, uint32_t entity_idx) {
    if (type == 0)
        created += 1;
    else if (type == 1)
        destroyed += 1;
});

// created == 2, destroyed == 1
assert(!mgr.has_pending_entity_signals());
\`\`\`

> **Buffer capacity:** 1024 entries (a power of two). When the buffer is full, events fall into \`overflow_chain\`, \`overflow_count\` accumulates, and \`flush\` consumes them too — nothing is dropped silently.

### 9.5 Component-level deferred signals

Component add/remove events are pushed into a ring buffer and processed in batch when \`flush_component_signals\` is called.

**Interfaces:**

| Interface | Description |
|------|------|
| \`flush_component_signals(handler)\` | Process all pending component signals in batch: \`handler(uint32_t type, uint32_t entity_idx, uint32_t component_id)\` |
| \`has_pending_component_signals()\` | Whether there are pending component signals |

**Signal types:**
- \`type=0\`: component added
- \`type=1\`: component removed

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 2});    // pushed into the buffer
mgr.add(e, Velocity{10, 20});  // pushed into the buffer
mgr.add(e, Health{100, 100});  // pushed into the buffer
mgr.hard_remove<Position>(e);  // pushed into the buffer

int added = 0, removed = 0;

mgr.flush_component_signals([&](uint32_t type, uint32_t entity_idx, uint32_t component_id) {
    if (type == 0)
        added += 1;
    else if (type == 1)
        removed += 1;
});

// added == 3, removed == 1
assert(!mgr.has_pending_component_signals());
\`\`\`

### 9.6 Immediate vs deferred callbacks

After an immediate callback is registered, the corresponding event fires synchronously:

- After \`set_on_add<T>\` is registered: \`add\` triggers \`on_add\` synchronously.
- After \`set_on_remove<T>\` is registered: \`hard_remove\` triggers \`on_remove\` synchronously.
- After \`set_on_entity_created\` is registered: \`create_entity\` fires synchronously.
- After \`set_on_entity_destroyed\` is registered: \`delete_entity\` fires synchronously.

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(10);
int add_cb = 0;
mgr.set_on_add<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &add_cb);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 0});   // add_cb == 1, not enqueued

int add_sig = 0;
mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
    if (type == 0) ++add_sig;
});
// add_sig == 0 (immediate callback already registered; not enqueued again)
\`\`\`

> **Don't:** subscribe an immediate callback for an event and also expect \`flush\` to receive it. The design is mutually exclusive — an event takes only one path.

### 9.7 Signal switches and overflow

**Signal switches** control whether deferred queues enqueue (they do not affect immediate callbacks):

| Interface | Description |
|------|------|
| \`disable_comp_signals()\` / \`enable_comp_signals()\` | Disable/enable enqueuing of component deferred signals |
| \`disable_entity_signals()\` / \`enable_entity_signals()\` | Disable/enable enqueuing of entity deferred signals |

**Overflow handling:** When the ring buffer is full, events fall into \`overflow_chain\`; \`flush_*_signals\` consumes the buffer first, then the chain. \`overflow_count\` accumulates overflow occurrences (not cleared by flush) and must be cleared manually with \`reset_*_overflow_count\`.

| Interface | Description |
|------|------|
| \`comp_signal_overflow_count()\` / \`entity_signal_overflow_count()\` | Query the accumulated overflow count |
| \`reset_comp_signal_overflow_count()\` / \`reset_entity_signal_overflow_count()\` | Clear the overflow count |
| \`preallocate_component_signals(n)\` / \`preallocate_entity_signals(n)\` | Preallocate overflow chain capacity |

\`\`\`cpp
ecs::manager mgr;
mgr.disable_entity_signals();
auto e1 = mgr.create_entity();   // not enqueued
mgr.enable_entity_signals();
auto e2 = mgr.create_entity();   // enqueued
// has_pending_entity_signals() == true

mgr.preallocate_component_signals(2048);  // preallocate overflow capacity
\`\`\`

> **Don't:** ignore \`overflow_count\` for long periods. A value > 0 means overflow has occurred; confirm that \`flush\` has consumed the whole chain (\`has_pending_*_signals()\` is false).

### 9.8 Component cleanup in delete_entity

\`delete_entity\` first iterates all components on the entity to trigger cleanup, then destroys the entity:

- Components with \`on_remove<T>\` registered: \`on_remove\` is triggered synchronously.
- Components without \`on_remove<T>\` registered: a remove signal is enqueued to the component deferred queue.

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(10);
int pos_removed = 0;
mgr.set_on_remove<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &pos_removed);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 0});
mgr.add(e, Velocity{2, 0});   // Velocity has no on_remove registered
mgr.delete_entity(e);         // pos_removed == 1; Velocity remove signal enqueued

int removed_sig = 0;
mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
    if (type == 1) ++removed_sig;
});
// removed_sig == 1 (Velocity)
\`\`\`

> **Don't:** access an already-destroyed entity inside an \`on_remove\` callback. The callback fires before the entity is destroyed, so the entity is still valid at that point; the entity is destroyed only after the callback returns.
>
> **Suggested flush order:** call \`flush_component_signals\` before \`flush_entity_signals\`. Component remove signals are enqueued before the entity is destroyed, so consuming component signals first avoids misattribution caused by entity id reuse.

### 9.9 Immediate vs deferred signal selection guide

| Scenario | Recommended | Reason |
|------|----------|------|
| Logging/debugging | Immediate signals | Output is needed immediately; deferral is meaningless |
| Data validation | Immediate signals | Validation is needed at the moment the component is written |
| Network synchronization | Deferred signals | Batch sending reduces network overhead |
| Physics/render callbacks | Deferred signals | Avoid triggering reentrancy in the middle of ECS operations |
| File persistence | Deferred signals | Batch writing reduces I/O |
| Third-party integration | Deferred signals | Decouples ECS internal state from external systems |

### What not to do

| Wrong approach | Problem | Correct approach |
|---------|------|---------|
| Adding/removing entities or components inside an immediate signal callback | May cause reentrancy | Use deferred signals and handle them during flush; flush has reentrancy protection and a budget limit |
| Assuming the deferred signal buffer never drops events | When the buffer is full, events fall into overflow_chain | Flush regularly; in batch scenarios preallocate with \`preallocate_*_signals\` |
| Forgetting to \`flush\` deferred signals | Events pile up in the buffer and chain unprocessed | Call \`flush_*_signals\` at the start or end of each frame |
| Using a capturing lambda as an immediate signal callback | It cannot be converted to a function pointer | Use a captureless lambda + \`user_data\` to pass context |
| Expecting \`soft_remove\` to trigger \`on_remove\` | \`soft_remove\` only hides logically; it neither destroys nor triggers | Use \`hard_remove\` when a callback is needed |
| Both registering an immediate callback for an event and waiting for flush | The mutually exclusive design never notifies twice; flush does not receive that event | Pick one: register a callback for immediate behavior, or don't register for batch behavior |
| Accessing a destroyed entity inside an \`on_remove\` callback | The entity is destroyed only after the callback returns; inside the callback you see the pre-destruction state | You may safely read components inside the callback, but don't rely on the entity remaining valid afterwards |
| Pointing \`user_data\` at a local variable and letting it go out of scope | The callback holds a dangling pointer; later triggers write to wild memory and corrupt adjacent stack variables | Unbind before the block ends with \`set_on_*(nullptr, nullptr)\`, or point \`user_data\` at an object with a sufficiently long lifetime |

---
`
};

window.DOCS_DATA_EN['command_buffer'] = {
  title: "command_buffer — Deferred structural changes",
  content: `## 10. command_buffer — Deferred structural changes

Structural changes such as adding/removing components and destroying entities are staged and applied to the manager all at once on \`flush\`. Suitable for batch submission at frame end and deferred execution in the main loop.

> For deletion during iteration inside a single \`view\`, you can use \`for_each_safe\` directly and do not need \`command_buffer\`. \`command_buffer\` is meant for deferred batch operations across views or across frames.

### Usage

\`\`\`cpp
ecs::manager mgr;
auto a = mgr.create_entity();
auto b = mgr.create_entity();
auto c = mgr.create_entity();
mgr.add(a, Position{1, 0, 0});

// Create a command_buffer
auto cb = mgr.create_command_buffer();

// Record commands (not executed immediately)
cb.add_component<Position>(b, Position{2, 0, 0});
cb.add_component<Velocity>(a, Velocity{10, 0, 0});
cb.remove_component<Position>(a);
cb.delete_entity(c);

// State is unchanged before flush
// mgr.get_ptr<Position>(b) == nullptr
// mgr.get_ptr<Position>(a) != nullptr

// Apply all at once
cb.flush();

// After flush
// mgr.get_ptr<Position>(b) != nullptr, x == 2.0f
// mgr.get_ptr<Velocity>(a) != nullptr
// mgr.get_ptr<Position>(a) == nullptr
// !mgr.is_entity_valid(c)
\`\`\`

**Interfaces:**

| Interface | Description |
|------|------|
| \`create_command_buffer()\` | Manager factory method; returns a command_buffer bound to this manager |
| \`deferred_entity create_entity()\` | Record a create-entity command; returns a placeholder entity id |
| \`entity resolve(deferred_entity)\` | Query the real entity corresponding to a placeholder entity; returns an invalid entity if not yet replayed, if creation failed, or if the entity has been deleted |
| \`add_component<T>(entity/deferred_entity, T&&)\` | Record an add-component command |
| \`add_component_batch<T>(entities, components)\` | Record a batch add-component command |
| \`remove_component<T>(entity/deferred_entity)\` | Record a remove-component command (soft_remove semantics) |
| \`remove_component_batch<T>(entities)\` | Record a batch remove-component command |
| \`add_def_component(entity/deferred_entity, def_id/name, data)\` | Record an add-def-component command |
| \`delete_entity(entity/deferred_entity)\` | Record a delete-entity command |
| \`preallocate(n)\` | Reserve command buffer capacity to avoid reallocation while recording |
| \`capacity()\` | Current command buffer capacity |
| \`operating_message flush()\` | Apply all commands in recorded order, then clear them; successful commands are retained, and the first error is returned on failure |
| \`clear()\` | Clear all unapplied commands and the placeholder mapping; old \`deferred_entity\` values become invalid |
| \`size()\` | Number of unapplied commands |
| \`empty()\` | Whether it is empty |

### What not to do

| Wrong approach | Problem | Correct approach |
|---------|------|---------|
| Using \`entity.is_valid()\` after \`flush\` to determine whether an entity was destroyed | \`delete_entity\` takes the argument by value, so it modifies a copy | Check with \`mgr.is_entity_valid(entity)\` |
| Continuing to use the handle of a destroyed entity after \`flush\` | The version has expired; operations are invalid | Re-acquire a valid entity after \`flush\` |
| Using a command_buffer across managers | The apply function is bound to the manager at creation time | Create a command_buffer independently for each manager |
| Recording new commands into the same command_buffer during \`flush\` | The buffer is cleared when \`flush\` finishes, so new commands are lost | Record new commands only after \`flush\` completes |

---
`
};

window.DOCS_DATA_EN['reorder'] = {
  title: "reorder — Component reordering",
  content: `## 13. reorder_group — Multi-component reordered iteration

Namespace \`ecs\`. Intersects multiple component sets and provides joint iteration.

### Interfaces

| Interface | Description |
|------|------|
| \`reorder_group(mgr, sets)\` | Construct; binds a manager and an array of component sets |
| \`rebuild()\` | Rebuild the intersection index |
| \`share_with(other)\` | Share intersection state with another reorder_group |
| \`size()\` | Number of entities in the intersection |
| \`empty()\` | Whether the intersection is empty |
| \`contains(e)\` | Whether an entity is in the intersection |
| \`get<T>(e)\` | Get the pointer to the T-typed component of entity e |
| \`front()\` / \`back()\` | First/last entity of the intersection |
| \`for_each(func)\` | Iterate the intersection; \`func\` signature \`(entity, T&...)\` or \`(T&...)\` |

### Usage

\`\`\`cpp

ecs::manager mgr;
auto e1 = mgr.create_entity();
auto e2 = mgr.create_entity();
mgr.add<Position>(e1, Position{1, 0, 0});
mgr.add<Velocity>(e1, Velocity{2, 0, 0});
mgr.add<Position>(e2, Position{3, 0, 0});
mgr.add<Velocity>(e2, Velocity{4, 0, 0});

// Construct a reorder_group
std::array<single_class_set*, 2> sets = {
    mgr.get_single_class_set<Position>(),
    mgr.get_single_class_set<Velocity>()
};
reorder_group<Position, Velocity> rg(&mgr, sets);

// Iterate all entities that have both Position and Velocity
rg.for_each([](entity e, Position& p, Velocity& v) {
    p.x += v.dx;
    p.y += v.dy;
});

// Check whether an entity is in the intersection
bool has = rg.contains(e1);  // true

// Get a component
Velocity* v = rg.get<Velocity>(e1);
\`\`\`

### Caveats

| Wrong approach | Problem | Correct approach |
|---------|------|---------|
| Not calling \`rebuild\` after modifying component pools | Iterates stale data | Call \`rebuild()\` after adding or removing components |
| Modifying component pool structure inside \`for_each\` | Iterator invalidation | Don't add or remove during iteration |
| Calling \`rebuild\` independently after \`share_with\` | Shared state gets overwritten | The sharing party should not rebuild independently |
`
};
