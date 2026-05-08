
//┌─────────────────────────────────────────────────────┐
//│                              上层(MainWindow / 其他)                      　　　　　　　　　　　　　　　 │
//│                                                                           　　　　　　　　　　　　　　　 │
//│                          只依赖 ScanService 和 ScanResultViewModel   　　　     　　　　　　　　　　　   │
//└───────────────────────┬─────────────────────────────┘
//     　　　　　　　　　　　　　　　　　　　　   │
//     　　　　　　　　　　　　　　　　　　　   　▼
//     　　　　　　　　　       ┌──────────────────┐
//     　　　　　　　　　 　    │   ScanService    　　　　　　　　　│(门面 + 线程调度)
//      　　　　　　　　 　   　│                  　　　　　　　　　│
//                       　     │ - startScan()   　　　　　　　　 　│
//      　　　　　　　　 　   　│ - cancel()       　　　　　　　　　│
//      　　　　　　　　 　   　│ - resultModel()  　　　　　　　　　│─── > ScanResultViewModel
//       　　　　　　　　     　│ - isScanning()   　　　　　　　　　│
//      　　　　　　　　 　   　└────────┬─────────┘
//                                                │ 内部持有
//                      ┌────────────┼────────────┐
//                      │                        │                        │
//                      ▼                        ▼                        ▼
//┌─────────────┐ ┌──────────────────┐ ┌─────────────────────┐
//│ ScanEngine  　　　　　 　│ │       ScanResultRepository　       │ │ ScanResultViewModel 　　　　　           │
//│             　　　　　 　│ │                　　　              │ │                     　　　　           　│
//│ 纯计算      　　　　 　　│ │ 　　      线程安全存储           　│ │ QAbstractTableModel 　　　             　│
//│ 返回结果    　　　　　 　│ │　　       快照、增量更新   　    　│ │ 格式化显示          　　               　│
//└─────────────┘ └──────────────────┘ └─────────────────────┘
//            │
//            ▼(仅使用)
//┌─────────────┐
//│ ProcessManager           │
//│ memory()                 │(进程抽象层，读取进程内存，静态工具)
//└─────────────┘
//            │
//            ▼(抽象接口使用)
//┌─────────────┐
//│ IMemoryAccessor          │
//│                          │
//│ read()                   │(提供内存访问结构接口，静态工具)
//└─────────────┘






#pragma once
#include <QObject>
#include <QTimer>
#include <atomic>
#include <memory>
#include "scan_request_result_type_define.h"
#include "scan_engine.h"

class ScanResultRepository;
class ScanResultViewModel;

class ScanService : public QObject {
	Q_OBJECT
public:
	explicit ScanService(QObject* parent = nullptr);
	~ScanService() override;

	// 扫描控制
	void startScan(const ScanRequest& request);
	void cancel();

	// 状态与结果
	bool isScanning() const;
	bool hasResults() const;
	int  totalResults() const;
	ScanResultViewModel* resultModel() const;

	// 自动刷新（现在仅负责触发 UI 重绘）
	void startAutoRefresh(int intervalMs = 200);
	void stopAutoRefresh();

	void clear();
	void reset();

signals:
	void scanCompleted();
	void progressChanged(int completed, int total);

private:
	void onScanFinished(ScanEngine::ResultPack pack, ScanMode mode);

	std::unique_ptr<ScanEngine>            m_engine;
	std::unique_ptr<ScanResultRepository>  m_repository;
	ScanResultViewModel* m_viewModel;

	QTimer* m_refreshTimer;
	QTimer* m_progressTimer = nullptr;

	std::atomic<bool> m_scanning{ false };
	std::atomic<bool> m_cancelling{ false };

	// 记录首次扫描的快照路径和索引
	std::string m_firstPath;
	std::map<uint64_t, size_t> m_firstIndex;
};