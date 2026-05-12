#include "ViewModel\address_list_model.h"
#include "process\process_manager.h"
#include "interface\imemory_accessor.h"

#include <QBrush>
#include <QColor>
#include <algorithm>
#include <cstring>

// 快捷整行刷新：发射整行的 dataChanged，包含所有角色
static void emitRowChanged(QAbstractItemModel* model, int row)
{
    QModelIndex topLeft = model->index(row, 0);
    QModelIndex bottomRight = model->index(row, AddressListModel::ColumnCount_ - 1);
    emit model->dataChanged(topLeft, bottomRight);
}

// 从内存重读某一行的值（根据当前 type/stringLength）
static void refreshItemValue(AddressItem& item, std::shared_ptr<IMemoryAccessor> mem)
{
    if (!mem) return;
    if (isStringValueType(item.type) || isByteArrayValueType(item.type)) {
        int readLen = (item.stringLength > 0) ? item.stringLength :
                      static_cast<int>(valueTypeSize(item.type));
        item.buffer.resize(readLen);
        mem->read(item.address, item.buffer.data(), readLen);
    } else {
        size_t size = valueTypeSize(item.type);
        mem->read(item.address, &item.rawValue, size);
    }
    item.changed = false;
}

// 读取字符串后，在空字节/空字处截断，更新 buffer 和 stringLength
static void truncateStringBuffer(AddressItem& item, int readLen)
{
    if (item.type != ValueType::String) return;

    int realLen = 0;
    if (item.encoding == StringEncoding::UTF16) {
        // UTF-16LE: 查找双字节空字
        const char16_t* u16 = reinterpret_cast<const char16_t*>(item.buffer.data());
        int u16len = readLen / 2;
        while (realLen < u16len && u16[realLen] != 0)
            ++realLen;
        realLen *= 2; // 转为字节数
    } else {
        // ASCII / UTF-8: 查找单字节空
        const char* data = reinterpret_cast<const char*>(item.buffer.data());
        while (realLen < readLen && data[realLen] != '\0')
            ++realLen;
    }

    if (realLen < static_cast<int>(item.buffer.size())) {
        item.buffer.resize(realLen);
        item.stringLength = realLen;
    }
}

AddressListModel::AddressListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int AddressListModel::rowCount(const QModelIndex&) const
{
    return m_items.size();
}

int AddressListModel::columnCount(const QModelIndex&) const
{
    return ColumnCount_;
}

