// scan_service.cpp
#include "scan_service.h"
#include "scan_result_repository.h"
#include "scan_result_view_model.h"
#include "thread_pool.h"

ScanService::ScanService(QObject* parent)
	: QObject(parent)
	, m_engine(std::make_unique<ScanEngine>())
	, m_repository(std::make_unique<ScanResultRepository>())
	, m_refreshTimer(new QTimer(this))
{
	// ViewModel 现在通过仓库进行懒加载，不直接操作 Service
	m_viewModel = new ScanResultViewModel(m_repository.get(), this);

	// 自动刷新：现在只需要让视图重新拉取内存值即可，不需要后台计算
	connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
		m_viewModel->onRepositoryReplaced(); // 触发重绘
		});
}

ScanService::~ScanService() = default;

// ---------------- 公有接口 ----------------

void ScanService::startScan(const ScanRequest& request) {
	if (m_scanning.exchange(true)) return;

	stopAutoRefresh();
	m_cancelling = false;

	// 进度定时器 (逻辑保持原样)
	m_progressTimer = new QTimer(this);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		if (!m_scanning.load()) {
			m_progressTimer->stop();
			m_progressTimer->deleteLater();
			m_progressTimer = nullptr;
			return;
		}
		emit progressChanged(m_engine->progress(), m_engine->totalItems());
		});
	m_progressTimer->start(100);

	std::vector<ScanResult> currentResults;
	if (request.mode == ScanMode::Next) {
		currentResults = m_repository->getResults();
	}

	GlobalThreadPool::instance().enqueue([this, request,currentResults] {
		auto pack = m_engine->execute(request, currentResults);

		// 任务结束，切回 UI 线程同步仓库与 ViewModel
		QMetaObject::invokeMethod(this, [this, pack, mode = request.mode] {
			this->onScanFinished(pack, mode);
			}, Qt::QueuedConnection);
		});
}

void ScanService::cancel()
{
	m_cancelling = true;
	m_engine->cancel();
	if (m_scanning.load()) {
		// 确保进度定时器停止
		if (m_progressTimer) {
			m_progressTimer->stop();
			m_progressTimer->deleteLater();
			m_progressTimer = nullptr;
		}
		m_scanning = false;
	}
	stopAutoRefresh();
}

bool ScanService::isScanning() const
{
	return m_scanning.load();
}

bool ScanService::hasResults() const
{
	return m_repository->resultCount() > 0 ;
}

int ScanService::totalResults() const
{
	return static_cast<int>(m_repository->resultCount());
}

ScanResultViewModel* ScanService::resultModel() const
{
	return m_viewModel;
}

void ScanService::startAutoRefresh(int intervalMs)
{
	if (!m_scanning.load())
		m_refreshTimer->start(intervalMs);
}

void ScanService::stopAutoRefresh()
{
	m_refreshTimer->stop();
}

void ScanService::clear()
{
	stopAutoRefresh();
	m_repository->replaceAllResults({});
	m_viewModel->onRepositoryReplaced();
}

void ScanService::reset()
{
	clear();
	m_engine->cancel();
	m_scanning = false;
	// 如果正在扫描，等待线程结束？简单处理：直接重置标志
}


void ScanService::onScanFinished(ScanEngine::ResultPack pack, ScanMode mode) {
	if (m_progressTimer) { m_progressTimer->stop(); m_progressTimer->deleteLater(); }

	// 1. 结果分发：将自适应缓存中的数据存入仓库
	if (pack.results) {
		m_repository->replaceAllResults(pack.results->readChunk(0, pack.results->total_size()));
	}

	// 2. 快照同步：同步最新的快照对象，供 ViewModel 懒加载显示
	m_repository->setSnapshots(m_engine->getFirstSnapshot(), m_engine->getPreviousSnapshot());

	m_viewModel->onRepositoryReplaced();
	m_scanning = false;
	emit scanCompleted();
}