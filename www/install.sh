#!/bin/sh
# Callie installer - https://callieapp.org
#
#   curl -fsSL https://callieapp.org/install.sh | sh
#   curl -fsSL https://callieapp.org/install.sh | sh -s -- --dry-run
#
# Installs the latest release's package with the system's package manager, on
# the systems Callie ships packages for: Fedora 44 or later, Debian 13 (trixie)
# or later, and Ubuntu 26.04 or later, on x86_64. Anything else stops before changing anything.
# Run again, it updates Callie, or says it is up to date without downloading.
# It also adds Callie's own dnf or apt repository, so later updates come with
# the system's.
set -eu

repo="greylocklabs/callie"
dry_run=0
force=0
version=""
# Tests stand in for this system and for GitHub with these.
os_release="${CALLIE_INSTALL_OS_RELEASE:-/etc/os-release}"
release_file="${CALLIE_INSTALL_RELEASE_JSON:-}"
# Set, even empty, it stands in for the installed version.
installed_override="${CALLIE_INSTALL_INSTALLED-}"
installed_overridden="${CALLIE_INSTALL_INSTALLED+yes}"
# Prefixed to the system's repository files.
root="${CALLIE_INSTALL_ROOT:-}"
site="https://callieapp.org"

say() { printf 'callie: %s\n' "$*"; }
fail() {
    printf 'callie: %s\n' "$*" >&2
    exit 1
}
unsupported() {
    fail "$1
Packages are built for Fedora 44 or later, Debian 13 (trixie) or later and Ubuntu 26.04 or later,
on x86_64.
On other systems, build Callie from source: https://github.com/$repo#development"
}

while [ $# -gt 0 ]; do
    case "$1" in
    --dry-run) dry_run=1 ;;
    --force) force=1 ;;
    --version)
        [ $# -ge 2 ] || fail "--version needs a version, such as 0.2.1"
        version="$2"
        shift
        ;;
    --version=*) version="${1#--version=}" ;;
    -h | --help)
        printf '%s\n' "usage: install.sh [--dry-run] [--force] [--version X.Y.Z]" \
            "Installs or updates Callie from its GitHub release with dnf or apt, and" \
            "adds Callie's repository so later updates come with the system's." \
            "--version installs that release, older ones included; --force installs" \
            "it again when it is the one installed."
        exit 0
        ;;
    *) fail "unknown option $1; try --help" ;;
    esac
    shift
done

# ---- Is this a system Callie ships for? ----------------------------------

[ "$(uname -s)" = Linux ] || unsupported "Callie is a Linux app, and this is $(uname -s)."
arch="$(uname -m)"
[ "$arch" = x86_64 ] || unsupported "There are no packages for $arch yet."
[ -r "$os_release" ] || unsupported "Cannot tell which Linux this is: $os_release is missing."

ID=""
VERSION_ID=""
PRETTY_NAME=""
# shellcheck disable=SC1090
. "$os_release"
system="${PRETTY_NAME:-$ID}"
major="${VERSION_ID%%.*}"

case "$ID" in
fedora)
    case "$major" in
    '' | *[!0-9]*) unsupported "Cannot read the Fedora version of $system." ;;
    esac
    [ "$major" -ge 44 ] || unsupported "$system is older than Fedora 44."
    kind=rpm
    ;;
debian)
    # Testing and unstable have no VERSION_ID; they are newer than trixie.
    if [ -n "$major" ]; then
        case "$major" in
        *[!0-9]*) unsupported "Cannot read the Debian version of $system." ;;
        esac
        [ "$major" -ge 13 ] || unsupported "$system is older than Debian 13 (trixie)."
    fi
    kind=deb
    ;;
ubuntu)
    case "$major" in
    '' | *[!0-9]*) unsupported "Cannot read the Ubuntu version of $system." ;;
    esac
    [ "$major" -ge 26 ] ||
        unsupported "$system is older than Ubuntu 26.04, the first with the Qt Callie needs."
    kind=deb
    ;;
*)
    unsupported "$system is not one Callie ships packages for."
    ;;
esac

# ---- Which file, and how to install it -----------------------------------

sudo=""
needs_root=0
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        sudo="sudo"
    else
        needs_root=1
    fi
fi

if [ -n "$version" ]; then
    api="https://api.github.com/repos/$repo/releases/tags/v${version#v}"
else
    api="https://api.github.com/repos/$repo/releases/latest"
fi
if [ -n "$release_file" ]; then
    release="$(cat "$release_file")"
else
    command -v curl >/dev/null 2>&1 || fail "curl is needed to download Callie."
    release="$(curl --proto '=https' --tlsv1.2 -fsSL "$api")" ||
        fail "Could not read the release from GitHub (${version:-latest})."
fi

# The release lists each file's name, digest and download address, in that
# order; the package is the one that is not source, debug info or debug symbols.
case "$kind" in
rpm) pattern='callie-[0-9][^"]*\.x86_64\.rpm' ;;
deb) pattern='callie_[0-9][^"]*_amd64\.deb' ;;
esac
# Named apart, so an installer from before Ubuntu's debs never takes one for Debian.
[ "$ID" != ubuntu ] || pattern='callie-ubuntu_[0-9][^"]*_amd64\.deb'
package="$(printf '%s\n' "$release" | grep -o "\"name\": *\"$pattern\"" | head -n 1 |
    sed 's/.*"\([^"]*\)"$/\1/')"
[ -n "$package" ] || fail "The release has no $kind package for $arch."
# Only the package's own digest: its entry ends at its download address.
digest="$(printf '%s\n' "$release" | awk -v name="\"$package\"" '
    index($0, "\"name\"") && index($0, name) { found = 1; next }
    found && index($0, "browser_download_url") { exit }
    found && match($0, /sha256:[0-9a-f]+/) { print substr($0, RSTART + 7, RLENGTH - 7); exit }')"
