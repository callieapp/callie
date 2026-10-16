#!/usr/bin/env bash
# Builds Callie's dnf and apt repositories from release packages, signed with
# a key in the current GnuPG keyring:
#
#   scripts/package-repo.sh <packages dir> <output dir> <key fingerprint>
#
# The output holds callie.asc, the public key; rpm/, with the signed packages,
# their metadata and callie.repo; and deb/, an apt archive with Debian's
# packages in the "stable" suite, named by callie.sources, and Ubuntu's in the
# "ubuntu" suite, named by callie-ubuntu.sources. Needs rpmsign, createrepo_c,
# apt-ftparchive and gpg.
set -euo pipefail

if [ $# -ne 3 ]; then
    echo "usage: package-repo.sh <packages dir> <output dir> <key fingerprint>" >&2
    exit 2
fi
packages="$1"
out="$2"
key="$3"
# Tests point the repo files at a local server.
site="${CALLIE_REPO_URL:-https://callieapp.org}"

mkdir -p "$out/rpm"
gpg --armor --export "$key" >"$out/callie.asc"

cp "$packages"/*.rpm "$out/rpm/"
rpmsign --addsign --define "_gpg_name $key" "$out"/rpm/*.rpm >/dev/null
createrepo_c --quiet "$out/rpm"
gpg --batch --yes --local-user "$key" --armor --detach-sign "$out/rpm/repodata/repomd.xml"
cat >"$out/rpm/callie.repo" <<EOF
[callie]
name=Callie
baseurl=$site/rpm/
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=$site/callie.asc
EOF

# An apt suite from the debs given, with a pool of its own and the sources file
# that names it: suite <name> <pool> <sources file> <deb>...
suite() {
    local name="$1" pool="$2" sources="$3"
    shift 3
    mkdir -p "$out/deb/pool/$pool" "$out/deb/dists/$name/main/binary-amd64"
    cp "$@" "$out/deb/pool/$pool/"
    (
        cd "$out/deb"
        apt-ftparchive packages "pool/$pool" >"dists/$name/main/binary-amd64/Packages"
        gzip -9kn "dists/$name/main/binary-amd64/Packages"
        cd "dists/$name"
        # Written beside, as the listing would otherwise take in its own empty file.
        apt-ftparchive \
            -o APT::FTPArchive::Release::Origin=Callie \
            -o APT::FTPArchive::Release::Label=Callie \
            -o "APT::FTPArchive::Release::Suite=$name" \
            -o "APT::FTPArchive::Release::Codename=$name" \
            -o APT::FTPArchive::Release::Architectures=amd64 \
            -o APT::FTPArchive::Release::Components=main \
            release . >../Release.new
        mv ../Release.new Release
        gpg --batch --yes --local-user "$key" --clearsign -o InRelease Release
        gpg --batch --yes --local-user "$key" --armor --detach-sign -o Release.gpg Release
    )
    cat >"$out/deb/$sources" <<EOF
Types: deb
URIs: $site/deb
Suites: $name
Components: main
Architectures: amd64
Signed-By: /usr/share/keyrings/callie.asc
EOF
}

# Debian's debs, and Ubuntu's, built against Ubuntu's own libraries.
suite stable main callie.sources "$packages"/callie_*.deb
shopt -s nullglob
ubuntu=("$packages"/callie-ubuntu_*.deb)
if [ ${#ubuntu[@]} -gt 0 ]; then
    suite ubuntu ubuntu callie-ubuntu.sources "${ubuntu[@]}"
fi
