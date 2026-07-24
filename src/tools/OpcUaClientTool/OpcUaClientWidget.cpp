/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: OpcUaClientWidget.cpp
 *
 * Date: 2026-07-11 / 2026-07-13 重构
 *
 * Author: turnarond
 *
 * Description: OPC UA 客户端 Tool 前端实现 — 四面板布局（已重构）：
 *   ① 连接配置（压缩为两行） ② 地址空间浏览（左主舞台，扁平表格：
 *      #/显示名/NodeId/类型 + 搜索 + 计数 + 排序）
 *   ③ 读/写 + 订阅（右侧操作栏） ④ 日志（缩小）。
 */

#include "OpcUaClientWidget.h"
#include "OpcUaClientBackend.h"
#include "config/ConfigStore.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QScrollBar>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

// ============================================================
// 辅助：OPC UA 类型映射
// ============================================================

// OPC UA 内置类型 NodeId → 友好名
// 处理 "i=24" / "ns=0;i=24" / "ns=2;i=3001" 三种格式
static QString dataTypeIdToName(const QString& nodeId)
{
    bool ok = false;
    int id = -1;
    if (nodeId.startsWith("ns=0;i=")) {
        id = nodeId.mid(7).toInt(&ok);
    } else if (nodeId.startsWith("i=")) {
        id = nodeId.mid(2).toInt(&ok);
    } else if (nodeId.contains("i=")) {
        id = nodeId.section("i=", -1).toInt(&ok);
    }
    if (ok) {
        switch (id) {
        case  1: return "Boolean";
        case  2: return "SByte";
        case  3: return "Byte";
        case  4: return "Int16";
        case  5: return "UInt16";
        case  6: return "Int32";
        case  7: return "UInt32";
        case  8: return "Int64";
        case  9: return "UInt64";
        case 10: return "Float";
        case 11: return "Double";
        case 12: return "String";
        case 13: return "DateTime";
        case 14: return "UInt8";
        case 15: return "ByteString";
        case 16: return "XmlElement";
        case 17: return "NodeId";
        case 18: return "ExpandedNodeId";
        case 22: return "StatusCode";
        case 24: return "Int32";     // 常见 Int32 别名
        default: break;
        }
    }
    return nodeId;
}

// nodeClass 数字 → 友好名
static QString nodeClassToName(int nodeClass)
{
    switch (nodeClass) {
    case 1:  return "Object";
    case 2:  return "Variable";
    case 4:  return "Method";
    case 8:  return "Folder";
    case 16: return "View";
    case 32: return "ObjectType";
    case 64: return "VariableType";
    default:  return QString::number(nodeClass);
    }
}

// nodeClass → QSS 背景色（工业仪表盘色板）
static QString nodeClassToBackground(const QString& className)
{
    if (className == "Variable") return "#1a3a30";
    if (className == "Object")   return "#1e2030";
    if (className == "Folder")   return "#2a2520";
    if (className == "Method")   return "#2a2010";
    return "transparent";
}

OpcUaClientWidget::OpcUaClientWidget(QWidget* parent) : ToolWidget(parent)
{
    setupUi();

    for (const auto& it : ConfigStore::instance().list("opcua.endpoint", 20)) {
        const QString url = it["url"].toString();
        if (m_endpointEdit->findText(url) < 0)
            m_endpointEdit->addItem(url);
    }
}

// ============================================================
// setupUi — 编程式布局（无 .ui 文件）
// ============================================================

void OpcUaClientWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(4);

    // ---------- 面板 1：连接配置（压为两行） ----------
    auto* connGroup = new QGroupBox("连接配置", this);
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setContentsMargins(8, 4, 8, 4);
    connLayout->setSpacing(4);

    // 行 1：Endpoint + 连接按钮
    auto* row1 = new QHBoxLayout();
    row1->addWidget(new QLabel("Endpoint:", this));
    m_endpointEdit = new QComboBox(this);
    m_endpointEdit->setEditable(true);
    m_endpointEdit->setMinimumWidth(280);
    m_endpointEdit->lineEdit()->setPlaceholderText("opc.tcp://192.168.1.10:4840");
    row1->addWidget(m_endpointEdit, 1);
    m_connectBtn = new QPushButton("连接", this);
    row1->addWidget(m_connectBtn);
    connLayout->addLayout(row1);

    // 行 2：安全策略 / 认证 / 状态
    auto* row2 = new QHBoxLayout();
    row2->addWidget(new QLabel("安全策略:", this));
    m_securityPolicyCombo = new QComboBox(this);
    m_securityPolicyCombo->addItem("None");
    row2->addWidget(m_securityPolicyCombo);
    row2->addSpacing(12);
    row2->addWidget(new QLabel("认证:", this));
    m_authCombo = new QComboBox(this);
    m_authCombo->addItem("匿名");
    row2->addWidget(m_authCombo);
    row2->addSpacing(12);
    row2->addWidget(new QLabel("状态:", this));
    m_statusLabel = new QLabel("○ 未连接", this);
    row2->addWidget(m_statusLabel);
    row2->addStretch();
    connLayout->addLayout(row2);
    mainLayout->addWidget(connGroup);

    // ---------- 面板 2 + 3：地址空间（左主舞台）| 操作区（右） ----------
    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);

    // ===== 面板 2：地址空间浏览（左，stretch 3） =====
    auto* browseGroup = new QGroupBox("地址空间浏览", this);
    auto* browseLayout = new QVBoxLayout(browseGroup);
    browseLayout->setContentsMargins(4, 4, 4, 4);
    browseLayout->setSpacing(4);

    // 顶行：[浏览] 搜索框  计数
    auto* browseTop = new QHBoxLayout();
    m_browseBtn = new QPushButton("浏览", this);
    browseTop->addWidget(m_browseBtn);
    m_browseSearch = new QLineEdit(this);
    m_browseSearch->setPlaceholderText("搜索 显示名 / NodeId ...");
    m_browseSearch->setClearButtonEnabled(true);
    browseTop->addWidget(m_browseSearch, 1);
    m_browseCount = new QLabel("共 0 项", this);
    browseTop->addWidget(m_browseCount);
    browseLayout->addLayout(browseTop);

    // 扁平表格：# / 显示名 / NodeId / 数据类型 / 节点类
    m_browseTable = new QTableWidget(0, 5, this);
    m_browseTable->setHorizontalHeaderLabels({"#", "显示名", "NodeId", "数据类型", "节点类"});
    m_browseTable->setAlternatingRowColors(true);
    m_browseTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_browseTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_browseTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_browseTable->setSortingEnabled(true);
    m_browseTable->verticalHeader()->setVisible(false); // # 列代替行号

    // 列宽：#(40) / 显示名(200固定) / NodeId(自适应) / 数据类型(90) / 节点类(90)
    m_browseTable->horizontalHeader()->setStretchLastSection(false);
    m_browseTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_browseTable->horizontalHeader()->resizeSection(0, 40);
    m_browseTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_browseTable->horizontalHeader()->resizeSection(1, 200);
    m_browseTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_browseTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_browseTable->horizontalHeader()->resizeSection(3, 90);
    m_browseTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_browseTable->horizontalHeader()->resizeSection(4, 90);
    browseLayout->addWidget(m_browseTable, 1);
    mainSplitter->addWidget(browseGroup);

    // ===== 面板 3 + 4：读/写 + 订阅（右，stretch 2，垂直排布） =====
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);

    // 面板 3：读 / 写
    auto* rwGroup = new QGroupBox("读 / 写", this);
    auto* rwLayout = new QVBoxLayout(rwGroup);
    rwLayout->setContentsMargins(4, 4, 4, 4);
    rwLayout->setSpacing(4);

    auto* nodeRow = new QHBoxLayout();
    nodeRow->addWidget(new QLabel("NodeId:", this));
    m_nodeIdEdit = new QLineEdit(this);
    m_nodeIdEdit->setPlaceholderText("ns=2;s=Demo.Static.Scalar.Double");
    nodeRow->addWidget(m_nodeIdEdit, 1);
    rwLayout->addLayout(nodeRow);

    auto* valRow = new QHBoxLayout();
    valRow->addWidget(new QLabel("值:", this));
    m_valueEdit = new QLineEdit(this);
    valRow->addWidget(m_valueEdit, 1);
    rwLayout->addLayout(valRow);

    auto* btnRow = new QHBoxLayout();
    m_readBtn = new QPushButton("读", this);
    m_writeBtn = new QPushButton("写", this);
    btnRow->addWidget(m_readBtn);
    btnRow->addWidget(m_writeBtn);
    btnRow->addStretch();
    rwLayout->addLayout(btnRow);

    m_batchTable = new QTableWidget(0, 4, this);
    m_batchTable->setHorizontalHeaderLabels({"NodeId", "值", "质量", ""});
    m_batchTable->setAlternatingRowColors(true);
    m_batchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_batchTable->horizontalHeader()->setStretchLastSection(true);
    m_batchTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_batchTable->horizontalHeader()->resizeSection(3, 28);
    rwLayout->addWidget(m_batchTable, 1);
    rightLayout->addWidget(rwGroup, 1);

    // 面板 4：订阅
    auto* subGroup = new QGroupBox("订阅", this);
    auto* subLayout = new QVBoxLayout(subGroup);
    subLayout->setContentsMargins(4, 4, 4, 4);
    subLayout->setSpacing(4);
    auto* subBtnRow = new QHBoxLayout();
    m_subscribeBtn = new QPushButton("订阅", this);
    m_subscribeBtn->setCheckable(true);
    subBtnRow->addWidget(m_subscribeBtn);
    subBtnRow->addStretch();
    subLayout->addLayout(subBtnRow);

    m_subscriptionTable = new QTableWidget(0, 4, this);
    m_subscriptionTable->setHorizontalHeaderLabels({"NodeId", "最新值", "时间戳", ""});
    m_subscriptionTable->setAlternatingRowColors(true);
    m_subscriptionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_subscriptionTable->horizontalHeader()->setStretchLastSection(true);
    m_subscriptionTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_subscriptionTable->horizontalHeader()->resizeSection(3, 28);
    subLayout->addWidget(m_subscriptionTable, 1);
    rightLayout->addWidget(subGroup, 1);
    mainSplitter->addWidget(rightWidget);

    // stretch 比例：地址空间 3, 操作区 2
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 2);
    mainLayout->addWidget(mainSplitter, 2);

    // ---------- 日志 ----------
    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(80);
    mainLayout->addWidget(m_logView);

    // ---------- 信号连接 ----------
    connect(m_connectBtn,    &QPushButton::clicked,       this, &OpcUaClientWidget::onConnectClicked);
    connect(m_browseBtn,     &QPushButton::clicked,       this, &OpcUaClientWidget::onBrowseClicked);
    connect(m_browseSearch,  &QLineEdit::textChanged,     this, &OpcUaClientWidget::onBrowseSearchChanged);
    connect(m_browseTable,   &QTableWidget::cellActivated, this, &OpcUaClientWidget::onBrowseRowActivated);
    connect(m_readBtn,       &QPushButton::clicked,       this, &OpcUaClientWidget::onReadClicked);
    connect(m_writeBtn,      &QPushButton::clicked,       this, &OpcUaClientWidget::onWriteClicked);
    connect(m_subscribeBtn,  &QPushButton::clicked,       this, &OpcUaClientWidget::onSubscribeClicked);

    // 列3(cellWidget)是×删除按钮
    // × 删除按钮在 setCellWidget 上，cellClicked 信号不可靠——
    // 按钮自己的 clicked 会消费事件，cellClicked 不再触发
    // (row 索引会因前面 removeRow 而错位，故在点击时按几何反查行号)

    updateConnectionStatus(false);
}

