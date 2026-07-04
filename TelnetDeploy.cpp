#include "TelnetDeploy.h"

#include "ui_tab_telnet.h"
#include "DeployMaster.h" // 访问 deviceBusWidget()->allDevices()

#include "TelnetClient.h"

#include <QTreeWidgetItem>
#include <QDateTimeEdit>
#include <QtConcurrent/QtConcurrent>
#include <QMessageBox>
#include <QMetaObject>
#include <QFile>
#include <QTextStream>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QThread>

TelnetDeploy::TelnetDeploy(DeployMaster* parentWindow, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::TabTelnetDeploy)
    , m_mainWindow(parentWindow)
{
    ui->setupUi(this);
    txt_globalLog = m_mainWindow->getGlobalLogItem();

    // 连接按钮信号
    connect(ui->btn_executeTelnet, &QPushButton::clicked, this, &TelnetDeploy::on_ExecuteTelnetCmdClicked);
    connect(ui->btn_stopTelnet, &QPushButton::clicked, this, &TelnetDeploy::on_StopTelnetClicked);
    connect(ui->tree_telnetResults, &QTreeWidget::itemDoubleClicked, this, &TelnetDeploy::onTreeItemDoubleClicked);

    // 复制/保存按钮
    connect(ui->btn_copyOutput, &QPushButton::clicked, this, [this]() {
        ui->txt_outputDetail->selectAll();
        ui->txt_outputDetail->copy();
    });
    connect(ui->btn_saveOutput, &QPushButton::clicked, this, [this]() {
        QString txt = ui->txt_outputDetail->toPlainText();
        QString path = QFileDialog::getSaveFileName(this, tr("保存输出到文件"), QString(), tr("Text Files (*.txt);;All Files (*)"));
        if (!path.isEmpty()) {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QTextStream ts(&f);
                ts << txt;
                f.close();
            }
        }
    });

    // export all results
    connect(ui->btn_exportAllResults, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("导出所有结果"), QString(), tr("Text Files (*.txt);;All Files (*)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        QTextStream ts(&f);
        for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
            ts << "==== " << it.key() << " ====\n" << it.value() << "\n\n";
        }
        f.close();
    });
}

TelnetDeploy::~TelnetDeploy()
{
    delete ui;
}

void TelnetDeploy::appendLog(const QString& log)
{
}

QStringList TelnetDeploy::getTargetIPs()
{
    if (!m_mainWindow) return {};
    return m_mainWindow->getTargetIPList();
}

