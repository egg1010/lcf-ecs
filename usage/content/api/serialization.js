window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['serialization'] = {
  id: 'serialization',
  title: "serialization — 序列化/反序列化",
  category: 'serialization',
  icon: 'Z',
  order: 12,
  content: `## 12. serialization — 序列化/反序列化

命名空间 \`ecs\`。\`noexcept\`。

模块分两部分。**对象与数组序列化**不依赖 ECS，可序列化任意数据类型；**整存档**在绑定 \`ecs::manager\` 后枚举实体与组件。两者共用同一份字段层与编解码原语，统一经 \`serialize::manager\` 调用。

| 能力 | 说明 |
|------|------|
| 统一入口 | 一次引入序列化与全部子能力 |
| 通用对象序列化 | 单对象 / 对象数组，不依赖 ECS |
| 完整 ECS 存档 | 实体与组件的保存 / 加载门面 |
| 存档模型 | header / metadata / 实体重映射 / 安全限制 / 格式与模式枚举 |
| 字段层 | 反射字段 ↔ 编码器，格式分派与变换链 |
| 类型登记 | 稳定类型名、类型工厂表、枚举与实体引用字段注册 |
| 实体枚举与重建 | 运行期枚举实体、索引保序重建 |
| 选择性过滤 | 按条件筛选存档内容 |
| 统计与导出 | 统计信息、跨语言 schema 导出 |
| 编码器 | 抽象接口 + JSON / 二进制 / Protobuf / FlatBuffers |
| 安全工具 | 安全限制、base64 / RLE / CRC32C |

---

# 一、通用对象序列化（不依赖 ECS）

### 12.1 载荷约定

| 数据类型 | 序列化方式 | 要求 |
|---------|-----------|------|
| 提供 \`to_json()\` / \`from_json()\` | 原生 JSON 值 | 两方法成对提供 |
| 已注册反射（\`add_field\` / \`add_fields\`） | 遍历字段生成对象 / 数组 | 字段为基本类型、\`std::string\`、嵌套对象、数组或已注册枚举 |
| trivially copyable 且未反射 | 原始字节（JSON 格式下为 base64 字符串） | 无 |
| 其他 | 空载荷（加载时跳过） | — |

### 12.2 manager 接口

命名空间 \`serialize\`。序列化统一入口，可失败接口返回 \`operating_message\`（空对象表示成功）。构造时可选注入 \`ecs::manager\` 以启用整存档。

| 接口 | 说明 |
|------|------|
| \`manager()\` | 构造，用于对象与数组序列化 |
| \`manager(ecs::manager& world)\` | 构造并绑定 ECS 世界，可整存档 |
| \`set_format(fmt)\` | 输出格式，默认 \`json\` |
| \`set_checksum(on)\` | CRC32C 校验开关 |
| \`set_compression(c, d)\` / \`set_encryption(e, d)\` | 压缩 / 加密函数对 |
| \`set_save_transform(cb)\` / \`set_load_transform(cb)\` | 保存 / 加载变换钩子 |
| \`save_object(out, obj)\` / \`load_object(data, obj)\` | 单对象字符串读写 |
| \`save_array(out, items, count)\` / \`load_array(data, dense<T>&)\` | 对象数组读写，\`items\` 为 \`const T*\` |
| \`save_object_to_file(path, obj)\` / \`load_object_from_file(path, obj)\` | 单对象文件读写 |
| \`save_array_to_file(path, items, count)\` / \`load_array_from_file(path, dense<T>&)\` | 对象数组文件读写 |
| \`save_world(out)\` / \`save_world_to_file(path)\` | 整存档保存，需绑定 world |
| \`load_world(data)\` / \`load_world_from_file(path)\` | 整存档加载，自动检测格式 |
| \`set_world_metadata(key, value)\` | 整存档元数据 |
| \`world_schema_proto()\` / \`world_schema_fbs()\` | 按注册表生成 \`.proto\` / \`.fbs\` |
| \`last_stats()\` | 上一次存档统计，未绑定 world 返回 nullptr |
| \`world()\` / \`objects()\` | 底层 \`full_archive\` / \`object_archive\` 访问器 |

\`fmt\` 为 \`archive_format::json\`（默认）/ \`binary\` / \`protobuf\` / \`flatbuffer\`；加载自动检测格式。

### 12.3 使用示例

序列化读取 \`reflect::global()\` 的注册信息。需要参与序列化的类型和字段必须注册到全局注册表；仅注册到独立的 \`reflect::manager\` 实例不会被序列化读取。

\`\`\`cpp

struct item_record { int id; float weight; std::string name; };
reflect::global().register_type_only<item_record>("item_record");
reflect::global().register_member_runtime<item_record>("item_record", "id", &item_record::id);
reflect::global().register_member_runtime<item_record>("item_record", "weight", &item_record::weight);
reflect::global().register_member_runtime<item_record>("item_record", "name", &item_record::name);

serialize::manager m;
m.set_format(archive_format::json);

// 单对象
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

// 对象数组 (配置表 / 数据块)
dense<item_record> items;
items.emplace_back(item_record{1, 1.0f, "a"});
items.emplace_back(item_record{2, 2.0f, "b"});
std::string bin;
m.set_format(archive_format::protobuf).save_array(bin, items.data(), items.size());

dense<item_record> loaded;
if (auto e = m.load_array(bin, loaded); !e)   // 自动检测格式
{
    std::fprintf(stderr, "load array failed: %u\n", e.code());
}

// 变换链 (校验默认启用)
m.set_checksum(true)
 .set_compression(rle_compress, rle_decompress)
 .set_encryption(my_encrypt, my_decrypt);
\`\`\`

### 12.4 对象存档表示

JSON 格式的根结构：

\`\`\`json
{
  "v": 1,
  "items": [
    {"id": 7, "weight": 1.5, "name": "alpha"}
  ]
}
\`\`\`

- \`v\`：格式版本
- \`items\`：对象数组；单对象即长度为 1
- 元素内容按 12.1 生成（反射类型为对象，原始字节为 base64 字符串）

### 12.5 编码器抽象接口

各编码器实现下列接口，与格式无关。

\`archive_writer\`：

| 接口 | 说明 |
|------|------|
| \`begin_object()\` / \`end_object()\` | 对象起止 |
| \`begin_array(count)\` / \`end_array()\` | 数组起止 |
| \`key(k)\` | 字段键 |
| \`write_bool(v)\` / \`write_i32(v)\` / \`write_u32(v)\` / \`write_i64(v)\` / \`write_u64(v)\` | 写入整数 |
| \`write_f32(v)\` / \`write_f64(v)\` | 写入浮点 |
| \`write_string(v)\` | 写入字符串 |
| \`write_bytes(data, len)\` | 写入原始字节 |
| \`write_raw(fragment)\` | 嵌入已格式化片段 |
| \`take()\` / \`size()\` | 取走结果 / 当前长度 |

\`archive_reader\`：

| 接口 | 说明 |
|------|------|
| \`enter_object()\` / \`leave_object()\` | 进入 / 退出对象 |
| \`enter_array()\` / \`leave_array()\` | 进入 / 退出数组 |
| \`next_element()\` / \`end_element()\` | 数组迭代 |
| \`next_key()\` | 下一字段键（空表示结束） |
| \`read_bool()\` / \`read_i32()\` / \`read_u32()\` / \`read_i64()\` / \`read_u64()\` | 读取整数 |
| \`read_f32()\` / \`read_f64()\` | 读取浮点 |
| \`read_string_view()\` / \`read_string()\` | 读取字符串（视图 / 拷贝） |
| \`read_bytes_view(len)\` | 读取字节视图 |
| \`set_element_size(size)\` | 声明数组元素编码宽度 |
| \`skip_value()\` | 跳过当前值 |
| \`has_error()\` / \`last_error()\` / \`peek_type()\` | 状态查询 |

\`archive_codec\`：

| 接口 | 说明 |
|------|------|
| \`magic[4]\` | 格式标识（可覆写 \`matches\` 自定义检测） |
| \`create_writer()\` / \`create_reader(data)\` | 创建读写器 |
| \`destroy_writer(w)\` / \`destroy_reader(r)\` | 销毁 |
| \`matches(data)\` / \`validate(data)\` | 格式检测 / 结构校验 |

### 12.6 编码器注册表


| 接口 | 说明 |
|------|------|
| \`codec_registry::instance()\` | 单例 |
| \`register_codec(c)\` | 注册自定义编码器 |
| \`detect(data)\` | 按各编码器 \`matches\` 检测格式 |
| \`get(idx)\` / \`count()\` | 按索引取编码器 / 数量 |
| \`get_codec(fmt_idx)\` | 按 \`codec_index\` 取编码器 |
| \`detect_codec(data)\` | 自动检测并返回编码器 |

\`codec_index\`：\`json=0\` / \`binary=1\` / \`protobuf=2\` / \`flatbuffer=3\`。

### 12.7 四种内置编码器

| 编码器 | 格式标识 | 字段标识方式 |
|--------|---------|------------|
| \`json_codec\` | 首字符 \`{\` | 字段名 |
| \`binary_codec\` | \`LCE1\` | 字段名 |
| \`protobuf_codec\` | 无（按 wire 结构判定） | 字段编号 = 写入顺序 |
| \`flatbuffer_codec\` | file_identifier \`LCFB\` | 字段编号 = 写入顺序 |

JSON / 二进制按字段名读写；Protobuf / FlatBuffers 不依赖键名，按写入顺序分配字段编号，读取按同样顺序推进，因此任意键名（含非 ECS 数据）均可正确往返。用于长期存档时不要重排或删除既有字段，新增字段应追加在末尾。

### 12.8 json_writer 接口

全局命名空间。

| 接口 | 说明 |
|------|------|
| \`json_writer(reserve, pretty)\` | 构造，预分配缓冲、可选美化输出 |
| \`begin_object()\` / \`end_object()\` | 对象 |
| \`begin_array()\` / \`end_array()\` | 数组 |
| \`key(k)\` | 键 |
| \`value(v)\` | 值（string / int / uint / float / double / bool） |
| \`null()\` | null 值 |
| \`raw_value(json)\` | 嵌入原始 JSON 片段 |
| \`string()\` / \`take()\` | 取结果 |

### 12.9 json_reader 接口

全局命名空间。

| 接口 | 说明 |
|------|------|
| \`json_reader(src)\` | 构造，传入 JSON 字符串 |
| \`enter_object()\` / \`exit_object()\` | 进入 / 退出对象 |
| \`enter_array()\` / \`exit_array()\` | 进入 / 退出数组 |
| \`next_key()\` | 下一键（空表示结束） |
| \`next_element()\` / \`end_element()\` | 数组迭代 |
| \`read_int32()\` / \`read_uint32()\` / \`read_int64()\` / \`read_uint64()\` | 读取整数 |
| \`read_double()\` / \`read_bool()\` / \`read_string()\` | 读取其他标量 |
| \`read_raw_value()\` | 读取原始 JSON 片段 |
| \`skip_value()\` | 跳过当前值 |
| \`has_error()\` / \`last_error()\` / \`clear_error()\` | 错误状态 |

### 12.10 用户手写 JSON 偏差容忍

\`json_reader\` 兼容常见非标准写法：

| 偏差 | 示例 | 支持 |
|------|------|------|
| 多余空白 | \`{ "a" : 1 }\` | ✓ |
| 尾随逗号 | \`{"a":1,}\` / \`[1,2,]\` | ✓ |
| 单引号字符串 | \`{'a':'x'}\` | ✓ |
| 块注释 | \`{"a":1/* c */}\` | ✓ |
| 行注释 | \`{"a":1}\` 后跟 \`// c\` | ✓ |
| 数字前导 \`+\` | \`{"a":+1}\` | ✓ |

### 12.11 反射字段层（field_codec）

四种格式共用同一份反射字段实现。

| 字段类型 | 处理方式 |
|---------|---------|
| 基本类型（int / float / bool / string） | 直接读写 |
| 嵌套对象 | 递归进入子对象 |
| 数组字段 | 逐元素读写 |
| 已注册枚举 | 按底层整数读写 |

| 接口 | 说明 |
|------|------|
| \`to_archive(w, obj)\` / \`from_archive(r, obj)\` | 反射对象 ↔ 编码器读写 |
| \`has_json_serialize<T>\` / \`has_json_deserialize<T>\` | 概念：类型提供 \`to_json\` / \`from_json\` |
| \`is_reflected<T>()\` / \`is_type_reflected(tid)\` | 反射注册查询 |

### 12.12 跨语言 schema 导出


| 接口 | 说明 |
|------|------|
| \`to_proto<Ts...>()\` | proto3 schema 文本 |
| \`to_fbs<Ts...>()\` | FlatBuffers schema 文本（file_identifier \`LCFB\`） |

字段编号约定：根 \`version\` / \`engine\` / \`meta\` / \`cv\` / \`entities\` / \`components\` = f1..f6；实体 \`i\` / \`v\` / \`f\` / \`t\` / \`l\` / \`g\` = f1..f6；组件条目 \`i\` / \`v\` / \`d\` = f1..f3；组件表按注册顺序编号。

### 12.13 原语使用示例

\`\`\`cpp

// 直接使用编码器
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

// 自动检测格式
const archive_codec* codec = detect_codec(data);

// 注册自定义编码器
class my_codec final : public archive_codec {
    // create_writer / create_reader / destroy_writer / destroy_reader / matches
};
codec_registry::instance().register_codec(&my_codec_instance);
\`\`\`

---

# 二、整存档

### 12.14 组件载荷约定

| 组件类型 | 序列化方式 | 要求 |
|---------|-----------|------|
| 提供 \`to_json()\` / \`from_json()\` | 原生 JSON 值 | 两方法成对提供 |
| 已注册反射 | 按反射字段顺序写入类型化字段 | 字段编号 = 声明顺序 |
| trivially copyable 且未反射 | 原始字节 | 无 |
| 其他 | 空载荷（加载时跳过） | — |

### 12.15 整存档接口

绑定 \`ecs::manager\` 后经 manager 保存 / 加载；版本、过滤器、加载策略等高级配置经 \`world()\` 访问底层 \`full_archive\`。

| 接口 | 说明 |
|------|------|
| \`manager(ecs::manager& world)\` | 绑定 ECS 世界 |
| \`world()->register_components<Ts...>()\` | 注册组件类型（幂等），保存与加载两端次序须一致 |
| \`save_world(out)\` / \`save_world_to_file(path)\` | 全量保存 |
| \`load_world(data)\` / \`load_world_from_file(path)\` | 全量加载，自动检测格式 |
| \`set_world_metadata(key, value)\` | 写入元数据 |
| \`world_schema_proto()\` / \`world_schema_fbs()\` | 按注册表生成 \`.proto\` / \`.fbs\` |
| \`world()\` | 高级配置：\`set_archive_version\` / \`set_engine_version\` / \`set_filter\` / \`set_load_mode\` / \`set_load_policy\` / \`set_progress_callback\` / \`component_schema()\` / \`limits()\` / \`last_stats()\` |

### 12.16 使用示例

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
if (auto e = loader.load_world(data); !e)   // 自动检测格式
{
    std::fprintf(stderr, "load failed: %u\n", e.code());
}

uint32_t idx = mgr2.get_single_class_set<Position>()->get_entity_indices()[0];
// 索引与状态还原: idx == e1.parts_.index_, mgr2.get_entity_state_by_index(idx).tag == 5

std::string proto = loader.world_schema_proto();
std::string fbs   = loader.world_schema_fbs();
\`\`\`

### 12.17 存档表示

JSON 格式的根结构：

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

- \`version\` / \`engine\`：存档 / 引擎版本，加载时高版本拒绝
- \`meta\` / \`cv\`：元数据 / 组件版本表（嵌套对象）
- \`entities\`：实体表；\`i\` 索引、\`v\` 版本、\`f\` / \`t\` / \`l\` / \`g\` 为 flags / tag / layer / group_id
- \`components\`：组件表，键为类型名，元素为 \`i\` / \`v\` / \`d\`
- \`d\`：组件载荷（同 12.14）
- 官方 JSON 值：\`NaN\` / \`Infinity\` 映射为 \`null\`，读回取默认值

覆盖范围：存活实体（含无组件实体）、实体状态全部字段、全部已注册模板组件、平凡运行期 def 组件、元数据与组件版本表；瞬态队列与系统表不在范围内。非平凡 def 组件的对象表示不可直接按字节持久化，保存时会显式返回不支持错误。

### 12.18 安全限制

\`safety_limits\` 控制加载上限，经 \`m.world()->limits()\` 或 \`m.objects().limits\` 修改。

| 字段 | 默认值 | 说明 |
|------|--------|------|
| \`max_file_size\` | 256 MB | 单文件最大字节数 |
| \`max_string_length\` | 16 MB | 单字符串最大长度 |
| \`max_array_elements\` | 10,000,000 | 数组最大元素数 |
| \`max_object_fields\` | 65536 | 对象最大字段数 |
| \`max_depth\` | 64 | JSON 最大嵌套深度 |
| \`max_entity_count\` | 10,000,000 | 最大实体数 |

### 12.19 类型名与引用字段注册

跨编译器时 \`typeid(T).name()\` 不一致，用稳定名保证可移植。

| 接口 | 说明 |
|------|------|
| \`register_type_name<T>(stable_name)\` | 注册类型 T 的稳定名 |
| \`register_type_alias<T>(old_name)\` | 注册旧名别名（旧名 → T） |
| \`lookup_type_name(type_id)\` / \`lookup_type_id(name)\` | 名字 ↔ id 查询 |
| \`register_entity_field<T>(field_name)\` | 注册 entity 引用字段（加载时自动重映射） |
| \`register_enum<T>()\` / \`lookup_enum_underlying(type_id)\` | 枚举底层类型注册 / 查询 |

### 12.20 选择性过滤与单实体保存

| 接口 | 说明 |
|------|------|
| \`serialize_filter::by_layer(l)\` | 仅保留指定 layer |
| \`serialize_filter::by_tag(t)\` | 仅保留指定 tag |
| \`serialize_filter::by_group(g)\` | 仅保留指定 group |
| \`serialize_filter::by_flags(mask, value)\` | \`(flags & mask) == value\` |
| \`serialize_filter::by_entities(ids)\` | 仅保留白名单索引；收窄到单个实体即单实体保存 |

多条件为 AND 关系，经 \`m.world()->set_filter(&filter)\` 应用。

### 12.21 加载模式

| 模式 | 行为 |
|------|------|
| \`load_mode::replace\` | 先 \`manager::clear()\`，再按存档索引保序重建（默认） |
| \`load_mode::append\` | 保留现有实体，追加加载（不保序） |
| \`load_mode::merge\` | 与 append 一致（预留语义扩展） |

### 12.22 存档元数据

\`set_metadata(key, value)\` 存入自定义键值对，保存写入 \`meta\`，加载回填；\`get_metadata(key)\` / \`all_metadata()\` 读取。

### 12.24 加载策略

\`strict\`（默认）遇错即失败；\`best_effort\` 跳过损坏组件继续加载，跳过数记入 \`last_stats().skipped_count\`。

### 12.25 校验与加密

保存按 \`校验 → 压缩 → 加密 → on_save\` 依次变换，加载严格逆序。

| 接口 | 说明 |
|------|------|
| \`set_checksum_enabled(b)\` | 启用（默认）/ 关闭 CRC32C 校验，启用后数据前加 8 字节 \`LCCS\` 前缀 |
| \`set_encryption(encrypt_fn, decrypt_fn)\` | 加密 / 解密函数对，\`std::string(*)(const std::string&)\` |

兼容未启用校验的数据：无 \`LCCS\` 前缀时原样处理。需外部工具读取格式标识时，先 \`set_checksum_enabled(false)\`。

### 12.26 压缩与变换钩子

| 接口 | 说明 |
|------|------|
| \`set_compression(compress_fn, decompress_fn)\` | 保存后压缩、加载前解压 |
| \`on_save(cb)\` / \`on_load(cb)\` | 保存后 / 加载前自定义变换，\`void(*)(std::string&)\` |

安全工具提供 \`rle_compress\` / \`rle_decompress\`。

### 12.27 进度回调与统计

| 接口 | 说明 |
|------|------|
| \`set_progress_callback(cb)\` | \`void(*)(size_t current, size_t total)\` |
| \`last_stats()\` | \`serialize_stats\`：\`entity_count\` / \`total_bytes\` / \`archive_version\` / \`skipped_count\` / \`per_type\`（每类型组件数与字节数） |

---

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 未提供 \`to_json\` / \`from_json\`，且未反射、非 trivially copyable | 载荷为空，加载被跳过 | 提供两方法、注册反射或确保 trivially copyable |
| \`to_json\` 与 \`from_json\` 字段不一致 | 数据丢失 | 两端字段保持一致 |
| 保存与加载注册的组件类型或次序不一致 | 组件表字段错位、组件被跳过 | 两端 \`register_components\` 一致 |
| 跨编译器加载未注册稳定名 | 类型名不匹配 | 用 \`register_type_name\` 注册稳定名 |
| 组件含 entity 引用字段未注册 | 引用指向旧索引 | 用 \`register_entity_field<T>(field_name)\` 注册 |
| 加载高于当前版本的存档 | 版本检查失败 | 提高 \`set_archive_version\` 或降级存档 |
| 版本不匹配 | 加载失败 | 保存端与加载端 \`set_archive_version\` 保持一致 |
| 启用校验后直接读格式标识 | 前 8 字节为 \`LCCS\` | 先 \`set_checksum_enabled(false)\`，或用 \`detect_codec\` |
| \`replace\` 加载到含信号的 manager | 旧实体销毁触发信号 | 属预期行为，注意信号槽清理 |
| \`create_writer\` / \`create_reader\` 后未 \`destroy_*\` | 内存泄漏 | 用完立即销毁 |
| Protobuf / FlatBuffers 按字段名读取 | 读取返回编号键 | 用 \`f1\` / \`f2\` / ... 迭代 |
| \`read_string_view\` 视图超出源缓冲生命周期 | 悬空指针 | 保留源缓冲区 |

`
};
