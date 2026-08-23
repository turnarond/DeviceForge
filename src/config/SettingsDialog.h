#pragma once

#include <QDialog>

class QTableWidget;
class QPushButton;
class QComboBox;
class QLineEdit;
class QSlider;

// 本地 ConfigStore 配置管理面板：列表 / 筛选 / 删除 / 导入导出
// v2.8：新增部署并发度滑块（type="deploy"、key="concurrency"，accept 时写回）
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

protected:
    // 确认（关闭按钮/回车）时把滑块值写回 ConfigStore 后再关窗
    void accept() override;

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
    QSlider*      m_deployConcurrencySlider = nullptr;
};
