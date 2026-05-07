#pragma once
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <map>
#include <string>
#include "scan_request_result_type_define.h"
#include "scan_snapshot.h"

class ScanResultRepository {
public:
    /// 极速替换结果集：仅移动地址列表，无任何比对逻辑
    void replaceAllResults(std::vector<ScanResult>&& newResults);
    // ----- 数据查询 -----
    size_t resultCount() const;
    const ScanResult* resultAt(size_t index) const;
    uint64_t addressAtIndex(size_t index) const;

    void setSnapshots(std::shared_ptr<ScanSnapshot> first, std::shared_ptr<ScanSnapshot> prev);
    int currentGeneration() const;
    std::string getDisplayValue(uint64_t addr, int column, ScanDataType type) const;

    std::vector<ScanResult> getResults() const;
private:
    std::vector<ScanResult> m_data; // 此时每个元素仅 8 字节
    mutable std::mutex m_mutex;
    std::atomic<int> m_generation{ 0 };


    std::shared_ptr<ScanSnapshot> m_firstSnapshot;
    std::shared_ptr<ScanSnapshot> m_prevSnapshot;
};