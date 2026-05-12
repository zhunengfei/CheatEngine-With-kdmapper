#pragma once
#include <QString>
#include <QByteArray>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "scan/scan_data_stream_define.h"

// 前向声明
class IMemoryAccessor;

/// @brief 地址列表中值的显示类型（映射 ScanDataType 到地址列表可用的类型子集）
enum class ValueType : uint8_t {
    Integer = 0,
    Int8,
    Int16,
    Int32,
    Int64,
    Float,
    Double,
    String,      // 字符串类型
    ByteArray,   // 字节数组类型
};

/// @brief 字符串编码类型
enum class StringEncoding : uint8_t {
    ASCII = 0,
    UTF8,
    UTF16,
};

/// @brief 将 ScanDataType 转换为 ValueType
inline ValueType scanDataTypeToValueType(ScanDataType sdt)
{
    switch (sdt) {
    case ScanDataType::Bit:     return ValueType::Int8;
    case ScanDataType::Int8:    return ValueType::Int8;
    case ScanDataType::Int16:   return ValueType::Int16;
    case ScanDataType::Int32:   return ValueType::Int32;
    case ScanDataType::Int64:   return ValueType::Int64;
    case ScanDataType::Float32: return ValueType::Float;
    case ScanDataType::Float64: return ValueType::Double;
    case ScanDataType::AsciiString:
    case ScanDataType::Utf8String:
    case ScanDataType::Utf16String: return ValueType::String;
    case ScanDataType::ByteArray:   return ValueType::ByteArray;
    default:                    return ValueType::Int32;
    }
}

/// @brief 获取 ValueType 对应的字节数（字符串/字节数组返回默认读取大小）
inline size_t valueTypeSize(ValueType vt)
{
    switch (vt) {
    case ValueType::Int8:    return 1;
    case ValueType::Int16:   return 2;
    case ValueType::Int32:   return 4;
    case ValueType::Int64:   return 8;
    case ValueType::Float:   return 4;
    case ValueType::Double:  return 8;
    case ValueType::String:  return 32;    // 默认读取32字节
    case ValueType::ByteArray: return 256; // 默认读取256字节
    default:                 return 4;
    }
}

/// @brief 判断是否为整数类型
inline bool isIntegerType(ValueType vt)
{
    return vt == ValueType::Int8 || vt == ValueType::Int16 ||
           vt == ValueType::Int32 || vt == ValueType::Int64 ||
           vt == ValueType::Integer;
}

/// @brief 判断是否为浮点类型
inline bool isFloatType(ValueType vt)
{
    return vt == ValueType::Float || vt == ValueType::Double;
}

/// @brief 判断是否为数值类型（整数或浮点）
inline bool isNumericType(ValueType vt)
{
    return isIntegerType(vt) || isFloatType(vt);
}

/// @brief 判断是否为字符串类型
inline bool isStringValueType(ValueType vt)
{
    return vt == ValueType::String;
}

/// @brief 判断是否为字节数组类型
inline bool isByteArrayValueType(ValueType vt)
{
    return vt == ValueType::ByteArray;
}

struct AddressItem
{
    QString     description;
    uint64_t    address = 0;
    uint64_t    rawValue = 0;     // 当前内存中的原始值（按类型读取的位模式，仅数值类型使用）
    ValueType   type = ValueType::Int32;
    bool        frozen = false;

    // 变化追踪
    uint64_t    lastRawValue = 0;
    bool        changed = false;

    // ---- 显示模式选项 ----
    bool        hexDisplay = false;     // 16进制显示（整数/浮点/字节数组）
    bool        signedDisplay = false;  // 有符号显示（仅整数类型）
    StringEncoding encoding = StringEncoding::UTF8; // 字符串编码（仅字符串类型）

    // ---- 字符串/字节数组数据缓冲区 ----
    std::vector<uint8_t> buffer;        // 存储字符串或字节数组的原始字节
    int         stringLength = 0;       // 字符串/字节数组的长度（用户可编辑）

    /// @brief 将原始值格式化为当前类型的显示字符串
    QString formattedValue() const;

    /// @brief 根据 hexDisplay 和 signedDisplay 格式化数值
    QString formattedNumericValue() const;

    /// @brief 格式化字符串值
    QString formattedStringValue() const;

    /// @brief 格式化字节数组值
    QString formattedByteArrayValue() const;

