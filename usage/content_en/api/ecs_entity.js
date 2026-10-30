window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['entity'] = {
  title: "ecs::entity — Entity",
  content: `## 1. ecs::entity — Entity

An entity is a lightweight handle composed of a 32-bit index and a 32-bit version number, stored together in the 64-bit \`handle_\`.

### Interface

| Interface | Description |
|------|------|
| \`entity()\` | Default constructor, \`handle_=0\`, invalid entity |
| \`entity(uint32_t idx, uint32_t ver)\` | Construct with an explicit index and version |
| \`is_valid()\` | Whether the entity is valid (\`handle_ != 0\`) |
| \`parts_.index_\` | Entity index (32-bit) |
| \`parts_.version_\` | Entity version number (32-bit) |
| \`handle_\` | 64-bit handle (shares a union with index_ + version_) |
| \`operator==\` | Whether two entities are equal (compares handle_) |
| \`operator!=\` | Whether two entities are not equal |
| \`std::hash<entity>\` | Hash specialization, usable with \`std::unordered_map\` |

### Usage

\`\`\`cpp
entity e1;                    // default construction, invalid entity
entity e2(3, 1);              // index=3, version=1
e2.is_valid();                // true
e2.parts_.index_;             // 3
e2.parts_.version_;           // 1

std::unordered_map<entity, int> map;
map[e2] = 42;                 // usable as a hash key
\`\`\`

### What not to do

| Anti-pattern | Problem | Correct approach |
|---------|------|---------|
| Manually constructing an entity's parts_.index_ and parts_.version_ | A mismatched version makes the ECS manager treat the entity as invalid | Always create entities through \`manager::create_entity()\` |
| Reusing the handle of a deleted entity | The version has already been incremented, so the old handle is invalid | Discard the handle after deletion and create a new one |
| Treating internal entity fields as ordinary integer arithmetic | \`parts_.index_\` and \`parts_.version_\` should not be manipulated directly | Operate on entities only through the public interface |

---
`
};

window.DOCS_DATA_EN['view_tags'] = {
  title: "view_tags — View tag types",
  content: `## 2. view_tags — View tag types

Located in the \`ecs\` namespace. Used to build tag arguments for View / Group / runtime_view.

### Interface

| Tag | Type | Description |
|------|------|------|
| \`without<Types...>\` | \`without_t<Types...>\` | Exclude entities that have any of the Types components |
| \`with<Types...>\` | \`with_t<Types...>\` | Additionally fetch pointers to the Types components (may be \`nullptr\`; null checking is the user's responsibility) |
| \`exclude<Types...>\` | \`without_t<Types...>\` | Alias of \`without\` |
| \`get<Types...>\` | \`with_t<Types...>\` | Alias of \`with\` |
| \`owned<Types...>\` | \`owned_t<Types...>\` | Marks Types as owned by the Group (rearranges the dense array) |
| \`reorder<Types...>\` | \`reorder_t<Types...>\` | Marks Types as reorderable by the Group (lightweight owned semantics) |
| \`ordered<Types...>\` | \`struct\` | Marks the sort order |
| \`Component<T>\` | concept | \`is_copy_constructible_v<T> \\|\\| is_move_constructible_v<T>\` |

### Usage

\`\`\`cpp
// single-component view, excluding entities with the Static tag
auto v = mgr.view<Position>(ecs::without<Static>{});

// multi-component view, additionally fetching a Velocity reference
auto v = mgr.view<Position, Velocity>(ecs::with<Acceleration>{});

// OwningGroup: Position's dense array is laid out contiguously with Velocity
auto g = mgr.group<Position, Velocity>(ecs::owned<Position>{});

// ReorderGroup: allows reordering Position
auto g = mgr.group<Position, Velocity>(ecs::reorder<Position>{});

// runtime_view exclusion
auto rv = mgr.runtime_view_create({pos_id, vel_id}, {static_id});
// equivalent to view<Position, Velocity>(without<Static>{})
\`\`\`

### What not to do

| Anti-pattern | Problem | Correct approach |
|---------|------|---------|
| Passing the same type to \`without\` and \`with\` | Contradictory semantics, behavior undefined | Do not use both for the same type |
| Marking \`owned\` with a non-first template parameter | A Group requires owned to be the first parameter | \`group<First, Rest...>(owned<First>{})\` |
| Mixing \`reorder\` and \`owned\` on the same Group | Conflicting semantics | Use only \`owned\` or \`reorder\` per Group, never both |

---
`
};

