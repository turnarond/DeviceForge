/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FtpDeployWidget.cpp（双面板宿主重构 — Task 4）
 * Date: 2026-07-25
 * Author: turnarond
 *
 * Description: FTP 部署双面板宿主实现。批量部署链路零改动（FtpDeployBackend 不变）；
 *              文件浏览/导航/右键操作/拖拽全部下沉 FileBrowserPanel（Task 2/3），
 *              本类仅保留：工具级批量部署配置（协议/设备/FTPS/端口/清空/重启）+
 *              部署入口（校验 → collectLocalFiles → startUpload）+ 远程源按
 *              协议/设备变化重建（RemoteFileSource 注入右侧面板）。
 */

#include "FtpDeployWidget.h"
#include "FtpDeployBackend.h"
#include "MultiProgressWidget.h"
#include "DeployReport.h"
#include "framework/DeviceInfo.h"
#include "ui/DeviceBusWidget.h"
#include "ui/FileBrowserPanel.h"
#include "ui/IFileSource.h"
#include "ui/LocalFileSource.h"
#include "ui/RemoteFileSource.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <QPointer>
#include <QDateTime>
#include <QStringList>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFile>
#include <QtConcurrent>
#include <algorithm>

FtpDeployWidget::FtpDeployWidget(QWidget* parent)
    : ToolWidget(parent)
{
    setupUi();
}

void FtpDeployWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(6);

    setupToolbar(mainLayout);

    // === 双栏区域 (QSplitter)：FileBrowserPanel 组装 ===
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(2);

    m_leftPanel = new FileBrowserPanel(this);
    m_leftPanel->setObjectName("localPanel");
    m_leftPanel->setSource(std::make_shared<LocalFileSource>());

    m_rightPanel = new FileBrowserPanel(this);
    m_rightPanel->setObjectName("remotePanel");
    // 远程源延迟到协议/设备确定后 setSource（onRefreshRemote 首次调用时注入）

    // 面板互指（F5 复制/F6 移动/Tab 切换/拖拽方向语义依赖 peer）
    m_leftPanel->setPeerPanel(m_rightPanel);
    m_rightPanel->setPeerPanel(m_leftPanel);

    // 源选择器默认布局：左面板本地（设备下拉隐藏）；右面板=工具栏配置（FTP + 设备，
    // 设备列表在 setDeviceBusWidget 填充）——左面板可再切换远程浏览，右面板恒为部署目标浏览
    m_leftPanel->setSourceProto(QStringLiteral("local"));
    m_rightPanel->setSourceProto(QStringLiteral("ftp"));

    // Task 3：面板源选择器 → 重建对应面板源（blockSignals 防循环见各处理器）
    connect(m_leftPanel, &FileBrowserPanel::sourceChooserChanged, this,
            &FtpDeployWidget::onLeftSourceChanged);
    connect(m_rightPanel, &FileBrowserPanel::sourceChooserChanged, this,
            &FtpDeployWidget::onRightSourceChanged);

    // 方向指示（Task 5）：任一面板路径/选中变化 → 组合左右路径发射（状态栏中央指示）
    // FileBrowserPanel 的 currentPathChanged/selectionChanged 已由面板自身落实（setModel 后连接）
    auto emitDirection = [this]() {
        emit directionChanged(m_leftPanel->currentPath()
                              + QStringLiteral(" → ")
                              + m_rightPanel->currentPath());
    };
    connect(m_leftPanel, &FileBrowserPanel::currentPathChanged, this, emitDirection);
    connect(m_rightPanel, &FileBrowserPanel::currentPathChanged, this, emitDirection);
    connect(m_leftPanel, &FileBrowserPanel::selectionChanged, this, emitDirection);
    connect(m_rightPanel, &FileBrowserPanel::selectionChanged, this, emitDirection);

    m_splitter->addWidget(m_leftPanel);
    m_splitter->addWidget(m_rightPanel);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(m_splitter, 1);

    setupBottomBar(mainLayout);
    connectBackendSignals();
}

