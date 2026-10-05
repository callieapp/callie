#include "callie/AccountStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace callie {

AccountStore::AccountStore(QString path) : m_path(std::move(path)) {}

QString AccountStore::defaultPath()
{
    // GenericConfigLocation rather than AppConfigLocation, because the app and
    // the CLI register different application names.
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/callie/accounts.json");
}

QList<Account> AccountStore::accounts() const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QList<Account> out;
    const QJsonArray array =
        QJsonDocument::fromJson(file.readAll()).object()[u"accounts"].toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        Account account{o[u"provider"].toString(), o[u"id"].toString()};
        if (!account.provider.isEmpty() && !account.id.isEmpty())
            out.append(account);
    }
    return out;
}

bool AccountStore::contains(const Account &account) const
{
    return accounts().contains(account);
}

bool AccountStore::add(const Account &account)
{
    QList<Account> list = accounts();
    if (list.contains(account))
        return true;
    list.append(account);
    return save(list);
}

bool AccountStore::remove(const Account &account)
{
    QList<Account> list = accounts();
    if (!list.removeOne(account))
        return true;
    return save(list);
}

bool AccountStore::save(const QList<Account> &accounts)
{
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        m_error = QStringLiteral("cannot create %1").arg(QFileInfo(m_path).absolutePath());
        return false;
    }

    QJsonArray array;
    for (const Account &account : accounts)
        array.append(QJsonObject{{QStringLiteral("provider"), account.provider},
                                 {QStringLiteral("id"), account.id}});

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("accounts"), array}}).toJson());
    if (!file.commit()) {
        m_error = file.errorString();
        return false;
    }
    return true;
}

} // namespace callie
