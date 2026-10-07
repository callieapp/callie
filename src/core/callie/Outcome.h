#pragma once

#include <QString>

namespace callie {

/// How a change sent to a calendar went: no error on success. A failure that
/// may pass by itself (no network, a busy server, a sign-in to renew) asks to
/// be tried again later; one the server refused does not.
struct Outcome
{
    QString error;
    bool retry = false;

    Outcome() = default;
    // Implicit, so a plain message reads as a refusal.
    Outcome(QString message, bool tryAgain = false) // NOLINT(google-explicit-constructor)
        : error(std::move(message)), retry(tryAgain)
    {}

    // Implicit, so callers that only want the message keep taking a QString.
    operator QString() const { return error; } // NOLINT(google-explicit-constructor)
    [[nodiscard]] bool isEmpty() const { return error.isEmpty(); }
};

} // namespace callie
