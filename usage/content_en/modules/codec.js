window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['codec'] = {
  title: "codec — Archive codec interface",
  content: `## 59. codec — Archive codec interface

Global namespace; headers live under \`part/codec/\`. Provides the archive read/write abstraction, four built-in formats and a registry; \`object_archive\` / \`full_archive\` read and write data through this layer.

### 59.1 archive_type — Value types

| Enumerant | Description |
|------|------|
| \`archive_type::null_t\` | Null value |
| \`archive_type::bool_t\` | Boolean |
| \`archive_type::int32_t\` / \`uint32_t\` | 32-bit integer |
| \`archive_type::int64_t\` / \`uint64_t\` | 64-bit integer |
| \`archive_type::float32_t\` / \`float64_t\` | Floating point |
| \`archive_type::string_t\` | String |
| \`archive_type::bytes_t\` | Raw bytes |
| \`archive_type::object_t\` | Object |
| \`archive_type::array_t\` | Array |

### 59.2 archive_writer — Writer

| Interface | Description |
|------|------|
| \`begin_object()\` / \`end_object()\` | Enter / leave an object |
| \`begin_array(count = 0)\` / \`end_array()\` | Enter / leave an array; \`count\` of 0 means unknown length |
| \`key(k)\` | Write an object key |
| \`write_bool(v)\` / \`write_i32(v)\` / \`write_u32(v)\` | Write a scalar |
| \`write_i64(v)\` / \`write_u64(v)\` | Write a 64-bit integer |
| \`write_f32(v)\` / \`write_f64(v)\` | Write a floating-point value |
| \`write_string(v)\` | Write a string |
| \`write_bytes(data, len)\` | Write raw bytes |
| \`write_raw(fragment)\` | Write an already formatted fragment |
| \`take()\` / \`size()\` | Take the buffer / current length |

### 59.3 archive_reader — Reader

| Interface | Description |
|------|------|
| \`enter_object()\` / \`leave_object()\` | Enter / leave an object; entering returns whether it succeeded |
| \`enter_array()\` / \`leave_array()\` | Enter / leave an array |
| \`next_element()\` / \`end_element()\` | Array iteration; \`next_element\` returning \`false\` means the end |
| \`next_key()\` | Next object key; an empty view means the end |
| \`read_bool()\` / \`read_i32()\` / \`read_u32()\` | Read a scalar |
| \`read_i64()\` / \`read_u64()\` | Read a 64-bit integer |
| \`read_f32()\` / \`read_f64()\` | Read a floating-point value |
| \`read_string_view()\` / \`read_string()\` | Read a string (zero-copy / owning) |
| \`read_bytes_view(len)\` | Read a byte view |
| \`skip_value()\` | Skip the current value |
| \`peek_type()\` | Type of the current value |
| \`has_error()\` / \`last_error()\` | Error state |

### 59.4 archive_codec — Codec interface

| Interface | Description |
|------|------|
| \`magic\` | 4-byte format identifier |
| \`create_writer()\` | Create a writer |
| \`create_reader(data)\` | Create a reader (does not copy the input) |
| \`destroy_writer(w)\` / \`destroy_reader(r)\` | Destroy the writer / reader |
| \`matches(data)\` | Whether the data matches this format |
| \`validate(data)\` | Structural validation; returns \`operating_message\`, empty on success |

### 59.5 Registry and built-in formats

| Interface | Description |
|------|------|
| \`codec_registry::instance()\` | Global registry |
| \`register_codec(c)\` | Register a custom codec |
| \`get(index)\` | Get a codec by index |
| \`detect(data)\` | Detect by format identifier; falls back to JSON when nothing matches |
| \`count()\` | Number of registered codecs |
| \`codec_index::json\` / \`binary\` / \`protobuf\` / \`flatbuffer\` | Built-in format indices 0~3 |
| \`get_codec(fmt_idx)\` | Get a codec by index (free function) |
| \`detect_codec(data)\` | Automatic detection (free function) |

Built-in codec types: \`json_codec\` / \`binary_codec\` / \`protobuf_codec\` / \`flatbuffer_codec\`.

### 59.6 binary_writer / binary_reader

| Interface | Description |
|------|------|
| \`binary_writer(reserve)\` | Constructor, preallocates the write buffer |
| \`begin_object()\` / \`end_object()\` / \`begin_array(n)\` / \`end_array()\` | Structure placeholders |
| \`key(k)\` | Write a key |
| \`value(v)\` | Write an integer / floating point / boolean / string |
| \`value_trivial(v)\` | Write a trivially copyable object |
| \`write_raw_bytes(data, len)\` | Write raw bytes |
| \`take()\` | Take the buffer |
| \`binary_reader(data)\` | Constructor |
| \`read_u32()\` / \`read_u64()\` / \`read_i32()\` / \`read_i64()\` | Read integers |
| \`read_f32()\` / \`read_f64()\` / \`read_bool()\` | Read floating point / boolean |
| \`read_string_view()\` / \`read_string()\` | Read a string |
| \`read_trivial(out)\` | Read a trivially copyable object |
| \`skip(n)\` / \`has_error()\` | Skip bytes / error state |

The \`json_writer\` / \`json_reader\` interfaces are described on the serialization page 12.8 and 12.9.

### 59.7 Usage example

\`\`\`cpp
const archive_codec* codec = get_codec(codec_index::json);
archive_writer* writer = codec->create_writer();
writer->begin_object();
writer->key("hp");
writer->write_i32(100);
writer->end_object();
std::string data = writer->take();
codec->destroy_writer(writer);

const archive_codec* detected = detect_codec(data);
archive_reader* reader = detected->create_reader(data);
if (reader->enter_object())
{
    std::string_view key = reader->next_key();
    int32_t hp = reader->read_i32();
    reader->leave_object();
}
detected->destroy_reader(reader);
\`\`\`

### 59.8 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Not destroying a writer / reader after creating it | Memory leak | Return it with \`destroy_writer\` / \`destroy_reader\` |
| Not checking \`enter_object()\` before reading | Reading on a structure mismatch yields invalid values | Check the entry result and \`has_error()\` |
| Relying on \`matches\` for formats without magic | protobuf has no fixed header | Use \`detect_codec\` for automatic detection |
`
};
