#include "config/SettingsDialog.h"
#include "config/ConfigStore.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractItemView>

namespace {

// 已知配置类型（与各 Tool 持久化 type 对齐）
const char* kKnownTypes[] = {
    "device.list",
    "opcua.endpoint",
    "ftp.credential",
    "telnet.prefs",
    "modbus.slave",
    "netrelay.server",
    "websocket.endpoint",
};

// ConfigStore 使用毫秒时间戳
QString fmtTs(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("-");
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

QList<QVariantMap> listAllKnown(int limitPerType = 1000)
{
    QList<QVariantMap> all;
    for (const char* t : kKnownTypes) {
        all.append(ConfigStore::instance().list(QString::fromLatin1(t), limitPerType));
    }
    return all;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("配置管理"));
    resize(720, 480);

    // Task 4 前 ConfigStore 可能尚未 open：防御性打开默认路径
    if (!ConfigStore::instance().open()) {
        // 仍继续构建 UI，列表为空；用户可见刷新无数据
    }

    auto* topLayout = new QHBoxLayout;
    m_typeFilter = new QComboBox(this);
    m_typeFilter->addItem(QStringLiteral("所有类型"));
    for (const char* t : kKnownTypes)
        m_typeFilter->addItem(QString::fromLatin1(t));
    connect(m_typeFilter, &QComboBox::currentTextChanged,
            this, &SettingsDialog::onFilterChanged);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索键..."));
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &SettingsDialog::onFilterChanged);

    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &SettingsDialog::onRefresh);
    topLayout->addWidget(m_typeFilter);
    topLayout->addWidget(m_searchEdit, 1);
    topLayout->addWidget(refreshBtn);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("类型"),
        QStringLiteral("键"),
        QStringLiteral("更新时间"),
        QStringLiteral("值预览"),
        QStringLiteral(""),
    });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(0, 120);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(1, 200);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(2, 140);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(4, 40);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);

    auto* bottomLayout = new QHBoxLayout;
    auto* editBtn = new QPushButton(QStringLiteral("查看"), this);
    auto* delBtn = new QPushButton(QStringLiteral("删除"), this);
    auto* expBtn = new QPushButton(QStringLiteral("导出..."), this);
    auto* impBtn = new QPushButton(QStringLiteral("导入..."), this);
    auto* clrBtn = new QPushButton(QStringLiteral("清空所有"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    connect(editBtn, &QPushButton::clicked, this, &SettingsDialog::onEditClicked);
    connect(delBtn, &QPushButton::clicked, this, &SettingsDialog::onDeleteClicked);
    connect(expBtn, &QPushButton::clicked, this, &SettingsDialog::onExportClicked);
    connect(impBtn, &QPushButton::clicked, this, &SettingsDialog::onImportClicked);
    connect(clrBtn, &QPushButton::clicked, this, &SettingsDialog::onClearAllClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    bottomLayout->addWidget(editBtn);
    bottomLayout->addWidget(delBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(expBtn);
    bottomLayout->addWidget(impBtn);
    bottomLayout->addWidget(clrBtn);
    bottomLayout->addWidget(closeBtn);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_table, 1);
    mainLayout->addLayout(bottomLayout);

    populateTable();
}

void SettingsDialog::populateTable()
{
    m_table->setRowCount(0);
    const QString typeFilter = m_typeFilter->currentText();
    const QString search = m_searchEdit->text().trimmed();

    QList<QVariantMap> all;
    if (typeFilter == QStringLiteral("所有类型")) {
        all = listAllKnown(1000);
    } else {
        all = ConfigStore::instance().list(typeFilter, 1000);
    }

    for (const auto& item : all) {
        const QString key = item.value(QStringLiteral("key")).toString();
        if (!search.isEmpty() && !key.contains(search, Qt::CaseInsensitive))
            continue;

        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(item.value(QStringLiteral("type")).toString()));
        m_table->setItem(row, 1, new QTableWidgetItem(key));
        m_table->setItem(row, 2, new QTableWidgetItem(
            fmtTs(item.value(QStringLiteral("updated_at")).toLongLong())));

        QStringList keys;
        for (auto it = item.constBegin(); it != item.constEnd(); ++it) {
            if (it.key() == QLatin1String("type")
                || it.key() == QLatin1String("key")
                || it.key() == QLatin1String("updated_at"))
                continue;
            QString v = it.value().toString();
            if (v.size() > 20)
                v = v.left(20) + QStringLiteral("…");
            // 凭证类字段不展示明文
            if (it.key().contains(QStringLiteral("pass"), Qt::CaseInsensitive)
                || it.key().contains(QStringLiteral("password"), Qt::CaseInsensitive)
                || it.key().contains(QStringLiteral("token"), Qt::CaseInsensitive)
                || it.key().contains(QStringLiteral("secret"), Qt::CaseInsensitive)) {
                v = QStringLiteral("***");
            }
            keys << it.key() + QLatin1Char('=') + v;
        }
        m_table->setItem(row, 3, new QTableWidgetItem(keys.join(QStringLiteral(", "))));
        m_table->setCellWidget(row, 4, new QWidget(this));
    }
}

void SettingsDialog::onRefresh()
{
    populateTable();
}

void SettingsDialog::onFilterChanged(const QString&)
{
    populateTable();
}

void SettingsDialog::onEditClicked()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QString type = m_table->item(row, 0)->text();
    const QString key = m_table->item(row, 1)->text();
    QVariantMap v = ConfigStore::instance().load(type, key);
    if (v.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("查看配置"),
                                 QStringLiteral("配置不存在或已删除。"));
        return;
    }
    // 遮罩敏感字段（密码/token 等）；完整编辑写回留给后续任务
    static const QStringList kSensitive = {
        QStringLiteral("password"), QStringLiteral("pass"),
        QStringLiteral("token"), QStringLiteral("secret")
    };
    for (auto it = v.begin(); it != v.end(); ++it) {
        const QString k = it.key().toLower();
        for (const QString& s : kSensitive) {
            if (k.contains(s) && !it.value().toString().isEmpty()) {
                it.value() = QStringLiteral("***");
                break;
            }
        }
    }
    const QString preview = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(v)).toJson(QJsonDocument::Indented));
    QMessageBox::information(this, QStringLiteral("查看配置"),
                             QStringLiteral("类型: %1\n键: %2\n\n%3")
                                 .arg(type, key, preview));
}