QVariant AddressListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};

    const auto& item = m_items[index.row()];

    // ---- 显示文本 ----
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFrozen:
            return {};
        case ColDescription:
            return item.description;
        case ColAddress: {
            std::string display;
            bool isBase = false;
            ProcessManager::instance().resolveAddress(item.address, display, isBase);
            return QString::fromStdString(display);
        }
        case ColValue:
            return item.formattedValue();
        case ColType: {
            switch (item.type) {
            case ValueType::Int8:      return "Byte";
            case ValueType::Int16:     return "2 Bytes";
            case ValueType::Int32:     return "4 Bytes";
            case ValueType::Int64:     return "8 Bytes";
            case ValueType::Float:     return "Float";
            case ValueType::Double:    return "Double";
            case ValueType::String:    return "String";
            case ValueType::ByteArray: return "Byte Array";
            default:                   return "Int";
            }
        }
        case ColHex: {
            // Hex 列：显示当前 Hex 状态
            if (isNumericType(item.type)) {
                return item.hexDisplay ? QString("Hex") : QString("Dec");
            }
            if (isStringValueType(item.type)) {
                switch (item.encoding) {
                case StringEncoding::ASCII: return "ASCII";
                case StringEncoding::UTF8:  return "UTF-8";
                case StringEncoding::UTF16: return "UTF-16";
                }
            }
            if (isByteArrayValueType(item.type)) {
                return item.hexDisplay ? QString("Hex") : QString("Dec");
            }
            return {};
        }
        case ColSigned: {
            // Signed 列：仅整数类型显示 Signed/Unsigned
            if (isIntegerType(item.type)) {
                return item.signedDisplay ? QString("Signed") : QString("Unsigned");
            }
            // 非整数类型显示 "-"
            return QString("-");
        }
        case ColLength: {
            if (isNumericType(item.type)) {
                return QString::number(valueTypeSize(item.type)) + " bytes";
            }
            if (isStringValueType(item.type) || isByteArrayValueType(item.type)) {
                if (item.stringLength > 0)
                    return QString::number(item.stringLength) + " bytes";
                return QString::number(item.buffer.size()) + " bytes";
            }
            return {};
        }
        }
    }

    // ---- 工具提示 ----
    if (role == Qt::ToolTipRole) {
        if (index.column() == ColAddress) {
            return QString("0x%1").arg(item.address, 16, 16, QChar('0'));
        }
        if (index.column() == ColHex) {
            if (isNumericType(item.type)) {
                return item.hexDisplay ? tr("单击切换为十进制显示") : tr("单击切换为16进制显示");
            }
            if (isStringValueType(item.type)) {
                return tr("单击切换字符串编码");
            }
            if (isByteArrayValueType(item.type)) {
                return item.hexDisplay ? tr("单击切换为十进制显示") : tr("单击切换为16进制显示");
            }
        }
        if (index.column() == ColSigned) {
            if (isIntegerType(item.type)) {
                return item.signedDisplay ? tr("单击切换为无符号显示") : tr("单击切换为有符号显示");
            }
            return tr("仅整数类型支持有符号/无符号切换");
        }
        if (index.column() == ColLength) {
            if (isNumericType(item.type)) {
                return tr("数据类型大小: %1 字节").arg(valueTypeSize(item.type));
            }
            if (isStringValueType(item.type) || isByteArrayValueType(item.type)) {
                return tr("数据长度: %1 字节（可编辑）").arg(
                    item.stringLength > 0 ? item.stringLength : static_cast<int>(item.buffer.size()));
            }
        }
    }

    // ---- 对齐 ----
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColValue || index.column() == ColAddress)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        if (index.column() == ColLength)
            return QVariant(Qt::AlignCenter);
        if (index.column() == ColHex || index.column() == ColSigned)
            return QVariant(Qt::AlignCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    }

    // ---- 前景色 ----
    if (role == Qt::ForegroundRole) {
        // 基址地址用绿色
        if (index.column() == ColAddress) {
            std::string display;
            bool isBase = false;
            ProcessManager::instance().resolveAddress(item.address, display, isBase);
            if (isBase)
                return QBrush(QColor(0, 128, 0));
        }
        // 变化的值标红
        if (index.column() == ColValue && item.changed)
            return QBrush(Qt::red);
    }

    // ---- 复选框状态 ----
    if (role == Qt::CheckStateRole) {
        if (index.column() == ColFrozen)
            return item.frozen ? Qt::Checked : Qt::Unchecked;

        // Hex 列：数值类型和字节数组类型显示 CheckBox
        if (index.column() == ColHex) {
            if (isNumericType(item.type) || isByteArrayValueType(item.type))
                return item.hexDisplay ? Qt::Checked : Qt::Unchecked;
            // 字符串类型不显示 CheckBox，用 DisplayRole 显示编码名
            return {};
        }

        // Signed 列：仅整数类型显示 CheckBox
        if (index.column() == ColSigned) {
            if (isIntegerType(item.type))
                return item.signedDisplay ? Qt::Checked : Qt::Unchecked;
            return {};
        }
    }

    return {};
}

QVariant AddressListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case ColFrozen:      return "Frozen";
    case ColDescription: return "Description";
    case ColAddress:     return "Address";
    case ColValue:       return "Value";
    case ColType:        return "Type";
    case ColHex:         return "Hex";
    case ColSigned:      return "Signed";
    case ColLength:      return "Length";
    }
    return {};
}

