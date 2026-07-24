#pragma once
#include <QString>
#include <QVariantMap>
#include <QList>
#include <QSqlDatabase>

// SQLite 单表配置存储（type+key 唯一，value 为 JSON 文本）
// 连接对象由 ConfigStore 持有，避免仅用 connection name 取回时的悬空/析构时序问题。
class ConfigStore {
public:
    static ConfigStore& instance();

    // 空路径 → AppDataLocation/config.db；测试可指定临时 db
    bool open(const QString& dbPath = QString());
    void close();

    bool save(const QString& type, const QString& key,
              const QVariantMap& value);
    QVariantMap load(const QString& type, const QString& key);
    bool exists(const QString& type, const QString& key);
    bool remove(const QString& type, const QString& key);
    QList<QVariantMap> list(const QString& type, int limit = 1000);

    bool exportTo(const QString& jsonPath);
    bool importFrom(const QString& jsonPath);

private:
    ConfigStore() = default;
    ~ConfigStore();
    ConfigStore(const ConfigStore&) = delete;
    ConfigStore& operator=(const ConfigStore&) = delete;

    bool ensureOpen() const;

    QString m_dbPath;
    QString m_connName; // 每次 open 生成唯一连接名，避免 removeDatabase 竞态
    QSqlDatabase m_db;
    bool m_open = false;
};