void FtpDeployWidget::setupToolbar(QVBoxLayout* mainLayout)
{
    auto* toolbarWidget = new QWidget(this);
    auto* toolbar = new QHBoxLayout(toolbarWidget);
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(6);   // 组间间距

    // === 连接组：协议 | 设备 | 端口 | FTPS 加密 | 连接状态点 ===
    auto* connGroup = new QHBoxLayout();
    connGroup->setSpacing(4);   // 标签紧贴控件（QLabel 后紧跟控件，2-4px）

    connGroup->addWidget(new QLabel("协议:", this));
    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItem("FTP");
    m_protocolCombo->addItem("SFTP");
    m_protocolCombo->setFixedWidth(80);
    connect(m_protocolCombo, &QComboBox::currentTextChanged, this, [this](const QString& proto) {
        if (proto == "SFTP") {
            m_portSpin->setValue(22);
            m_ftpsCheck->setEnabled(false);
        } else {
            m_portSpin->setValue(21);
            m_ftpsCheck->setEnabled(true);
        }
        // 工具栏协议 → 右面板源选择器显示同步（blockSignals 防循环；重建走 onRefreshRemote）
        if (m_rightPanel)
            m_rightPanel->setSourceProto(proto == "SFTP" ? QStringLiteral("sftp")
                                                         : QStringLiteral("ftp"));
        // 协议变化 → 远程源重建 + 刷新（setSource 导航到根目录）
        onRefreshRemote();
    });
    connGroup->addWidget(m_protocolCombo);

    connGroup->addWidget(new QLabel("设备:", this));
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setMinimumWidth(160);
    m_deviceCombo->setToolTip("选择目标设备（多选支持——从设备总线同步）");
    // 设备切换 → 远程源重建 + 刷新（程序化填充期间 blockSignals，见 setDeviceBusWidget）
    connect(m_deviceCombo, &QComboBox::currentTextChanged, this, [this](const QString& dev) {
        if (m_rightPanel) m_rightPanel->setSourceDevice(dev);   // 右面板显示同步（blockSignals）
        onRefreshRemote();
    });
    connGroup->addWidget(m_deviceCombo);

    connGroup->addWidget(new QLabel("端口:", this));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(21);
    connGroup->addWidget(m_portSpin);

    m_ftpsCheck = new QCheckBox("FTPS 加密", this);
    connGroup->addWidget(m_ftpsCheck);

    // 连接状态点（圆形指示灯）：灰=未连接/未配置，灰闪=连接中，青绿=连接成功，红=连接失败
    // 底色由代码动态设置（固定暗色版，双主题下对比度足够——指示点非文字），圆角见双 QSS
    m_connStatusDot = new QLabel(this);
    m_connStatusDot->setObjectName("connStatusDot");
    m_connStatusDot->setFixedSize(10, 10);
    m_connStatusDot->setToolTip("远程连接状态：灰=未连接，灰闪=连接中，青绿=已连接，红=连接失败");
    // 灰闪定时器必须先于首次 setConnState 创建（setConnState 的静态态分支会停表）
    m_connFlashTimer = new QTimer(this);
    m_connFlashTimer->setInterval(500);
    connect(m_connFlashTimer, &QTimer::timeout, this, [this]() {
        if (!m_connStatusDot) return;
        m_connFlashOn = !m_connFlashOn;
        m_connStatusDot->setStyleSheet(
            QStringLiteral("background-color: %1;")
                .arg(m_connFlashOn ? QStringLiteral("#A8B0BF")
                                   : QStringLiteral("#7B8494")));
    });
    setConnState(RemoteConnState::Unknown);
    connGroup->addWidget(m_connStatusDot);

    // === 组间分隔条（QFrame VLine，石墨窄条） ===
    auto* separator = new QFrame(this);
    separator->setObjectName("toolbarSeparator");
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setFixedWidth(1);

    // === 部署组：清空 | 重启 | 刷新 | 部署 ===
    auto* deployGroup = new QHBoxLayout();
    deployGroup->setSpacing(4);

    m_clearCheck = new QCheckBox("部署前清空远程", this);
    deployGroup->addWidget(m_clearCheck);

    m_rebootCheck = new QCheckBox("部署后重启", this);
    deployGroup->addWidget(m_rebootCheck);

    // 「⟳ 刷新」按钮（2026-08-12 修复波回归：onRefreshRemote 的 UI 入口）
    m_refreshBtn = new QPushButton("⟳ 刷新", this);
    m_refreshBtn->setObjectName("refreshBtn");
    m_refreshBtn->setToolTip("重新连接远程并刷新文件列表");
    connect(m_refreshBtn, &QPushButton::clicked, this, &FtpDeployWidget::onRefreshRemote);
    deployGroup->addWidget(m_refreshBtn);

    m_deployBtn = new QPushButton(this);
    m_deployBtn->setObjectName("btnPrimary");
    m_deployBtn->setMinimumHeight(30);
    connect(m_deployBtn, &QPushButton::clicked, this, &FtpDeployWidget::onDeployClicked);
    // 初始文本「▶ 部署」：设备总线同步前不显示「0 台设备」；
    // 同步后由 setDeviceBusWidget → updateDeployBtnText() 更新为「▶ 部署到 N 台设备」
    m_deployBtn->setText(QStringLiteral("▶ 部署"));
    deployGroup->addWidget(m_deployBtn);

    // === 结果操作组（v2.8 Task 5）：导出报告 | 重试失败设备（部署结束后点亮） ===
    auto* resultSeparator = new QFrame(this);
    resultSeparator->setObjectName("toolbarSeparator");
    resultSeparator->setFrameShape(QFrame::VLine);
    resultSeparator->setFrameShadow(QFrame::Plain);
    resultSeparator->setFixedWidth(1);

    auto* resultGroup = new QHBoxLayout();
    resultGroup->setSpacing(4);

    m_exportReportBtn = new QPushButton(QStringLiteral("⬇ 导出报告"), this);
    m_exportReportBtn->setToolTip(
        QStringLiteral("导出最近一轮部署报告（CSV 归档 / HTML 打印）"));
    m_exportReportBtn->setEnabled(false);   // 首轮部署结束（finished 回调）后点亮
    connect(m_exportReportBtn, &QPushButton::clicked,
            this, &FtpDeployWidget::onExportReportClicked);
    resultGroup->addWidget(m_exportReportBtn);

    m_retryBtn = new QPushButton(QStringLiteral("↻ 重试失败设备"), this);
    m_retryBtn->setToolTip(
        QStringLiteral("把上一轮失败的设备单独重新部署（沿用上次文件/目录/选项；取消的设备不在重试范围）"));
    m_retryBtn->setEnabled(false);          // 存在失败台次时才点亮
    connect(m_retryBtn, &QPushButton::clicked,
            this, &FtpDeployWidget::onRetryFailedClicked);
    resultGroup->addWidget(m_retryBtn);

    toolbar->addLayout(connGroup);
    toolbar->addWidget(separator);
    toolbar->addLayout(deployGroup);
    toolbar->addWidget(resultSeparator);
    toolbar->addLayout(resultGroup);
    toolbar->addStretch();

    mainLayout->addWidget(toolbarWidget);
}

