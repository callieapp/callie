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

QList<Candidate> Completion::complete(const QStringList &words) const
{
    const QString typed = words.isEmpty() ? QString() : words.last();
    const QStringList before = words.mid(0, words.size() - 1);

    QList<Candidate> found;
    const QString previous = before.isEmpty() ? QString() : before.last();
    const auto option = std::find_if(m_options.cbegin(), m_options.cend(),
                                     [&](const Option &o) { return previous == u"--"_s + o.name; });
    if (option != m_options.cend() && option->takesValue) {
        found = valuesOf(option->name);
    } else if (typed.startsWith(u'-')) {
        for (const Option &each : m_options)
            found.append({u"--"_s + each.name, each.about});
    } else {
        // The words that are not options or their values.
        QStringList positional;
        for (qsizetype i = 0; i < before.size(); ++i) {
            const QString &word = before.at(i);
            if (!word.startsWith(u'-')) {
                positional.append(word);
                continue;
            }
            const auto named =
                std::find_if(m_options.cbegin(), m_options.cend(),
                             [&](const Option &o) { return word == u"--"_s + o.name; });
            if (named != m_options.cend() && named->takesValue && !word.contains(u'='))
                ++i;
        }
        found = positional.isEmpty() ? m_commands : argument(positional);
    }

    found.removeIf([&typed](const Candidate &c) { return !c.value.startsWith(typed); });
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
            return plain({u"hide"_s, u"show"_s, u"rename"_s, u"color"_s, u"reset"_s});
        if (at == 2)
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
