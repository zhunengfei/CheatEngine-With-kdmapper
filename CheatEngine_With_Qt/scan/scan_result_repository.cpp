#include "scan_result_repository.h"
#include "scan_result_formatter.h"
#include "process_manager.h"
#include <algorithm>

void ScanResultRepository::replaceAllResults(std::vector<ScanResult>&& newResults) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data = std::move(newResults); 
    m_generation.fetch_add(1, std::memory_order_release);
}

void ScanResultRepository::setSnapshots(std::shared_ptr<ScanSnapshot> first, std::shared_ptr<ScanSnapshot> prev) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_firstSnapshot = first;
    m_prevSnapshot = prev;
}

// ---------------- 以下逻辑保持精简 ----------------

size_t ScanResultRepository::resultCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.size();
}

const ScanResult* ScanResultRepository::resultAt(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (index < m_data.size()) ? &m_data[index] : nullptr;
}

uint64_t ScanResultRepository::addressAtIndex(size_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (index < m_data.size()) ? m_data[index].address : 0;
}

int ScanResultRepository::currentGeneration() const
{
    return m_generation.load();
}

std::string ScanResultRepository::getDisplayValue(uint64_t addr, int column, ScanDataType type) const {
    uint64_t raw = 0;
    bool success = false;

    switch (column) {
    case 1: // 当前实时值
        success = ProcessManager::instance().memory()->read(addr, &raw, scanDataTypeSize(type));
        break;
    case 2: // 上次快照值
        if (m_prevSnapshot) success = m_prevSnapshot->readValue(addr, raw);
        break;
    case 3: // 首次快照值
        if (m_firstSnapshot) success = m_firstSnapshot->readValue(addr, raw);
        break;
    default: return "";
    }

    return success ? ScanResultFormatter::formatValue(raw, type) : "---";
}

std::vector<ScanResult> ScanResultRepository::getResults() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data; // 返回拷贝以确保线程安全
}
