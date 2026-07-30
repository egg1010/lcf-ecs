// serialization.hpp - 序列化模块统一入口
// 命名空间: ecs
//
// 模块分层:
//   archive_codec.hpp        编码器抽象接口 (archive_writer/reader/codec)
//   codec_json.hpp           JSON 编码器 (适配 json_writer/reader)
//   codec_binary.hpp         二进制编码器 (适配 binary_writer/reader)
//   codec_protobuf.hpp       Protobuf 风格编码器 (自研 wire format)
//   codec_flatbuffer.hpp     FlatBuffers 风格偏移表编码器 (零拷贝读取)
//   codec_registry.hpp       编码器注册表 + 格式自动检测
//   archive_logic.hpp        公共逻辑层 (实体收集/过滤/版本, 与格式无关)
//   safety.hpp               安全限制 + 字节序 + Base64 + RLE 压缩
//   type_name.hpp            稳定类型名 + 枚举注册
//   reflect_bridge.hpp       反射桥接 (嵌套对象/数组/枚举)
//   binary_writer.hpp        原生二进制写入器
//   binary_reader.hpp        原生二进制读取器
//   filter.hpp               选择性序列化过滤器
//   migration.hpp            字段级迁移 + 组件版本控制
//   stats.hpp                序列化统计信息
//   serializer.hpp           序列化器主类 (集成全部功能)

#include "archive_codec.hpp"
#include "codec_json.hpp"
#include "codec_binary.hpp"
#include "codec_protobuf.hpp"
#include "codec_flatbuffer.hpp"
#include "codec_registry.hpp"
#include "archive_logic.hpp"
#include "safety.hpp"
#include "type_name.hpp"
#include "reflect_bridge.hpp"
#include "binary_writer.hpp"
#include "binary_reader.hpp"
#include "filter.hpp"
#include "migration.hpp"
#include "stats.hpp"
#include "serializer.hpp"
