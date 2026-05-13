#include "process\process_memory_snapshot_manager.h"
#include "process\process_manager.h"
#include "utils\TempPathManager.h"
#include "Factory\process_memory_snapshot_factory.h" // 新增工厂头文件
#include <filesystem>
#include <fstream>

ProcessMemorySnapshotManager::ProcessMemorySnapshotManager(MemoryBackend backend)
	: m_backend(backend)
	, m_first(nullptr)
	, m_prev(nullptr)

{
}

std::shared_ptr<IProcessMemorySnapshot> ProcessMemorySnapshotManager::createSnapshot(const std::vector<MemoryRegion>& regions) {
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

	// 3. 遍历区域并写入文件（分块读取，避免大区域单次 ReadProcessMemory 失败）
	static constexpr size_t READ_CHUNK_SIZE = 64 * 1024; // 64KB 分块
	for (const auto& reg : regions) {
		uint64_t chunkBase = reg.base;
		size_t remaining = reg.size;
		bool regionHasAnyWrites = false;

		while (remaining > 0) {
			size_t toRead = (std::min)(READ_CHUNK_SIZE, remaining);
			std::vector<uint8_t> buffer(toRead);

			if (!regionHasAnyWrites) {
				// 该区域第一次写入前记录索引（无论首块是否成功，后续零占位也保持线性映射）
				index[reg.base] = currentFileOffset;
				regionHasAnyWrites = true;
			}

			if (accessor->read(chunkBase, buffer.data(), toRead)) {
				outFile.write(reinterpret_cast<const char*>(buffer.data()), toRead);
			} else {
				// 读取失败的分块：写入全零占位，保持地址-文件偏移的线性映射
				std::vector<uint8_t> zeros(toRead, 0);
				outFile.write(reinterpret_cast<const char*>(zeros.data()), toRead);
			}
			currentFileOffset += toRead;

			chunkBase += toRead;
			remaining -= toRead;
		}
	}

	outFile.close();

	// 4. 返回封装好的快照实体
	return ProcessMemorySnapshotFactory::create(m_backend, path, std::move(index));
}


void ProcessMemorySnapshotManager::clear() {
	// 处理第一个快照：先 reset 释放 Win32 句柄，再执行文件删除
	if (m_first) {
		std::string path = m_first->path();
		m_first.reset(); // 此时会触发 Win32ProcessMemorySnapshot 的析构，调用 CloseHandle

		std::error_code ec;
		std::filesystem::remove(path, ec); // 使用 error_code 版本避免异常抛出
	}

	// 处理上一个快照
	if (m_prev) {
		std::string path = m_prev->path();
		m_prev.reset();

		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
}