void FtpDeployWidget::setupBottomBar(QVBoxLayout* mainLayout)
{
    // 多设备进度（部署按钮已移至工具栏部署组，2026-08-12 布局修复波）
    m_multiProgress = new MultiProgressWidget(this);
    connect(m_multiProgress, &MultiProgressWidget::cancelRequested, [this]() {
        if (m_backend) m_backend->cancelUpload();
    });
    mainLayout->addWidget(m_multiProgress);
}

void FtpDeployWidget::connectBackendSignals()
{
    if (!m_backend) return;
    m_backend->setProgressCallback([this](int pct) {
        QMetaObject::invokeMethod(this, [this, pct]() {
            m_multiProgress->setOverallProgress(pct);
        }, Qt::QueuedConnection);
    });
    // per-device 进度管道（v2.8 Task 4）：后端已完成 kOverallKey 分流，
    // 此处收到的 key 恒为 "ip:port"，直接派发到对应行。回调从 Runner 池线程
    // 触发 → 统一 QueuedConnection 编组到 GUI 线程（既有模式）
    m_backend->setDeviceProgressCallback([this](const std::string& key, int pct) {
        const QString devKey = QString::fromStdString(key);
        QMetaObject::invokeMethod(this, [this, devKey, pct]() {
            m_multiProgress->setDeviceProgress(devKey, pct);
        }, Qt::QueuedConnection);
    });
    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });
    m_backend->setFinishedCallback(
        [this](bool ok, const std::vector<std::string>& successes,
               const std::vector<std::string>& failures) {
            QMetaObject::invokeMethod(this, [this, ok, successes, failures]() {
                m_deployBtn->setEnabled(true);
                // 更新每个设备的状态
                for (const auto& key : successes)
                    m_multiProgress->setDeviceStatusByKey(
                        QString::fromStdString(key), true);
                for (const auto& key : failures)
                    m_multiProgress->setDeviceStatusByKey(
                        QString::fromStdString(key), false);
                // G2 补全：Backend 结果映射把 Cancelled 排除在两列之外（串行
                // 等价裁定，m_finishedCb 签名不可改），取消集由 Widget 侧做
                // 差集还原：本轮注册键 − 成功 − 失败 ＝ 取消。键公式与行注册/
                // 重试匹配严格同源（d.ip + ":" + m_lastPort）；重试轮行表只含
                // 子集，差集中未注册键由 setDeviceCancelled 静默忽略
                for (const auto& d : m_lastDevices) {
                    const std::string key = d.ip + ":" + std::to_string(m_lastPort);
                    const bool settled =
                        std::find(successes.begin(), successes.end(), key)
                            != successes.end()
                        || std::find(failures.begin(), failures.end(), key)
                               != failures.end();
                    if (!settled) {
                        m_multiProgress->setDeviceCancelled(
                            QString::fromStdString(key));
                    }
                }
                int total = static_cast<int>(successes.size() + failures.size());
                int done = static_cast<int>(successes.size());
                // 总条语义＝批量生命周期进度：全部台次已出结果即置满，成败比例
                // 由收尾文案与行级着色承载——失败收场归零会与 summary 并存矛盾
                m_multiProgress->setOverallProgress(100);
                m_multiProgress->setFinishedSummary(done, total);
                // v2.8 Task 5：缓存失败清单并按结果点亮结果操作按钮。
                // failures 仅含 Failed 台次（Backend 结果映射把 Cancelled 排除在
                // 两列之外）——重试口径只含 Failed，与设计语义边界一致
                m_lastFailures = failures;
                m_retryBtn->setEnabled(!failures.empty());
                m_exportReportBtn->setEnabled(true);   // 本轮报告已由 Backend 缓存
                if (ok && !successes.empty()) {
                    appendLog(QString("✅ 部署完成 — 成功: %1, 失败: %2")
                        .arg(successes.size()).arg(failures.size()));
                    // 自动刷新远程面板
                    onRefreshRemote();
                } else {
                    appendLog("❌ 部署失败");
                }
            }, Qt::QueuedConnection);
        });
}