bool AddressListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() >= m_items.size())
        return false;

    auto& item = m_items[index.row()];
    int row = index.row();

    // ---- CheckBox 切换 ----
    if (role == Qt::CheckStateRole) {
        bool checked = (value.toInt() == Qt::Checked);

        if (index.column() == ColFrozen) {
            item.frozen = checked;
            emitRowChanged(this, row);
            return true;
        }

        if (index.column() == ColHex) {
            if (isNumericType(item.type) || isByteArrayValueType(item.type)) {
                item.hexDisplay = checked;
                // 切换 Hex/Dec 后立即重读内存刷新 Value
                auto mem = ProcessManager::instance().memory();
                if (mem) refreshItemValue(item, mem);
                emitRowChanged(this, row);
                return true;
            }
            return false;
        }

        if (index.column() == ColSigned) {
            if (isIntegerType(item.type)) {
                item.signedDisplay = checked;
                auto mem = ProcessManager::instance().memory();
                if (mem) refreshItemValue(item, mem);
                emitRowChanged(this, row);
                return true;
            }
            return false;
        }

        return false;
    }

    // ---- 文本编辑 ----
    if (role == Qt::EditRole) {
        if (index.column() == ColDescription) {
            item.description = value.toString();
            emitRowChanged(this, row);
            return true;
        }

        if (index.column() == ColType) {
            ValueType newType = static_cast<ValueType>(value.toInt());
            if (newType != item.type) {
                item.type = newType;
                item.rawValue = 0;
                item.buffer.clear();
                item.stringLength = 0;

                // 类型变化后立即从内存重读
                auto mem = ProcessManager::instance().memory();
                if (mem) refreshItemValue(item, mem);

                emitRowChanged(this, row);
            }
            return true;
        }

        if (index.column() == ColValue) {
            QString text = value.toString().trimmed();
            if (text.isEmpty()) return false;

            auto mem = ProcessManager::instance().memory();
            if (!mem) return false;

            if (isStringValueType(item.type)) {
                QByteArray encoded;
                switch (item.encoding) {
                case StringEncoding::UTF16:
                    encoded = QByteArray(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
                    break;
                case StringEncoding::UTF8:
                    encoded = text.toUtf8();
                    break;
                case StringEncoding::ASCII:
                default:
                    encoded = text.toLatin1();
                    break;
                }
                if (!encoded.isEmpty()) {
                    mem->write(item.address, encoded.constData(), encoded.size());
                    item.buffer.resize(encoded.size());
                    mem->read(item.address, item.buffer.data(), encoded.size());
                }
                item.changed = false;
                emitRowChanged(this, row);
                return true;
            }

            if (isByteArrayValueType(item.type)) {
                QStringList byteTokens = text.split(' ', Qt::SkipEmptyParts);
                std::vector<uint8_t> newBuffer;
                newBuffer.reserve(byteTokens.size());
                bool ok = true;
                for (const QString& byteStr : byteTokens) {
                    uint val = byteStr.toUInt(&ok, item.hexDisplay ? 16 : 10);
                    if (!ok || val > 0xFF) { ok = false; break; }
                    newBuffer.push_back(static_cast<uint8_t>(val));
                }
                if (!ok || newBuffer.empty()) return false;
                mem->write(item.address, newBuffer.data(), newBuffer.size());
                item.buffer.resize(newBuffer.size());
                mem->read(item.address, item.buffer.data(), newBuffer.size());
                item.changed = false;
                emitRowChanged(this, row);
                return true;
            }

            // 数值类型编辑
            bool ok = false;
            uint64_t newRaw = 0;
            int base = item.hexDisplay ? 16 : 0;

            switch (item.type) {
            case ValueType::Int8: {
                int v = text.toInt(&ok, base);
                if (ok) { newRaw = static_cast<uint8_t>(static_cast<int8_t>(v)); mem->write(item.address, &newRaw, 1); }
                break;
            }
            case ValueType::Int16: {
                int v = text.toInt(&ok, base);
                if (ok) { newRaw = static_cast<uint16_t>(static_cast<int16_t>(v)); mem->write(item.address, &newRaw, 2); }
                break;
            }
            case ValueType::Int32: {
                uint v = text.toUInt(&ok, base);
                if (ok) { newRaw = v; mem->write(item.address, &newRaw, 4); }
                break;
            }
            case ValueType::Int64: {
                uint64_t v = text.toULongLong(&ok, base);
                if (ok) { newRaw = v; mem->write(item.address, &newRaw, 8); }
                break;
            }
            case ValueType::Float: {
                float f = text.toFloat(&ok);
                if (ok) { std::memcpy(&newRaw, &f, sizeof(f)); mem->write(item.address, &newRaw, 4); }
                break;
            }
            case ValueType::Double: {
                double d = text.toDouble(&ok);
                if (ok) { std::memcpy(&newRaw, &d, sizeof(d)); mem->write(item.address, &newRaw, 8); }
                break;
            }
            default: {
                uint v = text.toUInt(&ok, base);
                if (ok) { newRaw = v; mem->write(item.address, &newRaw, 4); }
                break;
            }
            }
            if (!ok) return false;

            size_t size = valueTypeSize(item.type);
            mem->read(item.address, &item.rawValue, size);
            item.changed = false;
            emitRowChanged(this, row);
            return true;
        }

        if (index.column() == ColHex) {
            // 字符串类型：解析 ComboBox 传来的编码数据
            if (isStringValueType(item.type)) {
                QString data = value.toString();
                QStringList parts = data.split(',');
                for (const QString& part : parts) {
                    QStringList kv = part.split(':');
                    if (kv.size() == 2 && kv[0] == "encoding") {
                        item.encoding = static_cast<StringEncoding>(kv[1].toInt());
                    }
                }
                emitRowChanged(this, row);
                return true;
            }
            return false;
        }

        if (index.column() == ColLength) {
            if (!isStringValueType(item.type) && !isByteArrayValueType(item.type))
                return false;
            bool ok = false;
            int newLen = value.toInt(&ok);
            if (!ok || newLen <= 0 || newLen > 4096) return false;
            item.stringLength = newLen;

            auto mem = ProcessManager::instance().memory();
            if (mem) {
                item.buffer.resize(newLen);
                mem->read(item.address, item.buffer.data(), newLen);
            }
            emitRowChanged(this, row);
            return true;
        }
    }

    return false;
}

