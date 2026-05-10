#include "scan_snapshot_manager.h"
#include "process_manager.h"
#include "TempPathManager.h"
#include <filesystem>
#include <fstream>

std::unique_ptr<ScanSnapshot> ProcessSnapshotManager::createSnapshot(const std::vector<MemoryRegion>& regions) {
    // 1. 生成唯一的临时文件名
    std::string path = (std::filesystem::path(TempPathManager::getWorkDir()) /
        ("snapshot_" + std::to_string(GetTickCount64()) + ".bin")).string();

    std::ofstream outFile(path, std::ios::binary);
    if (!outFile) return nullptr;

    std::map<uint64_t, size_t> index;
    size_t currentFileOffset = 0;

    // 2. 获取当前的内存读取器 (解耦的关键)
    auto accessor = ProcessManager::instance().memory();
    if (!accessor) return nullptr;

    // 3. 遍历区域并写入文件
    for (const auto& reg : regions) {
        std::vector<uint8_t> buffer(reg.size);

        // 使用 IMemoryAccessor 读取内存
        // 假设您的 IMemoryAccessor 接口是 read(uint64_t addr, void* buf, size_t size)
        if (accessor->read(reg.base, buffer.data(), reg.size)) {
            outFile.write(reinterpret_cast<const char*>(buffer.data()), reg.size);

            // 记录索引：内存虚拟地址 -> 镜像文件中的偏移位置
            index[reg.base] = currentFileOffset;
            currentFileOffset += reg.size;
        }
    }

    outFile.close();

    // 4. 返回封装好的快照实体
    return std::make_unique<ScanSnapshot>(path, std::move(index));
}

void ProcessSnapshotManager::setFirstSnapshot(std::shared_ptr<ScanSnapshot> snapshot) {
    m_first = snapshot;
}

void ProcessSnapshotManager::setPreviousSnapshot(std::shared_ptr<ScanSnapshot> snapshot) {
    m_prev = snapshot;
}

std::shared_ptr<ScanSnapshot> ProcessSnapshotManager::getFirst() const { return m_first; }
std::shared_ptr<ScanSnapshot> ProcessSnapshotManager::getPrevious() const { return m_prev; }

void ProcessSnapshotManager::clear() {
    // 自动清理物理文件
    if (m_first) std::filesystem::remove(m_first->path());
    if (m_prev) std::filesystem::remove(m_prev->path());
    m_first.reset();
    m_prev.reset();
}