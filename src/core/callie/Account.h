#pragma once

#include <QString>

namespace callie {

/// A connected calendar account. `id` is the provider's stable identifier for
/// the user, which for Google is the primary calendar id (the account email)
/// and for CalDAV the user name, with the server's host when it has no @.
struct Account
{
    QString provider;
    QString id;
    /// Where a CalDAV account's calendars are, and the name it signs in with.
    QString server = {};
    QString user = {};

    /// One account is one provider and id, wherever it is.
    friend bool operator==(const Account &a, const Account &b)
    {
        return a.provider == b.provider && a.id == b.id;
    }
};

} // namespace callie
