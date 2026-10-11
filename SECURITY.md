# Security policy

## Supported versions

Fixes go into the latest release only. Please check that a problem still happens on the
[latest release](https://github.com/callieapp/callie/releases/latest) before reporting it.

## Reporting a vulnerability

Please do not open a public issue. Report it privately through GitHub instead:
[report a vulnerability](https://github.com/callieapp/callie/security/advisories/new).

Include what you can of:

- the Callie version and how it was installed (rpm, deb, installer script, source)
- what an attacker could do, and what they need to do it
- steps or a file that reproduces it

Callie is maintained by one person, so replies can take a few days. You will hear back
within a week, and once a fix is released the advisory is published with credit to you,
unless you would rather not be named.

## Scope

In scope: the app, the `callie` command, the packages and the installer script at
callieapp.org. That includes how Callie stores account tokens, talks to Google and CalDAV
servers, reads calendar files and subscriptions, and opens links from events.

Out of scope: problems in Google Calendar or a CalDAV server themselves, and in Qt or
other libraries, unless Callie uses them unsafely. Please report those upstream.
