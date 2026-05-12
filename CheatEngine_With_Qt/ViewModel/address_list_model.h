#pragma once
#include <QAbstractTableModel>
#include <vector>
#include "type_define\address_item.h"

class AddressListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /// 列索引
    enum Column {
        ColFrozen = 0,      // 冻结列（CheckBox）
        ColDescription,
        ColAddress,
        ColValue,
        ColType,
        ColHex,             // Hex 开关列（CheckBox 形式，单击切换）
        ColSigned,          // 有符号/无符号开关列（CheckBox 形式，单击切换，仅整数类型）
        ColLength,          // 长度列（字符串/字节数组的长度）
        ColumnCount_
    };

    explicit AddressListModel(QObject* parent = nullptr);

    // QAbstractTableModel 接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // ---- 地址操作 ----

    /// 添加单个地址
    void addItem(uint64_t address, const QString& description,
                 uint64_t rawValue, ValueType type = ValueType::Int32);

    /// 批量添加地址（一次性发出 beginInsertRows / endInsertRows）
    void addItems(const std::vector<AddressItem>& newItems);

    /// 批量从扫描结果添加（自动读取当前值）
    void addItemsFromScanResults(const std::vector<uint64_t>& addresses,
                                 const std::vector<std::string>& addressTexts,
                                 ScanDataType scanDataType);

    void removeItem(int row);
    void clear();

    /// 更新所有条目的当前值（由定时器调用）
    void refreshValues(std::shared_ptr<class IMemoryAccessor> mem);

    // 访问内部数据
    std::vector<AddressItem>& items() { return m_items; }
    const std::vector<AddressItem>& items() const { return m_items; }

private:
    std::vector<AddressItem> m_items;
};
