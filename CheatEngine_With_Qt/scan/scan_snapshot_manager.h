#pragma once
#include "scan_snapshot.h"
#include "process_manager.h"
#include "memory_region.h"
#include <memory>
#include <vector>


class SnapshotManager {
public:
    // 创建新快照并返回实体
    std::unique_ptr<ScanSnapshot> createSnapshot(const std::vector<MemoryRegion>& regions);

    // 管理快照状态切换（First, Previous, Current）
    void setFirstSnapshot(std::shared_ptr<ScanSnapshot> snapshot);
    void setPreviousSnapshot(std::shared_ptr<ScanSnapshot> snapshot);

    std::shared_ptr<ScanSnapshot> getFirst() const;
    std::shared_ptr<ScanSnapshot> getPrevious() const;

    void clear(); // 处理临时文件的清理

private:
    std::shared_ptr<ScanSnapshot> m_first;
    std::shared_ptr<ScanSnapshot> m_prev;
};