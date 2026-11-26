#!/bin/sh
# Creates the project's Android release signing key (once). The certificate names the project,
# not a person. Keep the .jks and its password safe (a password manager): every future release
# must be signed with this same key, or installed copies can't update.
#
#   android/make-release-key.sh [out.jks]
#
# Then sign release builds with it (android/README.md, Release signing).
set -eu
out="${1:-gt4r-release.jks}"
if [ -e "$out" ]; then
    echo "$out already exists; not overwriting it." >&2
    exit 1
fi
keytool -genkeypair -v -keystore "$out" -storetype PKCS12 -alias gt4r \
    -keyalg RSA -keysize 4096 -validity 36500 \
    -dname "CN=GT4 Revisited, O=GT4 Revisited"
echo "Created $out (alias gt4r). Do not commit it."
