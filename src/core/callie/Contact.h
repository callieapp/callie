#pragma once

#include <QString>

namespace callie {

/// Someone who can be invited: an address, and a name when one is known.
struct Contact
{
    QString name;
    QString email;

    friend bool operator==(const Contact &, const Contact &) = default;
};

} // namespace callie
