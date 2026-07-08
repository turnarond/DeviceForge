/*
 * Copyright (c) 2024 ACOINFO CloudNative Team.
 * All rights reserved.
 *
 * File: NetRelayWidget.cpp
 *
 * Date: 2026-07-08
 *
 * Author: Yan.chaodong <yanchaodong@acoinfo.com>
 *
 * Description: 网络中继调试 Tool 前端实现 — 纯代码构建 UI，
 *              Hex+ASCII 实时视图、会话列表、数据导出。
 */

#include "NetRelayWidget.h"
#include "NetRelayBackend.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QFont>
#include <QHeaderView>
#include <QMessageBox>
#include <QMetaObject>
#include <QColor>

NetRelayWidget::NetRelayWidget(QWidget* parent)
    : ToolWidget(parent)
{
    setupUi();
}

void NetRelayWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // ========== 配置行 ==========
    auto* configGroup = new QGroupBox("中继配置", this);
    auto* configLayout = new QVBoxLayout(configGroup);

    auto* row1 = new QHBoxLayout();
    row1->addWidget(new QLabel("协议:", this));
    m_comboProtocol = new QComboBox(this);
    m_comboProtocol->addItem("TCP");
    m_comboProtocol->addItem("UDP");
    row1->addWidget(m_comboProtocol);

    row1->addSpacing(8);
    row1->addWidget(new QLabel("监听地址:", this));
    m_editListenAddr = new QLineEdit(this);
    m_editListenAddr->setPlaceholderText("127.0.0.1");
    m_editListenAddr->setText("127.0.0.1");
    m_editListenAddr->setFixedWidth(110);
    row1->addWidget(m_editListenAddr);

    row1->addWidget(new QLabel("端口:", this));
    m_spinListenPort = new QSpinBox(this);
    m_spinListenPort->setRange(1, 65535);
    m_spinListenPort->setValue(9000);
    m_spinListenPort->setFixedWidth(70);
    row1->addWidget(m_spinListenPort);

    row1->addSpacing(12);
    auto* arrowLbl = new QLabel("→", this);
    arrowLbl->setStyleSheet("font-size:16px; font-weight:bold; color:#F0A030;");
    row1->addWidget(arrowLbl);

    row1->addWidget(new QLabel("上游地址:", this));
    m_editUpstreamHost = new QLineEdit(this);
    m_editUpstreamHost->setPlaceholderText("192.168.1.100");
    m_editUpstreamHost->setText("192.168.1.100");
    m_editUpstreamHost->setFixedWidth(130);
    row1->addWidget(m_editUpstreamHost);

    row1->addWidget(new QLabel("端口:", this));
    m_spinUpstreamPort = new QSpinBox(this);
    m_spinUpstreamPort->setRange(1, 65535);
    m_spinUpstreamPort->setValue(502);
    m_spinUpstreamPort->setFixedWidth(70);
    row1->addWidget(m_spinUpstreamPort);

    row1->addStretch();
    configLayout->addLayout(row1);

    // ========== 控制按钮行 ==========
    auto* ctrlRow = new QHBoxLayout();
    m_btnStart = new QPushButton("▶ 启动", this);
    m_btnStart->setObjectName("btnPrimary");
    m_btnStop  = new QPushButton("■ 停止", this);
    m_btnStop->setEnabled(false);

    connect(m_btnStart,  &QPushButton::clicked, this, &NetRelayWidget::onStartClicked);
    connect(m_btnStop,   &QPushButton::clicked, this, &NetRelayWidget::onStopClicked);

    ctrlRow->addWidget(m_btnStart);
    ctrlRow->addWidget(m_btnStop);
    ctrlRow->addStretch();
    configLayout->addLayout(ctrlRow);
    mainLayout->addWidget(configGroup);

    // ========== 主区域：会话列表 + Hex 视图（水平分割） ==========
    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);

    // -- 会话列表 --
    auto* sessionGroup = new QGroupBox("会话列表", this);
    auto* sessionLayout = new QVBoxLayout(sessionGroup);
    m_sessionTree = new QTreeWidget(this);
    m_sessionTree->setColumnCount(7);
    m_sessionTree->setHeaderLabels({"ID", "协议", "客户端", "上游", "上行(字节)", "下行(字节)", "状态"});
    m_sessionTree->setRootIsDecorated(false);
    m_sessionTree->setAlternatingRowColors(true);
    m_sessionTree->header()->resizeSection(0, 40);  // ID
    m_sessionTree->header()->resizeSection(1, 40);  // 协议
    m_sessionTree->header()->resizeSection(2, 150); // 客户端
    m_sessionTree->header()->resizeSection(3, 150); // 上游
    m_sessionTree->header()->resizeSection(4, 90);  // 上行
    m_sessionTree->header()->resizeSection(5, 90);  // 下行
    m_sessionTree->header()->resizeSection(6, 50);  // 状态
    sessionLayout->addWidget(m_sessionTree);
    mainSplitter->addWidget(sessionGroup);

    // -- Hex 视图 --
    auto* hexGroup = new QGroupBox("十六进制视图", this);
    auto* hexLayout = new QVBoxLayout(hexGroup);
    m_hexView = new QPlainTextEdit(this);
    m_hexView->setReadOnly(true);
    m_hexView->setMaximumBlockCount(10000);
    QFont monoFont("Consolas", 9);
    monoFont.setStyleHint(QFont::Monospace);
    m_hexView->setFont(monoFont);
    m_hexView->setPlaceholderText("中继数据将在此以 Hex+ASCII 格式实时显示...");
    hexLayout->addWidget(m_hexView);

    auto* hexBtnRow = new QHBoxLayout();
    m_btnExport = new QPushButton("⬇ 导出", this);
    m_btnClear  = new QPushButton("🗑 清空", this);
    QObject::connect(m_btnExport, &QPushButton::clicked, this, &NetRelayWidget::onExportClicked);
    QObject::connect(m_btnClear,  &QPushButton::clicked, this, &NetRelayWidget::onClearClicked);
    hexBtnRow->addStretch();
    hexBtnRow->addWidget(m_btnExport);
    hexBtnRow->addWidget(m_btnClear);
    hexLayout->addLayout(hexBtnRow);
    mainSplitter->addWidget(hexGroup);

    mainSplitter->setSizes(QList<int>() << 380 << 600);
    mainLayout->addWidget(mainSplitter, 1);

    // ========== 日志区 ==========
    auto* logGroup = new QGroupBox("日志", this);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setFont(monoFont);
    m_logView->setFixedHeight(80);
    logLayout->addWidget(m_logView);
    mainLayout->addWidget(logGroup);
}

