#include "mainwindow.h"
#include "ui_CheatEngine_With_Qt.h"

#include "scan_result_model.h"
#include "address_list_model.h"
#include "thread_pool.h"
#include "process_dialog.h"
#include "process_manager.h"

#include <QTimer>
#include <QTableView>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QApplication>
#include <QStatusBar>

// ==================== 构造函数（精简版） ====================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::CheatEngine_With_QtClass>())
{
    setupUi();
    initModels();
    initViews();
    initTimers();
    connectSignals();
    applyDefaultState();
}

MainWindow::~MainWindow() = default;

// ==================== UI 基础设置 ====================
void MainWindow::setupUi()
{
    ui->setupUi(this);
}

// ==================== 模型初始化 ====================
void MainWindow::initModels()
{
    scanModel = new ScanResultModel(this);
    addressModel = new AddressListModel(this);
    m_scanEngine = std::make_unique<ScanEngine>();
}

// ==================== 视图初始化（嵌入自定义控件） ====================
void MainWindow::initViews()
{
    setupScanResultView();      // 扫描结果表格
    replaceAddressTable();      // 地址列表表格（替换QTableWidget）
}

void MainWindow::setupScanResultView()
{
    QWidget* firstTab = ui->tabWidget->widget(0);   // “查找1”
    QVBoxLayout* tabLayout = new QVBoxLayout(firstTab);
    tabLayout->setContentsMargins(0, 0, 0, 0);

    scanResultView = new QTableView(firstTab);
    scanResultView->setModel(scanModel);

    // 固定行高，提升性能
    scanResultView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    scanResultView->verticalHeader()->setDefaultSectionSize(24);

    // 标题栏设置
    scanResultView->horizontalHeader()->setStretchLastSection(false);

    // 列宽策略：第0列可手动调整，初始宽度150；第1列自动填充
    scanResultView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    scanResultView->setColumnWidth(0, 150);
    scanResultView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    // 选择行为
    scanResultView->setSelectionBehavior(QAbstractItemView::SelectRows);
    scanResultView->setSelectionMode(QAbstractItemView::SingleSelection);

    tabLayout->addWidget(scanResultView);
}

void MainWindow::replaceAddressTable()
{
    QLayout* addrLayout = ui->tableWidget_addressList->parentWidget()->layout();
    if (auto* vbox = qobject_cast<QVBoxLayout*>(addrLayout)) {
        vbox->removeWidget(ui->tableWidget_addressList);
        delete ui->tableWidget_addressList;   // 删除原QTableWidget

        addressView = new QTableView(this);
        addressView->setModel(addressModel);
        addressView->setSelectionBehavior(QAbstractItemView::SelectRows);
        addressView->setSelectionMode(QAbstractItemView::SingleSelection);
        vbox->addWidget(addressView);
    }
}

// ==================== 定时器初始化 ====================
void MainWindow::initTimers()
{
    // 扫描结果增量刷新定时器
    readTimer = new QTimer(this);
    connect(readTimer, &QTimer::timeout, this, &MainWindow::refreshScanResults);
    readTimer->start(200);

    // 扫描进度条更新定时器
    progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, this, &MainWindow::updateScanProgress);
    progressTimer->start(100);

    // 地址冻结定时器
    freezeTimer = new QTimer(this);
    connect(freezeTimer, &QTimer::timeout, this, [this]() {
        auto& items = addressModel->items();
        auto mem = ProcessManager::instance().memory();
        if (!mem) return;
        for (auto& item : items) {
            if (item.frozen) {
                mem->write(item.address, &item.value, sizeof(item.value));
            }
        }
        });
    freezeTimer->start(500);
}

// ==================== 信号连接（UI交互 -> 业务槽） ====================
void MainWindow::connectSignals()
{
    // 基本操作按钮
    connect(ui->pushButton_openProcess, &QPushButton::clicked, this, &MainWindow::onOpenProcess);
    connect(ui->pushButton_new_find, &QPushButton::clicked, this, &MainWindow::onFirstScan);
    connect(ui->pushButton_next_find, &QPushButton::clicked, this, &MainWindow::onNextScan);

    // 扫描结果双击 -> 添加到地址列表
    connect(scanResultView, &QTableView::doubleClicked, this, &MainWindow::onDoubleClickScanResult);

    // 地址列表删除全部
    connect(ui->pushButton_delete_all_address, &QPushButton::clicked, this, [this]() {
        addressModel->clear();
        });

    // 扫描类型改变时，动态控制UI控件的可见与使能
    connect(ui->comboBox_atribute_For_Find, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::updateScanTypeUi);

    // 立即以当前索引触发一次，保证初始状态正确
    updateScanTypeUi(ui->comboBox_atribute_For_Find->currentIndex());
}

