#include "scan_engine.h"
#include "thread_pool.h"
#include "process_manager.h"
#include <future>
#include <cstring>
#include <fstream>

static uint64_t readMemory(uint64_t addr)
{
	uint64_t value = 0;
	auto mem = ProcessManager::instance().memory();
	if (mem)
		mem->read(addr, &value, sizeof(value));
	return value;
}

void ScanEngine::firstScan(ScanType type, uint64_t value, uint64_t value2,
	uint64_t moduleBase, uint64_t moduleSize)
{
	if (m_scanning.exchange(true))
		return;

	m_cancel.store(false);

	auto mem = ProcessManager::instance().memory();
	if (!mem) { m_scanning = false; return; }

	auto regionEnum = ProcessManager::instance().regionEnumerator();
	if (!regionEnum) { m_scanning = false; return; }
	auto regions = regionEnum->enumerate();


	// 若指定了模块范围，则只保留该区域
	if (moduleSize > 0 && moduleBase != 0) {
		regions.clear();
		regions.push_back({ moduleBase, moduleSize });
	}

	if (regions.empty()) { m_scanning = false; return; }

	m_totalRegions = static_cast<int>(regions.size());
	m_regionsCompleted = 0;

	// 清空旧结果
	{
		std::lock_guard<std::mutex> lock(m_resultMutex);
		m_resultSet.clear();
		m_lastValues.clear();
	}

	// 使用局部收集器，避免频繁锁
	DiskBackedResultSet localSet;

	std::vector<std::future<std::vector<std::pair<uint64_t, uint64_t>>>> futures;

	for (const auto& region : regions) {
		futures.push_back(GlobalThreadPool::instance().enqueue([this, region, type, value, value2]() -> std::vector<std::pair<uint64_t, uint64_t>> {

			if (m_cancel.load()) return {};

			std::vector<std::pair<uint64_t, uint64_t>> local;
			const size_t step = sizeof(uint64_t);
			std::vector<uint8_t> buffer(4096);

			for (uint64_t addr = region.base;
				addr < region.base + region.size;
				addr += buffer.size()) {

				// 每处理一块前检查取消
				if (m_cancel.load()) break;

				size_t readSize = std::min(buffer.size(),
					region.base + region.size - addr);
				auto mem = ProcessManager::instance().memory();
				if (!mem->read(addr, buffer.data(), readSize))
					continue;

				for (size_t i = 0; i + step <= readSize; i += step) {
					uint64_t val = *reinterpret_cast<uint64_t*>(buffer.data() + i);
					uint64_t realAddr = addr + i;

					bool shouldAdd = false;
					switch (type) {
					case ScanType::ExactValue:
						shouldAdd = (val == value);
						break;
					case ScanType::GreaterThan:
						shouldAdd = (val > value);
						break;
					case ScanType::LessThan:
						shouldAdd = (val < value);
						break;
					case ScanType::Between:
						shouldAdd = (val >= value && val <= value2);
						break;
					case ScanType::UnknownInitial:
						shouldAdd = true;
						break;
					}
					if (shouldAdd)
						local.push_back({ realAddr, val });

					// 频繁检查取消
					if (m_cancel.load() && local.size() > 1024) break;
				}
			}
			m_regionsCompleted.fetch_add(1, std::memory_order_relaxed);
			return local;
			}));
	}



	// 汇总：逐个线程结果添加到 localSet（线程安全）
	for (auto& f : futures) {
		if (m_cancel.load()) {
			// 取消后仍然需要获取 future（避免阻塞），但不处理结果
			f.wait();
		}
		else {
			auto part = f.get();
			for (auto& p : part) { localSet.add(p.first, p.second); }
		}
	}

	// 如果取消，直接丢弃 localSet 并返回
	if (m_cancel.load()) {
		m_scanning = false;
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_resultMutex);
		// 内存模式下缓存 lastValues
		if (!localSet.onDisk()) {
			auto packed = localSet.toVector();   // 返回 PackedScanResult 向量
			m_lastValues.reserve(packed.size());
			for (const auto& p : packed) {
				m_lastValues.push_back(p.value);
			}
		}
		else {
			m_lastValues.clear();
		}

		// 将完整结果移入引擎
		localSet.moveTo(m_resultSet);
	}



	m_scanning = false;
}

