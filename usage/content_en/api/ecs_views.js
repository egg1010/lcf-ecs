window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['views'] = {
  title: "View system",
  content: `## 5. View system

Provides efficient component traversal, automatically choosing the smallest set as the primary set to iterate. Components that are soft-deleted (\`soft_remove\`) are invisible to all view traversals (single_view / multi_view / composite views / runtime_view all filter them uniformly), and the traversal count is the live count.

### 5.1 single_view\\<T> — Single-component view

| Interface | Description |
|------|------|
| \`size()\` | Number of component slots (soft-deleted entries counted) |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Whether the component of the specified entity is contained |
| \`for_each(func)\` | Traverse components (auto-detects the entity parameter: \`func(T&)\` or \`func(entity, T&)\`) |
| \`for_each_safe(func)\` | Traverse components; the callback may safely call \`hard_remove\` to delete the current entity (auto-detects the entity parameter) |
| \`begin()\` / \`end()\` | Entity iterators (soft-deleted entries skipped) |
| \`component_begin()\` / \`component_end()\` | Component iterators (\`T*\`) |
| \`get_component_for_entity(entity)\` | Get the component reference of the specified entity (nullptr if absent) |
| \`get_first_entity()\` | Return the first entity handle |
| \`get_last_entity()\` | Return the last entity handle |
| \`get_entity_at_index(index)\` | Return the index-th entity handle |
| \`get_component_at_index(index)\` | Return the pointer of the index-th component |

\`\`\`cpp
auto view = mgr.view<Position>();

// Traverse components
view.for_each([](Position& p) { /* ... */ });

// Traverse (with entity)
view.for_each([](entity e, Position& p) { /* ... */ });

// Range for
for (auto e : view) { /* e is an entity */ }

// Component iterators
for (auto it = view.component_begin(); it != view.component_end(); ++it) {
    /* *it is a component reference */
}

// Shorthand
mgr.view<Position>().for_each([](Position& p) { /* ... */ });

// Safe removal during iteration: hard_remove the current entity inside the callback
mgr.view<Health>().for_each_safe([](ecs::entity e, Health& h) {
    if (h.hp <= 0) mgr.hard_remove<Health>(e);
});
\`\`\`

### 5.2 multi_view\\<T1, T2, ...> — Multi-component view

Automatically chooses the smallest set as the primary set to iterate.

| Interface | Description |
|------|------|
| \`size()\` | Actual match count (number of entities that own all components at once) |
| \`pool_size()\` | Component count of the primary set (primary set pool size, without match filtering) |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Whether the entity owns all components at once |
| \`for_each(func)\` | Traverse multiple components (auto-detects the entity parameter) |
| \`get_component_for_entity<T>(entity)\` | Get the specified-type component of the specified entity (nullptr if absent) |
| \`get_first_entity()\` | Return the first entity matching all components |
| \`get_last_entity()\` | Return the last entity matching all components |
| \`get_entity_at_index(index)\` | Return the index-th entity handle of the primary set |
| \`include_optional_component<U>()\` | Chain an optional component; it is a pointer in the callback (nullptr when absent) |

The difference between \`size()\` and \`pool_size()\`: when some entities own only part of the components, \`pool_size()\` returns the total entity count of the primary set, while \`size()\` returns the number of entities that truly own all components; if all entities own all components, the two are equal.

\`\`\`cpp
// Two components
auto v2 = mgr.view<Position, Velocity>();
v2.for_each([](Position& p, Velocity& v) { /* ... */ });
v2.for_each([](entity e, Position& p, Velocity& v) { /* ... */ });

// Match count and primary set size
size_t matched = v2.size();       // number of entities owning both Position and Velocity
size_t total   = v2.pool_size();  // number of entities in the primary set (smallest set)

// Get the components of a specified entity
Position* pp = v2.get_component_for_entity<Position>(some_entity);
Velocity* vp = v2.get_component_for_entity<Velocity>(some_entity);

// Get the first/last entity
entity first = v2.get_first_entity();
entity last  = v2.get_last_entity();
entity nth   = v2.get_entity_at_index(5);

// Three components
auto v3 = mgr.view<Position, Velocity, Health>();
v3.for_each([](entity e, Position& p, Velocity& v, Health& h) { /* ... */ });

// Four components
auto v4 = mgr.view<Position, Velocity, Health, Name>();
v4.for_each([](entity e, Position&, Velocity&, Health&, Name& n) { /* ... */ });

// Append optional components
auto v_opt = mgr.view<Position, Velocity>()
    .include_optional_component<Health>()
    .include_optional_component<Name>();
v_opt.for_each([](entity e, Position& p, Velocity& v, Health* h, Name* n) {
    if (h) { /* has Health */ }
    if (n) { /* has Name */ }
});
\`\`\`

### 5.3 single_view_without — Exclusion view

Traverse entities that have T but do not have ExcludeTypes.

| Interface | Description |
|------|------|
| \`size()\` | Number of component slots (soft-deleted entries counted) |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Check whether the entity has T and no ExcludeTypes |
| \`get_component_for_entity(entity)\` | Get the reference of T (nullptr if absent) |
| \`get_first_entity()\` | Return the first matching entity |
| \`for_each(func)\` | Traverse components (excluding the specified types, auto-detects the entity parameter) |

\`\`\`cpp
// Exclude a single type
auto v = mgr.view<Position>(ecs::without<Health>);
v.for_each([](Position& p) { /* entities without Health */ });
v.for_each([](entity e, Position& p) { /* ... */ });

// Check whether an entity matches
if (v.contains(some_entity)) { /* ... */ }
Position* p = v.get_component_for_entity(some_entity);
entity first = v.get_first_entity();

// Exclude multiple types
auto v2 = mgr.view<Position>(ecs::without<Health, Name>);
\`\`\`

### 5.4 single_view_with — Acquisition view

Traverse T while also obtaining pointers to GetTypes (may be nullptr).

| Interface | Description |
|------|------|
| \`size()\` | Number of component slots (soft-deleted entries counted) |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Check whether the entity has T |
| \`get_component_for_entity(entity)\` | Get the reference of T (nullptr if absent) |
| \`get_optional_component_for_entity<U>(entity)\` | Get the pointer of the optional component U |
| \`get_first_entity()\` | Return the first matching entity |
| \`for_each(func)\` | Traverse components + optional pointers (auto-detects the entity parameter) |

\`\`\`cpp
// Obtain a single optional component
auto v = mgr.view<Position>(ecs::with<Health>);
v.for_each([](Position& p, Health* h) {
    if (h) { /* has Health */ }
    else  { /* no Health */ }
});
v.for_each([](entity e, Position& p, Health* h) { /* ... */ });

// Get the components of a specified entity
Position* p = v.get_component_for_entity(some_entity);
Health*  h = v.get_optional_component_for_entity<Health>(some_entity);

// Obtain multiple optional components
auto v2 = mgr.view<Position>(ecs::with<Health, Name>);
v2.for_each([](Position& p, Health* h, Name* n) { /* ... */ });
\`\`\`

### 5.5 or_view\\<A, B> — OR view

Traverse entities that have A **or** B, distinguished by nullable pointers. Two-phase traversal with zero extra memory allocation.

| Interface | Description |
|------|------|
| \`size()\` | Approximate size (A.size + B.size, upper bound) |
| \`empty()\` | Whether both sets are empty |
| \`contains(entity)\` | Whether it has A or B |
| \`get_first_entity()\` | Return the first matching entity |
| \`for_each(func)\` | Traverse A OR B, callback is \`func(entity, A*, B*)\` or \`func(A*, B*)\` |

\`\`\`cpp
auto ov = mgr.view_or<Position, Velocity>();
ov.for_each([](entity e, Position* p, Velocity* v) {
    if (p && v) { /* has both Position and Velocity */ }
    else if (p) { /* has only Position */ }
    else if (v) { /* has only Velocity */ }
});

if (ov.contains(some_entity)) { /* ... */ }
entity first = ov.get_first_entity();
\`\`\`

### 5.6 filter_view\\<T, Pred> — Predicate-filtered view

Pre-filters components satisfying the predicate and stores dense indices in a \`class_pool<size_t>\` for efficient iteration. Filtering happens automatically at construction and can also be triggered manually via \`rebuild()\`.

| Interface | Description |
|------|------|
| \`size()\` | Number of components after filtering |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Whether it is in the filtered result (linear scan) |
| \`get_component_for_entity(entity)\` | Get the component reference (nullptr if absent) |
| \`get_first_entity()\` | Return the first filtered entity |
| \`get_entity_at_index(index)\` | Return the index-th filtered entity |
| \`get_component_at_index(index)\` | Return the pointer of the index-th filtered component |
| \`rebuild()\` | Re-run the filtering |
| \`for_each(func)\` | Traverse the filtered components, callback is \`func(entity, T&)\` or \`func(T&)\` |
| \`and_<B>()\` | Chained call: append an AND component B on the filtered result |
| \`or_<B>()\` | Chained call: append an OR component B on the filtered result |

\`\`\`cpp
// Filter Position.x > 1
auto fv = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; });
fv.for_each([](Position& p) { /* only process entities with x > 1 */ });

// Indexed access
if (!fv.empty()) {
    entity first = fv.get_first_entity();
    entity nth   = fv.get_entity_at_index(3);
    Position* p  = fv.get_component_at_index(3);
    if (fv.contains(some_entity)) { /* ... */ }
}

// Manual re-filtering (after component data changes)
fv.rebuild();
\`\`\`

### 5.7 filter_and_view — Filter+AND composite view

Created by chaining \`filter_view::and_<B>()\`. Traverses entities that satisfy the predicate **and** also own component B.

| Interface | Description |
|------|------|
| \`size()\` | Number after filtering |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Whether it is in the filtered result (linear scan) |
| \`get_component_for_entity<T>(entity)\` | Get the reference of T (nullptr if absent) |
| \`get_optional_component_for_entity<B>(entity)\` | Get the pointer of B |
| \`get_first_entity()\` | Return the first matching entity |
| \`get_entity_at_index(index)\` | Return the index-th matching entity |
| \`rebuild()\` | Re-run the filtering |
| \`for_each(func)\` | Traverse the filter+AND result |

\`\`\`cpp
// Position.x > 1 AND also owns Health
auto fav = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; })
               .and_<Health>();
fav.for_each([](entity e, Position& p, Health& h) {
    // only process entities with x > 1 and owning Health
});

entity first = fav.get_first_entity();
entity nth   = fav.get_entity_at_index(3);
if (fav.contains(some_entity)) { /* ... */ }
\`\`\`

### 5.8 filter_or_view — Filter+OR composite view

Created by chaining \`filter_view::or_<B>()\`. Traverses entities that satisfy the predicate **or** own component B, distinguished by nullable pointers.

| Interface | Description |
|------|------|
| \`size()\` | Number after filtering |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Whether it is in the filtered result |
| \`get_first_entity()\` | Return the first matching entity |
| \`rebuild()\` | Re-run the filtering |
| \`for_each(func)\` | Traverse the filter+OR result |

\`\`\`cpp
// Position.x > 1 OR owns Velocity
auto fov = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; })
               .or_<Velocity>();
fov.for_each([](entity e, Position* p, Velocity* v) {
    if (p && v) { /* satisfies the predicate and owns Velocity */ }
    else if (p) { /* only satisfies the predicate */ }
    else if (v) { /* only owns Velocity (does not satisfy the predicate or has no Position) */ }
});
\`\`\`

### 5.9 sort_entities_by_component / reorder_by_component — Sorting utilities

Manager-level sorting utilities that reorder the dense array by component value, so later iterations follow the sorted order.

| Interface | Description |
|------|------|
| \`sort_entities_by_component<T>(cmp)\` | Sort the dense array by the value of component T (updating the sparse mapping accordingly) |
| \`reorder_by_component<T, Other>(cmp)\` | Reorder the dense array of T by the value of Other |
| \`sort_component_container<T>(cmp)\` | Sort by the value of component T and update the dense/sparse mapping accordingly (equivalent to \`sort_entities_by_component\`) |

**reorder_by_component semantics**: iterate all entities in the T pool and sort by the value of Other. If an entity has no Other component, a default-constructed \`Other{}\` participates in the comparison.

\`\`\`cpp
// Sort by Position.x ascending
mgr.sort_entities_by_component<Position>([](Position& a, Position& b) {
    return a.x < b.x;
});

// Reorder Position by Velocity.dx descending
mgr.reorder_by_component<Position, Velocity>([](Velocity& a, Velocity& b) {
    return a.dx > b.dx;
});

// Sort the component pool (update dense/sparse accordingly, keeping the entity-component mapping consistent)
mgr.sort_component_container<Position>([](Position& a, Position& b) {
    return a.x < b.x;
});
\`\`\`

**What not to do**

| Bad practice | Problem | Correct approach |
|---------|------|---------|
| Expecting \`reorder_by_component\` to exclude entities without Other | Entities without Other still sort using \`Other{}\` and are not excluded | If exclusion is needed, filter entities first, then sort |
| Using old indices to access components after sorting | The dense array has been reordered, old indices are invalid | Re-obtain via \`get_ptr<T>(entity)\` after sorting |

### 5.10 page — Paged view

Called by chaining \`page(offset, limit)\`, skipping the first \`offset\` results and limiting to \`limit\`. Applies to \`single_view\` and \`multi_view\`.

| Interface | Description |
|------|------|
| \`size()\` | Count after paging (\`min(limit, base_size - offset)\`, soft-deleted entries counted) |
| \`empty()\` | Whether it is empty |
| \`for_each(func)\` | Paged traversal (skip the first offset, process at most limit) |

\`\`\`cpp
// Skip the first 1, process at most 3
auto paged = mgr.view<Position, Velocity>().page(1, 3);
paged.for_each([](Position& p, Velocity& v) {
    // only process the 2nd to 4th matching entities
});
\`\`\`

### 5.11 sorted_by_component — Sorted view

Called by chaining \`sorted_by_component<T>(cmp)\`, temporarily sorting query results by the specified component value. The cache is rebuilt automatically when a version change is detected. Applies to \`single_view\` and \`multi_view\`.

| Interface | Description |
|------|------|
| \`size()\` | Count after sorting (only valid entities owning all components) |
| \`empty()\` | Whether it is empty |
| \`for_each(func)\` | Traverse in sorted order (auto-detects the entity parameter) |

\`\`\`cpp
// Sort by Position.x ascending
auto sorted = mgr.view<Position, Velocity>()
    .sorted_by_component<Position>([](Position& a, Position& b) {
        return a.x < b.x;
    });
sorted.for_each([](Position& p, Velocity& v) {
    // traverse in ascending p.x order
});
\`\`\`

### 5.12 sorted_by_component_value — Grouped view

Called by chaining \`sorted_by_component_value(keyFn)\`, grouping by component value and supporting per-group traversal. Applies to \`single_view\` and \`multi_view\`.

| Interface | Description |
|------|------|
| \`size()\` | Total count after grouping |
| \`empty()\` | Whether it is empty |
| \`group_count()\` | Number of groups |
| \`for_each(func)\` | Traverse all elements in group order |
| \`for_each_group(func)\` | Per-group traversal, callback is \`func(key, start_index, end_index)\` |

\`\`\`cpp
// Group by Position.x / 20
auto grouped = mgr.view<Position>()
    .sorted_by_component_value([](Position& p) -> int {
        return p.x / 20;
    });

// Per-group traversal
grouped.for_each_group([](int key, size_t start, size_t end) {
    std::cout << "Group " << key << ": " << (end - start) << " entities\\n";
});
\`\`\`

### 5.13 track_changes — Change-detection view

Called by chaining \`track_changes()\`, returning only entities whose components changed since the last iteration. Implemented via component pool version numbers; applies to \`single_view\` and \`multi_view\`.

| Interface | Description |
|------|------|
| \`size()\` | Number of changed entities |
| \`empty()\` | Whether it is empty |
| \`for_each(func)\` | Traverse changed entities (full result on the first pass, only changed entities afterwards) |
| \`reset_tracking()\` | Reset the tracking baseline (the next for_each returns the full result again) |

\`\`\`cpp
auto changed = mgr.view<Position, Velocity>().track_changes();

// First traversal: full result
changed.for_each([](Position& p, Velocity& v) {
    // process all entities
});

// Modify components ...
mgr.add(some_entity, Position{999, 0, 0}); // add triggers a version change

// Next traversal: only changed entities
changed.for_each([](Position& p, Velocity& v) {
    // process only changed entities
});

// Reset tracking
changed.reset_tracking();
\`\`\`

### 5.14 Chaining composition

\`single_view\` and \`multi_view\` support chained calls such as \`page\` / \`sorted_by_component\` / \`sorted_by_component_value\` / \`track_changes\` / \`filter_changed\` / \`filter_added\`. Note that these chained methods return independent view objects and **do not nest** — each chained view can only use its own \`for_each\` / \`size\` / \`empty\` / \`reset_tracking\` interfaces, and other chained methods such as \`page\` cannot be called after a chained view.

\`\`\`cpp
// Sorted view (used independently)
auto sorted = mgr.view<Position, Velocity>()
   .sorted_by_component<Position>([](Position& a, Position& b) { return a.x < b.x; });
sorted.for_each([](Position& p, Velocity& v) { /* traverse in sorted order */ });

// Paged view (used independently; only the base view can call page)
auto paged = mgr.view<Position, Velocity>().page(0, 10);
paged.for_each([](Position& p, Velocity& v) { /* first 10 matching entities */ });

// Change-detection view (used independently)
auto changed = mgr.view<Position>().track_changes();
changed.for_each([](Position& p) { /* changed entities */ });
changed.reset_tracking();

// Added-detection view (used independently)
auto added = mgr.view<Position>().filter_added();
added.for_each([](Position& p) { /* newly added entities */ });
added.reset_tracking();
\`\`\`

### 5.15 filter_changed — Per-entity change detection

Called by chaining \`filter_changed()\`, returning only entities whose component values changed since the last iteration. Based on per-entity version tracking, precise down to a single entity.

> **Note:** Only \`add()\` operations (including overwrite adds) increment the change version. Directly modifying component memory via \`get_ptr()\` does not trigger change detection.

| Interface | Description |
|------|------|
| \`size()\` | Number of changed entities |
| \`empty()\` | Whether it is empty |
| \`for_each(func)\` | Traverse changed entities (full result on the first pass, only changed entities afterwards) |
| \`reset_tracking()\` | Reset the snapshot baseline (the next for_each returns the full result again) |

> \`multi_view\` also provides \`filter_any_changed()\` (no template parameter), equivalent to \`filter_changed_view<0>\`, i.e. tracking changes to any one component. \`single_view\` has no such overload.

\`\`\`cpp
// Single-component change detection
auto cv = mgr.view<Position>().filter_changed();
cv.for_each([](Position& p) {
    // first time: return all Position entities
    // afterwards: only entities whose Position was modified
});

// Multi-component change detection (tracks the specified component, template parameter required)
auto mcv = mgr.view<Position, Velocity>().filter_changed<Position>();
mcv.for_each([](Position& p, Velocity& v) {
    // track Position changes, while also returning Velocity
});

// Reset the tracking baseline
cv.reset_tracking();  // the next for_each returns the full result again
\`\`\`

### 5.16 filter_added — Per-entity add detection

Called by chaining \`filter_added()\`, returning only components **newly added** after the view was created. Based on a global add counter.

| Interface | Description |
|------|------|
| \`size()\` | Number of newly added entities |
| \`empty()\` | Whether it is empty |
| \`for_each(func)\` | Traverse newly added entities (full result on the first pass, only newly added entities afterwards) |
| \`reset_tracking()\` | Reset the add-detection baseline (the next for_each returns the full result again) |

\`\`\`cpp
// Single-component add detection
auto av = mgr.view<Position>().filter_added();
// create the view first, then add components
mgr.add(e1, Position{1, 0});
mgr.add(e2, Position{2, 0});
av.for_each([](Position& p) {
    // first time: return all added entities
});
av.for_each([](Position& p) {
    // again: nothing newly added, returns empty
});

// Multi-component add detection (the tracked component type must be specified)
auto mav = mgr.view<Position, Velocity>().filter_added<Position>();
mav.for_each([](Position& p, Velocity& v) {
    // only return entities with newly added Position (which must also have Velocity)
});

// Reset the add-detection baseline
av.reset_tracking();  // the next for_each returns the full result again
\`\`\`

> **Difference from \`filter_changed\`:** \`filter_changed\` tracks "modification" (overwrite adds also trigger it), while \`filter_added\` tracks only "first add" (overwrite adds do not trigger it).

### 5.17 view_any_of — N-ary OR view

Created via \`view_any_of<Types...>()\`, traversing entities that own **any one** of the specified components. Uses a bitset for deduplication so each entity appears only once.

| Interface | Description |
|------|------|
| \`size()\` | Approximate size (sum of the set sizes, upper bound) |
| \`empty()\` | Whether all sets are empty |
| \`for_each(func)\` | Traverse any matching entity, callback is \`func(Types*...)\` or \`func(entity, Types*...)\` |

\`\`\`cpp
// Two-component OR: Position OR Velocity
auto av2 = mgr.view_any_of<Position, Velocity>();
av2.for_each([](Position* p, Velocity* v) {
    if (p && v) { /* has both */ }
    else if (p) { /* has only Position */ }
    else if (v) { /* has only Velocity */ }
});

// Three-component OR: Position OR Velocity OR Health
auto av3 = mgr.view_any_of<Position, Velocity, Health>();
av3.for_each([](entity e, Position* p, Velocity* v, Health* h) {
    // a non-null pointer means the component is owned
});

// Four-component OR
auto av4 = mgr.view_any_of<Position, Velocity, Health, Name>();
av4.for_each([](Position* p, Velocity* v, Health* h, Name* n) {
    // owns at least one component
});
\`\`\`

> **Difference from \`or_view\`:** \`or_view\` supports only 2 components, while \`view_any_of\` supports any number.

### 5.18 exactly_one — Exact single-entity access

Use \`exactly_one()\` to obtain component references of exactly one entity. The number of matching entities must be 1, otherwise it aborts.

**Return values:**
- \`single_view<T>::exactly_one()\` → \`T&\`
- \`multi_view<T1, T2, ...>::exactly_one()\` → \`std::tuple<T1&, T2&, ...>\`

\`\`\`cpp
// Exactly one entity has Position
auto& pos = mgr.view<Position>().exactly_one();
pos.x = 100;

// Exactly one entity has both Position and Velocity
auto [p, v] = mgr.view<Position, Velocity>().exactly_one();
p.x += v.dx;
p.y += v.dy;

// Three components
auto [p2, v2, h] = mgr.view<Position, Velocity, Health>().exactly_one();
h.hp -= 10;
\`\`\`

> **Note:** Both \`single_view\` and \`multi_view\` require exactly 1 entity in the view; otherwise they abort. Suitable for scenarios such as singleton entities and player entities.

### 5.19 find_one — Query a specified entity

Use \`find_one(entity)\` to query whether the specified entity owns all components required by the view. If so it returns a tuple of component pointers, otherwise null pointers.

**Return value:** \`std::tuple<First*, Rest*...>\`; when all components exist all pointers are non-null, otherwise all pointers are null.

\`\`\`cpp
// Query whether e1 has Position + Velocity
auto [p, v] = mgr.view<Position, Velocity>().find_one(e1);
if (p && v) {
    // e1 owns Position and Velocity
    p->x += v->dx;
}

// Query a non-existent entity
auto [p2, v2] = mgr.view<Position, Velocity>().find_one(invalid_entity);
// p2 == nullptr, v2 == nullptr

// Three-component query
auto [p3, v3, h] = mgr.view<Position, Velocity, Health>().find_one(e1);
if (p3 && v3 && h) {
    // e1 owns all three components
}
\`\`\`

### 5.20 iter_over_entities — Batch query over specified entities

Use \`iter_over_entities(entities)\` to iterate over a specified entity list, processing only entities that own all components of the view.

**Parameters:** \`entities\` supports iterable containers such as \`std::array<entity, N>\`, \`std::span<entity>\` and \`class_pool<entity>\`.

\`\`\`cpp
// Iterate over a specified entity list
std::array<entity, 3> targets = {e1, e2, e3};
auto ev = mgr.view<Position, Velocity>().iter_over_entities(targets);
ev.for_each([](Position& p, Velocity& v) {
    // only process entities among e1, e2, e3 that own both Position and Velocity
    p.x += v.dx;
});

// Using class_pool<entity>
class_pool<entity> entity_list;
entity_list.emplace_back(e1);
entity_list.emplace_back(e2);
auto ev2 = mgr.view<Position, Health>().iter_over_entities(entity_list);
ev2.for_each([](Position& p, Health& h) {
    // only process entities in the list that have Position and Health
});
\`\`\`

> **Note:** Entities in the list that do not meet the component conditions are silently skipped, without errors.

### What not to do with Views

| Bad practice | Problem | Correct approach |
|---------|------|---------|
| Adding or removing components inside a \`for_each\` callback | Iterators are invalidated, possibly causing crashes or missed processing | Use \`for_each_safe\` for safe removal, or collect the changed entities first and batch-operate after the traversal |
| Calling \`swap_dense_and_pool\` / \`reorder_dense_by_indices\` / \`clear\` inside a \`for_each_safe\` callback | Breaks the dense layout and triggers \`std::abort\` | Perform reordering/clearing only after the iteration |
| Forgetting \`rebuild()\` after the \`filter_view\` filter condition changes | Stale filtered results still return old data | Call \`rebuild()\` after component data changes |
| Using \`exactly_one()\` when the live entity count is not 1 | Aborts (\`LCF_TRAP\`) | Check \`live_count() == 1\` first (\`size()\` includes soft-deleted entries), or use \`find_one()\` |
| Relying on \`filter_changed\` to detect \`get_ptr()\` modifications | Direct memory modification does not trigger change detection | Trigger changes via an \`add()\` overwrite, or use \`track_changes\` |
| Mixing \`get_ptr_fast\` and \`get_ptr\` in a multi-component View | The type-safety boundary becomes blurred | Use one retrieval style consistently within a View |

---
`
};

