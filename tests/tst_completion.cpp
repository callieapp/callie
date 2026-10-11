#include "Completion.h"

#include <QTest>

using namespace callie::cli;
using namespace Qt::StringLiterals;

class TestCompletion : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void commandsFirst();
    void optionsAndTheirValues();
    void argumentsByCommand();
    void namesComeFromTheSources();
    void linesAreReadAsTheShellReadsThem();
    void valuesAfterAnEqualsSignOrAShortOption();

private:
    [[nodiscard]] static Completion make();
    [[nodiscard]] static QStringList values(const QList<Candidate> &candidates);
};

Completion TestCompletion::make()
{
    Completion::Sources sources;
    sources.calendars = [] { return QList<Candidate>{{u"Work"_s, {}}, {u"Deep work"_s, {}}}; };
    sources.events = [] {
        return QList<Candidate>{{u"work/a"_s, u"Mon 12 Oct 09:30 Standup"_s},
                                {u"work/b"_s, u"Tue 13 Oct 08:00 Berlin sync"_s}};
    };
    sources.invitations = [] { return QList<Candidate>{{u"work/b"_s, {}}}; };
    sources.accounts = [] { return QList<Candidate>{{u"me@example.com"_s, u"google"_s}}; };
    sources.settings = [] { return QStringList{u"viMode"_s, u"view"_s, u"weekNumbers"_s}; };
    return Completion({{u"agenda"_s, u"Upcoming events"_s},
                       {u"add"_s, u"Create an event"_s},
                       {u"edit"_s, u"Change an event"_s},
                       {u"respond"_s, u"Answer an invitation"_s},
                       {u"settings"_s, {}},
                       {u"calendars"_s, {}},
                       {u"accounts"_s, {}},
                       {u"completions"_s, {}}},
                      {{u"json"_s, u"Print JSON"_s, false},
                       {u"d"_s, u"Days of agenda"_s, true},
                       {u"days"_s, u"Days of agenda"_s, true},
                       {u"scope"_s, {}, true},
                       {u"repeat"_s, {}, true},
                       {u"video"_s, {}, true},
                       {u"calendar"_s, {}, true},
                       {u"sample"_s, {}, false}},
                      sources);
}

QStringList TestCompletion::values(const QList<Candidate> &candidates)
{
    QStringList result;
    for (const Candidate &candidate : candidates)
        result.append(candidate.value);
    return result;
}

void TestCompletion::commandsFirst()
{
    const Completion completion = make();
    QCOMPARE(values(completion.complete({u"a"_s})),
             (QStringList{u"agenda"_s, u"add"_s, u"accounts"_s}));
    QCOMPARE(completion.complete({u"ag"_s}).first().about, u"Upcoming events"_s);
    // Options before the command leave it first, with their values.
    QCOMPARE(values(completion.complete({u"--sample"_s, u"--days"_s, u"3"_s, u"ed"_s})),
             QStringList{u"edit"_s});
    QVERIFY(completion.complete({u"nothing"_s}).isEmpty());
}

void TestCompletion::optionsAndTheirValues()
{
    const Completion completion = make();
    QCOMPARE(values(completion.complete({u"agenda"_s, u"--j"_s})), QStringList{u"--json"_s});
    QCOMPARE(values(completion.complete({u"edit"_s, u"work/a"_s, u"--scope"_s, QString()})),
             (QStringList{u"this"_s, u"following"_s, u"all"_s}));
    QVERIFY(values(completion.complete({u"edit"_s, u"x"_s, u"--repeat"_s, u"we"_s}))
                .contains(u"weekly"_s));
    QCOMPARE(values(completion.complete({u"edit"_s, u"x"_s, u"--video"_s, QString()})),
             (QStringList{u"on"_s, u"off"_s}));
    QCOMPARE(values(completion.complete({u"add"_s, u"lunch"_s, u"--calendar"_s, u"D"_s})),
             QStringList{u"Deep work"_s});
    // A value with no choices offers nothing rather than commands.
    QVERIFY(completion.complete({u"agenda"_s, u"--days"_s, QString()}).isEmpty());
}

