#pragma once

#include "Account.h"

#include <QList>
#include <QString>

#include <optional>

namespace callie {

/// The list of connected accounts, kept as JSON in the user's config directory.
/// Holds no secrets: refresh tokens live in the system keyring via TokenStore.
class AccountStore
{
public:
    explicit AccountStore(QString path);

    /// `$XDG_CONFIG_HOME/callie/accounts.json`, shared by the app and the CLI.
    [[nodiscard]] static QString defaultPath();

    /// Reads the accounts into `out`. Returns false, with errorString() set, if
    /// the file exists but cannot be read or parsed.
    bool load(QList<Account> &out);

    /// Both return false on an I/O error, described by errorString().
    bool add(const Account &account);
    bool remove(const Account &account);

    [[nodiscard]] QString errorString() const { return m_error; }

private:
    /// The stored accounts, or nullopt if the file exists but cannot be used.
    std::optional<QList<Account>> read(QString *error) const;
    bool save(const QList<Account> &accounts);

    QString m_path;
    QString m_error;
};

} // namespace callie
