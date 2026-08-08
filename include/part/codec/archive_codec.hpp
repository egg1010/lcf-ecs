// archive_codec.hpp - 编码器抽象接口
// 定义统一的写入/读取语义, 各格式 (JSON/Binary/Protobuf/FlatBuffer) 各自实现
// 公共逻辑层 (archive_logic) 通过此接口操作编码器, 实现逻辑复用 + 格式切换零拷贝
#pragma once

#include "operating_message.hpp"
#include "dense.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

// ============================================================================
// 归档字段类型 (用于编解码时的类型判断, 与 type_id 解耦)
// ============================================================================
enum class archive_type : uint8_t {
    null_t,
    bool_t,
    int32_t,
    uint32_t,
    int64_t,
    uint64_t,
    float32_t,
    float64_t,
    string_t,
    bytes_t,
    object_t,
    array_t
};

// ============================================================================
// archive_writer — 抽象写入器接口
// 各格式编码器实现此接口, 公共逻辑层通过指针/引用调用
// 设计: 结构化文档模型 (begin/end + key/value), 非 flat 风格
// ============================================================================
class archive_writer
{
public:
    virtual ~archive_writer() = default;

    // === 结构化写入 ===
    virtual void begin_object() noexcept = 0;
    virtual void end_object() noexcept = 0;
    virtual void begin_array(size_t count = 0) noexcept = 0;  // count=0 表示未知
    virtual void end_array() noexcept = 0;
    virtual void key(std::string_view k) noexcept = 0;

    // === 标量写入 ===
    virtual void write_bool(bool v) noexcept = 0;
    virtual void write_i32(int32_t v) noexcept = 0;
    virtual void write_u32(uint32_t v) noexcept = 0;
    virtual void write_i64(int64_t v) noexcept = 0;
    virtual void write_u64(uint64_t v) noexcept = 0;
    virtual void write_f32(float v) noexcept = 0;
    virtual void write_f64(double v) noexcept = 0;
    virtual void write_string(std::string_view v) noexcept = 0;

    // === 原始字节 (用于 trivially copyable 组件的二进制编码) ===
    virtual void write_bytes(const void* data, size_t len) noexcept = 0;

    // === 嵌入已格式化的片段 (用于 to_json/raw_value 等场景) ===
    virtual void write_raw(std::string_view fragment) noexcept = 0;

    // === 完成 + 取出缓冲区 ===
    [[nodiscard]] virtual std::string take() noexcept = 0;
    [[nodiscard]] virtual size_t size() const noexcept = 0;
};

// ============================================================================
// archive_reader — 抽象读取器接口
// 零拷贝设计: string 类型返回 string_view, 不复制
// ============================================================================
class archive_reader
{
public:
    virtual ~archive_reader() = default;

    // === 结构化读取 ===
    virtual bool enter_object() noexcept = 0;
    virtual void leave_object() noexcept = 0;
    virtual bool enter_array() noexcept = 0;
    virtual void leave_array() noexcept = 0;
    virtual bool next_element() noexcept = 0;  // 数组迭代, 返回 false 表示结束
    virtual void end_element() noexcept = 0;

    // === object 内按键迭代 ===
    // 返回空视图表示对象结束
    [[nodiscard]] virtual std::string_view next_key() noexcept = 0;

    // === 标量读取 (零拷贝: string 返回 string_view) ===
    [[nodiscard]] virtual bool read_bool() noexcept = 0;
    [[nodiscard]] virtual int32_t read_i32() noexcept = 0;
    [[nodiscard]] virtual uint32_t read_u32() noexcept = 0;
    [[nodiscard]] virtual int64_t read_i64() noexcept = 0;
    [[nodiscard]] virtual uint64_t read_u64() noexcept = 0;
    [[nodiscard]] virtual float read_f32() noexcept = 0;
    [[nodiscard]] virtual double read_f64() noexcept = 0;
    [[nodiscard]] virtual std::string_view read_string_view() noexcept = 0;
    [[nodiscard]] std::string read_string() noexcept {
        return std::string(read_string_view());
    }

    // === 原始字节读取 (用于 trivially copyable 组件) ===
    // 返回字节视图 (零拷贝), 调用方负责 memcpy
    [[nodiscard]] virtual std::string_view read_bytes_view(size_t len) noexcept = 0;

    // === 跳过当前值 (未知字段/类型) ===
    virtual void skip_value() noexcept = 0;

    // === 状态查询 ===
    [[nodiscard]] virtual bool has_error() const noexcept = 0;
    [[nodiscard]] virtual operating_message last_error() const noexcept = 0;

    // === 当前值类型 (用于判断读取方式) ===
    [[nodiscard]] virtual archive_type peek_type() const noexcept = 0;
};

// ============================================================================
// archive_codec — 编解码器工厂接口
// 负责创建 writer/reader, 并提供格式标识 (magic header)
// ============================================================================
struct archive_codec
{
    // 格式标识 magic (4 字节, 用于自动检测)
    char magic[4] = {0, 0, 0, 0};

    // 创建写入器
    [[nodiscard]] virtual archive_writer* create_writer() const noexcept = 0;
    // 创建读取器 (零拷贝: 不复制 data, 直接在原缓冲区上操作)
    [[nodiscard]] virtual archive_reader* create_reader(std::string_view data) const noexcept = 0;
    // 销毁
    virtual void destroy_writer(archive_writer* w) const noexcept = 0;
    virtual void destroy_reader(archive_reader* r) const noexcept = 0;

    virtual ~archive_codec() = default;

    // 检测数据是否匹配本格式
    [[nodiscard]] bool matches(std::string_view data) const noexcept {
        if (data.size() < 4)
        {
            return false;
        }
        return std::memcmp(data.data(), magic, 4) == 0;
    }

    // 深度结构校验: 各格式可覆写以验证内部结构完整性
    // 默认实现: 创建 reader 检查构造期无错误
    [[nodiscard]] virtual operating_message validate(std::string_view data) const noexcept {
        archive_reader* r = create_reader(data);
        operating_message res;
        if (!r || r->has_error())
        {
            res.write_message(false, "格式校验失败");
        }
        if (r)
        {
            destroy_reader(r);
        }
        return res;
    }
};