[ -n "$digest" ] || fail "GitHub gave no checksum for $package, so it cannot be checked."
tag="$(printf '%s\n' "$release" | grep -o '"tag_name": *"[^"]*"' | head -n 1 |
    sed 's/.*"\([^"]*\)"$/\1/')"
url="https://github.com/$repo/releases/download/$tag/$package"
target="${tag#v}"

# ---- What is installed now -----------------------------------------------

if [ -n "$installed_overridden" ]; then
    installed="$installed_override"
else
    installed=""
    case "$kind" in
    rpm) installed="$(rpm -q --qf '%{VERSION}' callie 2>/dev/null)" || installed="" ;;
    deb)
        state="$(dpkg-query -W -f='${Status}|${Version}' callie 2>/dev/null)" || state=""
        case "$state" in
        "install ok installed|"*) installed="${state#*|}" ;;
        esac
        ;;
    esac
    # Only the release, without the packaging's own revision.
    installed="${installed%%-*}"
fi

# Callie's repository, added once; failing to add it leaves Callie installed.
case "$kind" in
rpm) repo_files="/etc/yum.repos.d/callie.repo" ;;
deb) repo_files="/usr/share/keyrings/callie.asc /etc/apt/sources.list.d/callie.sources" ;;
esac
# Ubuntu's section of the apt repository holds the debs built for it.
sources=callie.sources
[ "$ID" != ubuntu ] || sources=callie-ubuntu.sources
add_repo() {
    present=1
    for file in $repo_files; do
        [ -e "$root$file" ] || present=0
    done
    [ "$present" -eq 0 ] || return 0
    if [ "$dry_run" -eq 1 ]; then
        from="$site/rpm/callie.repo"
        [ "$kind" = rpm ] || from="$site/callie.asc and $site/deb/$sources"
        say "would add Callie's repository: $repo_files, from $from"
        return 0
    fi
    if [ "$needs_root" -eq 1 ]; then
        say "Callie's repository was not added: that needs root."
        return 0
    fi
    get="$sudo curl --proto =https --tlsv1.2 -fsSL"
    case "$kind" in
    rpm) $get -o "$root/etc/yum.repos.d/callie.repo" "$site/rpm/callie.repo" ;;
    deb)
        $get -o "$root/usr/share/keyrings/callie.asc" "$site/callie.asc" &&
            $get -o "$root/etc/apt/sources.list.d/callie.sources" "$site/deb/$sources"
        ;;
    esac || {
        say "Callie's repository could not be added; callie upgrade still updates Callie."
        return 0
    }
    say "Added Callie's repository; updates now come with the system's."
}

# Whether version $1 comes before version $2.
older() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n 1)" = "$1" ]; }

action=install
if [ -n "$installed" ]; then
    if [ "$installed" = "$target" ]; then
        if [ "$force" -eq 0 ]; then
            say "Callie $target is installed and up to date."
            add_repo
            exit 0
        fi
        action=reinstall
    elif older "$target" "$installed"; then
        # Only on request: the latest release can trail a build from source.
        if [ -z "$version" ]; then
            say "Callie $installed is installed, newer than the latest release, $target."
            add_repo
            exit 0
        fi
        action=downgrade
    else
        action=update
    fi
fi

# apt resolves Callie's dependencies from its package lists, which may be
# stale or, on a fresh system, empty; dnf refreshes its own when they expire.
refresh=""
case "$kind:$action" in
rpm:reinstall) install="dnf reinstall -y" ;;
rpm:downgrade) install="dnf downgrade -y" ;;
rpm:*) install="dnf install -y" ;;
deb:*)
    refresh="apt-get update"
    install="apt-get install -y"
    case "$action" in
    reinstall) install="$install --reinstall" ;;
    downgrade) install="$install --allow-downgrades" ;;
    esac
    ;;
esac
if [ -n "$sudo" ]; then
    install="$sudo $install"
    [ -z "$refresh" ] || refresh="$sudo $refresh"
fi

case "$action" in
install) say "installing Callie $target on $system" ;;
update) say "updating Callie $installed to $target on $system" ;;
reinstall) say "installing Callie $target again on $system" ;;
downgrade) say "going back from Callie $installed to $target on $system" ;;
esac
say "package: $url"
if [ "$dry_run" -eq 1 ]; then
    say "would check it against sha256 $digest"
    [ -z "$refresh" ] || say "then run: $refresh"
    say "then run: $install ./$package"
    add_repo
    exit 0
fi

# ---- Download, check and install -----------------------------------------

[ "$needs_root" -eq 0 ] || fail "Installing needs root: run as root, or install sudo."
command -v curl >/dev/null 2>&1 || fail "curl is needed to download Callie."
command -v sha256sum >/dev/null 2>&1 || fail "sha256sum is needed to check the package."

dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT INT TERM
curl --proto '=https' --tlsv1.2 -fL --progress-bar -o "$dir/$package" "$url" ||
    fail "Could not download $package."
printf '%s  %s\n' "$digest" "$dir/$package" | sha256sum -c --status ||
    fail "$package does not match its checksum, so it was not installed."
# Piped into sh, this script is the package manager's input too, so it gets
# none: -y answers its questions, and sudo asks for a password on the terminal.
if [ -n "$refresh" ]; then
    $refresh </dev/null || fail "Could not refresh the package lists with: $refresh"
fi
(cd "$dir" && $install "./$package") </dev/null || fail "The package manager could not install $package."

say "Callie $target is installed. Open it from your app menu, or run callie-gui."
add_repo