void SettingsDialog::onDeleteClicked()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QString type = m_table->item(row, 0)->text();
    const QString key = m_table->item(row, 1)->text();
    if (QMessageBox::question(this, QStringLiteral("确认删除"),
                              QStringLiteral("删除 %1 / %2 ?").arg(type, key))
        != QMessageBox::Yes)
        return;
    ConfigStore::instance().remove(type, key);
    populateTable();
}

void SettingsDialog::onExportClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出配置"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/deviceforge-config.json"),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    if (ConfigStore::instance().exportTo(path))
        QMessageBox::information(this, QStringLiteral("导出"),
                                 QStringLiteral("已导出到\n%1").arg(path));
    else
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("写入失败"));
}

void SettingsDialog::onImportClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入配置"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    if (ConfigStore::instance().importFrom(path)) {
        QMessageBox::information(
            this, QStringLiteral("导入"),
            QStringLiteral(
                "已导入。\n\n"
                "注意：密码等密文字段由 Windows DPAPI 绑定当前用户账号；\n"
                "若从其他机器/用户导出，密文可能无法解密，需重新输入密码。"));
        populateTable();
    } else {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("解析失败"));
    }
}

void SettingsDialog::onClearAllClicked()
{
    if (QMessageBox::question(this, QStringLiteral("确认清空"),
                              QStringLiteral("清空所有配置？此操作不可撤销。"))
        != QMessageBox::Yes)
        return;
    for (const char* t : kKnownTypes) {
        const QString type = QString::fromLatin1(t);
        for (const auto& item : ConfigStore::instance().list(type, 10000))
            ConfigStore::instance().remove(type, item.value(QStringLiteral("key")).toString());
    }
    populateTable();
}
