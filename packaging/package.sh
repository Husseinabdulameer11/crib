#!/bin/sh
# Pack a built Crib into a release archive.
#
#   packaging/package.sh <target>      e.g. linux-x86_64, macos-universal,
#                                           windows-x86_64
#
# Run from the crib folder after `make`. Writes dist/crib-<target>.tar.gz
# (dist/crib-<target>.zip for windows-*) holding one folder:
#
#   crib-<target>/crib[.exe]  README.md  LICENSE  examples/
#
# The archive names carry no version so that
# https://github.com/<owner>/crib/releases/latest/download/<name> always
# points at the newest release (install.sh relies on that).
set -eu

target=${1:?usage: packaging/package.sh <target>}
name=crib-$target
exe=build/crib
case $target in windows-*) exe=build/crib.exe ;; esac

[ -f "$exe" ] || { echo "package.sh: $exe not found, run make first" >&2; exit 1; }

rm -rf "dist/$name"
mkdir -p "dist/$name"
cp "$exe" "dist/$name/"
cp README.md LICENSE "dist/$name/"
cp -R examples "dist/$name/examples"

cd dist
case $target in
    windows-*) rm -f "$name.zip";    zip -qr "$name.zip" "$name";       echo "wrote dist/$name.zip" ;;
    *)         rm -f "$name.tar.gz"; tar -czf "$name.tar.gz" "$name";   echo "wrote dist/$name.tar.gz" ;;
esac
