window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['serialization'] = {
  title: "serialization — Serialization and deserialization",
  content: `## 12. serialization — Serialization/Deserialization

Namespace \`ecs\`. \`noexcept\`.

The module has two parts. **Object and array serialization** does not depend on ECS and can serialize arbitrary data types; **full archives** enumerate entities and components after binding \`ecs::manager\`. Both share the same field layer and codec primitives, and are called uniformly through \`serialize::manager\`.

| Capability | Description |
|------|------|
| Unified entry | Pulls in serialization together with all sub-capabilities |
| Generic object serialization | Single object / object array, independent of ECS |
| Full ECS archive | Facade for saving / loading entities and components |
| Archive model | header / metadata / entity remapping / safety limits / format and mode enums |
| Field layer | reflected fields ↔ encoders, format dispatch and transform chain |
| Type registration | stable type names, type factory table, enum and entity-reference field registration |
| Entity enumeration and rebuild | runtime entity enumeration, index-order-preserving rebuild |
| Selective filtering | filter archive contents by condition |
| Statistics and export | statistics, cross-language schema export |
| Encoders | abstract interface + JSON / binary / Protobuf / FlatBuffers |
| Safety utilities | safety limits, base64 / RLE / CRC32C |

### Quick start

\`\`\`cpp
struct item { int id; float weight; };

// Types and fields participating in serialization must be registered in the global reflection table
reflect::global().register_type_only<item>("item");
reflect::global().register_member_runtime<item>("item", "id", &item::id);
reflect::global().register_member_runtime<item>("item", "weight", &item::weight);

serialize::manager m;
m.set_format(archive_format::json);

item one{7, 1.5f};
std::string out;
(void)m.save_object(out, one);              // Save as a JSON string

item back{};
(void)m.load_object(out, back);             // Restore
\`\`\`

A full archive binds \`ecs::manager\` at construction time and then uses \`save_world\` / \`load_world\`; see 12.3 for details.

---

# I. Generic object serialization (independent of ECS)

### 12.1 Payload conventions

| Data type | Serialization method | Requirement |
|---------|-----------|------|
| Provides \`to_json()\` / \`from_json()\` | Native JSON value | Both methods provided as a pair |
| Registered reflection (\`add_field\` / \`add_fields\`) | Traverse fields to produce an object / array | Fields are primitive types, \`std::string\`, nested objects, arrays, or registered enums |
| trivially copyable and not reflected | Raw bytes (base64 string in JSON format) | None |
| Other | Empty payload (skipped on load) | — |

### 12.2 manager interface

Namespace \`serialize\`. The unified serialization entry; failable interfaces return \`operating_message\` (an empty object means success). At construction an \`ecs::manager\` may optionally be injected to enable full archives.

| Interface | Description |
|------|------|
| \`manager()\` | Construct, for object and array serialization |
| \`manager(ecs::manager& world)\` | Construct and bind an ECS world, enabling full archives |
| \`set_format(fmt)\` | Output format, default \`json\` |
| \`set_checksum(on)\` | CRC32C checksum switch |
| \`set_compression(c, d)\` / \`set_encryption(e, d)\` | Compression / encryption function pair |
| \`set_save_transform(cb)\` / \`set_load_transform(cb)\` | Save / load transform hook |
| \`save_object(out, obj)\` / \`load_object(data, obj)\` | Single-object string read/write |
| \`save_array(out, items, count)\` / \`load_array(data, dense<T>&)\` | Object array read/write, \`items\` is \`const T*\` |
| \`save_object_to_file(path, obj)\` / \`load_object_from_file(path, obj)\` | Single-object file read/write |
| \`save_array_to_file(path, items, count)\` / \`load_array_from_file(path, dense<T>&)\` | Object array file read/write |
| \`save_world(out)\` / \`save_world_to_file(path)\` | Full archive save, requires a bound world |
| \`load_world(data)\` / \`load_world_from_file(path)\` | Full archive load, detects format automatically |
| \`set_world_metadata(key, value)\` | Full archive metadata |
| \`world_schema_proto()\` / \`world_schema_fbs()\` | Generate \`.proto\` / \`.fbs\` from the registry |
| \`last_stats()\` | Statistics of the last archive, returns nullptr when no world is bound |
| \`world()\` / \`objects()\` | Accessors for the underlying \`full_archive\` / \`object_archive\` |

\`fmt\` is \`archive_format::json\` (default) / \`binary\` / \`protobuf\` / \`flatbuffer\`; loading detects the format automatically.

### 12.3 Usage example

Serialization reads the registration information of \`reflect::global()\`. Types and fields that need to participate in serialization must be registered in the global registry; registration only into a separate \`reflect::manager\` instance will not be read by serialization.

\`\`\`cpp

struct item_record { int id; float weight; std::string name; };
reflect::global().register_type_only<item_record>("item_record");
reflect::global().register_member_runtime<item_record>("item_record", "id", &item_record::id);
reflect::global().register_member_runtime<item_record>("item_record", "weight", &item_record::weight);
reflect::global().register_member_runtime<item_record>("item_record", "name", &item_record::name);

serialize::manager m;
m.set_format(archive_format::json);

// Single object
item_record one{7, 1.5f, "alpha"};
std::string js;
if (auto e = m.save_object(js, one); !e)
{
    std::fprintf(stderr, "save failed: %u\n", e.code());
}
item_record back{};
if (auto e = m.load_object(js, back); !e)
{
    std::fprintf(stderr, "load failed: %u\n", e.code());
}

// Object array (config tables / data blocks)
dense<item_record> items;
items.emplace_back(item_record{1, 1.0f, "a"});
items.emplace_back(item_record{2, 2.0f, "b"});
std::string bin;
m.set_format(archive_format::protobuf).save_array(bin, items.data(), items.size());

dense<item_record> loaded;
if (auto e = m.load_array(bin, loaded); !e)   // Format detected automatically
{
    std::fprintf(stderr, "load array failed: %u\n", e.code());
}

// Transform chain (checksum enabled by default)
m.set_checksum(true)
 .set_compression(rle_compress, rle_decompress)
 .set_encryption(my_encrypt, my_decrypt);
\`\`\`

### 12.4 Object archive representation

Root structure of the JSON format:

\`\`\`json
{
  "v": 1,
  "items": [
    {"id": 7, "weight": 1.5, "name": "alpha"}
  ]
}
\`\`\`

- \`v\`: format version
- \`items\`: object array; a single object is an array of length 1
- Element contents are produced as described in 12.1 (reflected types become objects, raw bytes become base64 strings)

### 12.5 Encoder abstract interface

Each encoder implements the following interfaces, independent of format.

\`archive_writer\`:

| Interface | Description |
|------|------|
| \`begin_object()\` / \`end_object()\` | Start / end of an object |
| \`begin_array(count)\` / \`end_array()\` | Start / end of an array |
| \`key(k)\` | Field key |
| \`write_bool(v)\` / \`write_i32(v)\` / \`write_u32(v)\` / \`write_i64(v)\` / \`write_u64(v)\` | Write integers |
| \`write_f32(v)\` / \`write_f64(v)\` | Write floating point |
| \`write_string(v)\` | Write a string |
| \`write_bytes(data, len)\` | Write raw bytes |
| \`write_raw(fragment)\` | Embed an already-formatted fragment |
| \`take()\` / \`size()\` | Take the result / current length |

\`archive_reader\`:

| Interface | Description |
|------|------|
| \`enter_object()\` / \`leave_object()\` | Enter / leave an object |
| \`enter_array()\` / \`leave_array()\` | Enter / leave an array |
| \`next_element()\` / \`end_element()\` | Array iteration |
| \`next_key()\` | Next field key (empty means end) |
| \`read_bool()\` / \`read_i32()\` / \`read_u32()\` / \`read_i64()\` / \`read_u64()\` | Read integers |
| \`read_f32()\` / \`read_f64()\` | Read floating point |
| \`read_string_view()\` / \`read_string()\` | Read a string (view / copy) |
| \`read_bytes_view(len)\` | Read a byte view |
| \`set_element_size(size)\` | Declare the encoded width of array elements |
| \`skip_value()\` | Skip the current value |
| \`has_error()\` / \`last_error()\` / \`peek_type()\` | State queries |

\`archive_codec\`:

| Interface | Description |
|------|------|
| \`magic[4]\` | Format identifier (override \`matches\` for custom detection) |
| \`create_writer()\` / \`create_reader(data)\` | Create a writer / reader |
| \`destroy_writer(w)\` / \`destroy_reader(r)\` | Destroy |
| \`matches(data)\` / \`validate(data)\` | Format detection / structural validation |

### 12.6 Encoder registry


| Interface | Description |
|------|------|
| \`codec_registry::instance()\` | Singleton |
| \`register_codec(c)\` | Register a custom encoder |
| \`detect(data)\` | Detect format via each encoder's \`matches\` |
| \`get(idx)\` / \`count()\` | Get encoder by index / count |
| \`get_codec(fmt_idx)\` | Get encoder by \`codec_index\` |
| \`detect_codec(data)\` | Detect automatically and return the encoder |

\`codec_index\`: \`json=0\` / \`binary=1\` / \`protobuf=2\` / \`flatbuffer=3\`.

### 12.7 The four built-in encoders

| Encoder | Format identifier | Field identification |
|--------|---------|------------|
| \`json_codec\` | first character \`{\` | Field name |
| \`binary_codec\` | \`LCE1\` | Field name |
| \`protobuf_codec\` | none (determined by wire structure) | Field number = write order |
| \`flatbuffer_codec\` | file_identifier \`LCFB\` | Field number = write order |

JSON / binary read and write by field name; Protobuf / FlatBuffers do not rely on key names — they assign field numbers by write order and advance in the same order when reading, so any key names (including non-ECS data) round-trip correctly. For long-term archives, do not reorder or delete existing fields; new fields should be appended at the end.

### 12.8 json_writer interface

Global namespace.

| Interface | Description |
|------|------|
| \`json_writer(reserve, pretty)\` | Construct, preallocates the buffer, optional pretty output |
| \`begin_object()\` / \`end_object()\` | Object |
| \`begin_array()\` / \`end_array()\` | Array |
| \`key(k)\` | Key |
| \`value(v)\` | Value (string / int / uint / float / double / bool) |
| \`null()\` | null value |
| \`raw_value(json)\` | Embed a raw JSON fragment |
| \`string()\` / \`take()\` | Take the result |

### 12.9 json_reader interface

Global namespace.

| Interface | Description |
|------|------|
| \`json_reader(src)\` | Construct, pass in a JSON string |
| \`enter_object()\` / \`exit_object()\` | Enter / exit an object |
| \`enter_array()\` / \`exit_array()\` | Enter / exit an array |
| \`next_key()\` | Next key (empty means end) |
| \`next_element()\` / \`end_element()\` | Array iteration |
| \`read_int32()\` / \`read_uint32()\` / \`read_int64()\` / \`read_uint64()\` | Read integers |
| \`read_double()\` / \`read_bool()\` / \`read_string()\` | Read other scalars |
| \`read_raw_value()\` | Read a raw JSON fragment |
| \`skip_value()\` | Skip the current value |
| \`has_error()\` / \`last_error()\` / \`clear_error()\` | Error state |

### 12.10 Tolerance for deviations in hand-written JSON

\`json_reader\` accepts common non-standard forms:

| Deviation | Example | Supported |
|------|------|------|
| Extra whitespace | \`{ "a" : 1 }\` | ✓ |
| Trailing commas | \`{"a":1,}\` / \`[1,2,]\` | ✓ |
| Single-quoted strings | \`{'a':'x'}\` | ✓ |
| Block comments | \`{"a":1/* c */}\` | ✓ |
| Line comments | \`{"a":1}\` followed by \`// c\` | ✓ |
| Leading \`+\` on numbers | \`{"a":+1}\` | ✓ |

### 12.11 Reflection field layer (field_codec)

The four formats share the same reflection field implementation.

| Field type | Handling |
|---------|---------|
| Primitive types (int / float / bool / string) | Read and write directly |
| Nested object | Recursively enter the sub-object |
| Array field | Read and write element by element |
| Registered enum | Read and write as the underlying integer |

| Interface | Description |
|------|------|
| \`to_archive(w, obj)\` / \`from_archive(r, obj)\` | Reflected object ↔ encoder read/write |
| \`has_json_serialize<T>\` / \`has_json_deserialize<T>\` | Concept: the type provides \`to_json\` / \`from_json\` |
| \`is_reflected<T>()\` / \`is_type_reflected(tid)\` | Reflection registration queries |

### 12.12 Cross-language schema export


| Interface | Description |
|------|------|
| \`to_proto<Ts...>()\` | proto3 schema text |
| \`to_fbs<Ts...>()\` | FlatBuffers schema text (file_identifier \`LCFB\`) |

Field numbering convention: root \`version\` / \`engine\` / \`meta\` / \`cv\` / \`entities\` / \`components\` = f1..f6; entity \`i\` / \`v\` / \`f\` / \`t\` / \`l\` / \`g\` = f1..f6; component entry \`i\` / \`v\` / \`d\` = f1..f3; component tables are numbered in registration order.

### 12.13 Primitive usage example

\`\`\`cpp

// Use encoders directly
protobuf_codec pc;
archive_writer* w = pc.create_writer();
w->begin_object();
w->key("version"); w->write_u32(42);
w->key("name");    w->write_string("hello");
w->end_object();
std::string data = w->take();
pc.destroy_writer(w);

archive_reader* r = pc.create_reader(data);
if (r->enter_object()) {
    std::string_view k;
    while (!(k = r->next_key()).empty()) {
        if (k == "f1") { (void)r->read_u32(); }
        else if (k == "f2") { (void)r->read_string_view(); }
        else { r->skip_value(); }
    }
}
pc.destroy_reader(r);

// Detect the format automatically
const archive_codec* codec = detect_codec(data);

// Register a custom encoder
class my_codec final : public archive_codec {
    // create_writer / create_reader / destroy_writer / destroy_reader / matches
};
codec_registry::instance().register_codec(&my_codec_instance);
\`\`\`

---

# II. Full archives

### 12.14 Component payload conventions

| Component type | Serialization method | Requirement |
|---------|-----------|------|
| Provides \`to_json()\` / \`from_json()\` | Native JSON value | Both methods provided as a pair |
| Registered reflection | Write typed fields in reflected field order | Field number = declaration order |
| trivially copyable and not reflected | Raw bytes | None |
| Other | Empty payload (skipped on load) | — |

### 12.15 Full archive interface

After binding \`ecs::manager\`, save / load through the manager; advanced configuration such as version, filters, and load strategy is accessed via \`world()\` on the underlying \`full_archive\`.

| Interface | Description |
|------|------|
| \`manager(ecs::manager& world)\` | Bind an ECS world |
| \`world()->register_components<Ts...>()\` | Register component types (idempotent); the order must match on the saving and loading sides |
| \`save_world(out)\` / \`save_world_to_file(path)\` | Full save |
| \`load_world(data)\` / \`load_world_from_file(path)\` | Full load, detects format automatically |
| \`set_world_metadata(key, value)\` | Write metadata |
| \`world_schema_proto()\` / \`world_schema_fbs()\` | Generate \`.proto\` / \`.fbs\` from the registry |
| \`world()\` | Advanced configuration: \`set_archive_version\` / \`set_engine_version\` / \`set_filter\` / \`set_load_mode\` / \`set_load_policy\` / \`set_progress_callback\` / \`component_schema()\` / \`limits()\` / \`last_stats()\` |

### 12.16 Usage example

\`\`\`cpp

ecs::manager mgr;
auto e1 = mgr.create_entity();
mgr.add(e1, Position{1.0f, 2.0f, 3.0f});
mgr.get_entity_state_by_index(e1.parts_.index_).tag = 5;

serialize::manager saver(mgr);
saver.world()->register_components<Position, Velocity>();
(void)saver.set_world_metadata("author", "dev");
saver.set_checksum(false).set_format(archive_format::protobuf);

std::string data;
if (auto e = saver.save_world(data); !e)
{
    std::fprintf(stderr, "save failed: %u\n", e.code());
}

ecs::manager mgr2;
serialize::manager loader(mgr2);
loader.world()->register_components<Position, Velocity>();
if (auto e = loader.load_world(data); !e)   // Format detected automatically
{
    std::fprintf(stderr, "load failed: %u\n", e.code());
}

uint32_t idx = mgr2.get_single_class_set<Position>()->get_entity_indices()[0];
// Index and state restored: idx == e1.parts_.index_, mgr2.get_entity_state_by_index(idx).tag == 5

std::string proto = loader.world_schema_proto();
std::string fbs   = loader.world_schema_fbs();
\`\`\`

### 12.17 Archive representation

Root structure of the JSON format:

\`\`\`json
{
  "version": 1,
  "engine": 0,
  "meta": {"author": "dev"},
  "cv": {"Hp": 2},
  "entities": [{"i": 1, "v": 1, "f": 0, "t": 5, "l": 0, "g": 0}],
  "components": {"Hp": [{"i": 1, "v": 1, "d": {"c": 42, "m": 100}}]}
}
\`\`\`

- \`version\` / \`engine\`: archive / engine version; a higher version is rejected on load
- \`meta\` / \`cv\`: metadata / component version table (nested objects)
- \`entities\`: entity table; \`i\` index, \`v\` version, \`f\` / \`t\` / \`l\` / \`g\` are flags / tag / layer / group_id
- \`components\`: component table, keys are type names, elements are \`i\` / \`v\` / \`d\`
- \`d\`: component payload (same as 12.14)
- Official JSON values: \`NaN\` / \`Infinity\` are mapped to \`null\` and read back as default values

Coverage: live entities (including entities without components), all entity state fields, all registered template components, trivially-destructible runtime def components, metadata and component version tables; transient queues and system tables are out of scope. The object representation of non-trivially-destructible def components cannot be persisted directly as bytes; saving explicitly returns an unsupported error.

### 12.18 Safety limits

\`safety_limits\` controls load limits, modified via \`m.world()->limits()\` or \`m.objects().limits\`.

| Field | Default value | Description |
|------|--------|------|
| \`max_file_size\` | 256 MB | Maximum bytes per file |
| \`max_string_length\` | 16 MB | Maximum length of a single string |
| \`max_array_elements\` | 10,000,000 | Maximum number of array elements |
| \`max_object_fields\` | 65536 | Maximum number of object fields |
| \`max_depth\` | 64 | Maximum JSON nesting depth |
| \`max_entity_count\` | 10,000,000 | Maximum number of entities |

### 12.19 Type names and reference field registration

Across compilers \`typeid(T).name()\` is not consistent; use stable names to guarantee portability.

| Interface | Description |
|------|------|
| \`register_type_name<T>(stable_name)\` | Register a stable name for type T |
| \`register_type_alias<T>(old_name)\` | Register an old-name alias (old name → T) |
| \`lookup_type_name(type_id)\` / \`lookup_type_id(name)\` | Name ↔ id lookup |
| \`register_entity_field<T>(field_name)\` | Register an entity reference field (remapped automatically on load) |
| \`register_enum<T>()\` / \`lookup_enum_underlying(type_id)\` | Enum underlying type registration / lookup |

### 12.20 Selective filtering and single-entity save

| Interface | Description |
|------|------|
| \`serialize_filter::by_layer(l)\` | Keep only the specified layer |
| \`serialize_filter::by_tag(t)\` | Keep only the specified tag |
| \`serialize_filter::by_group(g)\` | Keep only the specified group |
| \`serialize_filter::by_flags(mask, value)\` | \`(flags & mask) == value\` |
| \`serialize_filter::by_entities(ids)\` | Keep only whitelisted indices; narrowing to a single entity is a single-entity save |

Multiple conditions are combined with AND, applied via \`m.world()->set_filter(&filter)\`.

### 12.21 Load modes

| Mode | Behavior |
|------|------|
| \`load_mode::replace\` | Calls \`manager::clear()\` first, then rebuilds in archive index order (default) |
| \`load_mode::append\` | Keeps existing entities and appends the load (order not preserved) |
| \`load_mode::merge\` | Same as append (reserved for semantic extension) |

### 12.22 Archive metadata

\`set_metadata(key, value)\` stores custom key-value pairs, written into \`meta\` on save and restored on load; \`get_metadata(key)\` / \`all_metadata()\` read them back.

### 12.24 Load policy

\`strict\` (default) fails on the first error; \`best_effort\` skips corrupted components and continues, with the skip count recorded in \`last_stats().skipped_count\`.

### 12.25 Checksum and encryption

Saving applies transforms in the order \`checksum → compression → encryption → on_save\`; loading applies them in strict reverse order.

| Interface | Description |
|------|------|
| \`set_checksum_enabled(b)\` | Enable (default) / disable CRC32C checksum; when enabled, an 8-byte \`LCCS\` prefix is prepended to the data |
| \`set_encryption(encrypt_fn, decrypt_fn)\` | Encryption / decryption function pair, \`std::string(*)(const std::string&)\` |

Compatible with data that has no checksum enabled: data without the \`LCCS\` prefix is processed as-is. To have an external tool read the format identifier, call \`set_checksum_enabled(false)\` first.

### 12.26 Compression and transform hooks

| Interface | Description |
|------|------|
| \`set_compression(compress_fn, decompress_fn)\` | Compress after saving, decompress before loading |
| \`on_save(cb)\` / \`on_load(cb)\` | Custom transform after save / before load, \`void(*)(std::string&)\` |

Safety utilities provide \`rle_compress\` / \`rle_decompress\`.

### 12.27 Progress callback and statistics

| Interface | Description |
|------|------|
| \`set_progress_callback(cb)\` | \`void(*)(size_t current, size_t total)\` |
| \`last_stats()\` | \`serialize_stats\`: \`entity_count\` / \`total_bytes\` / \`archive_version\` / \`skipped_count\` / \`per_type\` (component count and bytes per type) |

---

### Notes

| Incorrect practice | Problem | Correct practice |
|---------|------|---------|
| No \`to_json\` / \`from_json\` provided, and not reflected, not trivially copyable | Payload is empty, loading is skipped | Provide both methods, register reflection, or ensure trivially copyable |
| \`to_json\` and \`from_json\` fields inconsistent | Data loss | Keep fields consistent on both sides |
| Component types or order registered for save and load differ | Component table fields misaligned, components skipped | \`register_components\` consistent on both sides |
| Loading across compilers without registering a stable name | Type names do not match | Register a stable name with \`register_type_name\` |
| Component contains an entity reference field that is not registered | The reference points to an old index | Register with \`register_entity_field<T>(field_name)\` |
| Loading an archive newer than the current version | Version check fails | Raise \`set_archive_version\` or downgrade the archive |
| Version mismatch | Load fails | Keep \`set_archive_version\` consistent on the saving and loading sides |
| Reading the format identifier directly with checksum enabled | The first 8 bytes are \`LCCS\` | Call \`set_checksum_enabled(false)\` first, or use \`detect_codec\` |
| \`replace\` loading into a manager that has signals | Old entities are destroyed and trigger signals | Expected behavior; take care to clean up signal slots |
| \`create_writer\` / \`create_reader\` without \`destroy_*\` | Memory leak | Destroy immediately after use |
| Reading Protobuf / FlatBuffers by field name | Reads return numbered keys | Iterate with \`f1\` / \`f2\` / ... |
| \`read_string_view\` view outlives the source buffer | Dangling pointer | Keep the source buffer alive |

`
};
