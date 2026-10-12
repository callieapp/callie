#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace callie::cli {

/// One way to finish the word being typed, and what it is.
struct Candidate
{
    QString value;
    QString about;

    bool operator==(const Candidate &) const = default;
};

/// What `callie __complete` offers the shell completion scripts for the
/// words typed so far.
class Completion
{
public:
    struct Option
    {
        /// A long name, or a letter, without the dashes.
        QString name;
        QString about;
        bool takesValue = false;
    };
    /// Where names that change, such as calendars and events, come from.
    struct Sources
    {
        std::function<QList<Candidate>()> calendars;
        /// Events by their ids, for edit, delete and duplicate.
        std::function<QList<Candidate>()> events;
        /// Invitations by their ids, for respond.
        std::function<QList<Candidate>()> invitations;
        std::function<QList<Candidate>()> accounts;
        std::function<QStringList()> settings;
    };

    Completion(QList<Candidate> commands, QList<Option> options, Sources sources);

    /// `words` follow `callie`, the last being the one typed so far, which
    /// may be empty. Gives the candidates that start with it.
    [[nodiscard]] QList<Candidate> complete(const QStringList &words) const;

    /// The words of a command line up to the cursor, as the shell reads them:
    /// quotes and backslashes taken out, and an empty last word when the line
    /// ends in a space. The first, the command's own name, is left out.
    [[nodiscard]] static QStringList wordsOf(const QString &line);

private:
    /// Folders, and .ics files, whose path starts with `typed`.
    [[nodiscard]] static QList<Candidate> calendarFiles(const QString &typed);
    [[nodiscard]] QList<Candidate> valuesOf(const QString &option) const;
    [[nodiscard]] QList<Candidate> argument(const QStringList &positional) const;

    QList<Candidate> m_commands;
    QList<Option> m_options;
    Sources m_sources;
};

} // namespace callie::cli