void FtpDeployWidget::onDeployClicked()
{
    if (!m_backend) { appendLog("Backend 未就绪"); return; }
    if (!m_deviceBus) { appendLog("设备总线未关联"); return; }

    auto devices = m_deviceBus->selectedDevices();
    if (devices.empty()) devices = m_deviceBus->allDevices(); // 未选中时部署到全部
    if (devices.empty()) { appendLog("错误：设备总线中没有目标设备"); return; }
    // 左侧面板可切远程浏览源（Task 3）：部署语义=从左侧本地收集文件，远程源时拒绝——
    // 后端按本地路径上传，远程路径会误传（批量部署目标始终按工具栏配置执行）
    if (m_leftPanel->source() && m_leftPanel->source()->sourceId() != "local") {
        appendLog("左侧面板当前为远程浏览源，请切换为「本地」后再部署");
        return;
    }
    auto files = collectLocalFiles();
    if (files.empty()) { appendLog("请先在左侧本地面板中选中要部署的文件"); return; }

    // 部署目标目录 = 右侧远程面板当前目录（远程路径栏已在面板内置）；
    // 空路径回退根目录（远程源延迟注入时 currentPath() 为空——SFTP 空路径会
    // 上传到登录 cwd 而非根目录，必须显式回退 "/"）
    QString remotePath = m_rightPanel ? m_rightPanel->currentPath() : QStringLiteral("/");
    if (remotePath.isEmpty()) remotePath = QStringLiteral("/");

    const bool clearBefore = m_clearCheck->isChecked();
    const bool rebootAfter = m_rebootCheck->isChecked();
    const std::string protocol = currentProtocol();
    const bool useFtps = m_ftpsCheck->isChecked();
    const int port = m_portSpin->value();

    // 缓存本次部署请求（v2.8 Task 5 失败重试依据）：放在全部校验通过之后，
    // 校验失败的点击不会覆盖上一轮有效请求
    m_lastDevices = devices;
    m_lastFiles = files;
    m_lastRemotePath = remotePath.toStdString();
    m_lastClearBefore = clearBefore;
    m_lastRebootAfter = rebootAfter;
    m_lastProtocol = protocol;
    m_lastUseFtps = useFtps;
    m_lastPort = port;

    startDeployment(devices, files, remotePath, clearBefore, rebootAfter,
                    protocol, useFtps, port, QString());
}

void FtpDeployWidget::startDeployment(const std::vector<DeviceInfo>& devices,
                                      const std::vector<std::string>& files,
                                      const QString& remotePath,
                                      bool clearBefore, bool rebootAfter,
                                      const std::string& protocol, bool useFtps,
                                      int port, const QString& logPrefix)
{
    AuthInfo auth;
    auth.user = m_deviceBus->user().toStdString();
    auth.password = m_deviceBus->password().toStdString();
    m_backend->bindCredentials(auth);
    m_backend->bindDevices(devices);

    m_deployBtn->setEnabled(false);
    // 新一轮批量启动：结果操作按钮先熄灭（finished 回调按本轮结果重新点亮）
    m_exportReportBtn->setEnabled(false);
    m_retryBtn->setEnabled(false);

    m_multiProgress->setDeviceCount(static_cast<int>(devices.size()));
    // 行键与 Runner 进度回调 / DeviceResult.deviceKey 严格同源："ip:port"。
    // 后端以工具栏端口覆盖全部设备端口（port 恒 >0），故此处行键端口
    // 直接取传入端口——否则 port=21 时行键缺端口段，终态回调将无法命中行
    for (const auto& d : devices) {
        m_multiProgress->setDeviceInfo(
            QString::fromStdString(d.ip) + ":" + QString::number(port));
    }

    appendLog(logPrefix + QString("开始部署到 %1 台设备...").arg(devices.size()));

    m_backend->startUpload(files,
        remotePath.toStdString(),
        clearBefore,
        rebootAfter,
        protocol,
        useFtps,
        port
    );
}

void FtpDeployWidget::startDeployFromMenu()
{
    if (!m_deployBtn->isEnabled()) {          // 部署进行中（按钮已禁用）
        appendLog("部署正在进行中，请先取消或等待完成");
        return;
    }
    onDeployClicked();
}

void FtpDeployWidget::cancelDeployFromMenu()
{
    if (m_backend) m_backend->cancelUpload();
    appendLog("已从菜单取消部署");
}

void FtpDeployWidget::onRetryFailedClicked()
{
    if (!m_backend) { appendLog("Backend 未就绪"); return; }
    if (!m_deviceBus) { appendLog("设备总线未关联"); return; }
    if (m_lastFailures.empty()) { appendLog("没有可重试的失败设备"); return; }

    // 行键与 DeviceResult.deviceKey 严格同源："ip:port"，端口取上次部署的
    // 工具栏端口（后端以该端口覆盖全部设备端口）。只筛 Failed 台次——
    // Cancelled（用户取消/未启动）不入 m_lastFailures，天然排除在重试口径外
    std::vector<DeviceInfo> failedDevices;
    for (const auto& d : m_lastDevices) {
        const std::string key =
            d.ip + ":" + std::to_string(m_lastPort);
        if (std::find(m_lastFailures.begin(), m_lastFailures.end(), key)
                != m_lastFailures.end()) {
            failedDevices.push_back(d);
        }
    }
    if (failedDevices.empty()) {
        appendLog("未能在上次部署设备中匹配到失败设备，无法重试");
        return;
    }

    // 沿用上次请求缓存（文件/目录/选项/协议），凭证取设备总线当前值；
    // 走与普通部署完全相同的 startDeployment → startUpload 链路
    appendLog(QString("重试 %1 台失败设备...").arg(failedDevices.size()));
    startDeployment(failedDevices, m_lastFiles,
                    QString::fromStdString(m_lastRemotePath),
                    m_lastClearBefore, m_lastRebootAfter,
                    m_lastProtocol, m_lastUseFtps, m_lastPort,
                    QStringLiteral("【重试】"));
}