// ==================== 扫描类型UI状态统一管理 ====================
void MainWindow::updateScanTypeUi(int index)
{
    // 索引3：介于两者之间
    bool isBetween = (index == 3);
    ui->label_and->setVisible(isBetween);
    ui->lineEdit_ValueInput2->setVisible(isBetween);

    // 索引4：未知初始值
    bool isUnknown = (index == 4);
    // 非未知初始值时，显示并启用 Hex、Value输入、数据大小选择
    ui->checkBox_Hex_Value->setVisible(!isUnknown);
    ui->lineEdit_ValueInput->setVisible(!isUnknown);
    ui->comboBox_Value_Data_Size->setVisible(!isUnknown);

    ui->checkBox_Hex_Value->setEnabled(!isUnknown);
    ui->lineEdit_ValueInput->setEnabled(!isUnknown);
    ui->comboBox_Value_Data_Size->setEnabled(!isUnknown);
}

// ==================== 默认状态设置 ====================
void MainWindow::applyDefaultState()
{
    // 初始禁止扫描按钮
    ui->pushButton_new_find->setEnabled(false);
    ui->pushButton_next_find->setEnabled(false);

    // 介于控件由 updateScanTypeUi 管理，但确保首次正确隐藏（已调用）
    // 无需额外操作
}


// ==================== 打开进程 ====================
void MainWindow::onOpenProcess()
{
    ProcessDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
    {
        auto p = dlg.selectedProcess();

        // 暂停刷新并等待当前任务结束
        readTimer->stop();
        freezeTimer->stop();  
        progressTimer->stop();

        // 请求取消当前扫描
        m_scanEngine->cancel();

        // ★ 等待扫描完成
        while (m_scanEngine->isScanning())
            QApplication::processEvents();

        while (m_refreshing.load())
            QApplication::processEvents();

        ProcessManager::instance().detach();

        if (ProcessManager::instance().attach(p))
        {
            // 清空数据
            scanModel->setResults({});
            m_scanEngine = std::make_unique<ScanEngine>();
            m_lastRefreshGen = -1;

            // 更新 UI 进程名
            ui->label_Process_name->setText(QString::fromStdString(p.name));
            setWindowTitle(QString("Cheat Engine - %1").arg(QString::fromStdString(p.name)));

            // ----- 填充模块列表 -----
            QComboBox* moduleBox = ui->comboBox_process_module_List;
            moduleBox->clear();
            moduleBox->addItem("All");   // 默认项
            const auto& modules = ProcessManager::instance().modules();
            for (const auto& mod : modules) {
                moduleBox->addItem(QString::fromStdString(mod.name));
            }


            // 启用扫描按钮
            ui->pushButton_new_find->setEnabled(true);
            ui->pushButton_next_find->setEnabled(false); // 刚打开进程时禁止 Next

            readTimer->start(200);
            progressTimer->start(100);
            freezeTimer->start(500);
        }
        else
        {
            QMessageBox::warning(this, "Error", "Failed to attach to process.");
            readTimer->start(200);
            progressTimer->start(100);
            freezeTimer->start(500);
        }
    }
}

// ==================== 首次扫描 ====================
void MainWindow::onFirstScan()
{
    if (!ProcessManager::instance().memory()) {
        QMessageBox::warning(this, "Error", "Please open a process first.");
        return;
    }
    if (m_scanEngine->isScanning())
        return;

    // 获取扫描类型
    int attrIndex = ui->comboBox_atribute_For_Find->currentIndex();
    ScanType scanType;
    switch (attrIndex) {
    case 0: scanType = ScanType::ExactValue; break;
    case 1: scanType = ScanType::GreaterThan; break;
    case 2: scanType = ScanType::LessThan; break;
    case 3: scanType = ScanType::Between; break;
    case 4: scanType = ScanType::UnknownInitial; break;
    default: scanType = ScanType::ExactValue; break;
    }

    bool isHex = ui->checkBox_Hex_Value->isChecked();
    uint64_t val = 0, val2 = 0;
    QString text = ui->lineEdit_ValueInput->text();
    bool ok = false;

    if (scanType != ScanType::UnknownInitial) {
        if (isHex)
            val = text.toULongLong(&ok, 16);
        else
            val = text.toULongLong(&ok, 10);
        if (!ok && !text.isEmpty()) {
            QMessageBox::warning(this, "Error", "Invalid value.");
            return;
        }
    }

    if (scanType == ScanType::Between) {
        QString text2 = ui->lineEdit_ValueInput2->text();
        bool ok2 = false;
        if (isHex)
            val2 = text2.toULongLong(&ok2, 16);
        else
            val2 = text2.toULongLong(&ok2, 10);
        if (!ok2) {
            QMessageBox::warning(this, "Error", "Invalid second value.");
            return;
        }
    }

    // 模块过滤（已有）
    QString modName = ui->comboBox_process_module_List->currentText();
    uint64_t modBase = 0, modSize = 0;
    if (modName != "All") {
        const ModuleInfo* mod = ProcessManager::instance().getModuleByName(modName.toStdString());
        if (mod) {
            modBase = mod->base;
            modSize = mod->size;
        }
        else {
            QMessageBox::warning(this, "Error", "Selected module not found.");
            return;
        }
    }

    // 禁用按钮，启动扫描
    ui->pushButton_new_find->setEnabled(false);
    ui->pushButton_next_find->setEnabled(false);

    GlobalThreadPool::instance().enqueue([this, scanType, val, val2, modBase, modSize]() {
        m_scanEngine->firstScan(scanType, val, val2, modBase, modSize);
        auto results = m_scanEngine->results();
        QMetaObject::invokeMethod(this, [this, results]() {
            scanModel->setResults(results);
            ui->pushButton_new_find->setEnabled(true);
            ui->pushButton_next_find->setEnabled(m_scanEngine->totalResults() > 0);
            });
        });
}

