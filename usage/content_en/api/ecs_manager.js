window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['manager'] = {
  title: "ecs::manager — ECS manager",
  content: `## 4. ecs::manager — ECS manager

Core ECS management class; it owns entities and all component sets.

> Note: \`manager\` is movable; copying is prohibited.
>
> The \`error\` used in this chapter's examples is the same \`operating_message\` object reused within one scope: \`operating_message error;\`. On failure the library writes code, level and site; on success the object is neither read nor written.

### Minimum usage

\`\`\`cpp
struct Position { float x, y; };
struct Velocity { float dx, dy; };

ecs::manager mgr;
mgr.preallocate_entities(1024);

ecs::entity e = mgr.create_entity();
mgr.add(e, Position{1.0f, 2.0f});
mgr.add(e, Velocity{3.0f, 4.0f});

Position* p = mgr.get_ptr<Position>(e);      // nullptr when the entity is invalid or the component is not attached
if (p) { p->x += 1.0f; }

mgr.view<Position, Velocity>().for_each([](Position& pos, Velocity& vel) {
    pos.x += vel.dx;
    pos.y += vel.dy;
});

(void)mgr.delete_entity(e);
\`\`\`

### Find the interface by task

| I want to… | See |
|------|------|
| Reserve capacity during startup | [Preallocation interfaces](#preallocation-interfaces) |
| Create or delete entities | [Entity management](#entity-management) |
| Add components, batch add | [Adding components](#adding-components) |
| Get component pointers, batch query | [Retrieving components](#retrieving-components) |
| Check whether a component exists | [Component existence queries](#component-existence-queries) |
| Iterate entities and components | [View system](#view-system) |
| Group and lay out contiguously | [Group system](#group-system) |
| Run-time composite queries | [runtime_view](#runtime_view) |
| Listen for add/remove events | [Lifecycle signals](#lifecycle-signals) |

For deferred entity or component add/remove across frames, see [command_buffer](#/command_buffer).

### Preallocation interfaces

All high-level preallocation interfaces use the \`preallocate_\` prefix and reserve capacity once at startup to avoid run-time growth. Each is independent; call them as needed.

| Interface | Description |
|------|------|
| \`preallocate_entities(count)\` | Preallocate entities into the pool (raises the capacity of every component set as well) |
| \`preallocate_components<T>(capacity)\` | Reserve set capacity for component T |
| \`preallocate_mask_blocks(num_blocks)\` | Preallocate mask blocks per entity (64 component kinds per block; call before registering components) |
| \`preallocate_entity_signals(n)\` | Preallocate the overflow chain capacity for deferred entity signals |
| \`preallocate_component_signals(n)\` | Preallocate the overflow chain capacity for deferred component signals |

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100000);
mgr.preallocate_components<Position>(100000);
mgr.preallocate_components<Velocity>(50000);
mgr.preallocate_mask_blocks(2);
mgr.preallocate_entity_signals(4096);
mgr.preallocate_component_signals(4096);
\`\`\`

### Entity management

| Interface | Description |
|------|------|
| \`create_entity()\` | Create an entity (prefers the preallocated pool) |
| \`is_entity_valid(entity)\` | Check entity validity (version matches) |
| \`delete_entity(entity)\` | Delete the entity and all its components (releases the ID, bumps the version); returns \`operating_message\` |
| \`delete_entity_batch(entities, size_t& deleted_count)\` | Batch delete entities, skipping invalid handles and reporting errors; returns \`operating_message\` |
| \`clear_entity(entity)\` | Clear all components of the entity while keeping the entity itself; returns \`operating_message\` |
| \`alive_count()\` | Current number of live entities |
| \`empty()\` | Whether no live entity exists |

\`\`\`cpp
ecs::entity e = mgr.create_entity();
if (!mgr.delete_entity(e))
{
    // invalid or already deleted handle
}

size_t removed = 0;
std::span<const ecs::entity> targets(entities.data(), entities.size());
mgr.delete_entity_batch(targets, removed);
\`\`\`

### Adding components

| Interface | Description |
|------|------|
| \`add<T>(entity, T)\` | Add a component; returns \`operating_message\` (entity first) |
| \`add<T>(T, entity)\` | Add a component; returns \`operating_message\` (component first, argument order is interchangeable) |
| \`add_batch<T>(span<const entity>, span<const T>)\` | Batch add (span version) |
| \`add_batch<T>(const class_pool<entity>&, const class_pool<T>&)\` | Batch add (lvalue references) |
| \`add_batch<T>(class_pool<entity>&&, class_pool<T>&&)\` | Batch add (rvalue references, move semantics) |
| \`add_batch<T>(const std::vector<entity>&, const std::vector<T>&)\` | Batch add (vector arguments) |
| \`add_batch<T>(const std::array<entity, N>&, const std::array<T, N>&)\` | Batch add (array arguments, fixed length at compile time) |
| \`add_batch<T>(const entity*, const T*, size_t)\` | Batch add (raw pointer + length) |
| \`create_with<Ts...>(entity& out, Ts&&...)\` | Create an entity with components attached; rolls back on failure; returns \`operating_message\` |
| \`clone(entity source, entity& out)\` | Copy all components and user state of the source entity; rolls back on failure; returns \`operating_message\` |

\`\`\`cpp
ecs::entity e{};
if (!mgr.create_with(e, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f}))
{
    // creation failed, e stays invalid
}

ecs::entity copy{};
if (!mgr.clone(e, copy))
{
    // a component is not copyable or capacity is insufficient, copy stays invalid
}
\`\`\`

> \`clone\` copies every template component, run-time def component, \`tag\` / \`layer\` / \`group\`, plus the \`active\` / \`disabled\` / \`static_entity\` flags; it does not copy the entity index/version, component slots, \`pending_destroy\`, signal queues, iteration state or the change log. If any component is not copyable it returns \`om_err_unsupported_operation\`, and if capacity is insufficient it returns \`om_err_capacity_exceeded\`.

### Run-time def components

Used with types registered via \`type_id::register_type_def\` (\`type_def\` is described on the type_id page). Trivial types are copied byte-wise; non-trivial types must provide \`copy_construct\` and \`destruct\`. def components share the mask/view/signal machinery with template components.

| Interface | Description |
|------|------|
| \`add_def(entity, def_id, const void* data)\` | Add a def component by ID (the data length is the \`type_def::size\` used at registration) |
| \`add_def(entity, name, const void* data)\` | Add by name (looked up by name) |
| \`get_def_ptr(entity, def_id)\` | Get the def component pointer by ID |
| \`get_def_ptr(entity, def_id) const\` | const version |
| \`get_def_ptr(entity, name)\` | Get by name |
| \`get_def_ptr(entity, name) const\` | const version |
| \`hard_remove_def(entity, def_id)\` | Hard remove by ID |
| \`hard_remove_def(entity, name)\` | Hard remove by name |
| \`soft_remove_def(entity, def_id)\` | Soft remove by ID |
| \`soft_remove_def(entity, name)\` | Soft remove by name |
| \`get_single_class_set_by_id(type_id)\` | Get the component set by ID (def and template alike) |

### Retrieving components

| Interface | Description |
|------|------|
| \`get_ptr<T>(entity)\` | Get the component pointer (with checks) |
| \`get_ptr<T>(entity) const\` | const version |
| \`get_ptr_fast<T>(entity)\` | Fast retrieval (skips the type_id check) |
| \`get_ptr_fast<T>(entity) const\` | const version |
| \`get_ptr_batch<T>(entities, results, count)\` | Batch query component pointers (raw pointer + length) |
| \`get_ptr_batch<T>(span<const entity>, span<T*>)\` | Batch query (span arguments, lengths must match) |
| \`get_ptr_batch<T>(const vector<entity>&, vector<T*>&)\` | Batch query (vector arguments) |
| \`get_ptr_batch<T>(const array<entity, N>&, array<T*, N>&)\` | Batch query (array arguments) |
| \`prefetch_ptr<T>(entity)\` | Prefetch the sparse entry of the entity |
| \`prefetch_ptr_batch<T>(entities, count)\` | Batch prefetch (raw pointer + length) |
| \`prefetch_ptr_batch<T>(span<const entity>)\` | Batch prefetch (span arguments) |
| \`prefetch_ptr_batch<T>(const vector<entity>&)\` | Batch prefetch (vector arguments) |
| \`prefetch_ptr_batch<T>(const array<entity, N>&)\` | Batch prefetch (array arguments) |
| \`prefetch_ptr_data<T>(entity)\` | Prefetch component data (the sparse entry must be loaded first to obtain the dense index) |
| \`get_ptr_fast_cached<T>(set, entity)\` | Fast query with a cached set pointer (avoids repeated get_single_class_set) |
| \`prefetch_ptr_cached<T>(set, entity)\` | Prefetch the sparse entry with a cached set pointer |
| \`prefetch_ptr_data_cached<T>(set, entity)\` | Prefetch component data with a cached set pointer |

> Query interfaces (\`get_ptr\` / \`get_ptr_fast\` / \`get_ptr_batch\`) return \`nullptr\` uniformly when nothing is found: an invalid entity handle, an entity that does not carry the component, a \`T\` that does not match the component type, or a component type that was never registered — all four cases yield \`nullptr\`. No compile-time assertion is triggered, and no run-time error or hint is produced; the caller must check for null.

> Both \`get_ptr\` and \`get_ptr_fast\` can be used directly, with no extra warm-up or caching.

### Component existence queries

| Interface | Description |
|------|------|
| \`has_component<T>(entity)\` | Whether the entity carries the component (\`false\` when the entity is invalid) |
| \`has_all_components<Ts...>(entity)\` | Whether the entity carries all the specified components |
| \`has_any_component<Ts...>(entity)\` | Whether the entity carries any of the specified components |
| \`has_component_batch<T>(span<const entity>, span<uint8_t>)\` | Batch query; returns an error if the result buffer is too small |
| \`component_count<T>()\` | Number of live instances of that component type |
| \`component_count_by_id(int)\` | Count live instances by type_id |

\`\`\`cpp
if (mgr.has_all_components<Position, Velocity>(e))
{
    // both components are present
}

const size_t positions = mgr.component_count<Position>();
\`\`\`

### query_context

| Interface | Description |
|------|------|
| \`query_context<T>(manager&)\` | Construct a query context, caching the set/sparse/pool pointers |
| \`get_ptr(entity)\` | Inline fast component pointer query, returns \`T*\` |
| \`get_ptr(entity) const\` | const version, returns \`const T*\` |
| \`prefetch_sparse(entity) const\` | Prefetch the sparse entry |
| \`prefetch_data(entity) const\` | Prefetch component data |
| \`valid() const\` | Whether the context is valid (whether the component type is registered) |

> When \`valid()\` is false (component type not registered), \`get_ptr\` always returns \`nullptr\`; it also returns \`nullptr\` when the entity does not carry the component. No compile-time assertion is triggered and no run-time error is produced.

### Removing components

| Interface | Description |
|------|------|
| \`soft_remove<T>(entity)\` | Soft remove the component; returns \`operating_message\` (clears only the sparse entry, leaving a hole) |
| \`hard_remove<T>(entity)\` | Hard remove the component; returns \`operating_message\` |
| \`soft_remove_batch<T>(span<const entity>, size_t& removed_count)\` | Batch soft remove; returns \`operating_message\` |
| \`hard_remove_batch<T>(span<const entity>, size_t& removed_count)\` | Batch hard remove; returns \`operating_message\` |
| \`clear_component_type<T>()\` | Clear the whole type container (including mask bits and entity state); returns \`operating_message\` |

> For \`std::is_trivially_copyable\` types, \`hard_remove\` and \`swap_dense_and_pool\` operate directly through \`typed_pool_data_\` + \`memcpy\`, skipping the function-pointer indirection. Non-trivial types fall back to the \`ops_.swap_pop\` / \`ops_.swap_pool\` function-pointer path.

### Container access

| Interface | Description |
|------|------|
| \`get_single_class_set<T>()\` | Get the single component set pointer |
| \`get_single_class_set<T>() const\` | const version |
| \`get_component_container<T>()\` | Get the typed component pool pointer |
| \`is_component_type_registered<T>()\` | Whether the component type is already registered in the manager (the component set exists; whether instances exist is irrelevant) |
| \`add/add_batch/hard_remove/soft_remove\` | Returns \`operating_message\`; the object is empty on success |
| \`get_component_meta(int type_id)\` | Get component metadata (including the component size) |
| \`get_single_class_set_by_id(int type_id)\` | Get the component set by type_id (used by run-time views) |
| \`get_entity_manager()\` | Get the \`entity_manager&\` reference (exposes mask / state / flag interfaces) |
| \`get_entity_manager() const\` | const version |

### Storage maintenance

| Interface | Description |
|------|------|
| \`compact<T>()\` / \`compact_all()\` | Reclaim free slots left by soft removals; returns \`operating_message\` |
| \`tombstone_count<T>()\` / \`has_tombstones<T>()\` | Query the number of unreclaimed slots and whether any exist |
| \`contains_entity<T>(entity)\` | Storage-level membership test |
| \`get_entity_indices<T>()\` / \`get_entity_versions<T>()\` | Return the container pointers of the entity indices and versions for the component; unregistered types return \`nullptr\` and are not registered implicitly |
| \`get_pool_version<T>()\` / \`get_version<T>(entity_index)\` | Version queries |
| \`set_hot_set_capacity<T>(entries)\` / \`hot_set_capacity<T>()\` / \`clear_hot_set<T>()\` | Query cache capacity configuration |
| \`get_raw_pool_data<T>()\` / \`get_type_id<T>()\` | Run-time type-erased access |
| \`capacity<T>()\` / \`reserved_entities()\` / \`memory_usage()\` | Capacity and memory statistics |
| \`for_each_alive_entity(func)\` | Iterate all live entities |
| \`reset()\` | Clear entity and component data and reset the entity allocation state |

### Extension interfaces

| Interface | Description |
|------|------|
| \`create_entities(count, out)\` | Batch create entities and append them to \`out\` |
| \`clone_batch(sources, out)\` / \`clone_into(dst, source)\` | Batch clone keeps the successful copies; when cloning into an existing entity fails, partial results may be left behind |
| \`get_or_add(entity, component, out)\` / \`emplace<T>(entity, out, args...)\` | Get or add a component |
| \`replace<T>(entity, component)\` | Overwrite an existing component; returns an error when it is missing |
| \`add_batch(component, entities)\` | Batch add the same component value; partial success is allowed |
| \`has_all_components_batch<Ts...>(...)\` / \`has_any_components_batch<Ts...>(...)\` | Multi-type batch existence tests |
| dense / container / raw-pointer overloads of \`soft_remove_batch/hard_remove_batch\` | Other argument forms for batch removal |
| \`add_def_batch\` / \`hard_remove_def_batch\` / \`soft_remove_def_batch\` | Batch def component operations; partial success is allowed |
| \`has_def(entity, def_id/name)\` / \`has_def_batch(...)\` | def component existence tests |
| \`set_entity_state(entity, state)\` | Write the whole entity state |
| \`set_tag_by_index\` / \`set_layer_by_index\` / \`set_group_by_index\` | Write tag / layer / group by index |
| \`add_entity_listener(event, fn, user_data, out_id)\` / \`remove_entity_listener(id)\` | Register and remove entity event listeners |
| \`set_on_add_def\` / \`set_on_remove_def\` / \`set_on_modify_def\` | def type callbacks |
| \`remove_system(index)\` / \`query_systems(phase, out)\` / \`run_systems(phase)\` | System removal, filtering and execution |
| \`swap(other)\` / \`merge(other)\` | Swap manager state / partially merge by entity; merged entities are removed from the source manager |
| \`view<T>() const\` / const versions of the \`group\` factories | Views and groups under const constraints |

> Batch operations allow partial success: successful items are kept, the first error is returned, and the success count is returned through an out parameter; when the whole batch of arguments is invalid, no data is modified.

### single_class_set sparse-table interfaces

The sparse table stores the \`entity_index → {dense_index, version}\` mapping with \`class_pool<sparse_entry>\`. \`sparse_entry\` keeps the dense index and version together.

| Interface | Description |
|------|------|
| \`sparse_dense_at(uint32_t idx) const\` | Read the dense index of the sparse entry; returns \`dense_invalid\` when absent |
| \`sparse_version_at(uint32_t idx) const\` | Read the version of the sparse entry; returns 0 when absent |
| \`sparse_find(uint32_t idx, uint32_t& out_version) const\` | Combined query: loads the sparse_entry once, returns the dense index and outputs the version |
| \`dense_invalid\` | Invalid dense index constant (\`0xFFFFFFFFu\`), a public static member |
| \`get_sparse_size() const\` | Largest index covered by the sparse table + 1 |
| \`prefetch_sparse_entry(uint32_t idx) const\` | Prefetch the sparse entry |
| \`clear_hot_set()\` | Clear the hot set cache (for debugging; not needed in normal use) |
| \`bump_pool_version()\` | Bump the pool version, automatically invalidating every hot set cache |

\`\`\`cpp
auto* set = mgr.get_single_class_set<Position>();

// query the dense index and version of an entity
uint32_t ver = 0;
uint32_t dense = set->sparse_find(entity_index, ver);

if (dense == single_class_set::dense_invalid)
{
    // the entity does not have this component
}

// clear the hot set cache
set->clear_hot_set();
\`\`\`

### Signal and tracking switches

| Interface | Description |
|------|------|
| \`disable_comp_signals()\` | Disable enqueueing of deferred component signals |
| \`enable_comp_signals()\` | Enable enqueueing of deferred component signals |
| \`disable_entity_signals()\` | Disable enqueueing of deferred entity signals |
| \`enable_entity_signals()\` | Enable enqueueing of deferred entity signals |
| \`disable_track_changes()\` | Disable version tracking |
| \`enable_track_changes()\` | Enable version tracking |

### Signal overflow and capacity

| Interface | Description |
|------|------|
| \`comp_signal_overflow_count()\` | Cumulative number of component signals that overflowed to the chain |
| \`entity_signal_overflow_count()\` | Cumulative number of entity signals that overflowed to the chain |
| \`reset_comp_signal_overflow_count()\` | Reset the component overflow counter |
| \`reset_entity_signal_overflow_count()\` | Reset the entity overflow counter |
| \`preallocate_component_signals(n)\` | Preallocate the component overflow chain capacity |
| \`preallocate_entity_signals(n)\` | Preallocate the entity overflow chain capacity |

### View system

| Interface | Description |
|------|------|
| \`view<T>()\` | Single-component view |
| \`view<T>().for_each(func)\` | Single-component view + iteration |
| \`view<First, Second, Rest...>()\` | Multi-component view |
| \`view<T>(without<Types...>)\` | Exclusion view |
| \`view<T>(with<Types...>)\` | Retrieval view |
| \`view_or<A, B>()\` | OR view |
| \`view_filtered<T>(Pred)\` | Predicate-filtered view |
| \`view<T>().page(offset, limit)\` | Paged view (chained) |
| \`view<T>().sorted_by_component<T>(cmp)\` | Sorted view (chained) |
| \`view<T>().sorted_by_component_value(keyFn)\` | Grouped view (chained) |
| \`view<T>().track_changes()\` | Change-detection view (chained) |
| \`view<T>().filter_changed()\` | Per-entity change detection (chained) |
| \`view<T>().filter_added()\` | Per-entity add detection (chained) |
| \`view_any_of<Types...>()\` | N-ary OR view (any component matches) |
| \`view<T>().exactly_one()\` | Retrieve the component of exactly one entity |
| \`view<First, Rest...>().exactly_one()\` | Retrieve multiple components of exactly one entity |
| \`view<First, Rest...>().find_one(entity)\` | Query multiple components of a given entity |
| \`view<First, Rest...>().iter_over_entities(entities)\` | Batch query over a given list of entities |

### Tiered sorting

Located in the \`detail\` namespace.

| Interface | Description |
|------|------|
| \`tiered_sort<T>(data, n, cmp)\` | Tiered sort of a value array |
| \`tiered_sort_indices<T>(indices, values, n)\` | Index sort: sorts indices in ascending order of values[indices[i]] |

**Constraints**:

- \`tiered_sort\` requires \`T\` to satisfy \`std::is_trivially_copyable_v\`
- \`tiered_sort_indices\` works on an index array, sorting in ascending order of \`values[indices[i]]\`
- Both are \`noexcept\`

\`\`\`cpp

// value sort
int data[] = {5, 3, 1, 4, 2};
tiered_sort(data, 5, std::less<int>{});

// index sort
size_t indices[] = {0, 1, 2, 3, 4};
float values[] = {5.0f, 3.0f, 1.0f, 4.0f, 2.0f};
tiered_sort_indices(indices, values, 5);
// indices: {2, 1, 4, 3, 0}
\`\`\`

### Group system

| Interface | Description |
|------|------|
| \`group<First, Rest...>()\` | Non-OwingGroup (caches the matching indices) |
| \`group<First, Rest...>(owned<First>)\` | OwningGroup (reorders the primary set, linear scan) |
| \`group<First, Rest...>(reorder<First>)\` | ReorderGroup (reorders the primary set, allows shared reorder state) |

### runtime_view

| Interface | Description |
|------|------|
| \`runtime_view_create(class_pool<int>, class_pool<int> = {})\` | Run-time view (class_pool arguments, bitmask matching) |
| \`runtime_view_create(span<const int>, span<const int> = {})\` | Run-time view (span arguments) |
| \`runtime_view_create(const vector<int>&, const vector<int>& = {})\` | Run-time view (vector arguments) |
| \`runtime_view_create(const array<int, N>&)\` | Run-time view (array arguments, required only) |
| \`runtime_view_create(const array<int, N>&, const array<int, M>&)\` | Run-time view (array arguments, required + excluded) |
| \`runtime_view_create(const int*, size_t, const int* = nullptr, size_t = 0)\` | Run-time view (raw pointer + length) |
| \`runtime_view_create_from_terms(class_pool<runtime_term>)\` | Term query (supports OR/OPTIONAL/NOT) |
| \`runtime_view_create_from_terms(span<const runtime_term>)\` | Term query (span arguments) |
| \`runtime_view_create_from_terms(const vector<runtime_term>&)\` | Term query (vector arguments) |
| \`runtime_view_create_from_terms(const array<runtime_term, N>&)\` | Term query (array arguments) |
| \`runtime_view_create_from_terms(const runtime_term*, size_t)\` | Term query (raw pointer + length) |
| \`get_component_mask(entity)\` | Get the component bitmask of the entity |
| \`get_component_bit<T>()\` | Get the bitmask bit of the type |

> Batch argument overloads: \`add_batch\` / \`get_ptr_batch\` / \`prefetch_ptr_batch\` / \`runtime_view_create\` / \`runtime_view_create_from_terms\` all accept \`std::vector\` / \`std::array\` / raw pointer + length / \`std::span\` arguments and do not hold external containers.

### Lifecycle signals

| Interface | Description |
|------|------|
| \`set_on_entity_created(fn, data)\` | Bind an immediate callback for entity creation |
| \`set_on_entity_destroyed(fn, data)\` | Bind an immediate callback for entity destruction |
| \`set_on_add<T>(fn, data)\` | Bind an immediate callback for adding component T |
| \`set_on_remove<T>(fn, data)\` | Bind an immediate callback for removing component T |
| \`set_on_modify<T>(fn, data)\` | Bind an immediate callback for overwriting component T |
| \`flush_entity_signals(handler)\` | Process deferred entity signals in a batch |
| \`has_pending_entity_signals()\` | Whether deferred entity signals are pending |
| \`flush_component_signals(handler)\` | Process deferred component signals in a batch |
| \`has_pending_component_signals()\` | Whether deferred component signals are pending |

### Usage

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(1000);

entity e1 = mgr.create_entity();
entity e2 = mgr.create_entity();

// add components
mgr.add(e1, Position{1, 2});
mgr.add(Velocity{10, 20}, e1);  // argument order is interchangeable

// the same component on multiple entities
mgr.add(Position{7, 8}, e1);
mgr.add(Position{7, 8}, e2);
mgr.add(Position{7, 8}, e3);

// multiple components on the same entity
mgr.add(e1, Position{1, 2});
mgr.add(e1, Velocity{3, 4, 5});
mgr.add(e1, Health{100, 200});

// retrieve a component
Position* p = mgr.get_ptr<Position>(e1);

// run-time def component: register the type once (cold path), then store/load by name/ID
type_def def;
def.size = 12; def.alignment = 4; def.trivially_copyable = true;
int did = type_id::register_type_def("VelocityDef", def);
float vel[3] = {1.0f, 2.0f, 3.0f};
mgr.add_def(e1, did, vel);                  // add by ID
mgr.add_def(e2, "VelocityDef", vel);         // add by name
float* v = static_cast<float*>(mgr.get_def_ptr(e1, did));   // v[0]==1.0f
mgr.hard_remove_def(e1, did);                // hard remove
mgr.soft_remove_def(e2, "VelocityDef");      // soft remove by name

// batch query components
class_pool<Position*> results;
results.increase_capacity(entities.size());
mgr.get_ptr_batch<Position>(entities.data(), results.data(), entities.size());

// prefetch component pointers
mgr.prefetch_ptr<Position>(e1);
mgr.prefetch_ptr_batch<Position>(entities.data(), entities.size());

// prefetch the sparse entry first, then the component data
mgr.prefetch_ptr<Position>(e1);
mgr.prefetch_ptr_data<Position>(e1);

// cached: reuse the set pointer, suitable for batch loops
auto* set = mgr.get_single_class_set<Position>();
mgr.prefetch_ptr_cached<Position>(set, e1);
mgr.prefetch_ptr_data_cached<Position>(set, e1);
auto* p = mgr.get_ptr_fast_cached<Position>(set, e1);

// query_context: fetch once, reuse for later queries
ecs::query_context<Position> ctx(mgr);
ctx.prefetch_sparse(e1);
ctx.prefetch_data(e1);
auto* p5 = ctx.get_ptr(e1);

// batch add
class_pool<entity> ents = {e1, e2};
class_pool<Health> comps = {Health{100,100}, Health{80,100}};
mgr.add_batch(ents, comps);

// remove
mgr.soft_remove<Health>(e1);
mgr.hard_remove<Velocity>(e2);

// multi-entity removal
mgr.soft_remove<Name>(e1);
mgr.soft_remove<Name>(e2);
mgr.hard_remove<Position>(e1);
mgr.hard_remove<Position>(e2);
mgr.hard_remove<Velocity>(e1);
mgr.hard_remove<Velocity>(e2);

// delete entities
mgr.delete_entity(e2);
\`\`\`

### Entity state pool

| Interface | Description |
|------|------|
| \`entity_flag\` | Entity state flag enum: \`active\`, \`disabled\`, \`pending_destroy\`, \`static_entity\` |
| \`get_entity_state(entity, entity_state&)\` | Read the state by entity handle; returns \`operating_message\` on failure |
| \`get_entity_state_by_index(entity_index)\` | Get the state reference by index (\`entity_state&\`, no validity check) |
| \`set_entity_flag(entity, flag)\` | Set a state flag by handle; returns \`operating_message\` |
| \`clear_entity_flag(entity, flag)\` | Clear a state flag by handle; returns \`operating_message\` |
| \`has_entity_flag(entity, flag)\` | Check a state flag by handle (\`false\` when the entity is invalid) |
| \`set_tag(entity, uint32_t)\` / \`get_tag(entity, uint32_t&)\` | Read/write the custom tag by handle; returns \`operating_message\` |
| \`set_layer(entity, uint32_t)\` / \`get_layer(entity, uint32_t&)\` | Read/write the render layer by handle; returns \`operating_message\` |
| \`set_group(entity, uint32_t)\` / \`get_group(entity, uint32_t&)\` | Read/write the group by handle; returns \`operating_message\` |
| \`set_entity_flag_by_index(entity_index, flag)\` | Set a flag by index (no validity check) |
| \`clear_entity_flag_by_index(entity_index, flag)\` | Clear a flag by index (no validity check) |
| \`has_entity_flag_by_index(entity_index, flag)\` | Check a flag by index (no validity check) |

\`\`\`cpp
if (!mgr.set_entity_flag(e, ecs::entity_flag::disabled))
{
    // invalid handle
}

if (mgr.has_entity_flag(e, ecs::entity_flag::active))
{
    // the entity is active
}

ecs::entity_state state{};
if (mgr.get_entity_state(e, state))
{
    // read succeeded
}

mgr.set_tag(e, 1);    // custom tag
mgr.set_layer(e, 3);  // render layer
\`\`\`

> The handle overloads first verify that the entity is alive and return \`om_err_invalid_entity\` on failure; the \`*_by_index\` versions read and write directly by index and are meant for batch paths where the index is already known to be valid.

### Change log pool

Every \`add\`/\`remove\` operation records a change automatically, which can be consumed at the end of a frame.

| Interface | Description |
|------|------|
| \`enable_change_log()\` | Enable change log recording |
| \`disable_change_log()\` | Disable change log recording |
| \`flush_change_log(handler)\` | Consume all pending change records |
| \`end_frame()\` | End the frame, bumping the frame counter |
| \`has_pending_change_records()\` | Whether pending change records exist |

\`\`\`cpp
mgr.enable_change_log();
// ... component add/remove operations ...
mgr.end_frame();
mgr.flush_change_log([](const ecs::change_record& r) {
    // r.entity_index, r.type_id, r.op (0=add,1=remove,2=modify)
    // r.frame, r.dense_index
});
\`\`\`

### System context pool

Registers system execution contexts and manages the execution order.

| Interface | Description |
|------|------|
| \`system_context\` | System context structure: \`required_mask\`, \`excluded_mask\`, \`order\`, \`phase\`, \`dependencies\` |
| \`register_system(ctx)\` | Register a system context |
| \`get_system_contexts()\` | Get all system contexts (\`const class_pool<system_context>&\`) |

\`\`\`cpp
mgr.register_system(ecs::system_context{
    .required_mask = 0x3,  // requires Position + Velocity
    .phase = 1,            // update phase
    .order = 100,          // execution order
});
\`\`\`

### Entity masks (no upper limit)

Dynamic bitmask storage built on \`multi_block_bitmask\`, with no component-type limit. The mask position (block index / offset within the block) is computed by \`type_id::mask_block_of(tid)\` / \`type_id::mask_offset_of(tid)\`. Use \`reserve_blocks(n)\` to preallocate the number of mask blocks.

#### \`component_meta\` structure

Each registered component type has one metadata entry.

| Field | Type | Description |
|------|------|------|
| \`size\` | \`size_t\` | Component type size (bytes) |

#### manager interfaces

| Interface | Description |
|------|------|
| \`get_component_mask(entity)\` | Get the block 0 mask of the entity (\`uint64_t\`, type_id 1-64; equivalent to \`get_component_mask_block(e, 0)\`) |
| \`get_component_mask_block(entity, uint32_t block_idx)\` | Get the mask of the specified block of the entity (\`uint64_t\`; block_idx covers type_id \`block_idx*64+1\` to \`block_idx*64+64\`) |
| \`get_component_mask_block_by_index(uint32_t entity_index, uint32_t block_idx)\` | Same as above, taking entity_index instead of an entity handle |
| \`get_component_bit<T>()\` | Get the mask bit of component T (returns \`1ULL<<offset\` for block 0 types and 0 otherwise) |
| \`get_component_meta(int type_id)\` | Get \`component_meta*\` (including the component size; returns nullptr when type_id is out of range) |
| \`preallocate_mask_blocks(uint32_t num_blocks)\` | Preallocate mask blocks per entity (64 component kinds per block; call before registering components; \`register_component_meta\` grows automatically when type_id exceeds the range) |
| \`num_mask_blocks() const\` | Current number of mask blocks per entity |
| \`get_entity_manager()\` | Get \`entity_manager&\`, on which \`set_mask_bit\` / \`clear_mask_bit\` / \`get_mask\` / \`get_block\` / \`set_entity_flag\` etc. can be called |

\`\`\`cpp
uint64_t mask = mgr.get_component_mask(e);
uint64_t pos_bit = mgr.get_component_bit<Position>();
if ((mask & pos_bit) != 0)
{
    // the entity has the Position component
}

// mask bit of an arbitrary type_id (block 0 types)
int vel_id = type_id::get_type_id<Velocity>();
if (type_id::mask_block_of(vel_id) == 0)
{
    uint64_t vel_bit = 1ULL << type_id::mask_offset_of(vel_id);
    bool has_vel = (mgr.get_component_mask(e) & vel_bit) != 0;
}

// preallocate 2 mask blocks at startup (supports 128 component kinds)
mgr.preallocate_mask_blocks(2);
\`\`\`

> The default block count is 1 (64 component kinds). \`register_component_meta\` grows the mask block count automatically when components are registered, so \`preallocate_mask_blocks\` need not be called manually. Query any block of a multi-block mask through \`get_component_mask_block(e, block_idx)\` or \`get_component_mask_block_by_index(idx, block_idx)\`. See [§ 29. multi_block_bitmask](#29-multi_block_bitmask--multi-block-bitmask-storage) for details.

### What not to do

| Wrong approach | Problem | Correct approach |
|---------|------|---------|
| Copying \`manager\` | Copying is prohibited; internal resource ownership becomes ambiguous | Use move semantics or pass by reference |
| Adding or removing components while iterating a View | Iterator invalidation or data races | Collect the changes first, then batch them after the iteration |
| Continuing to use a deleted entity's handle | The handle version is stale, \`is_entity_valid\` returns false | Discard the handle after deletion, or create a new entity |
| Forgetting \`preallocate_entities\` | Every entity creation may trigger growth | Estimate the entity count at startup and preallocate |
| Assuming \`size()\` decreases after \`soft_remove\` | Soft removal does not reduce \`size()\` | Use \`live_count()\` for the live count, or iterate through a View (iteration filters soft removals automatically) |
| Using \`get_ptr_fast\` across different type sets | The type_id check is skipped and a pointer of the wrong type may be returned | Use \`get_ptr_fast\` within the same type set and \`get_ptr\` across types |
| Treating the \`get_ptr<T>\` return value as a complete judgement of "type registered + entity valid + component present" | An invalid entity handle, an entity that does not carry the component, and an unregistered type all return \`nullptr\`, with no compile-time assertion and no run-time hint (a misspelled type name also silently returns \`nullptr\`) | Use \`is_entity_valid(entity)\` for liveness; confirm registration with \`get_single_class_set<T>() != nullptr\`; always check the component query result for null |

---
`
};