void FtpDeployWidget::onExportReportClicked()
{
    if (!m_backend) { appendLog("Backend 未就绪"); return; }
    const DeployReport report = m_backend->lastReport();
    if (report.results.empty()) { appendLog("暂无可导出的部署报告"); return; }

    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出部署报告"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/deploy-report.csv"),
        QStringLiteral("CSV (*.csv);;HTML (*.html)"),
        &selectedFilter);
    if (path.isEmpty())
        return;

    // 按所选过滤器选择渲染格式；文件名缺扩展名时按格式补齐
    const bool asHtml = selectedFilter.contains(QStringLiteral("HTML"));
    QString filePath = path;
    if (asHtml) {
        if (!filePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)
            && !filePath.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive))
            filePath += QStringLiteral(".html");
    } else if (!filePath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        filePath += QStringLiteral(".csv");
    }

    const std::string content = asHtml ? renderReportHtml(report)
                                       : renderReportCsv(report);
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        appendLog(QString("导出报告失败：无法写入 %1").arg(filePath));
        return;
    }
    const qint64 written = f.write(content.data(), static_cast<qint64>(content.size()));
    const bool complete =
        written == static_cast<qint64>(content.size()) && f.flush();
    f.close();
    // 写盘结果核验：磁盘满/权限问题导致写入不完整时不得谎报导出成功
    if (!complete) {
        appendLog(QString("导出报告失败：%1 写入不完整（%2/%3 字节），请检查磁盘空间与写权限")
                      .arg(filePath).arg(written)
                      .arg(static_cast<qint64>(content.size())));
        return;
    }
    appendLog(QString("部署报告已导出：%1（%2 条记录）")
                  .arg(filePath).arg(static_cast<int>(report.results.size())));
}

std::vector<std::string> FtpDeployWidget::collectLocalFiles() const
{
    std::vector<std::string> files;
    if (!m_leftPanel) return files;
    // 面板 selectedFiles 返回 FtpFileInfo（name 需拼面板当前路径）；
    // 跳过 ".." 导航条目；文件与文件夹均纳入部署（backend 的 uploadFolder 递归处理文件夹）
    const QString base = m_leftPanel->currentPath();
    for (const auto& f : m_leftPanel->selectedFiles()) {
        if (f.name == ".." || f.name.empty()) continue;
        QString full = base;
        if (!full.endsWith('/')) full += '/';
        full += QString::fromStdString(f.name);
        files.push_back(full.toStdString());
    }
    return files;
}

void FtpDeployWidget::onRefreshRemote()
{
    // 连接代际令牌（入口即 ++，含空设备/忙拒绝等早退分支）：任何一次刷新请求都使
    // 在途连接的陈旧回调失效——连接在途时切换设备/删除全部设备，回调到达后比对
    // 令牌丢弃，不挂载过期源（与面板 loadGeneration 同模式）
    const quint64 gen = ++m_connGeneration;

    if (!m_deviceBus || m_deviceBus->allDevices().empty()) {
        appendLog("错误：设备总线中没有目标设备");
        setConnState(RemoteConnState::Unknown);   // 无目标设备 = 未配置 → 灰
        // 删除全部设备后不残留旧设备目录（与连接失败 detach 同方案）
        detachRemotePanel();
        return;
    }
    if (!m_rightPanel) return;

    // 防止并发刷新（远程源适配器非线程安全，多次触发会同时操作同一连接）
    if (m_remoteBusy.load()) {
        appendLog("正在刷新中，请稍候...");
        return;
    }
    m_remoteBusy = true;

    // 从设备下拉框获取当前选中设备
    QString deviceIp = m_deviceCombo->currentText();
    if (deviceIp.isEmpty()) {
        deviceIp = QString::fromStdString(m_deviceBus->allDevices()[0].ip);
    }

    const int port = m_portSpin->value();
    const bool useFtps = m_ftpsCheck->isChecked();
    const QString proto = QString::fromStdString(currentProtocol());
    const QString protoDisplay = m_protocolCombo->currentText();   // 显示名（"FTP"/"SFTP"）

    // 协议/设备/端口/FTPS 变化时重建远程源（重建后 setSource 导航到根目录）
    const bool configChanged = !m_remoteSource
        || proto != m_remoteSrcProto
        || deviceIp != m_remoteSrcDevice
        || port != m_remoteSrcPort
        || useFtps != m_remoteSrcUseFtps;

    DeviceInfo dev;
    dev.ip = deviceIp.toStdString();
    dev.port = port;
    AuthInfo auth;
    auth.user = m_deviceBus->user().toStdString();
    auth.password = m_deviceBus->password().toStdString();

    // 连接异步化（Task 2）：connect 移入 QtConcurrent 工作线程，断网 connect 超时
    // （约 10s）不再冻结 UI；主线程回调负责状态点/缓存/面板挂载。RemoteFileSource
    // 内部已有 QMutex 串行化 adapter 操作（Task 1），worker 调用 connect 走其内部锁，
    // 无需重复加锁。lambda 按值捕获全部所需标量/结构（port 等）。
    // 注意：Connecting 态只在真正发起连接的两个分支里进入（纯刷新分支保持已连接态）
    if (configChanged) {
        // 全量重建：工作线程 create + connect，主线程回调成功则更新缓存并挂载面板。
        // guard 在捕获列表用主线程（调度时 this 必然存活）构造 QPointer，worker 回调前
        // 判空——应用退出等在途 connect ~10s 窗口内 widget 析构后不再回调，防悬垂 UAF
        setConnState(RemoteConnState::Connecting);   // 连接中 → 灰闪
        QtConcurrent::run([this, guard = QPointer<FtpDeployWidget>(this), gen,
                           proto, protoDisplay, deviceIp, useFtps, port, dev, auth]() {
            auto source = std::make_shared<RemoteFileSource>(
                proto, deviceIp + " (" + protoDisplay + ")");
            source->setUseFtps(useFtps);
            const bool ok = source->connect(dev, auth);
            const QString err = source->lastError();
            if (!guard) return;   // widget 已析构 → 丢弃（invokeMethod 到悬垂接收者才防不了）
            QMetaObject::invokeMethod(this, [this, gen, source, ok, err,
                                             deviceIp, proto, useFtps, port]() {
                m_remoteBusy = false;
                if (gen != m_connGeneration) {
                    // 连接期间配置已变更（切换设备/删除全部设备等）：丢弃陈旧回调不挂载
                    // 过期源；状态点按当前缓存源恢复（面板未被本次回调触碰，仍显示旧源）
                    appendLog("远程连接已取消（刷新期间配置变更）");
                    setConnState(m_remoteSource && m_remoteSource->isConnected()
                                     ? RemoteConnState::Connected
                                     : RemoteConnState::Unknown);
                    return;
                }
                if (!ok) {
                    appendLog("远程连接失败: " + err);
                    setConnState(RemoteConnState::Failed);   // 连接失败 → 红
                    // 切换设备/协议后旧源已失效：detach 防止面板残留旧设备目录
                    // （部署目标 = 面板当前路径，残留目录会误导部署目标）；
                    // 缓存一并清空 → 下次刷新走全量重建分支，不会出现
                    // 「缓存匹配但面板无源」的死锁状态（I-1）
                    detachRemotePanel();
                    return;
                }
                setConnState(RemoteConnState::Connected);    // 连接成功 → 青绿
                m_remoteSource = source;
                m_remoteSrcDevice = deviceIp;
                m_remoteSrcProto = proto;
                m_remoteSrcPort = port;
                m_remoteSrcUseFtps = useFtps;
                m_rightPanel->setSource(source);   // setSource 内 navigateTo → 异步 list
            }, Qt::QueuedConnection);
        });
    } else if (!m_remoteSource->isConnected()) {
        // 连接失效原位重连（保留当前面板路径，复用旧连接缓存语义）：
        // 与全量重建分支同构——工作线程 connect（复用既有源对象），主线程回调状态点
        setConnState(RemoteConnState::Connecting);   // 连接中 → 灰闪
        auto source = m_remoteSource;   // shared_ptr 拷贝：worker 持有期间源不被析构
        QtConcurrent::run([this, guard = QPointer<FtpDeployWidget>(this), gen,
                           source, dev, auth]() {
            const bool ok = source->connect(dev, auth);
            const QString err = source->lastError();
            if (!guard) return;   // widget 已析构 → 丢弃
            QMetaObject::invokeMethod(this, [this, gen, ok, err]() {
                m_remoteBusy = false;
                if (gen != m_connGeneration) {
                    // 连接期间配置已变更：丢弃陈旧回调（同全量重建分支）
                    appendLog("远程重连已取消（刷新期间配置变更）");
                    setConnState(m_remoteSource && m_remoteSource->isConnected()
                                     ? RemoteConnState::Connected
                                     : RemoteConnState::Unknown);
                    return;
                }
                if (!ok) {
                    appendLog("远程重连失败: " + err);
                    setConnState(RemoteConnState::Failed);
                    // 与连接失败分支统一 detach：不再调面板 refresh()——
                    // 否则无源/失败状态下面板内 loadDirectory 的自动重连链会二次触发；
                    // 面板置无源后仅提示「未连接远程，请选择设备并刷新」（M-2）
                    detachRemotePanel();
                    return;
                }
                setConnState(RemoteConnState::Connected);
                m_rightPanel->refresh();   // 异步 list（保留当前路径，Task 1）
            }, Qt::QueuedConnection);
        });
    } else {
        // 已连接：仅刷新列表（异步 list，Task 1）
        m_remoteBusy = false;
        m_rightPanel->refresh();
    }
}