window.DOCS_DATA_EN['groups'] = {
  title: "Group system",
  content: `## 6. Group system

A Group precomputes the matching entity set at construction, so iteration is branch-free.

### 6.1 Non-OwningGroup (\`group\`)

Created via \`mgr.group<First, Rest...>()\`; caches the dense indices of matching entities.

**Interfaces:**

| Interface | Description |
|------|------|
| \`size()\` | Number of matching entities |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Whether the specified entity is contained |
| \`for_each(func)\` | Traverse matching entities (auto-detects the entity parameter: \`func(Ts&...)\` or \`func(entity, Ts&...)\`) |
| \`get<T>(entity)\` | Get the pointer of component T of the specified entity |
| \`front()\` | First matching entity |
| \`back()\` | Last matching entity |
| \`rebuild()\` | Rebuild the cache (call after adding/removing components) |

\`\`\`cpp
// Two-component Group
auto g = mgr.group<Position, Velocity>();
g.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
    p.y += v.vy;
});

// With the entity parameter
g.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\\n";
});

// Rebuild after adding/removing components
mgr.add(new_entity, Position{0, 0});
mgr.add(new_entity, Velocity{1, 0});
g.rebuild();

// Three-component Group
auto g3 = mgr.group<Position, Velocity, Health>();
g3.for_each([](entity e, Position& p, Velocity& v, Health& h) {
    // entities owning all three components
});
\`\`\`

### 6.2 OwningGroup (\`group\` + \`owned\`)

Created via \`mgr.group<First, Rest...>(ecs::owned<First>)\`; reorders the dense array of the primary set \`First\` so that matching entities are contiguous at the front of the array.

**Note:** The component type marked by the \`owned\` tag will be reordered, and the component data order will change. If other code depends on that component's dense order, use it with care.

**Interfaces:**

| Interface | Description |
|------|------|
| \`size()\` | Number of matching entities |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Whether the specified entity is contained |
| \`for_each(func)\` | Traverse matching entities (auto-detects the entity parameter) |
| \`get<T>(entity)\` | Get the pointer of component T of the specified entity |
| \`front()\` | First matching entity |
| \`back()\` | Last matching entity |
| \`rebuild()\` | Rebuild the cache (call after adding/removing components) |

\`\`\`cpp
// OwningGroup: Position gets reordered
auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);
og.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
});

// With the entity parameter
og.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\\n";
});

// Three-component OwningGroup
auto og3 = mgr.group<Position, Velocity, Health>(ecs::owned<Position>);
og3.for_each([](entity e, Position& p, Velocity& v, Health& h) {
    // entities owning all three components
});
\`\`\`

### 6.3 ReorderGroup (\`group\` + \`reorder\`)

Created via \`mgr.group<First, Rest...>(ecs::reorder<First>)\`; reorders the primary set like OwningGroup but with lighter semantics — it only expresses "reordering is allowed" and implies no lifetime ownership.

**Interfaces:**

| Interface | Description |
|------|------|
| \`size()\` | Number of matching entities |
| \`empty()\` | Whether it is empty |
| \`contains(entity)\` | Whether the specified entity is contained |
| \`for_each(func)\` | Traverse matching entities (auto-detects the entity parameter) |
| \`get<T>(entity)\` | Get the pointer of component T of the specified entity |
| \`front()\` | First matching entity |
| \`back()\` | Last matching entity |
| \`rebuild()\` | Rebuild the cache (call after adding/removing components) |
| \`share_with(other_reorder_group)\` | Share reorder state with another ReorderGroup of the same type |

\`\`\`cpp
// ReorderGroup: Position gets reordered
auto rg = mgr.group<Position, Velocity>(ecs::reorder<Position>);
rg.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
});

// With the entity parameter
rg.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\\n";
});

// Three-component ReorderGroup
auto rg3 = mgr.group<Position, Velocity, Health>(ecs::reorder<Position>);
rg3.for_each([](entity e, Position& p, Velocity& v, Health& h) {
    // entities owning all three components
});
\`\`\`

**Sharing reordering across multiple Groups:** Multiple ReorderGroups of the same component types can share reorder state via \`share_with()\`, avoiding repeated reordering.

\`\`\`cpp
auto rg1 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
auto rg2 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
rg2.share_with(rg1);  // rg2 shares rg1's reorder state
// Both have the same size() and iteration results, sharing one cache
\`\`\`

### What not to do with Groups

| Bad practice | Problem | Correct approach |
|---------|------|---------|
| Forgetting \`rebuild()\` after adding/removing components | The Group cache is stale and may miss new entities or contain deleted ones | Call \`rebuild()\` after every batch add/remove |
| Relying on the dense order in an OwningGroup / ReorderGroup | \`owned\` / \`reorder\` reorders the primary set's dense array | If the order must be preserved, use a Non-OwningGroup |
| Using a Group for components that are frequently added/removed | Every add/remove requires \`rebuild()\` | Use Groups for stable components and Views for frequently changing ones |

---
`
};
