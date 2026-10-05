#pragma once

#include <QString>

namespace callie {

/// A connected calendar account. `id` is the provider's stable identifier for
/// the user, which for Google is the primary calendar id (the account email).
struct Account
{
    QString provider;
    QString id;

    friend bool operator==(const Account &, const Account &) = default;
};

} // namespace callie
