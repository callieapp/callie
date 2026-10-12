#include "Completion.h"

#include "callie/Repeat.h"

using namespace Qt::StringLiterals;

namespace callie::cli {

namespace {

QList<Candidate> plain(const QStringList &values)
{
    QList<Candidate> result;
    for (const QString &value : values)
        result.append({value, {}});
    return result;
}

QList<Candidate> from(const std::function<QList<Candidate>()> &source)
{
    return source ? source() : QList<Candidate>();
}

} // namespace

Completion::Completion(QList<Candidate> commands, QList<Option> options, Sources sources)
    : m_commands(std::move(commands)), m_options(std::move(options)), m_sources(std::move(sources))
{}

QStringList Completion::wordsOf(const QString &line)
{
    QStringList words;
    QString word;
    bool inWord = false;
    QChar quote;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else if (c == u'\\' && quote == u'"' && i + 1 < line.size())
                word += line.at(++i);
            else
                word += c;
        } else if (c == u'\'' || c == u'"') {
            quote = c;
            inWord = true;
        } else if (c == u'\\') {
            if (i + 1 < line.size())
                word += line.at(++i);
            inWord = true;
        } else if (c.isSpace()) {
            if (inWord)
                words.append(std::exchange(word, QString()));
            inWord = false;
        } else {
            word += c;
            inWord = true;
        }
    }
    // The word being typed, even when nothing of it is yet.
    words.append(word);
    return words.mid(1);
}

QList<Candidate> Completion::complete(const QStringList &words) const
{
    QString typed = words.isEmpty() ? QString() : words.last();
    const QStringList before = words.mid(0, words.size() - 1);

    // --name or -n, by the dashes it is written with.
    const auto option = [this](const QString &word) -> const Option * {
        for (const Option &o : m_options) {
            if (word == (o.name.size() == 1 ? u"-"_s : u"--"_s) + o.name)
                return &o;
        }
        return nullptr;
    };

    QList<Candidate> found;
    QString prefix;
    const Option *previous = before.isEmpty() ? nullptr : option(before.last());
    if (typed.startsWith(u"--"_s) && typed.contains(u'=')) {
        // --name=value: the value, written after the name.
        prefix = typed.section(u'=', 0, 0) + u'=';
        typed = typed.mid(prefix.size());
        if (const Option *named = option(prefix.chopped(1)); named && named->takesValue)
            found = valuesOf(named->name);
    } else if (previous && previous->takesValue) {
        found = valuesOf(previous->name);
    } else if (typed.startsWith(u'-')) {
        for (const Option &each : m_options) {
            if (each.name.size() > 1)
                found.append({u"--"_s + each.name, each.about});
        }
    } else {
        // The words that are not options or their values.
        QStringList positional;
        for (qsizetype i = 0; i < before.size(); ++i) {
            const QString &word = before.at(i);
            if (!word.startsWith(u'-')) {
                positional.append(word);
                continue;
            }
            const Option *named = option(word);
            if (named && named->takesValue)
                ++i;
        }
        found = positional.isEmpty() ? m_commands : argument(positional);
    }

    found.removeIf([&typed](const Candidate &c) { return !c.value.startsWith(typed); });
    for (Candidate &candidate : found)
        candidate.value.prepend(prefix);
    return found;
}

QList<Candidate> Completion::valuesOf(const QString &option) const
{
    if (option == u"scope")
        return plain({u"this"_s, u"following"_s, u"all"_s});
    if (option == u"repeat")
        return plain(Repeat::choices());
    if (option == u"video")
        return plain({u"on"_s, u"off"_s});
    if (option == u"calendar")
        return from(m_sources.calendars);
    return {};
}

QList<Candidate> Completion::argument(const QStringList &positional) const
{
    const QString command = positional.first();
    const qsizetype at = positional.size();
    if ((command == u"edit" || command == u"delete" || command == u"duplicate") && at == 1)
        return from(m_sources.events);
    if (command == u"respond") {
        if (at == 1)
            return from(m_sources.invitations);
        if (at == 2)
            return plain({u"yes"_s, u"maybe"_s, u"no"_s});
    }
    if (command == u"settings" && at == 1 && m_sources.settings)
        return plain(m_sources.settings());
    if (command == u"calendars") {
        if (at == 1)
            return plain(
                {u"new"_s, u"remove"_s, u"hide"_s, u"show"_s, u"rename"_s, u"color"_s, u"reset"_s});
        // A new calendar's name is the user's to make up.
        if (at == 2 && positional.at(1) != u"new")
            return from(m_sources.calendars);
    }
    if (command == u"accounts") {
        if (at == 1)
            return plain({u"list"_s, u"add"_s, u"remove"_s, u"rename"_s});
        const QString action = positional.at(1);
        if ((action == u"add" || action == u"remove") && at == 2)
            return plain({u"google"_s});
        if ((action == u"remove" && at == 3) || (action == u"rename" && at == 2))
            return from(m_sources.accounts);
    }
    if (command == u"completions" && at == 1)
        return plain({u"bash"_s, u"zsh"_s, u"fish"_s});
    return {};
}

} // namespace callie::cli