// ==================== 再次扫描 ====================
void MainWindow::onNextScan()
{
    if (!ProcessManager::instance().memory())
    {
        QMessageBox::warning(this, "Error", "No process attached.");
        return;
    }
    if (m_scanEngine->isScanning())
        return;

    // ★ 必须已经执行过首次扫描
    if (m_scanEngine->totalResults() == 0)
    {
        QMessageBox::warning(this, "Error", "Please perform a first scan first.");
        return;
    }

    // 此处可扩展为根据 UI 选择不同 NextScanType，目前固定为 Increased 示例
    NextScanType nextType = NextScanType::Increased;

    ui->pushButton_new_find->setEnabled(false);
    ui->pushButton_next_find->setEnabled(false);

    GlobalThreadPool::instance().enqueue([this, nextType]()
        {
            m_scanEngine->nextScan(nextType);
            auto results = m_scanEngine->results();

            QMetaObject::invokeMethod(this, [this, results]()
                {
                    scanModel->setResults(results);
                    ui->pushButton_new_find->setEnabled(true);
                    ui->pushButton_next_find->setEnabled(m_scanEngine->totalResults() > 0);
                });
        });
}

//定时刷新扫描结果，优化为增量更新
void MainWindow::refreshScanResults()
{
    if (m_refreshing.exchange(true))
        return;

    int currentGen = scanModel->generation();
    if (currentGen != m_lastRefreshGen)
        m_lastRefreshGen = currentGen;

    auto snapshot = scanModel->snapshot();
    if (!snapshot || snapshot->empty()) {
        m_refreshing = false;
        return;
    }

    int capturedGen = currentGen;
    auto memShared = ProcessManager::instance().memory();

    GlobalThreadPool::instance().enqueue([this, snapshot, capturedGen, memShared]() {
        auto mem = memShared.get();
        if (!mem) {
            QMetaObject::invokeMethod(this, [this]() { m_refreshing = false; });
            return;
        }

        // 用于安全传递数据的结构体
        struct DeltaData {
            int gen;
            std::vector<int> rows;
            std::vector<uint64_t> newValues;
            std::vector<uint8_t> changedFlags;
        };
        auto delta = std::make_shared<DeltaData>();
        delta->gen = capturedGen;

        const size_t count = snapshot->size();
        for (size_t i = 0; i < count; ++i) {
            const auto& item = (*snapshot)[i];
            uint64_t newVal = 0;
            if (!mem->read(item.address, &newVal, sizeof(newVal)))
                continue;

            bool changed = (newVal != item.value);
            if (changed) {
                delta->rows.push_back(static_cast<int>(i));
                delta->newValues.push_back(newVal);
                delta->changedFlags.push_back(static_cast<uint8_t>(1));
            }
        }

        // 投递 shared_ptr，内部向量不会被移动
        QMetaObject::invokeMethod(this, [this, delta]() {
            scanModel->applyDeltaUpdates(delta->gen, delta->rows, delta->newValues, delta->changedFlags);
            m_refreshing = false;
            });
        });
}

// ==================== 更新进度条与状态栏计数 ====================
void MainWindow::updateScanProgress()
{
    if (m_scanEngine && m_scanEngine->isScanning())
    {
        ui->progressBar->setVisible(true);
        int total = m_scanEngine->totalRegions();
        int done = m_scanEngine->regionsCompleted();
        ui->progressBar->setRange(0, total);
        ui->progressBar->setValue(done);
    }
    else
    {
        ui->progressBar->setVisible(false);
    }

    // 在状态栏显示结果数量

    int realCount = static_cast<int>(m_scanEngine->totalResults());        // 真实总结果数
    int displayedCount = scanModel->rowCount();
    if (realCount > displayedCount)
        statusBar()->showMessage(QString("Found: %1 addresses (displaying first %2)")
            .arg(realCount).arg(displayedCount));
    else
        statusBar()->showMessage(QString("Found: %1 addresses").arg(realCount));

}

void MainWindow::onDoubleClickScanResult(const QModelIndex& index)
{
    if (!ProcessManager::instance().memory())
        return;
    uint64_t addr = scanModel->getAddress(index.row());
    if (addr == 0) return;

    // 读取当前值
    uint64_t val = 0;
    ProcessManager::instance().memory()->read(addr, &val, sizeof(val));

    // 默认描述为 "Address"
    QString desc = QString("Address 0x%1").arg(addr, 0, 16);
    addressModel->addItem(addr, desc, val);
}