#pragma once
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include "scan_result.h"
#include "scan_types.h"
#include "disk_backed_result_set.h"

class ScanEngine
{
public:
    void firstScan(ScanType type, uint64_t value, uint64_t value2 = 0,
        uint64_t moduleBase = 0, uint64_t moduleSize = 0);

    void nextScan(NextScanType type, uint64_t value = 0);

    std::vector<ScanResult> results() const;   // 仅用于 UI 显示（限制行数）
    size_t totalResults() const;

    bool isScanning() const { return m_scanning.load(); }
    int regionsCompleted() const { return m_regionsCompleted.load(); }
    int totalRegions() const { return m_totalRegions; }
    
    void cancel();                     // 请求取消当前扫描
    bool isCancelled() const { return m_cancel.load(); }

private:
    DiskBackedResultSet m_resultSet;          // 完整结果集（内存+磁盘）
    std::vector<uint64_t> m_lastValues;       // 仅用于内存模式，磁盘模式不使用（nextScan 会重建）
    std::atomic<bool> m_scanning{ false };
    mutable std::mutex m_resultMutex;
    std::atomic<int> m_regionsCompleted{ 0 };
    int m_totalRegions = 0;

    std::atomic<bool> m_cancel{ false };  // 取消标志
};