window.DOCS_DATA_EN['single_class_set'] = {
  title: "ecs::single_class_set — Single component set",
  content: `## 3. ecs::single_class_set — Single component set

Manages storage for a single component type. The sparse table is a flat \`sparse_entry\` array (8B/entry: dense index + version), \`dense_invalid\` doubles as the tombstone marker, and a single memory access completes both the liveness check and the value fetch.

### Sparse access

| Interface | Description |
|------|------|
| \`sparse_dense_at(uint32_t idx)\` | Gets the dense index of a sparse entry; returns \`dense_invalid\` (0xFFFFFFFF) if absent |
| \`sparse_version_at(uint32_t idx)\` | Gets the version of a sparse entry; returns 0 if absent |
| \`dense_invalid\` | Invalid dense index constant (0xFFFFFFFF) |
| \`get_sparse_size()\` | Largest index used by the sparse table + 1 |
| \`clear_hot_set()\` | Clears the hot-set cache (for debugging; no manual call is needed in normal use) |
| \`bump_pool_version()\` | Increments the pool version, invalidating all hot-set caches |

### Construction and assignment

| Interface | Description |
|------|------|
| \`single_class_set()\` | Default constructor |
| \`single_class_set(size_t capacity)\` | Constructor with reserved capacity |
| \`single_class_set(entity e, T&& object)\` | Constructor from an entity + object |
| \`single_class_set(single_class_set&&)\` | Move constructor |
| \`operator=(single_class_set&&)\` | Move assignment |

> Note: \`single_class_set\` cannot be copied.

### Adding components

| Interface | Description |
|------|------|
| \`add(entity, T)\` | Add/overwrite a component (replaced if already present) |
| \`add_batch(span<const entity>, span<const T>)\` | Batch add (span version) |
| \`add_batch(const class_pool<entity>&, const class_pool<T>&)\` | Batch add (lvalue references) |
| \`add_batch(class_pool<entity>&&, class_pool<T>&&)\` | Batch add (rvalue references, move semantics) |

### Runtime def components (types registered by type_id::register_type_def)

| Interface | Description |
|------|------|
| \`add_def(entity, def_id, const type_def&, const void* data)\` | Add a def component (trivial types are copied byte-wise; non-trivial types call the registered \`copy_construct\`; the def pool is lazily initialized on the first call) |
| \`get_def_ptr(entity)\` | Get the def component pointer (hot set fast path) |
| \`get_def_ptr(entity) const\` | const version |
| \`get_def_element(dense_index)\` | Direct access by dense index (for traversal) |
| \`get_def_element(dense_index) const\` | const version |

### Getting components

| Interface | Description |
|------|------|
| \`get_ptr<T>(entity)\` | Get the component pointer (with validity, type_id, and version checks) |
| \`get_ptr<T>(entity) const\` | const version |
| \`get_ptr_fast<T>(entity)\` | Fast get (skips the type_id check, addressed through a cached pointer) |
| \`get_ptr_fast<T>(entity) const\` | const version |
| \`get_ptr_raw<T>(entity)\` | Zero-check get (the caller guarantees the entity is valid) |
| \`get_ptr_raw<T>(entity) const\` | const version |
| \`get_version(uint32_t entity_index)\` | Get the entity version number |
| \`get_version_unchecked(uint32_t entity_index)\` | Get the version number without checks |
| \`prefetch_ptr_batch(const entity*, size_t)\` | Batch prefetch of sparse entries |
| \`prefetch_ptr_data<T>(entity)\` | Prefetch component data (by entity; the sparse entry must be loaded first) |
| \`get_ptr_batch(const entity*, T**, size_t)\` | Batch query of component pointers (raw pointers + length) |

> Query interfaces (\`get_ptr\` / \`get_ptr_fast\` / \`get_ptr_batch\`) return \`nullptr\` on a miss: the entity handle is invalid, the entity does not contain the component (including soft deletion and version mismatch), or \`T\` does not match the set's type. No compile-time assertion is triggered and no runtime error is produced.

> \`select_primary_set(sets, count, out_index = nullptr)\` is a static helper: it picks the set with the smallest \`size()\` from the candidate list as the primary traversal set, returns that set pointer, and optionally writes the hit index back through \`out_index\`; null entries are skipped, and when all are null it returns \`nullptr\` (the index is written back as 0).

### Removal and clearing

| Interface | Description |
|------|------|
| \`hard_remove(entity)\` | Hard removal (swap removal, O(1); works for both def pools and template pools) |
| \`soft_remove(entity)\` | Soft removal (clears only the sparse entry, does not move components; dead slots are registered and can be reused by \`add\`; automatically reclaimed when the tombstone count exceeds the threshold) |
| \`compact()\` | Density compaction: live entries are moved forward, tombstones are physically removed and the \`on_remove\` callback is re-issued (must not be called during iteration) |
| \`clear()\` | Clears all data |

> \`soft_remove\` tombstones are physically removed lazily by \`compact\` (triggered manually or automatically by the threshold, which is \`max(n/4, 64)\`); the \`remove→add\` cycle reuses dead slots, keeping physical slots bounded.

### Capacity and queries

| Interface | Description |
|------|------|
| \`size()\` | Number of physical slots (including soft-deleted tombstones) |
| \`live_count()\` | Number of live entries (O(1), maintained on add/remove) |
| \`tombstone_count()\` | Number of soft-deleted tombstones (O(1)) |
| \`has_tombstones()\` | Whether soft-deleted dead slots exist (O(1), used for view dispatching) |
| \`empty()\` | Whether empty |
| \`increase_capacity(capacity)\` | Reserve capacity |
| \`set_hot_set_capacity(entries)\` | Set the hot-set capacity (a power of two; non-powers of two are rejected; the hot set is cleared after adjustment) |
| \`hot_set_capacity()\` | Get the current hot-set capacity |
| \`get_type_id_value()\` | Get the type ID value |
| \`get_typed_pool_ptr<T>()\` | Get the typed component pool pointer (with type_id check) |
| \`get_typed_pool_ptr<T>() const\` | const version |
| Return value of the \`add/remove\` interface family | \`operating_message\`, carrying an error code and level on failure |
| \`get_entity_indices()\` | Get the entity index array (dense array) |
| \`get_entity_indices() const\` | const version |
| \`get_entity_versions()\` | Get the entity version array |
| \`get_entity_versions() const\` | const version |
| \`get_pool_version()\` | Get the component pool version (used for automatic synchronization of persistent views) |

### Usage

\`\`\`cpp
single_class_set set;
entity e1(1, 1);
set.add(e1, Position{10, 20});

Position* p = set.get_ptr<Position>(e1);
Position* pf = set.get_ptr_fast<Position>(e1);  // fast
set.soft_remove(e1);  // soft removal (O(1), tombstones reclaimed lazily, add reuses dead slots automatically)
set.compact();        // density compaction (live entries moved forward, tombstones physically removed; soft_remove triggers it automatically above the threshold)
set.hard_remove(e1);  // hard removal (swap-pop, removed immediately)
set.set_hot_set_capacity(4096);  // adjust hot-set capacity (power of two)

// batch add
class_pool<entity> ents = {entity(2,1), entity(3,1)};
class_pool<Position> comps = {Position{1,2}, Position{3,4}};
set.add_batch(ents, comps);
\`\`\`

### What not to do

| Anti-pattern | Problem | Correct approach |
|---------|------|---------|
| Relying on \`size()\` to count live entries after \`soft_remove\` | \`size()\` is the number of physical slots (including tombstones) | Use \`live_count()\` for the live count and \`tombstone_count()\` for the tombstone count |
| Calling \`compact()\` during iteration | Breaks iterator positions | Call manual \`compact()\` outside iteration; automatic reclamation is automatically deferred during iteration |
| Using only \`hard_remove\` when frequent add/remove is needed | Each swap removal is O(1) but destroys order | Use \`hard_remove\` when order changes are acceptable, and \`soft_remove\` when order must be preserved (automatic reclamation keeps density) |
| Copying \`single_class_set\` | Copying is forbidden | Use move semantics |
| Manually calling \`clear_hot_set\` after batch add/remove | Unnecessary; the pool_version auto-increment already invalidates the cache | No manual call is needed in normal use; in debugging scenarios \`clear_hot_set()\` can be used to force a clear |
| Relying on the return value of \`sparse_dense_at\` to determine whether an entry exists | You must check whether the return value equals \`dense_invalid\` | Check \`sparse_dense_at(idx) != dense_invalid\`, or use the \`get_ptr\` interface family |
| Using the return value of \`get_ptr<T>\` to distinguish "invalid entity" from "component missing" | Both an invalid entity handle and an entity without the component return \`nullptr\` | Liveness is handled by \`manager::is_entity_valid\`; component queries use the \`get_ptr\` family |

---
`
};