void TelnetDeploy::on_ExecuteTelnetCmdClicked()
{
    if (m_running.load()) {
        QMessageBox::warning(this, tr("正在执行"), tr("已有任务正在运行，请先停止或等待完成。"));
        return;
    }

    QStringList ips = getTargetIPs();
    if (ips.isEmpty()) {
        QMessageBox::warning(this, tr("未设置目标"), tr("请在主窗口的目标 IP 列表中添加至少一个 IP。"));
        return;
    }

    QString commands = ui->txt_telnetCommands->toPlainText().trimmed();
    if (commands.isEmpty()) {
        QMessageBox::warning(this, tr("无命令"), tr("请在命令编辑区输入要执行的命令。"));
        return;
    }

    ui->btn_executeTelnet->setEnabled(false);
    ui->btn_stopTelnet->setEnabled(true);
    ui->tree_telnetResults->clear();
    m_outputs.clear();
    m_stopRequested.store(false);
    m_running.store(true);
    m_pendingTasks.store(static_cast<int>(ips.size()));

    // populate tree
    for (const QString& ip : ips) {
        QTreeWidgetItem* it = new QTreeWidgetItem(ui->tree_telnetResults);
        it->setText(0, ip);
        it->setText(1, tr("等待执行"));
        it->setText(2, "0");
        it->setText(3, "");
        ui->tree_telnetResults->addTopLevelItem(it);
    }

    // split commands into lines
    QStringList cmdLines = commands.split('\n', Qt::SkipEmptyParts);

    // run concurrent tasks sequentially per IP but in a separate thread pool (use QtConcurrent::run for each IP)
    for (const QString& ip : ips) {
        QtConcurrent::run([this, ip, cmdLines]() {
            QElapsedTimer timer; timer.start();
            bool success = true;

            TelnetClient client;
            // collect last error from client
            QString lastError;
            connect(&client, &TelnetClient::error, this, [&lastError](const QString& e) { lastError = e; });

            // connect live data
            connect(&client, &TelnetClient::dataReceived, this, [this, ip](const QString& data) {
                // append per-ip output via queued call
                QMetaObject::invokeMethod(this, "onTelnetDataReceived", Qt::QueuedConnection,
                    Q_ARG(QString, ip), Q_ARG(QString, data));
            });

            bool connected = client.syncConnectToHost(ip, 23, 3000);
            if (!connected) {
                QMetaObject::invokeMethod(this, "updateItemStatus", Qt::QueuedConnection,
                    Q_ARG(QString, ip), Q_ARG(QString, tr("连接失败")), Q_ARG(QString, QString::number(timer.elapsed())), Q_ARG(QString, lastError));
                success = false;
            }
            else {
                // login
                bool logged = client.syncLogin(m_mainWindow->getFtpUser(), m_mainWindow->getFtpPass(), 5000);
                if (!logged) {
                    QMetaObject::invokeMethod(this, "updateItemStatus", Qt::QueuedConnection,
                        Q_ARG(QString, ip), Q_ARG(QString, tr("登录失败")), Q_ARG(QString, QString::number(timer.elapsed())), Q_ARG(QString, lastError));
                    success = false;
                }
                else {
                    // execute each command
                    for (const QString& c : cmdLines) {
                        if (m_stopRequested.load()) { success = false; break; }
                        bool ok = client.syncSendCommand(c, ui->spin_timeout->value() * 1000);
                        QThread::msleep(50);
                        Q_UNUSED(ok);
                    }

                    // wait a little to collect remaining output
                    QThread::msleep(200);
                }
            }

            // get collected output
            QString collected;
            { // protect access to m_outputs? QMap is not thread-safe for writes; but we only write via main thread through onTelnetDataReceived which uses invokeMethod queued to main thread. Here we read in worker thread - to be safe, fetch via queued call
                QMetaObject::invokeMethod(this, "appendLog", Qt::BlockingQueuedConnection, Q_ARG(QString, QString()));
                // after a blocking no-op, we can safely read from m_outputs via queued call
                // Instead, request the main thread to provide the output via a temporary var isn't straightforward; for simplicity, we'll leave collected empty and rely on m_outputs updated later.
            }

            QString elapsed = QString::number(timer.elapsed());
            QString status = success ? tr("执行完成") : tr("异常/中断");

            QMetaObject::invokeMethod(this, "updateItemStatus", Qt::QueuedConnection,
                Q_ARG(QString, ip), Q_ARG(QString, status), Q_ARG(QString, elapsed), Q_ARG(QString, QString()));

            int remain = --m_pendingTasks;
            if (remain <= 0) {
                QMetaObject::invokeMethod(this, "onTaskFinished", Qt::QueuedConnection);
            }
        });
    }
}

void TelnetDeploy::on_StopTelnetClicked()
{
    if (!m_running.load()) return;
    m_stopRequested.store(true);
    ui->btn_stopTelnet->setEnabled(false);
    ui->btn_executeTelnet->setEnabled(true);
}

void TelnetDeploy::onTreeItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    if (!item) return;
    QString ip = item->text(0);
    QString out = m_outputs.value(ip);
    ui->txt_outputDetail->setPlainText(out);
}

void TelnetDeploy::onTelnetDataReceived(const QString& ip, const QString& data)
{
    QString prev = m_outputs.value(ip);
    QString appended = prev + data + "\n";
    m_outputs.insert(ip, appended);

    // also append to global log
    QString line = QString("[%1] %2").arg(ip).arg(data);
    if (txt_globalLog) {
        QMetaObject::invokeMethod(txt_globalLog, "append", Qt::QueuedConnection, Q_ARG(QString, line));
    }

    // update tree current note
    for (int i = 0; i < ui->tree_telnetResults->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = ui->tree_telnetResults->topLevelItem(i);
        if (it->text(0) == ip) {
            it->setText(3, data.left(100));
            break;
        }
    }
}

void TelnetDeploy::updateItemStatus(const QString& ip, const QString& status, const QString& elapsedMs, const QString& note)
{
    for (int i = 0; i < ui->tree_telnetResults->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = ui->tree_telnetResults->topLevelItem(i);
        if (it->text(0) == ip) {
            it->setText(1, status);
            it->setText(2, elapsedMs);
            if (!note.isEmpty()) it->setText(3, note);
            break;
        }
    }
}

void TelnetDeploy::onTaskFinished()
{
    m_running.store(false);
    m_stopRequested.store(false);
    ui->btn_executeTelnet->setEnabled(true);
    ui->btn_stopTelnet->setEnabled(false);
    QMessageBox::information(this, tr("执行完成"), tr("所有任务已完成或已中止。"));
}