// 连接状态点底色：固定用暗色版（灰 #7B8494 / 灰闪 #A8B0BF ↔ #7B8494 / 青绿 #40C8A0 /
// 红 #E85848）。说明：状态点为 10x10 纯色圆点而非文字，亮色主题下暗色版对比度足够，
// 故不按主题切换色值；圆角 border-radius 由双 QSS 的 #connStatusDot 规则提供。
// Connecting 态启动灰闪定时器（500ms 亮灰 ↔ 灰），其余状态停表并设静态底色。
void FtpDeployWidget::setConnState(RemoteConnState state)
{
    if (!m_connStatusDot) return;
    if (state == RemoteConnState::Connecting) {
        m_connFlashOn = true;
        if (m_connFlashTimer) m_connFlashTimer->start();
        m_connStatusDot->setStyleSheet(
            QStringLiteral("background-color: #A8B0BF;"));   // 亮灰（闪的第一相）
        return;
    }
    if (m_connFlashTimer) m_connFlashTimer->stop();
    const char* color = "#7B8494";
    if (state == RemoteConnState::Connected)      color = "#40C8A0";
    else if (state == RemoteConnState::Failed)    color = "#E85848";
    m_connStatusDot->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(color));
}

// 部署按钮文本「▶ 部署到 N 台设备」：N = 已选中设备数（未选中时与部署语义一致，取全部设备）
void FtpDeployWidget::updateDeployBtnText()
{
    if (!m_deployBtn) return;
    int n = 0;
    if (m_deviceBus) {
        auto devs = m_deviceBus->selectedDevices();
        if (devs.empty()) devs = m_deviceBus->allDevices();
        n = static_cast<int>(devs.size());
    }
    m_deployBtn->setText(QStringLiteral("▶ 部署到 %1 台设备").arg(n));
}

