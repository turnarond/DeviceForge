#include "ConfigStore.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QUuid>
#include <QDebug>

ConfigStore& ConfigStore::instance()
{
    static ConfigStore s;
    return s;
}

ConfigStore::~ConfigStore()
{
    close();
}

bool ConfigStore::ensureOpen() const
{
    return m_open && m_db.isValid() && m_db.isOpen();
}

bool ConfigStore::open(const QString& dbPath)
{
    if (m_open)
        close();

    QString path = dbPath;
    if (path.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (base.isEmpty()) {
            qWarning() << "ConfigStore: AppDataLocation empty";
            return false;
        }
        if (!QDir().mkpath(base)) {
            qWarning() << "ConfigStore: cannot create" << base;
            return false;
        }
        path = base + QStringLiteral("/config.db");
    }

    // 确保父目录存在（测试 temp 路径也适用）
    {
        const QFileInfo fi(path);
        const QString dir = fi.absolutePath();
        if (!dir.isEmpty() && !QDir().mkpath(dir)) {
            qWarning() << "ConfigStore: cannot create dir" << dir;
            return false;
        }
    }

    m_dbPath = path;
    // 唯一连接名：避免全局固定名在测试 open/close 循环中与残留对象冲突
    m_connName = QStringLiteral("config_store_%1")
                     .arg(QUuid::createUuid().toString(QUuid::Id128));

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    if (!m_db.isValid()) {
        qWarning() << "ConfigStore: QSQLITE driver unavailable";
        QSqlDatabase::removeDatabase(m_connName);
        m_connName.clear();
        return false;
    }
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning() << "ConfigStore: open failed" << path << m_db.lastError().text();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connName);
        m_connName.clear();
        return false;
    }

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS config_items ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  type TEXT NOT NULL,"
            "  key TEXT NOT NULL,"
            "  value TEXT NOT NULL,"
            "  created_at INTEGER NOT NULL,"
            "  updated_at INTEGER NOT NULL,"
            "  UNIQUE(type, key))"))) {
        qWarning() << "ConfigStore: CREATE TABLE failed" << q.lastError().text();
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connName);
        m_connName.clear();
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_config_type ON config_items(type)"));

    m_open = true;
    return true;
}

void ConfigStore::close()
{
    if (!m_connName.isEmpty()) {
        // 先释放持有的连接对象，再 removeDatabase
        if (m_db.isValid()) {
            if (m_db.isOpen())
                m_db.close();
            m_db = QSqlDatabase();
        }
        if (QSqlDatabase::contains(m_connName))
            QSqlDatabase::removeDatabase(m_connName);
        m_connName.clear();
    }
    m_dbPath.clear();
    m_open = false;
}

bool ConfigStore::save(const QString& type, const QString& key,
                       const QVariantMap& value)
{
    if (!ensureOpen())
        return false;

    const QByteArray jsonBytes =
        QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QSqlQuery q(m_db);
    if (!q.prepare(QStringLiteral(
            "INSERT INTO config_items(type, key, value, created_at, updated_at) "
            "VALUES(:t, :k, :v, :c, :u) "
            "ON CONFLICT(type, key) DO UPDATE SET "
            "  value=excluded.value, updated_at=excluded.updated_at"))) {
        qWarning() << "ConfigStore::save prepare failed" << q.lastError().text();
        return false;
    }
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":v"), QString::fromUtf8(jsonBytes));
    q.bindValue(QStringLiteral(":c"), now);
    q.bindValue(QStringLiteral(":u"), now);
    if (!q.exec()) {
        qWarning() << "ConfigStore::save exec failed" << q.lastError().text();
        return false;
    }
    return true;
}

QVariantMap ConfigStore::load(const QString& type, const QString& key)
{
    if (!ensureOpen())
        return {};

    QSqlQuery q(m_db);
    if (!q.prepare(QStringLiteral(
            "SELECT value FROM config_items WHERE type=:t AND key=:k")))
        return {};
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
    if (!ensureOpen())
        return false;

    QSqlQuery q(m_db);
    if (!q.prepare(QStringLiteral(
            "SELECT 1 FROM config_items WHERE type=:t AND key=:k")))
        return false;
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":k"), key);
    return q.exec() && q.next();
}

bool ConfigStore::remove(const QString& type, const QString& key)
{
    if (!ensureOpen())
        return false;

    QSqlQuery q(m_db);
    if (!q.prepare(QStringLiteral(
            "DELETE FROM config_items WHERE type=:t AND key=:k")))
        return false;
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":k"), key);
    return q.exec();
}

QList<QVariantMap> ConfigStore::list(const QString& type, int limit)
{
    QList<QVariantMap> out;
    if (!ensureOpen())
        return out;

    QSqlQuery q(m_db);
    if (!q.prepare(QStringLiteral(
            "SELECT type, key, value, updated_at FROM config_items "
            "WHERE type=:t ORDER BY updated_at DESC LIMIT :n")))
        return out;
    q.bindValue(QStringLiteral(":t"), type);
    q.bindValue(QStringLiteral(":n"), limit);
    if (!q.exec())
        return out;

    while (q.next()) {
        QVariantMap m;
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(2).toString().toUtf8());
        if (doc.isObject()) {
            const QVariantMap inner = doc.object().toVariantMap();
            for (auto it = inner.constBegin(); it != inner.constEnd(); ++it) {
                if (it.key() == QLatin1String("type")
                    || it.key() == QLatin1String("key")
                    || it.key() == QLatin1String("updated_at"))
                    continue;
                m.insert(it.key(), it.value());
            }
        }
        m.insert(QStringLiteral("type"), q.value(0).toString());
        m.insert(QStringLiteral("key"), q.value(1).toString());
        m.insert(QStringLiteral("updated_at"), q.value(3).toLongLong());
        out.append(m);
    }
    return out;
}

bool ConfigStore::exportTo(const QString& jsonPath)
{
    if (!ensureOpen())
        return false;

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT type, key, value, created_at, updated_at FROM config_items")))
        return false;

    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o.insert(QStringLiteral("type"), q.value(0).toString());
        o.insert(QStringLiteral("key"), q.value(1).toString());
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

    if (!ensureOpen())
        return false;

    if (!m_db.transaction())
        return false;

    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO config_items"
                "(type, key, value, created_at, updated_at) "
                "VALUES(:t, :k, :v, :c, :u)"))) {
            m_db.rollback();
            return false;
        }
        q.bindValue(QStringLiteral(":t"), o.value(QStringLiteral("type")).toString());
        q.bindValue(QStringLiteral(":k"), o.value(QStringLiteral("key")).toString());
        q.bindValue(QStringLiteral(":v"), o.value(QStringLiteral("value")).toString());
        q.bindValue(QStringLiteral(":c"),
                    o.value(QStringLiteral("created_at")).toVariant().toLongLong());
        q.bindValue(QStringLiteral(":u"),
                    o.value(QStringLiteral("updated_at")).toVariant().toLongLong());
        if (!q.exec()) {
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return false;
    }
    return true;
}
