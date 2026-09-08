#!/usr/bin/env bash
# Build the Linux AppImage from a configured+built tree.
#
#   packaging/linux/appimage.sh <build-dir> [out-dir]
#
# Installs the app component into <build-dir>/AppDir with the GNU layout the
# CMake install rules produce (usr/bin, usr/share/applications, hicolor
# icons), then lets linuxdeploy collect the shared libraries the binary needs
# (libcurl and friends; glibc, X11, Mesa and the like stay on the host per
# the AppImage exclude list) and write TackleBox-<version>-x86_64.AppImage.
#
# Build on the oldest distro you want to support: the bundled glibc floor is
# the builder's (the release workflow uses ubuntu-22.04, glibc 2.35).
set -euo pipefail

build_dir=${1:?usage: appimage.sh <build-dir> [out-dir]}
out_dir=${2:-$build_dir}
build_dir=$(cd "$build_dir" && pwd)
mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)

version=$(grep -oE 'project\(TackleBox VERSION [0-9.]+' "$(dirname "$0")/../../CMakeLists.txt" | grep -oE '[0-9.]+$')
arch=$(uname -m)
tools=${LINUXDEPLOY_DIR:-$build_dir/linuxdeploy}
mkdir -p "$tools"

fetch() {  # fetch <url> <file>
  if [ ! -x "$tools/$2" ]; then
    curl -fsSL --retry 3 -o "$tools/$2" "$1"
    chmod +x "$tools/$2"
  fi
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$arch.AppImage" \
      "linuxdeploy-$arch.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-$arch.AppImage" \
      "linuxdeploy-plugin-appimage-$arch.AppImage"

appdir=$build_dir/AppDir
rm -rf "$appdir"
cmake --install "$build_dir" --component app --prefix "$appdir/usr"

# No FUSE on CI runners: run the tools extracted.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$tools:$PATH"
export LINUXDEPLOY_OUTPUT_VERSION=$version
# AppImageUpdate metadata: points at the newest release's .zsync file.
export LDAI_UPDATE_INFORMATION="gh-releases-zsync|doyen-games|tacklebox|latest|TackleBox-*-$arch.AppImage.zsync"
export OUTPUT="$out_dir/TackleBox-$version-$arch.AppImage"

"$tools/linuxdeploy-$arch.AppImage" \
  --appdir "$appdir" \
  --desktop-file "$appdir/usr/share/applications/tacklebox.desktop" \
  --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/tacklebox.png" \
  --output appimage

ls -l "$out_dir"/TackleBox-*.AppImage*
