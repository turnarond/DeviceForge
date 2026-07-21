#include "ConfigStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

static const char kConnName[] = "config_store";

ConfigStore& ConfigStore::instance()
{
    static ConfigStore s;
    return s;
}

static QSqlDatabase db()
{
    return QSqlDatabase::database(kConnName);
}

bool ConfigStore::open(const QString& dbPath)
{
    if (m_open) {
        close();
    }

    QString path = dbPath;
    if (path.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(base);
        path = base + QStringLiteral("/config.db");
    }
    m_dbPath = path;

    // 连接名复用：若残留则先移除
    if (QSqlDatabase::contains(kConnName)) {
        {
            QSqlDatabase old = QSqlDatabase::database(kConnName);
            if (old.isOpen())
                old.close();
        }
        QSqlDatabase::removeDatabase(kConnName);
    }

    QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnName);
    d.setDatabaseName(path);
    if (!d.open())
        return false;

    QSqlQuery q(d);
    const bool ok = q.exec(
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS config_items ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  type TEXT NOT NULL,"
            "  key TEXT NOT NULL,"
            "  value TEXT NOT NULL,"
            "  created_at INTEGER NOT NULL,"
            "  updated_at INTEGER NOT NULL,"
            "  UNIQUE(type, key))"));
    if (!ok)
        return false;

    m_open = true;
    return true;
}

void ConfigStore::close()
{
    if (QSqlDatabase::contains(kConnName)) {
        {
            QSqlDatabase d = db();
            if (d.isOpen())
                d.close();
        }
        QSqlDatabase::removeDatabase(kConnName);
    }
    m_open = false;
}

bool ConfigStore::save(const QString& type, const QString& key,
                       const QVariantMap& value)
{
    QSqlDatabase d = db();
    if (!d.isOpen())
        return false;

    const QString json = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact));
    // 毫秒：便于 list 按 updated_at 区分短间隔写入（秒精度在测试/快速连写会撞点）
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QSqlQuery q(d);
    q.prepare(
        QStringLiteral(
            "INSERT INTO config_items(type, key, value, created_at, updated_at) "
            "VALUES(:t, :k, :v, :c, :u) "
            "ON CONFLICT(type, key) DO UPDATE SET "
            "  value=excluded.value, updated_at=excluded.updated_at"));
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":v"), json);
    q.bindValue(QStringLiteral(":c"), now);
    q.bindValue(QStringLiteral(":u"), now);
    return q.exec();
}

QVariantMap ConfigStore::load(const QString& type, const QString& key)
{
    QSqlDatabase d = db();
    if (!d.isOpen())
        return {};

    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT value FROM config_items WHERE type=:t AND key=:k"));
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":k"), key);
    if (!q.exec() || !q.next())
        return {};

    const QJsonDocument doc = QJsonDocument::fromJson(q.value(0).toString().toUtf8());
    if (!doc.isObject())
        return {};
    return doc.object().toVariantMap();
}

bool ConfigStore::exists(const QString& type, const QString& key)
{
    QSqlDatabase d = db();
    if (!d.isOpen())
        return false;

    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT 1 FROM config_items WHERE type=:t AND key=:k"));
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":k"), key);
    return q.exec() && q.next();
}

bool ConfigStore::remove(const QString& type, const QString& key)
{
    QSqlDatabase d = db();
    if (!d.isOpen())
        return false;

    QSqlQuery q(d);
    q.prepare(QStringLiteral("DELETE FROM config_items WHERE type=:t AND key=:k"));
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":k"), key);
    return q.exec();
}

QList<QVariantMap> ConfigStore::list(const QString& type, int limit)
{
    QList<QVariantMap> out;
    QSqlDatabase d = db();
    if (!d.isOpen())
        return out;

    QSqlQuery q(d);
    q.prepare(QStringLiteral(
        "SELECT type, key, value, updated_at FROM config_items "
        "WHERE type=:t ORDER BY updated_at DESC LIMIT :n"));
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":n"), limit);
    if (!q.exec())
        return out;

    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("type"), q.value(0).toString());
        m.insert(QStringLiteral("key"), q.value(1).toString());
        m.insert(QStringLiteral("updated_at"), q.value(3).toLongLong());
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(2).toString().toUtf8());
        if (doc.isObject()) {
            const QVariantMap inner = doc.object().toVariantMap();
            for (auto it = inner.constBegin(); it != inner.constEnd(); ++it)
                m.insert(it.key(), it.value());
        }
        out.append(m);
    }
    return out;
}

bool ConfigStore::exportTo(const QString& jsonPath)
{
    QSqlDatabase d = db();
    if (!d.isOpen())
        return false;

    QSqlQuery q(d);
    if (!q.exec(QStringLiteral(
            "SELECT type, key, value, created_at, updated_at FROM config_items")))
        return false;

    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o.insert(QStringLiteral("type"), q.value(0).toString());
        o.insert(QStringLiteral("key"), q.value(1).toString());
        // value 列为 JSON 文本；导出时保持字符串形态以便 import 原样写回
        o.insert(QStringLiteral("value"), q.value(2).toString());
        o.insert(QStringLiteral("created_at"), q.value(3).toLongLong());
        o.insert(QStringLiteral("updated_at"), q.value(4).toLongLong());
        arr.append(o);
    }

    QFile f(jsonPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool ConfigStore::importFrom(const QString& jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray())
        return false;

    QSqlDatabase d = db();
    if (!d.isOpen())
        return false;

    if (!d.transaction())
        return false;

    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        QSqlQuery q(d);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO config_items"
            "(type, key, value, created_at, updated_at) "
            "VALUES(:t, :k, :v, :c, :u)"));
        q.bindValue(QStringLiteral(":t"), o.value(QStringLiteral("type")).toString());
        q.bindValue(QStringLiteral(":k"), o.value(QStringLiteral("key")).toString());
        q.bindValue(QStringLiteral(":v"), o.value(QStringLiteral("value")).toString());
        q.bindValue(QStringLiteral(":c"),
                    o.value(QStringLiteral("created_at")).toVariant().toLongLong());
        q.bindValue(QStringLiteral(":u"),
                    o.value(QStringLiteral("updated_at")).toVariant().toLongLong());
        if (!q.exec()) {
            d.rollback();
            return false;
        }
    }

    if (!d.commit()) {
        d.rollback();
        return false;
    }
    return true;
}
