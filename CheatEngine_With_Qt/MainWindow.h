#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QTableView>
#include <QProgressBar>
#include <QLabel>
#include <atomic>
#include <memory>
#include "scan_engine.h"

// UI 文件生成的类
namespace Ui {
    class CheatEngine_With_QtClass;
}

class ScanResultModel;
class AddressListModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    // ========== UI 初始化相关 ==========
    void setupUi();                       // 加载ui文件并微调
    void initModels();                    // 创建数据模型
    void initViews();                     // 初始化所有自定义视图
    void setupScanResultView();           // 配置扫描结果表格
    void replaceAddressTable();           // 替换地址列表QTableWidget为QTableView
    void initTimers();                    // 创建并启动所有定时器
    void connectSignals();                // 连接UI信号到槽函数
    void updateScanTypeUi(int index);     // 根据扫描类型更新UI控件状态
    void applyDefaultState();             // 设置控件初始禁用/隐藏状态

    // ========== 业务逻辑 ==========
    void refreshScanResults();            // 定时增量刷新扫描结果
    void updateScanProgress();            // 更新进度条与状态栏

    // ========== 成员变量 ==========
    // UI 对象
    std::unique_ptr<Ui::CheatEngine_With_QtClass> ui;

    // 模型和引擎
    ScanResultModel* scanModel = nullptr;
    AddressListModel* addressModel = nullptr;
    std::unique_ptr<ScanEngine> m_scanEngine;

    // 视图（手动嵌入 UI）
    QTableView* scanResultView = nullptr;
    QTableView* addressView = nullptr;

    // 定时器
    QTimer* readTimer = nullptr;          // 刷新扫描值
    QTimer* progressTimer = nullptr;      // 扫描进度条更新
    QTimer* freezeTimer = nullptr;        // 地址冻结

    // 刷新控制
    std::atomic<bool> m_refreshing{ false };
    int m_lastRefreshGen = -1;

private slots:
    // ========== UI 交互槽 ==========
    void onOpenProcess();
    void onFirstScan();
    void onNextScan();
    void onDoubleClickScanResult(const QModelIndex& index);
};