// ============================================================
// Backend 注入 + 回调编组
// ============================================================

void OpcUaClientWidget::setBackend(OpcUaClientBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;

    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setConnectionCallback([this](bool connected) {
        QMetaObject::invokeMethod(this, [this, connected]() {
            updateConnectionStatus(connected);
            if (connected) {
                const QString endpoint = m_endpointEdit->currentText().trimmed();
                if (m_endpointEdit->findText(endpoint) < 0)
                    m_endpointEdit->addItem(endpoint);
                // ConfigStore 时间戳统一用毫秒（与 DeviceBusWidget / ConfigStore 测试一致）
                const QVariantMap value{{"url", endpoint},
                                        {"updated_at", QDateTime::currentMSecsSinceEpoch()}};
                ConfigStore::instance().save("opcua.endpoint", endpoint, value);
            }
        }, Qt::QueuedConnection);
    });

    m_backend->setDataChangeCallback([this](const QString& nodeId, const QVariant& value,
                                            quint64 ts, const QString& quality) {
        QMetaObject::invokeMethod(this, [this, nodeId, value, ts, quality]() {
            const QString valStr = value.toString();
            const QString tsStr  = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ts))
                                       .toString("hh:mm:ss");

            // 更新批量表
            bool found = false;
            for (int r = 0; r < m_batchTable->rowCount(); ++r) {
                if (m_batchTable->item(r, 0) && m_batchTable->item(r, 0)->text() == nodeId) {
                    m_batchTable->item(r, 1)->setText(valStr);
                    m_batchTable->item(r, 2)->setText(quality);
                    found = true;
                    break;
                }
            }
            if (!found) {
                int row = m_batchTable->rowCount();
                m_batchTable->insertRow(row);
                m_batchTable->setItem(row, 0, new QTableWidgetItem(nodeId));
                m_batchTable->setItem(row, 1, new QTableWidgetItem(valStr));
                m_batchTable->setItem(row, 2, new QTableWidgetItem(quality));
                auto* delBtn = new QPushButton("×");
                delBtn->setFixedSize(24, 24);
                delBtn->setCursor(Qt::PointingHandCursor);
                connect(delBtn, &QPushButton::clicked, this, [this, tbl=m_batchTable, btn=delBtn]() {
                    QPoint p = btn->parentWidget()->mapTo(tbl->viewport(), btn->pos());
                    int r = tbl->rowAt(p.y());
                    if (r >= 0) onRemoveBatchRow(r);
                });
                m_batchTable->setCellWidget(row, 3, delBtn);
            }

            // 更新订阅表
            for (int r = 0; r < m_subscriptionTable->rowCount(); ++r) {
                if (m_subscriptionTable->item(r, 0) &&
                    m_subscriptionTable->item(r, 0)->text() == nodeId) {
                    m_subscriptionTable->item(r, 1)->setText(valStr);
                    m_subscriptionTable->item(r, 2)->setText(tsStr);
                    break;
                }
            }
        }, Qt::QueuedConnection);
    });
}

