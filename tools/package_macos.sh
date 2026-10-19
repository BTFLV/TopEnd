#!/usr/bin/env bash
# Package the TopEnd plugin bundles into a disk image.
#
# Usage: tools/package_macos.sh <bundle-dir> <version> <output-dir>
#
# Signing is optional and driven by the environment:
#   APPLE_SIGNING_IDENTITY             Developer ID Application identity in an
#                                      unlocked keychain. Without it, bundles are
#                                      ad-hoc signed.
#   APPLE_API_KEY, APPLE_API_ISSUER,   App Store Connect API key ID, issuer ID and
#   APPLE_API_KEY_PATH                 .p8 file. With a signing identity, the
#                                      bundles and the disk image are notarized
#                                      and stapled.
set -euo pipefail

bundles=${1:?bundle directory}
version=${2:?version}
output=${3:?output directory}
name="TopEnd-${version}-macOS-arm64"
root=$(cd "$(dirname "$0")/.." && pwd)
stage="$output/stage/TopEnd $version"
plugins=(TopEnd.component TopEnd.vst3 TopEnd.clap)

rm -rf "$output/stage" "$output/$name.dmg"
mkdir -p "$stage"
for plugin in "${plugins[@]}"; do
  [ -d "$bundles/$plugin" ] || { echo "Missing $bundles/$plugin" >&2; exit 1; }
  ditto "$bundles/$plugin" "$stage/$plugin"
done

signed=false
notarize=false
if [ -n "${APPLE_SIGNING_IDENTITY:-}" ]; then
  signed=true
  if [ -n "${APPLE_API_KEY:-}" ] && [ -n "${APPLE_API_ISSUER:-}" ] && [ -n "${APPLE_API_KEY_PATH:-}" ]; then
    notarize=true
  fi
fi

submit() {
  # Submit a file for notarization and fail with Apple's log if it is rejected.
  local result
  result=$(xcrun notarytool submit "$1" --key "$APPLE_API_KEY_PATH" --key-id "$APPLE_API_KEY" \
    --issuer "$APPLE_API_ISSUER" --wait --timeout 30m --output-format json)
  echo "$result"
  local id status
  id=$(plutil -extract id raw - <<<"$result")
  status=$(plutil -extract status raw - <<<"$result")
  if [ "$status" != "Accepted" ]; then
    xcrun notarytool log "$id" --key "$APPLE_API_KEY_PATH" --key-id "$APPLE_API_KEY" \
      --issuer "$APPLE_API_ISSUER" || true
    echo "Notarization of $1 ended with status $status" >&2
    exit 1
  fi
}

for plugin in "${plugins[@]}"; do
  if $signed; then
    codesign --force --timestamp --options runtime --sign "$APPLE_SIGNING_IDENTITY" "$stage/$plugin"
  else
    codesign --force --sign - "$stage/$plugin"
  fi
  codesign --verify --strict --verbose=2 "$stage/$plugin"
done

if $notarize; then
  # Notarize and staple the bundles themselves, so their tickets travel with
  # them after they are copied out of the disk image.
  ditto -c -k --keepParent "$stage" "$output/stage/bundles.zip"
  submit "$output/stage/bundles.zip"
  rm "$output/stage/bundles.zip"
  for plugin in "${plugins[@]}"; do
    xcrun stapler staple "$stage/$plugin"
  done
fi

cp "$root/LICENSE" "$stage/LICENSE.txt"
cp "$root/assets/THIRD_PARTY_NOTICES.txt" "$stage/THIRD_PARTY_NOTICES.txt"
{
  echo "TopEnd $version for macOS (Apple Silicon)"
  echo
  echo "Install"
  echo "  Copy each plugin into your user plugin folder (Finder: Go > Go to Folder):"
  echo "    TopEnd.component  ->  ~/Library/Audio/Plug-Ins/Components"
  echo "    TopEnd.vst3       ->  ~/Library/Audio/Plug-Ins/VST3"
  echo "    TopEnd.clap       ->  ~/Library/Audio/Plug-Ins/CLAP"
  echo "  Then restart or rescan your audio host."
  echo
  if $notarize; then
    echo "These plugins are signed with a Developer ID and notarized by Apple."
  elif $signed; then
    echo "These plugins are signed with a Developer ID but not notarized."
  else
    echo "These plugins are not signed with a Developer ID. If macOS blocks them,"
    echo "remove the download quarantine after copying, for example:"
    echo "  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/TopEnd.vst3"
  fi
  echo
  echo "Uninstall: delete the three TopEnd bundles from those folders."
} >"$stage/Install.txt"

hdiutil create -quiet -volname "TopEnd $version" -srcfolder "$stage" -fs HFS+ -format UDZO \
  -ov "$output/$name.dmg"
if $signed; then
  codesign --force --timestamp --sign "$APPLE_SIGNING_IDENTITY" "$output/$name.dmg"
fi
if $notarize; then
  submit "$output/$name.dmg"
  xcrun stapler staple "$output/$name.dmg"
  xcrun stapler validate "$output/$name.dmg"
fi
rm -rf "$output/stage"
(cd "$output" && shasum -a 256 "$name.dmg" >SHA256SUMS.txt)
echo "signed=$signed notarized=$notarize"
echo "$output/$name.dmg"
