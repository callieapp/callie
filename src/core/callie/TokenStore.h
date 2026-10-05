#pragma once

#include "Account.h"

#include <QString>

#include <functional>

namespace callie {

/// Where refresh tokens live. Every call completes asynchronously and invokes
/// its callback exactly once, with an empty error string on success.
class TokenStore
{
public:
    using Done = std::function<void(const QString &error)>;
    using Loaded = std::function<void(const QString &secret, const QString &error)>;

    virtual ~TokenStore() = default;

    virtual void write(const Account &account, const QString &secret, Done done) = 0;
    virtual void read(const Account &account, Loaded loaded) = 0;

    /// Removing an entry that does not exist counts as success.
    virtual void remove(const Account &account, Done done) = 0;
};

/// The system keyring (Secret Service on Linux). Never falls back to plain text.
class KeychainTokenStore final : public TokenStore
{
public:
    void write(const Account &account, const QString &secret, Done done) override;
    void read(const Account &account, Loaded loaded) override;
    void remove(const Account &account, Done done) override;
};

} // namespace callie
