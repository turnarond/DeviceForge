#ifndef UI_TAB_OPCUA_CLIENT_H
#define UI_TAB_OPCUA_CLIENT_H

#include <QtWidgets/QWidget>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTextEdit>

namespace Ui {

class TabOpcUaClient
{
public:
    QHBoxLayout* mainLayout;
    QTableWidget* table_nodes;
    QVBoxLayout* rightPanelLayout;
    QGroupBox* groupBox_config;
    QGridLayout* gridLayout_config;
    QLabel* label_endpoint;
    QLineEdit* lineEdit_endpoint;
    QLabel* label_timeout;
    QSpinBox* spin_timeout;
    QGroupBox* groupBox_control;
    QVBoxLayout* verticalLayout_control;
    QPushButton* btn_connect;
    QPushButton* btn_browse;
    QPushButton* btn_readValue;
    QPushButton* btn_writeValue;
    QPushButton* btn_subscribe;
    QListWidget* list_endpoints;
    QTextEdit* txt_localLog;

    void setupUi(QWidget* TabOpcUaClient)
    {
        if (TabOpcUaClient->objectName().isEmpty())
            TabOpcUaClient->setObjectName("TabOpcUaClient");
        TabOpcUaClient->resize(960, 620);
        mainLayout = new QHBoxLayout(TabOpcUaClient);
        mainLayout->setContentsMargins(6, 6, 6, 6);

        table_nodes = new QTableWidget(TabOpcUaClient);
        table_nodes->setObjectName("table_nodes");
        table_nodes->setColumnCount(3);
        table_nodes->setHorizontalHeaderLabels({"NodeId", "BrowseName", "Value"});
        mainLayout->addWidget(table_nodes);

        rightPanelLayout = new QVBoxLayout();
        groupBox_config = new QGroupBox(TabOpcUaClient);
        groupBox_config->setTitle("Configuration");
        gridLayout_config = new QGridLayout(groupBox_config);
        label_endpoint = new QLabel(groupBox_config);
        label_endpoint->setText("Endpoint:");
        gridLayout_config->addWidget(label_endpoint, 0, 0);
        lineEdit_endpoint = new QLineEdit(groupBox_config);
        gridLayout_config->addWidget(lineEdit_endpoint, 0, 1);
        label_timeout = new QLabel(groupBox_config);
        label_timeout->setText("Timeout(ms):");
        gridLayout_config->addWidget(label_timeout, 1, 0);
        spin_timeout = new QSpinBox(groupBox_config);
        spin_timeout->setMinimum(100);
        spin_timeout->setMaximum(60000);
        spin_timeout->setValue(5000);
        gridLayout_config->addWidget(spin_timeout, 1, 1);
        rightPanelLayout->addWidget(groupBox_config);

        groupBox_control = new QGroupBox(TabOpcUaClient);
        groupBox_control->setTitle("Actions");
        verticalLayout_control = new QVBoxLayout(groupBox_control);
        btn_connect = new QPushButton(groupBox_control);
        btn_connect->setText("Connect/Disconnect");
        verticalLayout_control->addWidget(btn_connect);
        btn_browse = new QPushButton(groupBox_control);
        btn_browse->setText("Browse Nodes");
        verticalLayout_control->addWidget(btn_browse);
        btn_readValue = new QPushButton(groupBox_control);
        btn_readValue->setText("Read Value");
        verticalLayout_control->addWidget(btn_readValue);
        btn_writeValue = new QPushButton(groupBox_control);
        btn_writeValue->setText("Write Value");
        verticalLayout_control->addWidget(btn_writeValue);
        btn_subscribe = new QPushButton(groupBox_control);
        btn_subscribe->setText("Subscribe/Unsubscribe");
        verticalLayout_control->addWidget(btn_subscribe);
        rightPanelLayout->addWidget(groupBox_control);

        list_endpoints = new QListWidget(TabOpcUaClient);
        list_endpoints->setMinimumHeight(80);
        rightPanelLayout->addWidget(list_endpoints);

        txt_localLog = new QTextEdit(TabOpcUaClient);
        txt_localLog->setReadOnly(true);
        txt_localLog->setMinimumHeight(160);
        rightPanelLayout->addWidget(txt_localLog);

        mainLayout->addLayout(rightPanelLayout);
    }
};

} // namespace Ui

#endif // UI_TAB_OPCUA_CLIENT_H