void TestCompletion::argumentsByCommand()
{
    const Completion completion = make();
    QCOMPARE(values(completion.complete({u"respond"_s, u"work/b"_s, QString()})),
             (QStringList{u"yes"_s, u"maybe"_s, u"no"_s}));
    QCOMPARE(values(completion.complete({u"settings"_s, u"v"_s})),
             (QStringList{u"viMode"_s, u"view"_s}));
    QCOMPARE(values(completion.complete({u"calendars"_s, u"h"_s})), QStringList{u"hide"_s});
    QCOMPARE(values(completion.complete({u"calendars"_s, u"hide"_s, QString()})),
             (QStringList{u"Work"_s, u"Deep work"_s}));
    QCOMPARE(values(completion.complete({u"accounts"_s, u"remove"_s, QString()})),
             QStringList{u"google"_s});
    QCOMPARE(values(completion.complete({u"accounts"_s, u"remove"_s, u"google"_s, QString()})),
             QStringList{u"me@example.com"_s});
    QCOMPARE(values(completion.complete({u"accounts"_s, u"rename"_s, QString()})),
             QStringList{u"me@example.com"_s});
    QCOMPARE(values(completion.complete({u"completions"_s, QString()})),
             (QStringList{u"bash"_s, u"zsh"_s, u"fish"_s}));
    // Past what a command takes, nothing.
    QVERIFY(completion.complete({u"edit"_s, u"work/a"_s, QString()}).isEmpty());
}

void TestCompletion::namesComeFromTheSources()
{
    const Completion completion = make();
    const QList<Candidate> events = completion.complete({u"edit"_s, u"work/"_s});
    QCOMPARE(values(events), (QStringList{u"work/a"_s, u"work/b"_s}));
    QCOMPARE(events.first().about, u"Mon 12 Oct 09:30 Standup"_s);
    // Only invitations can be answered.
    QCOMPARE(values(completion.complete({u"respond"_s, QString()})), QStringList{u"work/b"_s});
    // Without a source, nothing rather than a crash.
    const Completion bare({}, {}, {});
    QVERIFY(bare.complete({u"edit"_s, QString()}).isEmpty());
}

void TestCompletion::linesAreReadAsTheShellReadsThem()
{
    QCOMPARE(Completion::wordsOf(u"callie edit "_s), (QStringList{u"edit"_s, QString()}));
    QCOMPARE(Completion::wordsOf(u"callie calendars hide Deep\\ w"_s),
             (QStringList{u"calendars"_s, u"hide"_s, u"Deep w"_s}));
    QCOMPARE(Completion::wordsOf(u"callie add 'Lunch at 1' --calendar \"Deep w"_s),
             (QStringList{u"add"_s, u"Lunch at 1"_s, u"--calendar"_s, u"Deep w"_s}));
    // Still typing callie itself, which is the shell's to finish.
    QVERIFY(Completion::wordsOf(u"callie"_s).isEmpty());
    // So a quoted name being typed still finds its calendar.
    QCOMPARE(values(make().complete(Completion::wordsOf(u"callie calendars hide Deep\\ "_s))),
             QStringList{u"Deep work"_s});
}

void TestCompletion::valuesAfterAnEqualsSignOrAShortOption()
{
    const Completion completion = make();
    QCOMPARE(values(completion.complete({u"edit"_s, u"x"_s, u"--scope=f"_s})),
             QStringList{u"--scope=following"_s});
    QCOMPARE(values(completion.complete({u"--days=3"_s, u"ag"_s})), QStringList{u"agenda"_s});
    // -d 3 is an option and its value, not two words of the command.
    QCOMPARE(values(completion.complete({u"-d"_s, u"3"_s, u"ag"_s})), QStringList{u"agenda"_s});
    QVERIFY(completion.complete({u"-d"_s, QString()}).isEmpty());
    // Short names are not offered, only long ones.
    QVERIFY(!values(completion.complete({u"-"_s})).contains(u"-d"_s));
}

QTEST_GUILESS_MAIN(TestCompletion)
#include "tst_completion.moc"
