#!/usr/bin/env bash
# Cuts a release from main: picks the next version, dates the notes in
# CHANGELOG.md, sets the version everywhere it is named, commits, tags and
# pushes. The tag runs the release workflow, which builds and publishes.
#
#   scripts/release.sh patch|minor|major [--dry-run]
#   scripts/release.sh 0.5.0 [--dry-run]
#
# Notes come from a "## Unreleased" section at the top of CHANGELOG.md, written
# as changes land. Without one, git cliff drafts the section and $EDITOR opens
# it to be written up. RELEASE_YES=1 skips the last question.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

level=""
dry=0
for arg in "$@"; do
    case "$arg" in
    --dry-run) dry=1 ;;
    *) level="$arg" ;;
    esac
done
if [ -z "$level" ]; then
    echo "usage: release.sh patch|minor|major|X.Y.Z [--dry-run]" >&2
    exit 2
fi
fail() {
    echo "release: $*" >&2
    exit 1
}

# From main as it is on GitHub, so the tag is on a commit everyone has.
branch="$(git rev-parse --abbrev-ref HEAD)"
[ "$branch" = main ] || fail "must be on main (on $branch)"
[ -z "$(git status --porcelain --untracked-files=no)" ] ||
    fail "tracked changes are not committed; commit or stash them first"
git fetch --quiet origin main
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] ||
    fail "main is not the same as origin/main; pull or push first"

current="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' CMakeLists.txt | head -1)"
IFS=. read -r major minor patch <<<"$current"
case "$level" in
major) next="$((major + 1)).0.0" ;;
minor) next="$major.$((minor + 1)).0" ;;
patch) next="$major.$minor.$((patch + 1))" ;;
[0-9]*.[0-9]*.[0-9]*) next="$level" ;;
*) fail "say patch, minor, major or a version such as 1.2.3, not $level" ;;
esac
[ "$next" != "$current" ] || fail "$current is the version already"
! git rev-parse -q --verify "refs/tags/v$next" >/dev/null || fail "v$next is tagged already"

today="$(date +%F)"
echo "Callie $current to $next, dated $today"

# The notes: the Unreleased section, else a draft to write up.
if grep -q '^## Unreleased *$' CHANGELOG.md; then
    notes="$(awk '/^## / { if (found) exit; if ($0 ~ /^## Unreleased *$/) { found = 1; next } }
        found { print }' CHANGELOG.md)"
    drafted=0
else
    command -v git-cliff >/dev/null || fail "no Unreleased notes in CHANGELOG.md, and no git cliff to draft them"
    notes="$(git cliff --unreleased --tag "v$next" --strip all | sed '1{/^## /d}')"
    drafted=1
fi
# Without the blank lines around it, as the heading brings its own.
notes="$(printf '%s\n' "$notes" | sed -e '/./,$!d')"
[ -n "${notes//[$'\n ']/}" ] || fail "no notes for $next; add them under ## Unreleased in CHANGELOG.md"
if [ "$dry" = 1 ]; then
    echo
    if [ "$drafted" = 1 ]; then
        echo "Notes, drafted by git cliff to be written up:"
    else
        echo "Notes, from Unreleased:"
    fi
    printf '%s\n' "$notes"
    echo
    echo "Would set $next in CMakeLists.txt, the RPM spec, the Debian changelog and the"
    echo "metainfo, commit \"chore: release $next\", tag v$next and push both to origin."
    exit 0
fi

# Dated in CHANGELOG.md, and written up first when drafted.
if [ "$drafted" = 1 ]; then
    draft="$(mktemp)"
    printf '## %s - %s\n\n%s\n\n' "$next" "$today" "$notes" >"$draft"
    awk -v draft="$draft" '
        !done && /^## / { while ((getline line < draft) > 0) print line; done = 1 }
        { print }' CHANGELOG.md >CHANGELOG.md.new
    mv CHANGELOG.md.new CHANGELOG.md
    rm -f "$draft"
    # Split as a shell would, so an editor given with options, as "code --wait", works.
    # shellcheck disable=SC2086
    if ! ${EDITOR:-vi} CHANGELOG.md; then
        git checkout -- CHANGELOG.md
        fail "the editor stopped with an error; nothing was changed"
    fi
else
    sed -i "s/^## Unreleased *\$/## $next - $today/" CHANGELOG.md
fi
notes="$(scripts/release-notes.sh "$next")"

# The maintainer as the packaging names them already.
maintainer="$(sed -n 's/^ -- \(.*>\)  .*/\1/p' packaging/debian/changelog | head -1)"

sed -i "s/^\( *VERSION \)$current\$/\1$next/" CMakeLists.txt
sed -i "s/^\(Version: *\)$current\$/\1$next/" packaging/rpm/callie.spec
spec_entry="* $(LC_ALL=C date '+%a %b %d %Y') $maintainer - $next-1\n- Update to $next\n"
sed -i "s/^%changelog\$/%changelog\n$spec_entry/" packaging/rpm/callie.spec
{
    printf 'callie (%s-1) unstable; urgency=medium\n\n  * New upstream release.\n\n' "$next"
    printf ' -- %s  %s\n\n' "$maintainer" "$(LC_ALL=C date -R)"
    cat packaging/debian/changelog
} >packaging/debian/changelog.new
mv packaging/debian/changelog.new packaging/debian/changelog

# Software centers show each release's first-level notes as a list.
items="$(printf '%s\n' "$notes" | awk '
    function flush() { if (item != "") print item; item = "" }
    /^- / { flush(); item = substr($0, 3); next }
    /^  [^ -]/ && item != "" { sub(/^ +/, ""); item = item " " $0; next }
    { flush() }
    END { flush() }' |
    sed -e 's/`//g' -e 's/\[\([^]]*\)\]([^)]*)/\1/g' \
        -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' \
        -e 's|.*|          <li>&</li>|')"
release="    <release version=\"$next\" date=\"$today\">
      <description>
        <ul>
$items
        </ul>
      </description>
    </release>"
awk -v release="$release" '!done && /<release version=/ { print release; done = 1 } { print }' \
    data/org.callieapp.Callie.metainfo.xml >metainfo.new
mv metainfo.new data/org.callieapp.Callie.metainfo.xml

scripts/check-version.sh "$next"
git --no-pager diff --stat
if [ "${RELEASE_YES:-0}" != 1 ]; then
    printf 'Commit, tag v%s and push to origin? [y/N] ' "$next"
    read -r answer
    case "$answer" in
    y | Y | yes) ;;
    *)
        echo "Stopped; the changes are left in the tree to look over or undo."
        exit 1
        ;;
    esac
fi
git add CHANGELOG.md CMakeLists.txt packaging/rpm/callie.spec packaging/debian/changelog \
    data/org.callieapp.Callie.metainfo.xml
git commit --quiet -m "chore: release $next"
# Annotated, as --follow-tags pushes only annotated tags; atomic, so a refused
# main leaves no tag behind to start a release.
git tag -a "v$next" -m "Callie $next"
git push --atomic --follow-tags origin main
echo "Released v$next; the release workflow builds and publishes it."