void NetRelayWidget::setBackend(NetRelayBackend* backend)
{
    m_backend = backend;
    if (!m_backend) return;

    m_backend->setLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            appendLog(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    });

    m_backend->setErrorCallback([this](const std::string& err) {
        QMetaObject::invokeMethod(this, [this, err]() {
            appendLog(QString::fromStdString("[错误] " + err));
            m_btnStart->setEnabled(true);
            m_btnStop->setEnabled(false);
        }, Qt::QueuedConnection);
    });

    m_backend->setDataCallback([this](RelayDirection dir, const QString& peer, int sessionId, const QByteArray& data) {
        QMetaObject::invokeMethod(this, [this, dir, peer, sessionId, data]() {
            appendHexView(dir, peer, sessionId, data);
        }, Qt::QueuedConnection);
    });

    m_backend->setSessionCallback([this](const RelaySession& session) {
        QMetaObject::invokeMethod(this, [this, session]() {
            updateSession(session);
        }, Qt::QueuedConnection);
    });
}

// ============ ToolWidget 生命周期 ============

void NetRelayWidget::onToolStart()
{
    appendLog("网络中继工具已就绪");
    emit toolStatusChanged("就绪");
}

void NetRelayWidget::onToolStop()
{
    appendLog("网络中继工具已停止");
    emit toolStatusChanged("已停止");
}

// ============ 按钮槽 ============

void NetRelayWidget::onStartClicked()
{
    if (!m_backend) {
        QMessageBox::warning(this, "错误", "Backend 未就绪");
        return;
    }
    if (m_backend->isRunning()) {
        QMessageBox::warning(this, "警告", "中继已在运行中");
        return;
    }

    RelayProtocol proto = (m_comboProtocol->currentIndex() == 0)
                          ? RelayProtocol::Tcp : RelayProtocol::Udp;
    QString listenAddr = m_editListenAddr->text().trimmed();
    quint16 listenPort  = static_cast<quint16>(m_spinListenPort->value());
    QString upstreamHost = m_editUpstreamHost->text().trimmed();
    quint16 upstreamPort  = static_cast<quint16>(m_spinUpstreamPort->value());

    if (upstreamHost.isEmpty() || upstreamPort == 0) {
        QMessageBox::warning(this, "警告", "请输入有效的上游地址和端口");
        return;
    }

    m_sessionTree->clear();

    m_backend->startRelay(proto, listenAddr, listenPort, upstreamHost, upstreamPort);

    m_btnStart->setEnabled(false);
    m_btnStop->setEnabled(true);
    m_comboProtocol->setEnabled(false);
    m_editListenAddr->setEnabled(false);
    m_spinListenPort->setEnabled(false);
    m_editUpstreamHost->setEnabled(false);
    m_spinUpstreamPort->setEnabled(false);
    emit toolStatusChanged("运行中");
}

void NetRelayWidget::onStopClicked()
{
    if (!m_backend) return;

    // 幂等停止：即使 isRunning()==false（如 DNS 未决/启动失败），也调用 stopRelay
    // 并复位 UI，避免按钮状态与后端状态不一致。
    m_backend->stopRelay();

    m_btnStart->setEnabled(true);
    m_btnStop->setEnabled(false);
    m_comboProtocol->setEnabled(true);
    m_editListenAddr->setEnabled(true);
    m_spinListenPort->setEnabled(true);
    m_editUpstreamHost->setEnabled(true);
    m_spinUpstreamPort->setEnabled(true);
    emit toolStatusChanged("已停止");
}

