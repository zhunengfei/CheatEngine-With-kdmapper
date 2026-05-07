#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <variant>

enum class ScanType
{
    ExactValue,
    GreaterThan,
    LessThan,
    Between,
	UnknownInitial, //只有首次扫描才会有这个选项，表示不限制初始值，后续再根据用户输入的数值进行过滤
    StringScan  
};

enum class NextScanType
{
    Equal,
    NotEqual,
    Increased,
    Decreased,
    Changed,
    Unchanged,
    Between
};

enum class ScanDataType : uint8_t {
    Int8,
    Int16,
    Int32,
    Int64,
    Float32,
    Float64,
    AsciiString, 
    Utf16String,   
    ByteArray
};

inline size_t scanDataTypeSize(ScanDataType t) {
    switch (t) {
    case ScanDataType::Int8:    return 1;
    case ScanDataType::Int16:   return 2;
    case ScanDataType::Int32:   return 4;
    case ScanDataType::Int64:   return 8;
    case ScanDataType::Float32: return 4;
    case ScanDataType::Float64: return 8;
    case ScanDataType::AsciiString: return 0;
    case ScanDataType::Utf16String: return 0;
    case ScanDataType::ByteArray:   return 0;
    default: return 0;
    }
}

enum class ScanMode { First, Next };



// 仅在 Between 时使用
struct ValueParams {
    uint64_t value1 = 0;
    uint64_t value2 = 0;   
};

// 字符串参数
struct StringParams {
    std::string text;
    bool caseSensitive = true;
};

// 字节数组 (AOB) 参数
struct AobParams {
    std::vector<uint8_t> pattern;
    std::vector<bool>   mask;
};

// 统一特殊类型参数体
using ScanParams = std::variant<ValueParams, StringParams, AobParams>;


struct ScanResult
{
    uint64_t address;
};


// 扫描请求（唯一入口）
struct ScanRequest {
    ScanMode    mode = ScanMode::First;
    ScanDataType dataType = ScanDataType::Int32;

    size_t      alignment = 1;
    ScanType    firstType = ScanType::ExactValue;
    NextScanType nextType = NextScanType::Equal;

    // 模块过滤（0 表示不作限制）
    uint64_t moduleBase = 0;
    uint64_t moduleSize = 0;

    // 具体参数（由 mode 和 dataType 决定取哪种）
    ScanParams  params;

    bool onlyWritable;
    bool includeExecutable;

    std::shared_ptr<const std::vector<ScanResult>> prevResults = nullptr;
};

inline bool isFloatingPoint(ScanDataType t) {
    return t == ScanDataType::Float32 || t == ScanDataType::Float64;
}

inline bool isStringType(ScanDataType t) {
    return t == ScanDataType::AsciiString || t == ScanDataType::Utf16String;
}

inline bool isByteArrayType(ScanDataType t) {
    return t == ScanDataType::ByteArray;
}