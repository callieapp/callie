#pragma once

#include "Account.h"

#include <QString>

#include <functional>

namespace callie {

/// Refresh tokens in the system keyring (Secret Service on Linux). Never falls
/// back to plain-text storage. Each call is asynchronous and invokes its
/// callback exactly once, with an empty error string on success.
class TokenStore
{
public:
    using Done = std::function<void(const QString &error)>;
    using Loaded = std::function<void(const QString &secret, const QString &error)>;

    static void write(const Account &account, const QString &secret, Done done);
    static void read(const Account &account, Loaded loaded);

    /// Removing an entry that does not exist counts as success.
    static void remove(const Account &account, Done done);
};

} // namespace callie