void NetRelayWidget::onExportClicked()
{
    if (m_exportBuffer.isEmpty()) {
        QMessageBox::information(this, "提示", "没有可导出的数据");
        return;
    }
    // 导出内容为截获的原始流量，可能含明文凭证/敏感数据，导出前提醒
    auto reply = QMessageBox::warning(this, "导出确认",
        "导出文件将包含截获的原始网络流量，可能含有明文凭证或敏感信息。\n"
        "请妥善保管导出文件。是否继续？",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    QString fileName = QFileDialog::getSaveFileName(this, "导出中继数据",
        "relay_dump_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".hex",
        "十六进制文件 (*.hex);;文本文件 (*.txt);;所有文件 (*.*)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件: " + file.errorString());
        return;
    }
    // 限制文件权限为仅所有者可读写（避免同机其他用户读取敏感捕获）
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QTextStream stream(&file);
    stream << m_exportBuffer;
    file.close();
    appendLog("数据已导出到: " + fileName);
}

void NetRelayWidget::onClearClicked()
{
    m_hexView->clear();
    m_exportBuffer.clear();
    m_sessionTree->clear();
    appendLog("数据已清空");
}

// ============ 日志 ============

void NetRelayWidget::appendLog(const QString& msg)
{
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    m_logView->appendPlainText("[" + ts + "] " + msg);
}

// ============ 十六进制视图 ============

void NetRelayWidget::appendHexView(RelayDirection dir, const QString& peer, int sessionId, const QByteArray& data)
{
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString dirMarker = (dir == RelayDirection::Upstream) ? "← 上行" : "→ 下行";

    // 单块渲染上限：超大数据块只渲染前 N 字节，避免 UI 内存尖峰/卡顿
    QByteArray shown = data;
    bool truncated = false;
    if (shown.size() > kMaxHexRenderBytes) {
        shown = shown.left(kMaxHexRenderBytes);
        truncated = true;
    }

    QString header = QString("[%1] %2 %3 [会话#%4] (%5 字节)\n")
                     .arg(ts, dirMarker, peer).arg(sessionId).arg(data.size());

    QString hexBody = formatHexDump(shown);
    if (truncated) {
        hexBody += QString("... （仅显示前 %1 字节，共 %2 字节）\n")
                   .arg(kMaxHexRenderBytes).arg(data.size());
    }
    QString block = header + hexBody + "\n";

    m_hexView->appendPlainText(block.trimmed());

    // 维护导出缓冲区（带大小上限保护，追加后截断到上限）
    if (m_exportBuffer.size() < kMaxExportChars) {
        m_exportBuffer += block;
        if (m_exportBuffer.size() > kMaxExportChars)
            m_exportBuffer.resize(static_cast<int>(kMaxExportChars));
    }
}

QString NetRelayWidget::formatHexDump(const QByteArray& data)
{
    QString result;
    const int bytesPerLine = 16;
    int totalLen = data.size();

    for (int offset = 0; offset < totalLen; offset += bytesPerLine) {
        // 偏移列
        result += QString("%1  ").arg(offset, 8, 16, QChar('0')).toUpper();

        // Hex 列
        QString hexPart;
        QString asciiPart;
        for (int i = 0; i < bytesPerLine; ++i) {
            int pos = offset + i;
            if (pos < totalLen) {
                unsigned char c = static_cast<unsigned char>(data[pos]);
                hexPart += QString("%1 ").arg(c, 2, 16, QChar('0')).toUpper();
                asciiPart += (c >= 0x20 && c <= 0x7E) ? QChar(c) : QChar('.');
            } else {
                hexPart += "   ";
                asciiPart += " ";
            }
            if (i == 7) hexPart += " ";  // 中间分隔
        }

        result += hexPart + " |" + asciiPart + "|\n";
    }
    return result;
}

// ============ 会话列表更新 ============

void NetRelayWidget::updateSession(const RelaySession& session)
{
    // 查找已有项（按 session id）
    QTreeWidgetItem* item = nullptr;
    bool isNew = false;
    for (int i = 0; i < m_sessionTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* candidate = m_sessionTree->topLevelItem(i);
        if (candidate->data(0, Qt::UserRole).toInt() == session.id) {
            item = candidate;
            break;
        }
    }

    if (!item) {
        item = new QTreeWidgetItem();
        item->setData(0, Qt::UserRole, session.id);
        m_sessionTree->insertTopLevelItem(0, item);
        isNew = true;
    }

    item->setText(0, QString::number(session.id));
    item->setText(1, (session.protocol == RelayProtocol::Tcp) ? "TCP" : "UDP");
    item->setText(2, session.clientAddr);
    item->setText(3, session.upstreamAddr);
    item->setText(4, QString::number(session.bytesUp));
    item->setText(5, QString::number(session.bytesDown));

    if (session.active) {
        item->setText(6, "活跃");
        item->setForeground(6, QColor("#40C8A0"));
    } else {
        item->setText(6, "已关闭");
        item->setForeground(6, QColor("#7B8494"));
    }

    // 仅在新会话项首次创建时滚动到顶部，避免高频更新时破坏用户浏览
    if (isNew) {
        m_sessionTree->scrollToTop();
    }
}