// 连接失败/重连失败/无设备统一 detach（I-1 / M-2 / M-3）：
// 面板置无源（清空表格 + 路径栏 + 面包屑 + 当前路径，setSource(nullptr) 支持清空），
// 远程源缓存一并失效——若只 detach 面板而保留缓存，下一次 onRefreshRemote 会走
// 「原位重连」分支（configChanged=false），而面板无源时 refresh() 只会提示
// 「未连接远程」永远不会重挂源，形成状态死锁；缓存清空后统一走全量重建分支
void FtpDeployWidget::detachRemotePanel()
{
    m_remoteSource.reset();
    m_remoteSrcDevice.clear();
    m_remoteSrcProto.clear();
    m_remoteSrcPort = 0;
    m_remoteSrcUseFtps = false;
    if (m_rightPanel) {
        m_rightPanel->setSource(nullptr);
        m_rightPanel->refresh();   // 无源 → loadDirectory 提示「未连接远程，请选择设备并刷新」
    }
}

// ============================================================
// Task 3：面板源选择器联动
// 语义分工：面板源=浏览配置；工具栏=批量部署目标（部署按钮仍按工具栏执行）。
// 双向联动全部经 blockSignals 防循环：程序化设置不发射 sourceChooserChanged，
// 仅用户操作面板下拉/工具栏下拉时单向驱动对侧显示，重建源走各自异步链路。
// ============================================================

void FtpDeployWidget::onLeftSourceChanged(const QString& proto, const QString& device)
{
    // 代际令牌（入口即 ++，含早退分支）：左面板源再切换作废在途连接的陈旧回调
    // （与 onRefreshRemote 同模式；回调首行清 busy，不会死锁）
    const quint64 gen = ++m_leftConnGeneration;

    if (proto == QStringLiteral("local")) {
        // 本地：直接挂本地源（浏览配置与部署收集一致）；缓存清空
        m_leftRemoteSource.reset();
        m_leftSrcDevice.clear();
        m_leftSrcProto.clear();
        m_leftSrcPort = 0;
        m_leftPanel->setSource(std::make_shared<LocalFileSource>());
        return;
    }
    if (!m_deviceBus || m_deviceBus->allDevices().empty()) {
        appendLog("错误：设备总线中没有目标设备，无法浏览远程");
        detachLeftPanel();
        return;
    }
    if (m_leftRemoteBusy.load()) {
        appendLog("正在连接中，请稍候...");
        return;
    }
    m_leftRemoteBusy = true;

    // 面板级设备（未选设备时回退首台）；端口取设备自身（无则按协议默认）——浏览配置
    const QString deviceIp = device.isEmpty()
        ? QString::fromStdString(m_deviceBus->allDevices().front().ip) : device;
    const QString protoKey = proto == QStringLiteral("sftp")
        ? QStringLiteral("ssh") : QStringLiteral("ftp");
    const QString display = proto == QStringLiteral("sftp") ? "SFTP" : "FTP";
    int port = 0;
    for (const auto& d : m_deviceBus->allDevices()) {
        if (QString::fromStdString(d.ip) == deviceIp) { port = d.port; break; }
    }
    if (port <= 0) port = (protoKey == QStringLiteral("ssh")) ? 22 : 21;

    DeviceInfo dev;
    dev.ip = deviceIp.toStdString();
    dev.port = port;
    AuthInfo auth;
    auth.user = m_deviceBus->user().toStdString();
    auth.password = m_deviceBus->password().toStdString();

    // 连接异步（与 onRefreshRemote 同构：QtConcurrent + QPointer 守卫 + 代际令牌）。
    // 左面板远程源仅影响浏览，不动工具栏部署目标
    QtConcurrent::run([this, guard = QPointer<FtpDeployWidget>(this), gen,
                       protoKey, display, deviceIp, port, dev, auth]() {
        auto source = std::make_shared<RemoteFileSource>(protoKey,
                                                         deviceIp + " (" + display + ")");
        const bool ok = source->connect(dev, auth);
        const QString err = source->lastError();
        if (!guard) return;   // widget 已析构 → 丢弃
        QMetaObject::invokeMethod(this, [this, gen, source, ok, err, deviceIp, protoKey, port]() {
            m_leftRemoteBusy = false;
            if (gen != m_leftConnGeneration) {
                // 连接期间左面板源再切换（如连回本地）：丢弃陈旧回调不挂载
                appendLog("左侧浏览连接已取消（刷新期间配置变更）");
                return;
            }
            if (!ok) {
                appendLog("左侧远程浏览连接失败: " + err);
                detachLeftPanel();   // 面板置无源 + 缓存清空（与右面板失败统一方案）
                return;
            }
            m_leftRemoteSource = source;
            m_leftSrcDevice = deviceIp;
            m_leftSrcProto = protoKey;
            m_leftSrcPort = port;
            m_leftPanel->setSource(source);   // setSource 内 navigateTo → 异步 list
        }, Qt::QueuedConnection);
    });
}

void FtpDeployWidget::onRightSourceChanged(const QString& proto, const QString& device)
{
    // 右面板=工具栏的浏览视图：源变化回写工具栏（blockSignals 防循环）+ 按新配置
    // 重建远程源。本地选项映射回 FTP（右面板语义上恒为远程部署目标浏览）
    m_protocolCombo->blockSignals(true);
    m_protocolCombo->setCurrentIndex(proto == QStringLiteral("sftp") ? 1 : 0);
    m_protocolCombo->blockSignals(false);
    if (!device.isEmpty()) {
        const int idx = m_deviceCombo->findText(device);
        if (idx >= 0) {
            m_deviceCombo->blockSignals(true);
            m_deviceCombo->setCurrentIndex(idx);
            m_deviceCombo->blockSignals(false);
        }
    }
    onRefreshRemote();   // 按新配置重建远程源（Task 2 异步链路就绪）
}

