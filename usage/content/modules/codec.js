window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['codec'] = {
  id: 'codec',
  title: "codec — 编解码原语",
  category: 'codec',
  icon: 'D',
  order: 59,
  content: `## 59. codec — 编解码原语

全局命名空间，头文件位于 \`part/codec/\`。提供归档读写抽象、四种内置格式与注册表，\`object_archive\` / \`full_archive\` 通过本层读写数据。

### 59.1 archive_type — 值类型

| 枚举 | 说明 |
|------|------|
| \`archive_type::null_t\` | 空值 |
| \`archive_type::bool_t\` | 布尔 |
| \`archive_type::int32_t\` / \`uint32_t\` | 32 位整数 |
| \`archive_type::int64_t\` / \`uint64_t\` | 64 位整数 |
| \`archive_type::float32_t\` / \`float64_t\` | 浮点 |
| \`archive_type::string_t\` | 字符串 |
| \`archive_type::bytes_t\` | 原始字节 |
| \`archive_type::object_t\` | 对象 |
| \`archive_type::array_t\` | 数组 |

### 59.2 archive_writer — 写入器

| 接口 | 说明 |
|------|------|
| \`begin_object()\` / \`end_object()\` | 进入 / 退出对象 |
| \`begin_array(count = 0)\` / \`end_array()\` | 进入 / 退出数组；\`count\` 为 0 表示长度未知 |
| \`key(k)\` | 写入对象键 |
| \`write_bool(v)\` / \`write_i32(v)\` / \`write_u32(v)\` | 写入标量 |
| \`write_i64(v)\` / \`write_u64(v)\` | 写入 64 位整数 |
| \`write_f32(v)\` / \`write_f64(v)\` | 写入浮点 |
| \`write_string(v)\` | 写入字符串 |
| \`write_bytes(data, len)\` | 写入原始字节 |
| \`write_raw(fragment)\` | 写入已格式化的片段 |
| \`take()\` / \`size()\` | 取出缓冲区 / 当前长度 |

### 59.3 archive_reader — 读取器

| 接口 | 说明 |
|------|------|
| \`enter_object()\` / \`leave_object()\` | 进入 / 退出对象，进入返回是否成功 |
| \`enter_array()\` / \`leave_array()\` | 进入 / 退出数组 |
| \`next_element()\` / \`end_element()\` | 数组迭代；\`next_element\` 返回 \`false\` 表示结束 |
| \`next_key()\` | 对象下一键；空视图表示结束 |
| \`read_bool()\` / \`read_i32()\` / \`read_u32()\` | 读取标量 |
| \`read_i64()\` / \`read_u64()\` | 读取 64 位整数 |
| \`read_f32()\` / \`read_f64()\` | 读取浮点 |
| \`read_string_view()\` / \`read_string()\` | 读取字符串（零拷贝 / 拥有型） |
| \`read_bytes_view(len)\` | 读取字节视图 |
| \`skip_value()\` | 跳过当前值 |
| \`peek_type()\` | 当前值类型 |
| \`has_error()\` / \`last_error()\` | 错误状态 |

### 59.4 archive_codec — 编解码器接口

| 接口 | 说明 |
|------|------|
| \`magic\` | 4 字节格式标识 |
| \`create_writer()\` | 创建写入器 |
| \`create_reader(data)\` | 创建读取器（不复制输入） |
| \`destroy_writer(w)\` / \`destroy_reader(r)\` | 销毁写入器 / 读取器 |
| \`matches(data)\` | 数据是否匹配本格式 |
| \`validate(data)\` | 结构校验，返回 \`operating_message\` |

### 59.5 注册表与内置格式

| 接口 | 说明 |
|------|------|
| \`codec_registry::instance()\` | 全局注册表 |
| \`register_codec(c)\` | 注册自定义编解码器 |
| \`get(index)\` | 按索引取编解码器 |
| \`detect(data)\` | 按格式标识检测，未匹配回退 JSON |
| \`count()\` | 已注册数量 |
| \`codec_index::json\` / \`binary\` / \`protobuf\` / \`flatbuffer\` | 内置格式索引 0~3 |
| \`get_codec(fmt_idx)\` | 按索引取编解码器（自由函数） |
| \`detect_codec(data)\` | 自动检测（自由函数） |

内置编解码器类型：\`json_codec\` / \`binary_codec\` / \`protobuf_codec\` / \`flatbuffer_codec\`。

### 59.6 binary_writer / binary_reader

| 接口 | 说明 |
|------|------|
| \`binary_writer(reserve)\` | 构造，预分配写入缓冲 |
| \`begin_object()\` / \`end_object()\` / \`begin_array(n)\` / \`end_array()\` | 结构占位 |
| \`key(k)\` | 写入键 |
| \`value(v)\` | 写入整数 / 浮点 / 布尔 / 字符串 |
| \`value_trivial(v)\` | 写入平凡可拷贝对象 |
| \`write_raw_bytes(data, len)\` | 写入原始字节 |
| \`take()\` | 取出缓冲区 |
| \`binary_reader(data)\` | 构造 |
| \`read_u32()\` / \`read_u64()\` / \`read_i32()\` / \`read_i64()\` | 读取整数 |
| \`read_f32()\` / \`read_f64()\` / \`read_bool()\` | 读取浮点 / 布尔 |
| \`read_string_view()\` / \`read_string()\` | 读取字符串 |
| \`read_trivial(out)\` | 读取平凡可拷贝对象 |
| \`skip(n)\` / \`has_error()\` | 跳过字节 / 错误状态 |

\`json_writer\` / \`json_reader\` 的接口见序列化页 12.8 与 12.9。

### 59.7 使用示例

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

### 59.8 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 创建 writer / reader 后不销毁 | 内存泄漏 | 用 \`destroy_writer\` / \`destroy_reader\` 归还 |
| 读取前不判断 \`enter_object()\` | 结构不符时继续读取得到无效值 | 检查进入结果与 \`has_error()\` |
| 对无 magic 的格式依赖 \`matches\` | protobuf 无固定头 | 用 \`detect_codec\` 自动检测 |
`
};