Qt::ItemFlags AddressListModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = QAbstractTableModel::flags(index);

    switch (index.column()) {
    case ColFrozen:
        flags |= Qt::ItemIsUserCheckable;
        break;
    case ColDescription:
        flags |= Qt::ItemIsEditable;
        break;
    case ColType:
        flags |= Qt::ItemIsEditable;
        break;
    case ColValue:
        flags |= Qt::ItemIsEditable;
        break;
    case ColHex:
        // 数值类型和字节数组类型：CheckBox 形式
        if (index.row() < m_items.size()) {
            const auto& item = m_items[index.row()];
            if (isNumericType(item.type) || isByteArrayValueType(item.type))
                flags |= Qt::ItemIsUserCheckable;
            else if (isStringValueType(item.type))
                flags |= Qt::ItemIsEditable; // 字符串编码用单击编辑循环切换
        }
        break;
    case ColSigned:
        // Signed 列仅对整数类型显示 CheckBox
        if (index.row() < m_items.size()) {
            const auto& item = m_items[index.row()];
            if (isIntegerType(item.type))
                flags |= Qt::ItemIsUserCheckable;
        }
        break;
    case ColLength:
        // Length 列仅对字符串/字节数组可编辑
        if (index.row() < m_items.size()) {
            const auto& item = m_items[index.row()];
            if (isStringValueType(item.type) || isByteArrayValueType(item.type))
                flags |= Qt::ItemIsEditable;
        }
        break;
    }

    return flags;
}

// ==================== 地址操作 ====================

void AddressListModel::addItem(uint64_t address, const QString& description,
                               uint64_t rawValue, ValueType type)
{
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    AddressItem item;
    item.description = description;
    item.address = address;
    item.rawValue = rawValue;
    item.type = type;
    m_items.push_back(item);
    endInsertRows();
}

void AddressListModel::addItems(const std::vector<AddressItem>& newItems)
{
    if (newItems.empty()) return;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + newItems.size() - 1);
    m_items.insert(m_items.end(), newItems.begin(), newItems.end());
    endInsertRows();
}

