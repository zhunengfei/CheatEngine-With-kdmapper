#include "scan_result_model.h"
#include "process_manager.h"
#include<QBrush>


ScanResultModel::ScanResultModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int ScanResultModel::rowCount(const QModelIndex&) const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    // 视图最多显示前50000行
    return std::min(static_cast<int>(m_results.size()), SCAN_RESULT_MODEL_MAX_DISPLAY_ROWS);
}

int ScanResultModel::columnCount(const QModelIndex&) const
{
    return 2; // Address + Value
}

QVariant ScanResultModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};

    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (index.row() < 0 || index.row() >= std::min(static_cast<int>(m_results.size()), SCAN_RESULT_MODEL_MAX_DISPLAY_ROWS))
        return {};

    const auto& item = m_results[index.row()];

    // ----- 地址列 (column 0) -----
    if (index.column() == 0)
    {
        if (role == Qt::DisplayRole)
        {
            std::string display;
            bool isBase = false;
            ProcessManager::instance().resolveAddress(item.address, display, isBase);
            return QString::fromStdString(display);
        }
        if (role == Qt::ForegroundRole)
        {
            std::string display;
            bool isBase = false;
            ProcessManager::instance().resolveAddress(item.address, display, isBase);
            if (isBase)
                return QBrush(QColor(0, 128, 0));   // 深绿色：模块基址
            // 可选：如果地址不在任何模块中，显示灰色
            // if (!display.empty() && display[0] == '0' && display[1] == 'x' 
            //     && display.find('+') == std::string::npos)
            //     return QBrush(Qt::gray);
            return {};
        }
        return {};
    }

    // ----- 值列 (column 1) -----
    if (index.column() == 1)
    {
        if (role == Qt::DisplayRole)
            return QString::number(item.value);
        if (role == Qt::ForegroundRole && item.changed)
            return QBrush(Qt::red);
        return {};
    }

    return {};
}

QVariant ScanResultModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};

    if (section == 0) return "Address";
    if (section == 1) return "Value";

    return {};
}

void ScanResultModel::setResults(const std::vector<ScanResult>& results)
{
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_results = results;   // 唯一一次全量拷贝
    }
    m_generation.fetch_add(1, std::memory_order_release); // 代数 +1

    // 强制刷新整个模型（只有新扫描才全量重建视图）
    beginResetModel();
    endResetModel();
}

std::shared_ptr<const std::vector<ScanResult>> ScanResultModel::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    int currentGen = m_generation.load(std::memory_order_acquire);
    if (!m_snapshot || m_snapshotGeneration.load() != currentGen)
    {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        m_snapshot = std::make_shared<const std::vector<ScanResult>>(m_results);
        m_snapshotGeneration.store(currentGen, std::memory_order_release);
    }
    return m_snapshot;
}

int ScanResultModel::totalCount() const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return static_cast<int>(m_results.size());
}

void ScanResultModel::applyDeltaUpdates(int generation,
    const std::vector<int>& rows,
    const std::vector<uint64_t>& newValues,
    const std::vector<uint8_t>& changedFlags)
{
    // 代数不匹配说明结果集已被覆盖，丢弃本次更新
    if (generation != m_generation.load(std::memory_order_acquire))
        return;

    if (rows.empty())
        return;

    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            int row = rows[i];
            if (row < 0 || row >= static_cast<int>(m_results.size()))
                continue;

            auto& item = m_results[row];
            item.lastValue = item.value;
            item.value = newValues[i];
            item.changed = changedFlags[i];
        }
    }

    // 废除快照，防止死循环刷新
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshot.reset();
    }

    // 寻找刷新的上下界
    int minRow = rows.front();
    int maxRow = rows.back();

    // 安全钳制边界
    int maxAllowed = static_cast<int>(m_results.size()) - 1;
    maxAllowed = std::min(maxAllowed, SCAN_RESULT_MODEL_MAX_DISPLAY_ROWS - 1);
    maxRow = std::min(maxRow, maxAllowed);

    if (minRow <= maxRow)
    {
        // 【核心修复】：
        // 1. 我们限定只刷新 Column 1 (即 Value 列)。Address 列完全没变，没必要通知视图重绘！
        // 2. 不传递大括号 {Qt::DisplayRole}，使用 Qt 默认的空容器，彻底避开临时 QList 在高压下的析构崩溃。
        QModelIndex topLeft = index(minRow, 1);
        QModelIndex bottomRight = index(maxRow, 1);
        emit dataChanged(topLeft, bottomRight);
    }
}



uint64_t ScanResultModel::getAddress(int row) const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (row < 0 || row >= m_results.size())
        return 0;

    return m_results[row].address;
}

std::vector<ScanResult> ScanResultModel::getResults() const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_results;
}
