#pragma once

// serialization.hpp - 序列化模块统一入口
//
// 分层:
//   part/codec/     通用编解码原语 (json_writer/reader, binary_writer/reader,
//                   archive_codec, codec_json/binary/protobuf/flatbuffer, codec_registry)
//   base/           存档基础: 存档模型、统计、迁移、过滤器、实体标识
//   codec/          编解码: 稳定类型注册、字段编解码、格式分派、运行期实体源
//   archive/        归档门面: 载荷编解码、对象序列化、schema 导出、完整存档入口

#include "part/codec/archive_codec.hpp"
#include "part/codec/codec_json.hpp"
#include "part/codec/codec_binary.hpp"
#include "part/codec/codec_protobuf.hpp"
#include "part/codec/codec_flatbuffer.hpp"
#include "part/codec/codec_registry.hpp"
#include "part/crc32c.hpp"
#include "part/safety.hpp"

// base 基础层
#include "serialization/base/model.hpp"
#include "serialization/base/stats.hpp"
#include "serialization/base/migration.hpp"
#include "serialization/base/archive_filter.hpp"
#include "serialization/base/entity_identity.hpp"

// codec 编解码层
#include "serialization/codec/type_registry.hpp"
#include "serialization/codec/field_codec.hpp"
#include "serialization/codec/codec_bridge.hpp"
#include "serialization/codec/entity_source.hpp"

// archive 归档层
#include "serialization/archive/payload_codec.hpp"
#include "serialization/archive/schema_export.hpp"
#include "serialization/archive/object_archive.hpp"
#include "serialization/archive/full_archive.hpp"
