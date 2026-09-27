# SPDX-License-Identifier: GPL-3.0-only
# Maintainer: saijsaij

pkgname=priomix
pkgver=0.1.0
_headsetcontrol_tag=4.0.0
pkgrel=1
pkgdesc="Audio device priority daemon with ChatMix for PipeWire, plus Qt settings GUI"
arch=('x86_64')
url="https://github.com/saijsaij/priomix"
license=('GPL-3.0-only')
depends=(
  'gcc-libs'
  'glibc'
  'hidapi'
  'kglobalaccel'
  'pipewire'
  'qt6-base'
  'sdbus-cpp'
  'systemd-libs'
  'wireplumber'
)
makedepends=('cmake' 'git' 'ninja')
checkdepends=('gtest')
optdepends=(
  'headsetcontrol: udev rules for non-root access to supported headsets'
  'upower: poll headsets less often on battery power'
)
# libheadsetcontrol is linked statically from source: the static library in Arch's
# headsetcontrol package consists of stripped slim-LTO objects and cannot be linked.
source=("git+https://github.com/saijsaij/priomix.git#tag=v${pkgver}"
        "git+https://github.com/Sapd/HeadsetControl.git#tag=${_headsetcontrol_tag}")
sha256sums=('SKIP'
            'SKIP')

build() {
  cmake -B build -S "${pkgname}" -G Ninja \
    -DCMAKE_BUILD_TYPE=None \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DPRIOMIX_HEADSETCONTROL=BUNDLED \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_HEADSETCONTROL="${srcdir}/HeadsetControl" \
    -Wno-dev
  cmake --build build
}

check() {
  # Integration tests start their own PipeWire session, which a build chroot may not
  # allow; they are run during development with `ctest -L integration`.
  ctest --test-dir build --output-on-failure -LE integration
}

package() {
  DESTDIR="${pkgdir}" cmake --install build
  install -Dm644 "${pkgname}/LICENSE" "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
}