void ScanEngine::nextScan(NextScanType type, uint64_t value)
{
	if (m_scanning.exchange(true))
		return;
	m_cancel.store(false);


	if (m_resultSet.size() == 0) {
		m_scanning = false;
		return;
	}

	DiskBackedResultSet newSet;
	const size_t count = m_resultSet.size();
	const size_t threadCount = std::thread::hardware_concurrency();
	const size_t chunkSize = (count + threadCount - 1) / threadCount;

	std::vector<std::future<std::vector<std::pair<uint64_t, uint64_t>>>> futures;

	if (!m_resultSet.onDisk()) {
		// 内存模式：转换为 PackedScanResult 向量，并准备 lastValues
		auto allPacked = m_resultSet.toVector();
		if (m_lastValues.size() != allPacked.size()) {
			m_lastValues.resize(allPacked.size());
			for (size_t i = 0; i < allPacked.size(); ++i) {
				m_lastValues[i] = allPacked[i].value;
			}
		}

		auto snapshot = std::make_shared<const std::vector<PackedScanResult>>(std::move(allPacked));
		auto lastSnapshot = std::make_shared<const std::vector<uint64_t>>(m_lastValues);

		for (size_t i = 0; i < count; i += chunkSize) {
			futures.push_back(GlobalThreadPool::instance().enqueue(
				[this,i, chunkSize, count, snapshot, lastSnapshot, type, value]() {

					if (m_cancel.load()) return std::vector<std::pair<uint64_t, uint64_t>>{};

					std::vector<std::pair<uint64_t, uint64_t>> local;
					size_t end = std::min(i + chunkSize, count);
					for (size_t j = i; j < end; ++j) {

						if (m_cancel.load()) break;

						uint64_t addr = (*snapshot)[j].address;
						uint64_t oldVal = (*lastSnapshot)[j];
						uint64_t newVal = readMemory(addr);
						bool match = false;
						switch (type) {
						case NextScanType::Equal:       match = (newVal == value); break;
						case NextScanType::NotEqual:    match = (newVal != value); break;
						case NextScanType::Increased:   match = (newVal > oldVal); break;
						case NextScanType::Decreased:   match = (newVal < oldVal); break;
						case NextScanType::Changed:     match = (newVal != oldVal); break;
						case NextScanType::Unchanged:   match = (newVal == oldVal); break;
						}
						if (match)
							local.push_back({ addr, newVal });
					}
					return local;
				}));
		}
	}
	else {
		// 磁盘模式：每个线程使用 readChunk 读取自己负责的块
		std::string filePath = m_resultSet.diskFile();
		for (size_t i = 0; i < count; i += chunkSize) {
			futures.push_back(GlobalThreadPool::instance().enqueue(
				[i, chunkSize, count, filePath, type, value]() {
					std::vector<std::pair<uint64_t, uint64_t>> local;
					size_t end = std::min(i + chunkSize, count);
					// 直接读取磁盘块
					DiskBackedResultSet tempSet;  
					std::ifstream file(filePath, std::ios::binary);
					if (!file) return local;
					file.seekg(i * sizeof(PackedScanResult));
					for (size_t j = i; j < end; ++j) {
						PackedScanResult entry;
						if (!file.read(reinterpret_cast<char*>(&entry), sizeof(entry)))
							break;
						uint64_t oldVal = entry.value;
						uint64_t newVal = readMemory(entry.address);
						bool match = false;
						switch (type) {
						case NextScanType::Equal:       match = (newVal == value); break;
						case NextScanType::NotEqual:    match = (newVal != value); break;
						case NextScanType::Increased:   match = (newVal > oldVal); break;
						case NextScanType::Decreased:   match = (newVal < oldVal); break;
						case NextScanType::Changed:     match = (newVal != oldVal); break;
						case NextScanType::Unchanged:   match = (newVal == oldVal); break;
						}
						if (match)
							local.push_back({ entry.address, newVal });
					}
					return local;
				}));
		}
	}

	// 汇总新结果
	 // 汇总
	for (auto& f : futures) {
		if (m_cancel.load()) { f.wait(); }
		else {
			auto part = f.get();
			for (auto& p : part) newSet.add(p.first, p.second);
		}
	}

	// 更新 lastValues
	if (!newSet.onDisk()) {
		auto packed = newSet.toVector();
		m_lastValues.resize(packed.size());
		for (size_t i = 0; i < packed.size(); ++i) {
			m_lastValues[i] = packed[i].value;
		}
	}
	else {
		m_lastValues.clear();
	}

	if (m_cancel.load()) {
		m_scanning = false;
		return;
	}

	// 替换结果集
	newSet.moveTo(m_resultSet);
	m_scanning = false;
}

std::vector<ScanResult> ScanEngine::results() const
{
	std::lock_guard<std::mutex> lock(m_resultMutex);
	constexpr size_t MAX_UI_RESULTS = 50000;

	if (m_resultSet.onDisk()) {
		// 高效：只读取前 MAX_UI_RESULTS 条
		auto packedChunk = m_resultSet.readChunk(0, MAX_UI_RESULTS);
		std::vector<ScanResult> vec;
		vec.reserve(packedChunk.size());
		for (const auto& p : packedChunk) {
			vec.push_back({ p.address, p.value });
		}
		return vec;
	}
	else {
		// 内存模式：全部在内存中，直接转换
		auto packedAll = m_resultSet.toVector();
		std::vector<ScanResult> vec;
		vec.reserve(packedAll.size());
		for (const auto& p : packedAll) {
			vec.push_back({ p.address, p.value });
		}
		return vec;
	}
}

size_t ScanEngine::totalResults() const
{
	std::lock_guard<std::mutex> lock(m_resultMutex);
	return m_resultSet.size();
}

void ScanEngine::cancel()
{
	m_cancel.store(true);
}