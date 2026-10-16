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
# With installed=ask, it asks the package manager standing in on $PATH.
installs_on() {
    printf '%s\n' "$@" >"$tmp/os-release"
    if [ "${installed:-}" = ask ]; then
        # shellcheck disable=SC2086
        output="$(PATH="$tmp/bin:$PATH" CALLIE_INSTALL_OS_RELEASE="$tmp/os-release" \
            CALLIE_INSTALL_ROOT="$tmp/root" \
            CALLIE_INSTALL_RELEASE_JSON="${json:-$release}" \
            sh "$script" --dry-run ${options:-} 2>&1)"
    else
        # shellcheck disable=SC2086
        output="$(CALLIE_INSTALL_OS_RELEASE="$tmp/os-release" CALLIE_INSTALL_ROOT="$tmp/root" \
            CALLIE_INSTALL_RELEASE_JSON="${json:-$release}" CALLIE_INSTALL_INSTALLED="${installed:-}" \
            sh "$script" --dry-run ${options:-} 2>&1)"
    fi
    status=$?
}

# A package manager that answers a query with $1 and exits with $2.
answers() {
    mkdir -p "$tmp/bin"
    for tool in rpm dpkg-query; do
        printf '#!/bin/sh\nprintf %%s %s\nexit %s\n' "'$1'" "$2" >"$tmp/bin/$tool"
        chmod +x "$tmp/bin/$tool"
    done
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
expect fedora "would add Callie's repository: /etc/yum.repos.d/callie.repo"

# Debian gets the deb, not the debug symbols, after refreshing apt's lists.
installs_on ID=debian VERSION_ID=13 'PRETTY_NAME="Debian GNU/Linux 13 (trixie)"'
expect_status debian 0
expect debian "/v0.2.1/callie_0.2.1-1_amd64.deb"
expect debian "apt-get update"
expect debian "apt-get install -y ./callie_0.2.1-1_amd64.deb"
expect debian "would add Callie's repository: /usr/share/keyrings/callie.asc /etc/apt/sources.list.d/callie.sources"

# Ubuntu 26.04 gets the deb built for it, and its own part of the apt repository;
# Debian keeps its own deb, though Ubuntu's comes first in the release.
installs_on ID=ubuntu VERSION_ID=26.04 'PRETTY_NAME="Ubuntu 26.04 LTS"'
expect_status ubuntu 0
expect ubuntu "/v0.2.1/callie-ubuntu_0.2.1-1~ubuntu26.04_amd64.deb"
expect ubuntu "sha256 7777777777777777777777777777777777777777777777777777777777777777"
expect ubuntu "apt-get install -y ./callie-ubuntu_0.2.1-1~ubuntu26.04_amd64.deb"
expect ubuntu "from https://callieapp.org/callie.asc and https://callieapp.org/deb/callie-ubuntu.sources"
installs_on ID=debian VERSION_ID=13 'PRETTY_NAME="Debian GNU/Linux 13 (trixie)"'
refuse "debian deb" "ubuntu"
expect "debian sources" "https://callieapp.org/deb/callie.sources"

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
expect_status "ubuntu 24.04" 1
expect "ubuntu 24.04" "older than Ubuntu 26.04"
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
# One installed before the repository existed gets it on running this again.
expect current "would add Callie's repository"
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

# The repository is added once: not again when its files are all there.
mkdir -p "$tmp/root/etc/yum.repos.d" "$tmp/root/usr/share/keyrings"
touch "$tmp/root/etc/yum.repos.d/callie.repo" "$tmp/root/usr/share/keyrings/callie.asc"
installs_on ID=fedora VERSION_ID=44 "$fedora"
refuse "rpm repo added" "would add Callie's repository"
installs_on ID=debian VERSION_ID=13 "$debian"
expect "deb repo half added" "would add Callie's repository"
rm -rf "$tmp/root"

# The installed version comes from rpm or dpkg-query, without the packaging's
# revision; a removed deb that left its configuration is not installed.
installed=ask
answers 0.2.1 0
installs_on ID=fedora VERSION_ID=44 "$fedora"
expect "rpm current" "Callie 0.2.1 is installed and up to date."
answers 'package callie is not installed' 1
installs_on ID=fedora VERSION_ID=44 "$fedora"
expect "rpm missing" "installing Callie 0.2.1 on"
answers 'install ok installed|0.2.0-1' 0
installs_on ID=debian VERSION_ID=13 "$debian"
expect "deb older" "updating Callie 0.2.0 to 0.2.1"
answers 'deinstall ok config-files|0.2.1-1' 0
installs_on ID=debian VERSION_ID=13 "$debian"
expect "deb removed" "installing Callie 0.2.1 on"
unset installed

if [ "$failures" -gt 0 ]; then
    printf '%s check(s) failed\n' "$failures"
    exit 1
fi
printf 'all installer checks passed\n'