void AddressListModel::addItemsFromScanResults(
    const std::vector<uint64_t>& addresses,
    const std::vector<std::string>& addressTexts,
    ScanDataType scanDataType)
{
    if (addresses.empty()) return;

    std::vector<AddressItem> newItems;
    newItems.reserve(addresses.size());

    auto mem = ProcessManager::instance().memory();
    ValueType vt = scanDataTypeToValueType(scanDataType);
    size_t readSize = valueTypeSize(vt);

    for (int i = 0; i < addresses.size(); ++i) {
        uint64_t addr = addresses[i];
        AddressItem item;
        item.address = addr;
        item.type = vt;

            if (isStringValueType(vt) || isByteArrayValueType(vt)) {
                // 根据扫描时的编码类型设置正确编码
                if (isStringValueType(vt)) {
                    switch (scanDataType) {
                    case ScanDataType::Utf16String:
                        item.encoding = StringEncoding::UTF16;
                        break;
                    case ScanDataType::Utf8String:
                        item.encoding = StringEncoding::UTF8;
                        break;
                    default:
                        item.encoding = StringEncoding::ASCII;
                        break;
                    }
                }

                // 对字符串类型：先读取256字节以确定真实长度，最多截断到32字节
                int probeLen = isStringValueType(vt) ? 256 : static_cast<int>(readSize);
                item.buffer.resize(probeLen);
                if (mem) {
                    mem->read(addr, item.buffer.data(), probeLen);
                }
                if (isStringValueType(vt)) {
                    // 查找空终止符位置，确定真实字符串长度
                    int realLen = 0;
                    if (item.encoding == StringEncoding::UTF16) {
                        const char16_t* u16 = reinterpret_cast<const char16_t*>(item.buffer.data());
                        int u16len = probeLen / 2;
                        while (realLen < u16len && u16[realLen] != 0)
                            ++realLen;
                        realLen = std::min(realLen * 2, 32); // 最大32字节
                    } else {
                        const char* data = reinterpret_cast<const char*>(item.buffer.data());
                        while (realLen < probeLen && data[realLen] != '\0')
                            ++realLen;
                        realLen = std::min(realLen, 32); // 最大32字节
                    }
                    if (realLen <= 0) realLen = 1;
                    item.buffer.resize(realLen);
                    item.stringLength = realLen;
            } else {
                // 字节数组：按默认读取大小，默认16进制显示
                item.buffer.resize(readSize);
                item.stringLength = static_cast<int>(readSize);
                item.hexDisplay = true;
            }
        } else {
            if (mem)
                mem->read(addr, &item.rawValue, readSize);
        }

        if (i < static_cast<int>(addressTexts.size()) && !addressTexts[i].empty())
            item.description = QString::fromStdString(addressTexts[i]);
        else
            item.description = QString("0x%1").arg(addr, 0, 16);

        newItems.push_back(item);
    }

    addItems(newItems);
}

void AddressListModel::removeItem(int row)
{
    if (row < 0 || row >= m_items.size()) return;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.erase(m_items.begin() + row);
    endRemoveRows();
}

void AddressListModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

void AddressListModel::refreshValues(std::shared_ptr<IMemoryAccessor> mem)
{
    if (!mem || m_items.empty()) return;

    int firstChanged = -1;
    int lastChanged = -1;

    for (int i = 0; i < m_items.size(); ++i) {
        auto& item = m_items[i];

        if (isStringValueType(item.type) || isByteArrayValueType(item.type)) {
            int readLen = (item.stringLength > 0) ? item.stringLength : 32;
            std::vector<uint8_t> oldBuffer = item.buffer;
            item.buffer.resize(readLen);
            mem->read(item.address, item.buffer.data(), readLen);
            // mem->read 返回 bool，用 readLen 作为实际读取大小

            // 比较完整 buffer（formattedStringValue 显示时会自动在空字符处截断）
            bool bufferChanged = (oldBuffer != item.buffer);
            if (!item.frozen && bufferChanged) {
                item.changed = true;
                if (firstChanged == -1) firstChanged = i;
                lastChanged = i;
            } else if (item.changed && !bufferChanged) {
                item.changed = false;
                if (firstChanged == -1) firstChanged = i;
                lastChanged = i;
            }
        } else {
            uint64_t oldRaw = item.rawValue;
            size_t size = valueTypeSize(item.type);
            mem->read(item.address, &item.rawValue, size);

            if (!item.frozen && oldRaw != item.rawValue) {
                item.lastRawValue = item.changed ? item.lastRawValue : oldRaw;
                item.changed = true;
                if (firstChanged == -1) firstChanged = i;
                lastChanged = i;
            } else if (item.changed && oldRaw == item.rawValue) {
                item.changed = false;
                if (firstChanged == -1) firstChanged = i;
                lastChanged = i;
            }
        }
    }

    if (firstChanged != -1) {
        QModelIndex top = index(firstChanged, ColValue);
        QModelIndex bottom = index(lastChanged, ColValue);
        emit dataChanged(top, bottom, { Qt::DisplayRole, Qt::ForegroundRole });
    }
}