// ============================================================
// 生命周期
// ============================================================

void OpcUaClientWidget::onToolStart() { appendLog("OPC UA 客户端已就绪"); }

void OpcUaClientWidget::onToolStop()
{
    appendLog("OPC UA 客户端已停止");
}

// ============================================================
// 槽函数 — 连接
// ============================================================

void OpcUaClientWidget::onConnectClicked()
{
    if (!m_backend) return;
    if (m_connected) {
        m_backend->disconnectFromServer();
    } else {
        const QString endpoint = m_endpointEdit->currentText().trimmed();
        if (endpoint.isEmpty()) {
            appendLog("请填写 Endpoint 地址");
            return;
        }
        m_backend->connectToServer(endpoint);
    }
}

// ============================================================
// 槽函数 — 地址空间浏览 + 搜索
// ============================================================

void OpcUaClientWidget::onBrowseClicked()
{
    if (!m_backend) return;
    const QString json = m_backend->browseAddressSpace();
    populateBrowseTable(json);
}

void OpcUaClientWidget::onBrowseSearchChanged(const QString& text)
{
    const QString filter = text.trimmed();
    int visibleCount = 0;
    for (int r = 0; r < m_browseTable->rowCount(); ++r) {
        if (filter.isEmpty()) {
            m_browseTable->setRowHidden(r, false);
            ++visibleCount;
            continue;
        }
        bool match = false;
        for (int c = 0; c < m_browseTable->columnCount(); ++c) {
            QTableWidgetItem* it = m_browseTable->item(r, c);
            if (it && it->text().contains(filter, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        m_browseTable->setRowHidden(r, !match);
        if (match) ++visibleCount;
    }
    if (filter.isEmpty())
        m_browseCount->setText(QString("共 %1 项").arg(m_browseTotal));
    else
        m_browseCount->setText(QString("共 %1 项 · 匹配 %2").arg(m_browseTotal).arg(visibleCount));
}

void OpcUaClientWidget::onBrowseRowActivated(int row, int /*column*/)
{
    // col1=显示名 col2=NodeId
    QTableWidgetItem* nameItem = m_browseTable->item(row, 1);
    QTableWidgetItem* nidItem  = m_browseTable->item(row, 2);
    if (nidItem) {
        m_nodeIdEdit->setText(nidItem->text());
        appendLog(QString("选中: %1 (%2)").arg(nameItem ? nameItem->text() : "", nidItem->text()));
    }
}

void OpcUaClientWidget::populateBrowseTable(const QString& json)
{
    m_browseTable->setSortingEnabled(false); // 填表期间禁排序
    m_browseTable->setRowCount(0);
    m_browseTable->verticalScrollBar()->setSingleStep(20); // 改善滚动手感
    m_browseSearch->clear();
    m_browseTotal = 0;

    if (json.isEmpty()) {
        m_browseCount->setText("共 0 项");
        m_browseTable->setSortingEnabled(true);
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        appendLog("地址空间 JSON 解析失败: " + err.errorString());
        m_browseCount->setText("共 0 项");
        m_browseTable->setSortingEnabled(true);
        return;
    }

    const QJsonArray arr = doc.array();
    m_browseTotal = arr.size();
    m_browseTable->setRowCount(m_browseTotal);

    const int BATCH = 100; // 每 100 行刷新一次 viewport（防卡顿）
    for (int i = 0; i < m_browseTotal; ++i) {
        const QJsonObject obj = arr[i].toObject();
        const QString nodeId      = obj.value("nodeId").toString();
        const QString displayName = obj.value("displayName").toString();
        const QString browseName  = obj.value("browseName").toString();
        const QString rawDataType  = obj.value("dataType").toString();
        const int nodeClass       = obj.value("nodeClass").toInt(-1);

        const QString label = displayName.isEmpty() ? browseName : displayName;
        const QString typeName = dataTypeIdToName(rawDataType);
        const QString className = nodeClassToName(nodeClass);
        const QString classBg   = nodeClassToBackground(className);

        // 列0: #
        m_browseTable->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));

        // 列1: 显示名
        auto* nameItem = new QTableWidgetItem(label.isEmpty() ? nodeId : label);
        nameItem->setToolTip(nodeId);
        m_browseTable->setItem(i, 1, nameItem);

        // 列2: NodeId
        auto* nidItem = new QTableWidgetItem(nodeId);
        nidItem->setToolTip(nodeId);
        m_browseTable->setItem(i, 2, nidItem);

        // 列3: 数据类型（友好名）
        m_browseTable->setItem(i, 3, new QTableWidgetItem(typeName));

        // 列4: 节点类（色块背景）
        auto* classItem = new QTableWidgetItem(className);
        classItem->setBackground(QBrush(QColor(classBg)));
        classItem->setForeground(QBrush(QColor("#C8CCD4")));
        m_browseTable->setItem(i, 4, classItem);

        // 分批渲染：每 BATCH 行刷新一次可见区域
        if ((i + 1) % BATCH == 0)
            m_browseTable->viewport()->update();
    }

    m_browseTable->viewport()->update(); // 最后一批
    m_browseCount->setText(QString("共 %1 项").arg(m_browseTotal));
    m_browseTable->setSortingEnabled(true);
    appendLog(QString("地址空间浏览完成，共 %1 个节点").arg(m_browseTotal));
}

// ============================================================
// 槽函数 — 读 / 写
// ============================================================

void OpcUaClientWidget::onReadClicked()
{
    if (!m_backend) return;
    const QString nodeId = m_nodeIdEdit->text().trimmed();
    if (nodeId.isEmpty()) {
        appendLog("请填写要读取的 NodeId");
        return;
    }
    m_backend->readNodes(QStringList{nodeId});
}

void OpcUaClientWidget::onWriteClicked()
{
    if (!m_backend) return;
    const QString nodeId = m_nodeIdEdit->text().trimmed();
    if (nodeId.isEmpty()) {
        appendLog("请填写要写入的 NodeId");
        return;
    }
    // 值类型推断
    const QString text = m_valueEdit->text().trimmed();
    const QString low = text.toLower();
    QVariant value;
    bool okI = false, okD = false;
    const qlonglong asInt = text.toLongLong(&okI);
    const double asDouble = text.toDouble(&okD);
    if (low == "true" || low == "false") {
        value = (low == "true");
    } else if (okI) {
        value = asInt;
    } else if (okD) {
        value = asDouble;
    } else {
        value = m_valueEdit->text();
    }
    m_backend->writeNodes(QStringList{nodeId}, QVariantList{value});
}

// ============================================================
// 槽函数 — 订阅
// ============================================================

void OpcUaClientWidget::onSubscribeClicked()
{
    if (!m_backend) return;
    if (m_subscribeBtn->isChecked()) {
        const QString nodeId = m_nodeIdEdit->text().trimmed();
        if (nodeId.isEmpty()) {
            appendLog("请先填写要订阅的 NodeId");
            m_subscribeBtn->setChecked(false);
            return;
        }
        upsertSubscriptionRow(nodeId);
        m_backend->subscribeNodes(QStringList{nodeId});
        m_subscribeBtn->setText("停止");
    } else {
        m_subscribeBtn->setText("订阅");
        m_backend->unsubscribeAll();
        appendLog("订阅已停止");
    }
}

void OpcUaClientWidget::upsertSubscriptionRow(const QString& nodeId)
{
    for (int r = 0; r < m_subscriptionTable->rowCount(); ++r) {
        if (m_subscriptionTable->item(r, 0) && m_subscriptionTable->item(r, 0)->text() == nodeId)
            return; // 已存在
    }
    int row = m_subscriptionTable->rowCount();
    m_subscriptionTable->insertRow(row);
    m_subscriptionTable->setItem(row, 0, new QTableWidgetItem(nodeId));
    m_subscriptionTable->setItem(row, 1, new QTableWidgetItem("—"));
    m_subscriptionTable->setItem(row, 2, new QTableWidgetItem("—"));
    auto* delBtn = new QPushButton("×");
    delBtn->setFixedSize(24, 24);
    delBtn->setCursor(Qt::PointingHandCursor);
    connect(delBtn, &QPushButton::clicked, this, [this, tbl=m_subscriptionTable, btn=delBtn]() {
        QPoint p = btn->parentWidget()->mapTo(tbl->viewport(), btn->pos());
        int r = tbl->rowAt(p.y());
        if (r >= 0) onRemoveSubscriptionRow(r);
    });
    m_subscriptionTable->setCellWidget(row, 3, delBtn);
}

// ============================================================
// 辅助：删除按钮
// ============================================================

void OpcUaClientWidget::onRemoveBatchRow(int row)
{
    m_batchTable->removeRow(row);
}

void OpcUaClientWidget::onRemoveSubscriptionRow(int row)
{
    m_subscriptionTable->removeRow(row);
}

// ============================================================
// 辅助方法
// ============================================================

void OpcUaClientWidget::updateConnectionStatus(bool connected)
{
    m_connected = connected;
    QPalette pal = m_statusLabel->palette();
    if (connected) {
        m_statusLabel->setText("● 已连接");
        pal.setColor(QPalette::WindowText, QColor("#40C8A0")); // 青绿，已连接态
        m_connectBtn->setText("断开");
    } else {
        m_statusLabel->setText("○ 未连接");
        pal.setColor(QPalette::WindowText, QColor("#C8CCD4")); // 主文字色，未连接态
        m_connectBtn->setText("连接");
        if (m_subscribeBtn) { m_subscribeBtn->setChecked(false); m_subscribeBtn->setText("订阅"); }
    }
    m_statusLabel->setPalette(pal);
}

void OpcUaClientWidget::appendLog(const QString& msg)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logView->append("[" + ts + "] " + msg);
}
