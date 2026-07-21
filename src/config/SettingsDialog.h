#pragma once

#include <QDialog>

class QTableWidget;
class QPushButton;
class QComboBox;
class QLineEdit;

// 本地 ConfigStore 配置管理面板：列表 / 筛选 / 删除 / 导入导出
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onRefresh();
    void onFilterChanged(const QString& text);
    void onEditClicked();
    void onDeleteClicked();
    void onExportClicked();
    void onImportClicked();
    void onClearAllClicked();

private:
    void populateTable();

    QTableWidget* m_table = nullptr;
    QComboBox*    m_typeFilter = nullptr;
    QLineEdit*    m_searchEdit = nullptr;
};