    /// @brief 从内存读取数据（根据类型自动选择读取方式）
    void readFromMemory(void* mem);

    /// @brief 写入数据到内存
    bool writeToMemory(void* mem) const;
};

// ===== inline 实现 =====
inline QString AddressItem::formattedValue() const
{
    if (isStringValueType(type))
        return formattedStringValue();
    if (isByteArrayValueType(type))
        return formattedByteArrayValue();
    return formattedNumericValue();
}

inline QString AddressItem::formattedStringValue() const
{
    if (buffer.empty()) return {};

    switch (encoding) {
    case StringEncoding::UTF16: {
        // UTF-16LE: 在空字处截断，然后转换为 QString
        const char16_t* u16 = reinterpret_cast<const char16_t*>(buffer.data());
        int u16len = buffer.size() / 2;
        int realLen = 0;
        while (realLen < u16len && u16[realLen] != 0)
            ++realLen;
        return QString::fromUtf16(u16, realLen);
    }
    case StringEncoding::UTF8: {
        // UTF-8: 在空字节处截断
        const char* data = reinterpret_cast<const char*>(buffer.data());
        int len = static_cast<int>(buffer.size());
        int realLen = 0;
        while (realLen < len && data[realLen] != '\0')
            ++realLen;
        return QString::fromUtf8(data, realLen);
    }
    case StringEncoding::ASCII:
    default: {
        // ASCII/Latin1: 在空字节处截断
        const char* data = reinterpret_cast<const char*>(buffer.data());
        int len = static_cast<int>(buffer.size());
        int realLen = 0;
        while (realLen < len && data[realLen] != '\0')
            ++realLen;
        return QString::fromLatin1(data, realLen);
    }
    }
}

inline QString AddressItem::formattedByteArrayValue() const
{
    if (buffer.empty()) return {};

    QString result;
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (i > 0) result += ' ';
        if (hexDisplay)
            result += QString::asprintf("%02X", buffer[i]);
        else
            result += QString::number(buffer[i]); // 十进制：222 12 73
    }
    return result;
}

inline QString AddressItem::formattedNumericValue() const
{
    switch (type) {
    case ValueType::Int8: {
        if (hexDisplay)
            return QString("0x%1").arg(static_cast<uint8_t>(rawValue & 0xFF), 2, 16, QChar('0'));
        auto v = static_cast<int8_t>(rawValue & 0xFF);
        if (signedDisplay)
            return QString::number(v);
        else
            return QString::number(static_cast<uint8_t>(rawValue & 0xFF));
    }
    case ValueType::Int16: {
        if (hexDisplay)
            return QString("0x%1").arg(static_cast<uint16_t>(rawValue & 0xFFFF), 4, 16, QChar('0'));
        auto v = static_cast<int16_t>(rawValue & 0xFFFF);
        if (signedDisplay)
            return QString::number(v);
        else
            return QString::number(static_cast<uint16_t>(rawValue & 0xFFFF));
    }
    case ValueType::Int32: {
        if (hexDisplay)
            return QString("0x%1").arg(static_cast<uint32_t>(rawValue), 8, 16, QChar('0'));
        if (signedDisplay)
            return QString::number(static_cast<int32_t>(rawValue));
        return QString::number(static_cast<uint32_t>(rawValue));
    }
    case ValueType::Int64: {
        if (hexDisplay)
            return QString("0x%1").arg(rawValue, 16, 16, QChar('0'));
        if (signedDisplay)
            return QString::number(static_cast<int64_t>(rawValue));
        return QString::number(rawValue);
    }
    case ValueType::Float: {
        if (hexDisplay) {
            uint32_t bits;
            std::memcpy(&bits, &rawValue, sizeof(bits));
            return QString("0x%1").arg(bits, 8, 16, QChar('0'));
        }
        float f;
        std::memcpy(&f, &rawValue, sizeof(f));
        return QString::number(f, 'g', 7);
    }
    case ValueType::Double: {
        if (hexDisplay) {
            return QString("0x%1").arg(rawValue, 16, 16, QChar('0'));
        }
        double d;
        std::memcpy(&d, &rawValue, sizeof(d));
        return QString::number(d, 'g', 15);
    }
    default: // Integer / Int32
        if (hexDisplay)
            return QString("0x%1").arg(static_cast<uint32_t>(rawValue), 8, 16, QChar('0'));
        return QString::number(static_cast<uint32_t>(rawValue));
    }
}

