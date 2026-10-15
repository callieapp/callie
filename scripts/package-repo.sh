#!/usr/bin/env bash
# Builds Callie's dnf and apt repositories from release packages, signed with
# a key in the current GnuPG keyring:
#
#   scripts/package-repo.sh <packages dir> <output dir> <key fingerprint>
#
# The output holds callie.asc, the public key; rpm/, with the signed packages,
# their metadata and callie.repo; and deb/, with an apt archive for the
# "stable" suite and callie.sources. Needs rpmsign, createrepo_c,
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

mkdir -p "$out/rpm" "$out/deb/pool/main" "$out/deb/dists/stable/main/binary-amd64"
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

cp "$packages"/*.deb "$out/deb/pool/main/"
(
    cd "$out/deb"
    apt-ftparchive packages pool >dists/stable/main/binary-amd64/Packages
    gzip -9kn dists/stable/main/binary-amd64/Packages
    cd dists/stable
    # Written beside, as the listing would otherwise take in its own empty file.
    apt-ftparchive \
        -o APT::FTPArchive::Release::Origin=Callie \
        -o APT::FTPArchive::Release::Label=Callie \
        -o APT::FTPArchive::Release::Suite=stable \
        -o APT::FTPArchive::Release::Codename=stable \
        -o APT::FTPArchive::Release::Architectures=amd64 \
        -o APT::FTPArchive::Release::Components=main \
        release . >../Release.new
    mv ../Release.new Release
    gpg --batch --yes --local-user "$key" --clearsign -o InRelease Release
    gpg --batch --yes --local-user "$key" --armor --detach-sign -o Release.gpg Release
)
cat >"$out/deb/callie.sources" <<EOF
Types: deb
URIs: $site/deb
Suites: stable
Components: main
Architectures: amd64
Signed-By: /usr/share/keyrings/callie.asc
EOF
