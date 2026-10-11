#!/bin/sh
# Checks www/install.sh against stand-in systems and a stand-in GitHub release,
# with --dry-run, so nothing is downloaded or installed.
#
#   tests/install_test.sh www/install.sh tests/data/github-release.json
set -u

script="$1"
release="$2"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
failures=0

# Runs the installer on a system whose /etc/os-release holds the arguments,
# with Callie ${installed:-not} installed and the installer given ${options:-nothing}.
installs_on() {
    printf '%s\n' "$@" >"$tmp/os-release"
    # shellcheck disable=SC2086
    output="$(CALLIE_INSTALL_OS_RELEASE="$tmp/os-release" \
        CALLIE_INSTALL_RELEASE_JSON="${json:-$release}" CALLIE_INSTALL_INSTALLED="${installed:-}" \
        sh "$script" --dry-run ${options:-} 2>&1)"
    status=$?
}

refuse() {
    case "$output" in
    *"$2"*)
        printf 'FAIL %s: did not expect "%s" in:\n%s\n\n' "$1" "$2" "$output"
        failures=$((failures + 1))
        ;;
    esac
}

expect() {
    case "$output" in
    *"$2"*) ;;
    *)
        printf 'FAIL %s: expected "%s" in:\n%s\n\n' "$1" "$2" "$output"
        failures=$((failures + 1))
        ;;
    esac
}

expect_status() {
    if [ "$status" -ne "$2" ]; then
        printf 'FAIL %s: exited %s, expected %s:\n%s\n\n' "$1" "$status" "$2" "$output"
        failures=$((failures + 1))
    fi
}

# Fedora gets the binary rpm, not the source, debuginfo or debugsource ones,
# with the checksum GitHub gave for that file.
installs_on ID=fedora VERSION_ID=44 'PRETTY_NAME="Fedora Linux 44"'
expect_status fedora 0
expect fedora "/v0.2.1/callie-0.2.1-1.fc44.x86_64.rpm"
expect fedora "sha256 350e52621a778c23e0946ffef28b7e718442db280b9ea2888fc22d4892f8c345"
expect fedora "dnf install -y ./callie-0.2.1-1.fc44.x86_64.rpm"

# Debian gets the deb, not the debug symbols, after refreshing apt's lists.
installs_on ID=debian VERSION_ID=13 'PRETTY_NAME="Debian GNU/Linux 13 (trixie)"'
expect_status debian 0
expect debian "/v0.2.1/callie_0.2.1-1_amd64.deb"
expect debian "apt-get update"
expect debian "apt-get install -y ./callie_0.2.1-1_amd64.deb"

# Testing and unstable carry no version; they are newer than trixie.
installs_on ID=debian 'PRETTY_NAME="Debian GNU/Linux forky/sid"'
expect_status "debian testing" 0
expect "debian testing" "callie_0.2.1-1_amd64.deb"

# Systems without packages stop, saying why.
installs_on ID=fedora VERSION_ID=43 'PRETTY_NAME="Fedora Linux 43"'
expect_status "fedora 43" 1
expect "fedora 43" "older than Fedora 44"
installs_on ID=debian VERSION_ID=12 'PRETTY_NAME="Debian GNU/Linux 12 (bookworm)"'
expect_status "debian 12" 1
expect "debian 12" "older than Debian 13"
installs_on ID=ubuntu VERSION_ID=24.04 'PRETTY_NAME="Ubuntu 24.04 LTS"'
expect_status ubuntu 1
expect ubuntu "Callie needs Qt 6.8"
installs_on ID=arch 'PRETTY_NAME="Arch Linux"'
expect_status arch 1
expect arch "not one Callie ships packages for"

# A package without a checksum is not installed.
json="$tmp/no-digest.json"
awk '/"name": "callie-0\.2\.1-1\.fc44\.x86_64\.rpm"/ { skip = 1 }
    skip && /"digest"/ { skip = 0; next }
    { print }' "$release" >"$json"
installs_on ID=fedora VERSION_ID=44 'PRETTY_NAME="Fedora Linux 44"'
expect_status "no digest" 1
expect "no digest" "no checksum for callie-0.2.1-1.fc44.x86_64.rpm"
unset json

# Run again, it updates, or stops before downloading when there is nothing to do.
fedora='PRETTY_NAME="Fedora Linux 44"'
debian='PRETTY_NAME="Debian GNU/Linux 13 (trixie)"'
installed=0.2.0
installs_on ID=fedora VERSION_ID=44 "$fedora"
expect_status update 0
expect update "updating Callie 0.2.0 to 0.2.1"
expect update "dnf install -y ./callie-0.2.1-1.fc44.x86_64.rpm"

installed=0.2.1
installs_on ID=debian VERSION_ID=13 "$debian"
expect_status current 0
expect current "Callie 0.2.1 is installed and up to date."
refuse current "package:"
options=--force
installs_on ID=debian VERSION_ID=13 "$debian"
expect "forced" "installing Callie 0.2.1 again"
expect "forced" "apt-get install -y --reinstall ./callie_0.2.1-1_amd64.deb"
installs_on ID=fedora VERSION_ID=44 "$fedora"
expect "forced rpm" "dnf reinstall -y ./callie-0.2.1-1.fc44.x86_64.rpm"
unset options

# Going back takes asking for the version, and works the same on both.
installed=0.2.2
installs_on ID=debian VERSION_ID=13 "$debian"
expect_status "newer" 0
expect "newer" "Callie 0.2.2 is installed, newer than the latest release, 0.2.1."
refuse "newer" "package:"
options="--version 0.2.1"
installs_on ID=debian VERSION_ID=13 "$debian"
expect "downgrade" "going back from Callie 0.2.2 to 0.2.1"
expect "downgrade" "apt-get install -y --allow-downgrades ./callie_0.2.1-1_amd64.deb"
installs_on ID=fedora VERSION_ID=44 "$fedora"
expect "downgrade rpm" "dnf downgrade -y ./callie-0.2.1-1.fc44.x86_64.rpm"
unset options installed

if [ "$failures" -gt 0 ]; then
    printf '%s check(s) failed\n' "$failures"
    exit 1
fi
printf 'all installer checks passed\n'