// 左面板远程浏览源连接失败统一 detach：面板置无源 + 缓存清空（与右面板 I-1 同方案）
void FtpDeployWidget::detachLeftPanel()
{
    m_leftRemoteSource.reset();
    m_leftSrcDevice.clear();
    m_leftSrcProto.clear();
    m_leftSrcPort = 0;
    if (m_leftPanel) {
        m_leftPanel->setSource(nullptr);
        m_leftPanel->refresh();   // 无源提示「未连接远程，请选择设备并刷新」
    }
}

// 设备列表同步到两面板源选择器（blockSignals 防循环；面板级设备=浏览配置）
void FtpDeployWidget::populatePanelDeviceCombos()
{
    if (!m_deviceBus || !m_leftPanel || !m_rightPanel) return;
    QStringList ips;
    for (const auto& d : m_deviceBus->allDevices())
        ips << QString::fromStdString(d.ip);
    m_leftPanel->setSourceDevices(ips);
    m_rightPanel->setSourceDevices(ips);
}

void FtpDeployWidget::setBackend(FtpDeployBackend* backend)
{
    m_backend = backend;
    connectBackendSignals();
}

void FtpDeployWidget::setDeviceBusWidget(DeviceBusWidget* deviceBus)
{
    m_deviceBus = deviceBus;

    // 同步设备下拉框（填充期间 blockSignals，避免 currentTextChanged 触发远程连接）
    if (m_deviceBus) {
        m_deviceCombo->blockSignals(true);
        m_deviceCombo->clear();
        for (const auto& d : m_deviceBus->allDevices()) {
            m_deviceCombo->addItem(QString::fromStdString(d.ip));
        }
        m_deviceCombo->blockSignals(false);
        populatePanelDeviceCombos();   // 面板源选择器设备列表同步（Task 3）
        updateDeployBtnText();

        // 设备总线变更（添加/删除/选中切换）时同步下拉框，并在 blockSignals(false)
        // 之后主动调用 onRefreshRemote() —— 修复触发链断裂：程序化填充期间
        // currentTextChanged 被屏蔽，设备添加后远程源永不创建（面板 m_source 为
        // null 静默无列表）。lambda 捕获 this，connect 以 this 为 context 对象，
        // FtpDeployWidget 析构后连接自动断开，无悬垂捕获风险
        connect(m_deviceBus, &DeviceBusWidget::deviceSelectionChanged, this, [this]() {
            m_deviceCombo->blockSignals(true);
            // 重填前保存当前选中设备，重填后恢复——否则 clear() 后 currentIndex 回 0，
            // 用户选中的非首台设备被静默切回首台 → configChanged=true 触发断连重建。
            // 恢复操作同样在 blockSignals 内，不触发 currentTextChanged（I-2）
            const QString prev = m_deviceCombo->currentText();
            m_deviceCombo->clear();
            for (const auto& d : m_deviceBus->allDevices()) {
                m_deviceCombo->addItem(QString::fromStdString(d.ip));
            }
            const int idx = m_deviceCombo->findText(prev);
            if (idx >= 0) m_deviceCombo->setCurrentIndex(idx);
            m_deviceCombo->blockSignals(false);
            populatePanelDeviceCombos();   // 面板源选择器设备列表同步（Task 3）
            // 左面板远程浏览源的目标设备被删除：作废在途连接 + detach 清缓存
            // （对齐右面板 onRefreshRemote 兜底语义；面板选择器列表已由
            // populatePanelDeviceCombos 重填，失效源不再挂载）
            if (m_leftRemoteSource && !m_leftSrcDevice.isEmpty()) {
                bool stillExists = false;
                for (const auto& d : m_deviceBus->allDevices()) {
                    if (QString::fromStdString(d.ip) == m_leftSrcDevice) {
                        stillExists = true;
                        break;
                    }
                }
                if (!stillExists) {
                    ++m_leftConnGeneration;   // 防陈旧回调把已删设备源挂回面板
                    appendLog(QStringLiteral("左侧浏览目标设备 %1 已删除，已断开左侧远程浏览")
                                  .arg(m_leftSrcDevice));
                    detachLeftPanel();
                }
            }
            updateDeployBtnText();
            // 设备添加/删除后自动连接刷新（无设备时 onRefreshRemote 内部提示并置灰状态点；
            // 被删除的设备不在列表中 → 落到首台，onRefreshRemote 按当前文本刷新）
            onRefreshRemote();
        });
    }
}

void FtpDeployWidget::onToolStart()
{
    appendLog("文件部署工具已就绪 — 左侧选择本地文件，点击「部署」上传到右侧当前远程目录");
    emit toolStatusChanged("就绪");
}

void FtpDeployWidget::onToolStop()
{
    appendLog("文件部署工具已停止");
    emit toolStatusChanged("已停止");
}

void FtpDeployWidget::appendLog(const QString& msg)
{
    if (m_globalLogCb) {
        QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
        m_globalLogCb("[" + ts + "] " + msg);
    }
}

std::string FtpDeployWidget::currentProtocol() const
{
    return m_protocolCombo->currentText() == "SFTP" ? "ssh" : "ftp";
}
