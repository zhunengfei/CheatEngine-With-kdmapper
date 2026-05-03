#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <filesystem>

// 磁盘存储的最小单元（可根据需要扩展字段）
struct PackedScanResult {
    uint64_t address;
    uint64_t value;
};

constexpr size_t DISK_BACKED_DEFAULT_MEM_THRESHOLD = 100'000;

class DiskBackedResultSet {
public:
    explicit DiskBackedResultSet(size_t memoryThreshold = DISK_BACKED_DEFAULT_MEM_THRESHOLD);
    ~DiskBackedResultSet();

    // 禁止拷贝，允许移动
    DiskBackedResultSet(const DiskBackedResultSet&) = delete;
    DiskBackedResultSet& operator=(const DiskBackedResultSet&) = delete;
    DiskBackedResultSet(DiskBackedResultSet&& other) noexcept;
    DiskBackedResultSet& operator=(DiskBackedResultSet&& other) noexcept;

    // 添加单个结果（会自动切换到磁盘模式）
    void add(uint64_t address, uint64_t value);

    // 获取当前结果总数
    size_t size() const;

    // 清空所有数据（包括磁盘临时文件）
    void clear();

    // 将所有数据移动到另一个集合（自身清空）
    void moveTo(DiskBackedResultSet& other);

    // 导出全部数据到内存向量（仅在数据量小时使用）
    std::vector<PackedScanResult> toVector() const;

    // 分段读取：返回 [start, start+count) 范围的数据
    // 注意：仅在磁盘模式下高效，内存模式下也会从内存拷贝
    std::vector<PackedScanResult> readChunk(size_t start, size_t count) const;

    // 当前是否在磁盘模式
    bool onDisk() const;
    // 获取磁盘文件路径（仅在磁盘模式有效）
    const std::string& diskFile() const;

private:
    void flushToDisk();   // 内存 -> 磁盘
    void reset();         // 清理资源，不加锁
    std::string createTempFile();

    std::vector<PackedScanResult> m_memBuffer;
    std::string m_diskFile;
    std::ofstream m_diskStream;     // 磁盘追加流
    size_t m_count = 0;
    bool m_onDisk = false;
    size_t m_memThreshold;
    mutable std::mutex m_mutex;
};