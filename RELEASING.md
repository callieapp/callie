# Releasing Callie

Notes for the next release collect under `## Unreleased` at the top of `CHANGELOG.md` as changes
land, in words for the people using Callie; the app shows each release's section in "What's new?".
With everything for the release on `main`:

```sh
make release-dry minor   # what it would do, changing nothing
make release minor       # or patch or major; VERSION=1.2.3 names one
```

`make release` runs only on `main`, clean and level with `origin/main`. It dates the Unreleased
notes, or has `git cliff` draft them and opens `$EDITOR` to write them up when there are none. Then
it sets the version in `CMakeLists.txt`, `packaging/rpm/callie.spec`, `packaging/debian/changelog`
and the metainfo, whose release lists the notes' first-level points, runs `scripts/check-version.sh`
and `make check`, and asks before it commits `chore: release X.Y.Z`, tags `vX.Y.Z` and pushes both.
When `make check` fails, it puts the version back and leaves the notes under `## Unreleased`; notes
it drafted are then uncommitted, to commit and push before releasing again. The push goes straight
to `main`, which repository admins may do.

Pushing the tag runs the release workflow. It checks the versions again, builds the source tarball
and the `.deb` and `.rpm` packages, and publishes a GitHub release with the notes from
`CHANGELOG.md`. Then it starts the site's deploy, which builds Callie's dnf and apt repositories at
`callieapp.org/rpm` and `callieapp.org/deb` from the packages of the last ten releases, signed with
the key in the `CALLIE_REPO_SIGNING_KEY` secret. Without that secret, the site goes out without
them.

The packages take the Google OAuth client from the repository's `CALLIE_GOOGLE_CLIENT_ID` and
`CALLIE_GOOGLE_CLIENT_SECRET` Actions secrets, so the installed app can sign in. A tag build without
them fails rather than publish packages that cannot connect a Google account. The Microsoft app id
comes from the `CALLIE_MICROSOFT_CLIENT_ID` secret the same way; without it the packages build, and
only Microsoft accounts cannot connect.
