#include "FakeTokenStore.h"
#include "SupportActions.h"

#include "callie/AccountStore.h"
#include "callie/Diagnostics.h"

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

class TestDiagnostics : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void emailsAreNumberedConsistently();
    void homeFolderIsHidden();
    void issueUrlCarriesShortenedReport();
    void reportCoversAccountsAndHidesThem();
    void copyPutsReportOnClipboard();

private:
    QString collect(TokenStore &tokens);

    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestDiagnostics::init()
{
    // Every path the report reads from, kept out of the real home folder.
    m_dir = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_dir->filePath(u"config"_s).toUtf8());
    qputenv("XDG_CACHE_HOME", m_dir->filePath(u"cache"_s).toUtf8());
    qputenv("XDG_STATE_HOME", m_dir->filePath(u"state"_s).toUtf8());
}

QString TestDiagnostics::collect(TokenStore &tokens)
{
    QString report;
    diagnostics::collect(tokens, u"Extra: yes"_s, [&](const QString &r) { report = r; });
    [&] { QTRY_VERIFY_WITH_TIMEOUT(!report.isEmpty(), 5000); }();
    return report;
}

void TestDiagnostics::emailsAreNumberedConsistently()
{
    QCOMPARE(diagnostics::redact(
                 u"a@x.com then B@X.com, a@x.com again, team@group.calendar.google.com"_s),
             u"<email-1> then <email-2>, <email-1> again, <email-3>"_s);
}

void TestDiagnostics::homeFolderIsHidden()
{
    const QString text = QDir::homePath() + u"/.local/state/callie/logs"_s;
    QCOMPARE(diagnostics::redact(text), u"~/.local/state/callie/logs"_s);
}

void TestDiagnostics::issueUrlCarriesShortenedReport()
{
    const QUrl url = diagnostics::issueUrl(QString(10000, u'x'));
    const QUrlQuery query(url);
    QCOMPARE(url.host(), u"github.com"_s);
    QCOMPARE(query.queryItemValue(u"template"_s), u"bug_report.yml"_s);
    const QString body = query.queryItemValue(u"diagnostics"_s, QUrl::FullyDecoded);
    QVERIFY(body.size() < 6100);
    QVERIFY(body.endsWith(u"run `callie doctor` for the rest]"_s));
}

void TestDiagnostics::reportCoversAccountsAndHidesThem()
{
    AccountStore store(AccountStore::defaultPath());
    QVERIFY(store.add(Account{u"google"_s, u"me@example.com"_s}));
    FakeTokenStore tokens;
    tokens.secrets.insert(u"me@example.com"_s, u"rt"_s);

    const QString report = collect(tokens);

    QVERIFY2(report.contains(u"Account <email-1> (google): token stored, never synced"_s),
             qPrintable(report));
    QVERIFY(report.contains(u"Extra: yes"_s));
    QVERIFY(report.contains(u"Qt "_s));
    QVERIFY(!report.contains(u"me@example.com"_s));
    QVERIFY(!report.contains(QDir::homePath()));
}

void TestDiagnostics::copyPutsReportOnClipboard()
{
    SupportActions support;
    QSignalSpy copied(&support, &SupportActions::copied);

    support.copyDebugInfo();

    QVERIFY(copied.wait(5000));
    QVERIFY(QGuiApplication::clipboard()->text().startsWith(u"Callie "_s));
}

QTEST_MAIN(TestDiagnostics)
#include "tst_diagnostics.moc"
