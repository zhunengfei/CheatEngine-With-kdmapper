#include "disk_backed_result_set.h"
#include <stdexcept>
#include <cstring>

static constexpr size_t PACKED_RECORD_SIZE = sizeof(PackedScanResult);

DiskBackedResultSet::DiskBackedResultSet(size_t memoryThreshold)
    : m_memThreshold(memoryThreshold)
{
    m_memBuffer.reserve(std::min(m_memThreshold, size_t(100'000))); // 预留一小部分
}

DiskBackedResultSet::~DiskBackedResultSet()
{
    clear();
}

DiskBackedResultSet::DiskBackedResultSet(DiskBackedResultSet&& other) noexcept
    : m_memBuffer(std::move(other.m_memBuffer)),
    m_diskFile(std::move(other.m_diskFile)),
    m_diskStream(std::move(other.m_diskStream)),
    m_count(other.m_count),
    m_onDisk(other.m_onDisk),
    m_memThreshold(other.m_memThreshold)
{
    // 重置 other 状态
    other.m_count = 0;
    other.m_onDisk = false;
}

DiskBackedResultSet& DiskBackedResultSet::operator=(DiskBackedResultSet&& other) noexcept
{
    if (this != &other) {
        clear();
        m_memBuffer = std::move(other.m_memBuffer);
        m_diskFile = std::move(other.m_diskFile);
        m_diskStream = std::move(other.m_diskStream);
        m_count = other.m_count;
        m_onDisk = other.m_onDisk;
        m_memThreshold = other.m_memThreshold;

        other.m_count = 0;
        other.m_onDisk = false;
    }
    return *this;
}

void DiskBackedResultSet::add(uint64_t address, uint64_t value)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_onDisk) {
        m_memBuffer.push_back({ address, value });
        if (m_memBuffer.size() >= m_memThreshold) {
            flushToDisk();
        }
    }
    else {
        // 磁盘模式：直接写入文件
        if (!m_diskStream.is_open()) {
            m_diskStream.open(m_diskFile, std::ios::binary | std::ios::app);
            if (!m_diskStream) {
                throw std::runtime_error("Failed to open disk file for writing");
            }
        }
        m_diskStream.write(reinterpret_cast<const char*>(&address), sizeof(address));
        m_diskStream.write(reinterpret_cast<const char*>(&value), sizeof(value));
        if (!m_diskStream) {
            throw std::runtime_error("Failed to write to disk file");
        }
    }
    ++m_count;
}

size_t DiskBackedResultSet::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

void DiskBackedResultSet::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    reset();
}

//void DiskBackedResultSet::moveTo(DiskBackedResultSet& other)
//{
//    std::lock_guard<std::mutex> lock(m_mutex);
//
//    other.clear();
//    if (!m_onDisk) {
//        std::swap(other.m_memBuffer, m_memBuffer);
//        other.m_count = m_count;
//    }
//    else {
//        if (m_diskStream.is_open()) {
//            m_diskStream.close();
//        }
//        other.m_diskFile = std::move(m_diskFile);
//        other.m_onDisk = true;
//        other.m_count = m_count;
//    }
//
//    // 清空自身
//    m_memBuffer.clear();
//    m_memBuffer.shrink_to_fit();
//
//    m_diskFile.clear();
//    m_count = 0;
//    m_onDisk = false;
//}

// disk_backed_result_set.cpp

void DiskBackedResultSet::moveTo(DiskBackedResultSet& other)
{
    // 【修改点3】：加锁保护（如果该对象可能在其他地方被访问）
    // std::lock_guard<std::mutex> lock1(m_mutex); // 自身锁
    // std::lock_guard<std::mutex> lock2(other.m_mutex); // 目标锁

    other.clear(); // 先清理旧的目标数据

    // 强制关闭当前流，确保文件落盘并释放句柄
    if (m_diskStream.is_open()) {
        m_diskStream.close();
    }

    other.m_memBuffer = std::move(m_memBuffer);
    other.m_diskFile = std::move(m_diskFile);
    other.m_onDisk = m_onDisk;
    other.m_count = m_count;
    other.m_memThreshold = m_memThreshold;

    // 重置自身状态，防止析构时删除已转移的文件
    m_onDisk = false;
    m_count = 0;
    m_diskFile.clear();
}


std::vector<PackedScanResult> DiskBackedResultSet::toVector() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackedScanResult> result;
    result.reserve(m_count);

    if (!m_onDisk) {
        result.assign(m_memBuffer.begin(), m_memBuffer.end());
    }
    else {
        std::ifstream file(m_diskFile, std::ios::binary);
        PackedScanResult entry;
        while (file.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<PackedScanResult> DiskBackedResultSet::readChunk(size_t start, size_t count) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackedScanResult> chunk;
    if (start >= m_count) return chunk;

    size_t end = std::min(start + count, m_count);
    chunk.reserve(end - start);

    if (!m_onDisk) {
        for (size_t i = start; i < end; ++i) {
            chunk.push_back(m_memBuffer[i]);
        }
    }
    else {
        std::ifstream file(m_diskFile, std::ios::binary);
        file.seekg(start * PACKED_RECORD_SIZE);
        PackedScanResult entry;
        for (size_t i = start; i < end && file.read(reinterpret_cast<char*>(&entry), sizeof(entry)); ++i) {
            chunk.push_back(entry);
        }
    }
    return chunk;
}

bool DiskBackedResultSet::onDisk() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_onDisk;
}

const std::string& DiskBackedResultSet::diskFile() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_diskFile;
}

void DiskBackedResultSet::flushToDisk()
{
    // 调用者已持有 m_mutex
    if (m_memBuffer.empty()) return;

    if (m_diskFile.empty()) {
        m_diskFile = createTempFile();
    }

    m_diskStream.open(m_diskFile, std::ios::binary | std::ios::app);
    if (!m_diskStream) {
        throw std::runtime_error("Failed to open disk file for flush");
    }

    for (const auto& p : m_memBuffer) {
        m_diskStream.write(reinterpret_cast<const char*>(&p.address), sizeof(p.address));
        m_diskStream.write(reinterpret_cast<const char*>(&p.value), sizeof(p.value));
    }
    if (!m_diskStream) {
        throw std::runtime_error("Failed to flush data to disk");
    }
    m_diskStream.close();

    m_memBuffer.clear();
    m_memBuffer.shrink_to_fit();
    m_onDisk = true;
}

void DiskBackedResultSet::reset()
{
    // 调用者已持有 m_mutex
    m_memBuffer.clear();
    m_memBuffer.shrink_to_fit();
    if (m_diskStream.is_open()) {
        m_diskStream.close();
    }
    if (!m_diskFile.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_diskFile, ec);  // 忽略删除失败
        m_diskFile.clear();
    }
    m_count = 0;
    m_onDisk = false;
}

std::string DiskBackedResultSet::createTempFile()
{
    // 使用标准库生成唯一临时文件
    auto tmpDir = std::filesystem::temp_directory_path();
    std::string prefix = "CE_";
    // 简单生成唯一文件名 (使用时间和随机数)
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::string filename = prefix + std::to_string(now) + ".tmp";
    auto fullPath = tmpDir / filename;
    // 如果文件已存在（极小概率），追加随机数
    while (std::filesystem::exists(fullPath)) {
        filename = prefix + std::to_string(now) + "_" + std::to_string(rand()) + ".tmp";
        fullPath = tmpDir / filename;
    }
    return fullPath.string();
}