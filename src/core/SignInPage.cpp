#include "callie/SignInPage.h"

#include <QCoreApplication>
#include <QFile>

#include <utility>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

QString font(const QString &family, const QString &fallback)
{
    return family.isEmpty() ? fallback : u"'%1', %2"_s.arg(family, fallback);
}

/// The fonts the app bundles, inlined so the page matches the app on systems
/// without them.
QString bundledFonts()
{
    QString faces;
    for (const auto &[family, file] : {std::pair{u"Nunito"_s, u"Nunito-Variable.ttf"_s},
                                       std::pair{u"Fraunces"_s, u"Fraunces-Variable.ttf"_s}}) {
        QFile ttf(u":/callie/fonts/"_s + file);
        if (!ttf.open(QIODevice::ReadOnly))
            continue;
        faces += u"@font-face { font-family: '%1'; font-weight: 100 900; "
                 "src: url(data:font/ttf;base64,%2); }\n"_s.arg(
                     family, QString::fromLatin1(ttf.readAll().toBase64()));
    }
    return faces;
}

} // namespace

QString signInPage(const ThemeSpec &theme)
{
    QFile logo(u":/callie/assets/logo.svg"_s);
    const QString svg =
        logo.open(QIODevice::ReadOnly) ? QString::fromUtf8(logo.readAll()) : QString();
    const auto &c = theme.colors;

    // The page says nothing about the outcome: a refused consent lands here
    // too, and the app reports what happened.
    const QString title = QCoreApplication::translate("SignInPage", "All done here");
    const QString body = QCoreApplication::translate(
        "SignInPage",
        "Callie is finishing your sign-in. You can close this tab and head back to the app.");

    // The tab shows the logo and a name, rather than the loopback address.
    const QString icon =
        u"data:image/svg+xml;base64,"_s + QString::fromLatin1(svg.toUtf8().toBase64());

    return uR"(<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%15</title>
<link rel="icon" type="image/svg+xml" href="%16">
<style>
%1
  html, body { margin: 0; height: 100%; }
  body {
    display: flex; align-items: center; justify-content: center;
    background: %2; color: %3; font-family: %4;
  }
  .card {
    max-width: 22rem; margin: 1.5rem; padding: 2.5rem 2rem;
    text-align: center; background: %5; border: 1px solid %6;
    border-radius: %7px; box-shadow: 0 %8px 0 %9;
  }
  .card svg { width: 96px; height: 96px; }
  h1 { margin: 1rem 0 0.5rem; font-family: %10; font-size: 1.75rem; }
  p { margin: 0; color: %11; line-height: 1.5; }
</style>
<div class="card">
%12
  <h1>%13</h1>
  <p>%14</p>
</div>)"_s
        .arg(bundledFonts(), c.background.name(), c.text.name(),
             font(theme.type.family, u"system-ui, sans-serif"_s), c.surface.name(), c.border.name(),
             QString::number(theme.shape.radiusXLarge), QString::number(theme.shape.stickerEdge),
             c.edge.name())
        .arg(font(theme.type.displayFamily, u"Georgia, serif"_s), c.textMuted.name(), svg,
             title.toHtmlEscaped(), body.toHtmlEscaped())
        .arg(QCoreApplication::translate("SignInPage", "Callie"), icon);
}

} // namespace callie
