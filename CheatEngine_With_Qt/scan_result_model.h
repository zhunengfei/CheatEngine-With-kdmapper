#pragma once

#include <QAbstractTableModel>
#include <vector>
#include <cstdint>
#include <memory>
#include <atomic>
#include "scan_result.h"
#include <mutex>

constexpr int SCAN_RESULT_MODEL_MAX_DISPLAY_ROWS = 50000;

class ScanResultModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ScanResultModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setResults(const std::vector<ScanResult>& results);

    // 工具方法
    uint64_t getAddress(int row) const;
    std::vector<ScanResult> getResults() const;


    // 增量更新：只更新指定行的值和变化状态
    void applyDeltaUpdates(int generation,
        const std::vector<int>& rows,
        const std::vector<uint64_t>& newValues,
        const std::vector<uint8_t>& changedFlags);

    // 获取当前代数（用于快照校验）
    int generation() const { return m_generation.load(); }

    // 获取只读快照（后台线程使用）
    std::shared_ptr<const std::vector<ScanResult>> snapshot() const;

    // 返回真实总结果数（不受显示上限影响）
    int totalCount() const;

private:
    mutable std::mutex m_dataMutex;   // 保护 m_results
    std::vector<ScanResult> m_results;

    std::atomic<int> m_generation{ 0 }; // 结果集版本号

    // 快照缓存（由主线程在需要时更新）
    mutable std::mutex m_snapshotMutex;
    mutable std::shared_ptr<const std::vector<ScanResult>> m_snapshot;
    mutable std::atomic<int> m_snapshotGeneration{ 0 };
};