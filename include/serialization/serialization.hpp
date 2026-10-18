// serialization.hpp - 序列化模块统一入口
//
// 分层:
//   part/codec/          通用编解码原语 (json_writer/reader, binary_writer/reader,
//                        archive_codec, codec_json/binary/protobuf/flatbuffer, codec_registry)
//   model.hpp            存档模型 (limits / header / entity_remap / metadata / 格式与模式枚举)
//   codec_bridge.hpp     格式分派 + 变换链 (校验 → 压缩 → 加密)
//   type_registry.hpp    稳定类型名 + 类型工厂表 + 枚举/实体引用字段注册
//   field_codec.hpp      反射字段 ↔ archive_writer/reader
//   payload_codec.hpp    组件载荷编解码 + 类型化加载 (load_context)
//   entity_source.hpp    运行期 ECS 枚举 (注册类型 / def 类型 / 集合槽位 / 存活实体)
//   entity_identity.hpp  实体索引保序重建与空槽回收
//   archive_filter.hpp   选择性序列化过滤器
//   migration.hpp        字段级迁移 + 组件版本控制
//   stats.hpp            序列化统计信息
//   schema_export.hpp    跨语言 schema 导出 (.proto / .fbs)
//   full_archive.hpp     完整 ECS 存档门面 (唯一入口)
//   object_archive.hpp   与 ECS 解耦的通用对象序列化 (单对象 / 对象数组)

#include "../part/codec/archive_codec.hpp"
#include "../part/codec/codec_json.hpp"
#include "../part/codec/codec_binary.hpp"
#include "../part/codec/codec_protobuf.hpp"
#include "../part/codec/codec_flatbuffer.hpp"
#include "../part/codec/codec_registry.hpp"
#include "../part/crc32c.hpp"
#include "../part/safety.hpp"
#include "model.hpp"
#include "codec_bridge.hpp"
#include "type_registry.hpp"
#include "field_codec.hpp"
#include "payload_codec.hpp"
#include "entity_source.hpp"
#include "entity_identity.hpp"
#include "archive_filter.hpp"
#include "migration.hpp"
#include "stats.hpp"
#include "schema_export.hpp"
#include "full_archive.hpp"
#include "object_archive.hpp"
