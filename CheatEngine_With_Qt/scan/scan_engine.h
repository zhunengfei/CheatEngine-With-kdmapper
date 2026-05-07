#pragma once
#include "scan_request_result_type_define.h"
#include "adaptive_cache.h"
#include "scan_snapshot_manager.h"
#include "scan_simd_accelerate.h"
#include "thread_pool.h"
#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include <cctype>


class ScanEngine {
public:
    struct ResultPack {
        std::shared_ptr<AdaptiveCachePool<ScanResult>> results;
        ScanDataType dataType;
    };

    ScanEngine();
    ~ScanEngine() = default;

    // 唯一外部入口
    ResultPack execute(const ScanRequest& request, const std::vector<ScanResult>& prevResults);

    void cancel() { m_cancel.store(true, std::memory_order_release); }
    bool isCancelled() const { return m_cancel.load(std::memory_order_acquire); }
    int progress() const { return m_progress.load(std::memory_order_relaxed); }
    int totalItems() const { return m_totalItems.load(); }

    std::shared_ptr<ScanSnapshot> getFirstSnapshot() const { return m_snapshotMgr->getFirst(); }
    std::shared_ptr<ScanSnapshot> getPreviousSnapshot() const { return m_snapshotMgr->getPrevious(); }

private:
    static ThreadPool& getGlobalPool() {
        static ThreadPool pool(std::thread::hardware_concurrency());
        return pool;
    }

    template <typename T>
    void dispatchScan(const ScanRequest& req, const std::vector<ScanResult>& prevResults,
        std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

    template <typename T>
    void taskFirstScan(const ScanRequest& req, MemoryRegion region,
        std::shared_ptr<ScanSnapshot> current,
        std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

    template <typename T>
    void taskNextScan(const ScanRequest& req, const std::vector<ScanResult>& oldBatch,
        std::shared_ptr<ScanSnapshot> current,
        std::shared_ptr<ScanSnapshot> previous,
        std::shared_ptr<AdaptiveCachePool<ScanResult>> outCache);

    // 特殊类型匹配算法

    void performStringSearch(const std::vector<uint8_t>& buf, uint64_t base, const StringParams& p, ScanDataType type, std::vector<uint64_t>& matched);
    void performAobSearch(const std::vector<uint8_t>& buf, uint64_t base, const AobParams& p, std::vector<uint64_t>& matched);

    std::atomic<bool> m_cancel{ false };
    std::atomic<int>  m_progress{ 0 };
    std::atomic<int>  m_totalItems{ 0 };
    std::unique_ptr<SnapshotManager> m_snapshotMgr;
};