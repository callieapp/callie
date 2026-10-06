# Releasing Callie

1. Set the new version in `CMakeLists.txt`, `packaging/rpm/callie.spec` (with a `%changelog`
   entry), `packaging/debian/changelog` and a new `<release>` in
   `data/org.callieapp.Callie.metainfo.xml`, dated as in `CHANGELOG.md`.
2. Draft the notes with `git cliff --unreleased --tag vX.Y.Z`, then write them up at the top of
   `CHANGELOG.md` under `## X.Y.Z - YYYY-MM-DD`, in words for the people using Callie. The app shows
   this section in "What's new?".
3. Run `scripts/check-version.sh X.Y.Z`; `make check` runs it too.
4. Merge, then tag the merge on `main` with `git tag vX.Y.Z` and push the tag.

Pushing the tag runs the release workflow. It checks the versions again, builds the source tarball
and the `.deb` and `.rpm` packages, and publishes a GitHub release with the notes from
`CHANGELOG.md`.
