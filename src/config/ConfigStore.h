#pragma once
#include <QString>
#include <QVariantMap>
#include <QList>

// SQLite 单表配置存储（type+key 唯一，value 为 JSON 文本）
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
    QString m_dbPath;
    bool m_open = false;
};
