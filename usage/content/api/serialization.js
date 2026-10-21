window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['serialization'] = {
  id: 'serialization',
  title: "serialization — 序列化/反序列化",
  category: 'serialization',
  icon: 'Z',
  order: 12,
  content: `## 12. serialization — 序列化/反序列化

命名空间 \`ecs\`。\`noexcept\`。

模块分两层。**通用对象序列化**（\`object_archive\`）与 \`part/codec\` 编解码原语不依赖 ECS，可序列化任意数据；**ECS 存档**（\`full_archive\`）在其之上枚举实体与组件。两层共用同一份字段层与编解码原语。

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
| 字段迁移 | 字段级迁移 + 组件版本控制 |
| 统计与导出 | 统计信息、跨语言 schema 导出 |
| 编码器 | 抽象接口 + JSON / 二进制 / Protobuf / FlatBuffers |
| 安全工具 | 安全限制、base64 / RLE / CRC32C |

---

# 一、通用对象序列化（不依赖 ECS）

### 12.1 载荷约定

| 数据类型 | 序列化方式 | 要求 |
|---------|-----------|------|
| 提供 \`to_json()\` / \`from_json()\` | 原生 JSON 值 | 两方法成对提供 |
| 已注册反射（\`REGISTER_MEMBERS\`） | 遍历字段生成对象 / 数组 | 字段为基本类型、\`std::string\`、嵌套对象、数组或已注册枚举 |
| trivially copyable 且未反射 | 原始字节（JSON 格式下为 base64 字符串） | 无 |
| 其他 | 空载荷（加载时跳过） | — |

### 12.2 object_archive 接口

命名空间 \`ecs\`。序列化任意数据类型，不需要 \`register_components\`，不依赖 \`manager\`。

| 接口 | 说明 |
|------|------|
| \`object_archive()\` | 构造 |
| \`bridge\` | \`codec_bridge\`：校验开关、压缩 / 加密函数对、保存 / 加载变换钩子 |
| \`limits\` | \`safety_limits\`：文件大小、元素数等上限 |
| \`format_version\` | 存档格式版本，加载时高于当前值拒绝（默认 1） |
| \`save_object(out, fmt, obj)\` / \`load_object(data, obj)\` | 单对象字符串读写 |
| \`save_array(out, fmt, items, count)\` / \`load_array(data, dense<T>& out)\` | 对象数组字符串读写，\`items\` 为 \`const T*\` |
| \`save_object_to_file(path, fmt, obj)\` / \`load_object_from_file(path, obj)\` | 单对象文件读写 |
| \`save_array_to_file(path, fmt, items, count)\` / \`load_array_from_file(path, dense<T>& out)\` | 对象数组文件读写 |

\`fmt\` 为 \`archive_format::json\`（默认）/ \`binary\` / \`protobuf\` / \`flatbuffer\`；加载自动检测格式。

### 12.3 使用示例

\`\`\`cpp

struct item_record { int id; float weight; std::string name; };
REGISTER_TYPE_ONLY(item_record);
REGISTER_MEMBERS(item_record, id, weight, name);

object_archive oa;

// 单对象
item_record one{7, 1.5f, "alpha"};
std::string js;
oa.save_object(js, archive_format::json, one);
item_record back{};
oa.load_object(js, back);

// 对象数组 (配置表 / 数据块)
dense<item_record> items;
items.push_back(item_record{1, 1.0f, "a"});
items.push_back(item_record{2, 2.0f, "b"});
std::string bin;
oa.save_array(bin, archive_format::protobuf, items.data(), items.size());

dense<item_record> loaded;
oa.load_array(bin, loaded);              // 自动检测格式

// 变换链 (校验默认启用)
oa.bridge.enable_checksum = true;
oa.bridge.compress_cb = rle_compress;
oa.bridge.decompress_cb = rle_decompress;
oa.bridge.encrypt_cb = my_encrypt;
oa.bridge.decrypt_cb = my_decrypt;
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

JSON / 二进制按字段名读写；Protobuf / FlatBuffers 不依赖键名，按写入顺序分配字段编号，读取按同样顺序推进，因此任意键名（含非 ECS 数据）均可正确往返。

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

# 二、ECS 存档（full_archive）

### 12.14 组件载荷约定

| 组件类型 | 序列化方式 | 要求 |
|---------|-----------|------|
| 提供 \`to_json()\` / \`from_json()\` | 原生 JSON 值 | 两方法成对提供 |
| 已注册反射 | 按反射字段顺序写入类型化字段 | 字段编号 = 声明顺序 |
| trivially copyable 且未反射 | 原始字节 | 无 |
| 其他 | 空载荷（加载时跳过） | — |

### 12.15 full_archive 接口

命名空间 \`ecs\`。ECS 存档唯一入口，\`register_components\` 一次后保存 / 加载免 \`Ts...\`。

| 接口 | 说明 |
|------|------|
| \`full_archive(manager& m)\` | 构造，绑定 manager |
| \`register_components<Ts...>()\` | 注册组件类型（幂等），保存与加载两端次序须一致 |
| \`save_to_string(out, fmt)\` / \`save_to_file(path, fmt)\` | 全量保存，\`fmt\` 同 12.2 |
| \`load_from_string(data)\` / \`load_from_file(path)\` | 全量加载，自动检测格式 |
| \`set_archive_version(v)\` / \`archive_version()\` | 存档版本读写 |
| \`set_engine_version(v)\` / \`engine_version()\` | 引擎版本读写 |
| \`set_metadata(key, value)\` / \`get_metadata(key)\` / \`all_metadata()\` | 元数据读写 |
| \`set_checksum_enabled(b)\` / \`is_checksum_enabled()\` | CRC32C 校验开关（默认启用） |
| \`set_filter(f)\` / \`filter()\` | 选择性过滤器 |
| \`set_load_mode(m)\` / \`get_load_mode()\` | 加载模式 |
| \`set_load_policy(p)\` / \`get_load_policy()\` | 加载策略 |
| \`set_compression(c, d)\` / \`set_encryption(e, d)\` | 压缩 / 加密函数对 |
| \`on_save(cb)\` / \`on_load(cb)\` / \`set_progress_callback(cb)\` | 变换钩子 / 进度回调 |
| \`limits()\` / \`last_stats()\` | 安全限制 / 上一次统计 |
| \`component_schema()\` | 运行期收集的组件类型描述 |
| \`schema_proto()\` / \`schema_fbs()\` | 按注册表生成 \`.proto\` / \`.fbs\` |

### 12.16 使用示例

\`\`\`cpp

ecs::manager mgr;
auto e1 = mgr.create_entity();
mgr.add(e1, Position{1.0f, 2.0f, 3.0f});
mgr.get_entity_state(e1.parts_.index_).tag = 5;

full_archive saver(mgr);
saver.register_components<Position, Velocity>();
saver.set_metadata("author", "dev");
saver.set_checksum_enabled(false);

std::string data;
saver.save_to_string(data, archive_format::protobuf);

ecs::manager mgr2;
full_archive loader(mgr2);
loader.register_components<Position, Velocity>();
loader.load_from_string(data);          // 自动检测格式

uint32_t idx = mgr2.get_single_class_set<Position>()->get_entity_indices()[0];
// 索引与状态还原: idx == e1.parts_.index_, mgr2.get_entity_state(idx).tag == 5

std::string proto = saver.schema_proto();
std::string fbs   = saver.schema_fbs();
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

覆盖范围：存活实体（含无组件实体）、实体状态全部字段、全部已注册模板组件、运行期 def 组件、元数据与组件版本表；瞬态队列与系统表不在范围内。

### 12.18 安全限制

\`safety_limits\` 控制加载上限，经 \`full_archive::limits()\` 或 \`object_archive::limits\` 修改。

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

多条件为 AND 关系，经 \`full_archive::set_filter(&filter)\` 应用。

### 12.21 加载模式

| 模式 | 行为 |
|------|------|
| \`load_mode::replace\` | 先 \`manager::clear()\`，再按存档索引保序重建（默认） |
| \`load_mode::append\` | 保留现有实体，追加加载（不保序） |
| \`load_mode::merge\` | 与 append 一致（预留语义扩展） |

### 12.22 存档元数据

\`set_metadata(key, value)\` 存入自定义键值对，保存写入 \`meta\`，加载回填；\`get_metadata(key)\` / \`all_metadata()\` 读取。

### 12.23 组件版本与字段级迁移

| 接口 | 说明 |
|------|------|
| \`register_component_version<T>(version)\` | 注册组件当前版本，保存写入 \`cv\` |
| \`register_migration<T>(from, to, fn)\` | 注册单步迁移函数，加载按 \`from → to\` 链式调用 |
| \`lookup_component_version<T>()\` | 查询组件当前版本 |
| \`register_field_rename<T>(old_name, new_name)\` | 加载时字段改名 |
| \`register_field_drop<T>(field_name)\` | 加载时丢弃字段 |
| \`register_field_default<T>(field_name, default_json)\` | 缺失字段注入默认值 |

迁移函数签名 \`void(*)(json_reader& old, json_writer& neu)\`，需自行调用 \`old.enter_object()\`。存档无 \`cv\` 条目时不迁移。

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
| 迁移函数未调用 \`old.enter_object()\` | 读取不到字段 | 在迁移 lambda 开头调用 |
| 启用校验后直接读格式标识 | 前 8 字节为 \`LCCS\` | 先 \`set_checksum_enabled(false)\`，或用 \`detect_codec\` |
| \`replace\` 加载到含信号的 manager | 旧实体销毁触发信号 | 属预期行为，注意信号槽清理 |
| \`create_writer\` / \`create_reader\` 后未 \`destroy_*\` | 内存泄漏 | 用完立即销毁 |
| Protobuf / FlatBuffers 按字段名读取 | 读取返回编号键 | 用 \`f1\` / \`f2\` / ... 迭代 |
| \`read_string_view\` 视图超出源缓冲生命周期 | 悬空指针 | 保留源缓冲区 |

`
};
