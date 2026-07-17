#include "OpcUaClient.h"
#include "ui_tab_opcua_client.h"
#include "DeployMaster.h"

#include <QMessageBox>
#include <QTableWidgetItem>
#include <QInputDialog>
#include <QDateTime>

OpcUaClientTab::OpcUaClientTab(DeployMaster* parentWindow, QWidget* parent)
    : QWidget(parent), ui(new Ui::TabOpcUaClient), m_mainWindow(parentWindow)
{
    ui->setupUi(this);
    m_globalLog = m_mainWindow->getGlobalLogItem();

    connect(ui->btn_connect, &QPushButton::clicked, this, &OpcUaClientTab::on_btn_connect_clicked);
    connect(ui->btn_browse, &QPushButton::clicked, this, &OpcUaClientTab::on_btn_browse_clicked);
    connect(ui->btn_readValue, &QPushButton::clicked, this, &OpcUaClientTab::on_btn_readValue_clicked);
    connect(ui->btn_writeValue, &QPushButton::clicked, this, &OpcUaClientTab::on_btn_writeValue_clicked);
    connect(ui->btn_subscribe, &QPushButton::clicked, this, &OpcUaClientTab::on_btn_subscribe_clicked);

    // placeholder initial table
    ui->table_nodes->setRowCount(0);
}

OpcUaClientTab::~OpcUaClientTab()
{
    delete ui;
}

void OpcUaClientTab::setEndpointList(const QStringList& endpoints)
{
    m_endpoints = endpoints;
    ui->list_endpoints->clear();
    for (const QString& e : endpoints) ui->list_endpoints->addItem(e);
}

void OpcUaClientTab::on_btn_connect_clicked()
{
    if (!m_connected) {
        QString endpoint = ui->lineEdit_endpoint->text().trimmed();
        if (endpoint.isEmpty()) {
            QMessageBox::warning(this, "Warning", "Please fill Endpoint URL");
            return;
        }
        // TODO: connect to endpoint using OPC UA stack (omitted)
        m_connected = true;
        appendLog(QString("Connected: %1").arg(endpoint));
        ui->btn_connect->setText("Disconnect");
    }
    else {
        // disconnect
        m_connected = false;
        ui->btn_connect->setText("Connect/Disconnect");
        appendLog("Disconnected");
    }
}

void OpcUaClientTab::on_btn_browse_clicked()
{
    if (!m_connected) {
        appendLog("Please connect to server first");
        return;
    }
    // placeholder: populate some nodes
    ui->table_nodes->setRowCount(3);
    ui->table_nodes->setItem(0, 0, new QTableWidgetItem("ns=2;s=Demo.Static.Scalar.Int32"));
    ui->table_nodes->setItem(0, 1, new QTableWidgetItem("Int32Var"));
    ui->table_nodes->setItem(0, 2, new QTableWidgetItem("123"));

    ui->table_nodes->setItem(1, 0, new QTableWidgetItem("ns=2;s=Demo.Static.Scalar.Float"));
    ui->table_nodes->setItem(1, 1, new QTableWidgetItem("FloatVar"));
    ui->table_nodes->setItem(1, 2, new QTableWidgetItem("3.14"));

    ui->table_nodes->setItem(2, 0, new QTableWidgetItem("ns=2;s=Demo.Static.Scalar.Boolean"));
    ui->table_nodes->setItem(2, 1, new QTableWidgetItem("BoolVar"));
    ui->table_nodes->setItem(2, 2, new QTableWidgetItem("true"));

    appendLog("Browse nodes finished (demo data)");
}

void OpcUaClientTab::on_btn_readValue_clicked()
{
    if (!m_connected) { appendLog("Please connect to server first"); return; }
    auto items = ui->table_nodes->selectedItems();
    if (items.isEmpty()) { QMessageBox::warning(this, "Select node first", "Please select a node row in the table"); return; }
    int row = items.first()->row();
    // placeholder read
    QString val = ui->table_nodes->item(row, 2)->text();
    appendLog(QString("Read node %1 value: %2").arg(ui->table_nodes->item(row,0)->text()).arg(val));
}

void OpcUaClientTab::on_btn_writeValue_clicked()
{
    if (!m_connected) { appendLog("Please connect to server first"); return; }
    auto items = ui->table_nodes->selectedItems();
    if (items.isEmpty()) { QMessageBox::warning(this, "Select node first", "Please select a node row in the table"); return; }
    int row = items.first()->row();
    QString newVal = QInputDialog::getText(this, "Write Value", "New value:");
    if (newVal.isEmpty()) return;
    // placeholder write
    ui->table_nodes->item(row, 2)->setText(newVal);
    appendLog(QString("Write node %1 value: %2").arg(ui->table_nodes->item(row,0)->text()).arg(newVal));
}

void OpcUaClientTab::on_btn_subscribe_clicked()
{
    if (!m_connected) { appendLog("Please connect to server first"); return; }
    // toggle subscribe state (placeholder)
    static bool subscribed = false;
    subscribed = !subscribed;
    appendLog(subscribed ? "Subscribed to selected nodes (demo)" : "Unsubscribed from selected nodes (demo)");
}

void OpcUaClientTab::appendLog(const QString& log)
{
    if (m_globalLog) {
        m_globalLog->append(QDateTime::currentDateTime().toString("hh:mm:ss") + " " + log);
    }
}
