#!/bin/bash

# Script for building qemu + deps on macOS, without using brew
# Run in `build` subdirectory of a qemu checkout.

# Tested with qemu 7c18f2d663521f1b31b821a13358ce38075eaf7d (HEAD as of 2023 May 2)

set -e

mkdir -p local-install
D=$PWD/local-install

curl -LO https://github.com/libffi/libffi/releases/download/v3.4.4/libffi-3.4.4.tar.gz
tar xzf libffi-3.4.4.tar.gz
mkdir -p build/libffi
pushd build/libffi
../../libffi-3.4.4/configure --prefix=$D
make -j10
make install
popd

curl -LO https://ftp.gnu.org/pub/gnu/gettext/gettext-0.21.1.tar.gz
tar xzf gettext-0.21.1.tar.gz
mkdir -p build/gettext
pushd build/gettext
../../gettext-0.21.1/configure --prefix=$D
make -j10
make install
popd

curl -LO https://github.com/mesonbuild/meson/releases/download/1.1.0/meson-1.1.0.tar.gz
tar xzf meson-1.1.0.tar.gz

curl -LO https://download.gnome.org/sources/glib/2.76/glib-2.76.2.tar.xz
tar xJf glib-2.76.2.tar.xz
# Doesn't seem to support out-of-sourcedir builds, so build in a subfolder of glib.
pushd glib-2.76.2
../meson-1.1.0/meson.py setup --prefix $D _build
../meson-1.1.0/meson.py compile -C _build
../meson-1.1.0/meson.py install -C _build
popd

curl -LO https://pkgconfig.freedesktop.org/releases/pkg-config-0.29.2.tar.gz
tar xzf pkg-config-0.29.2.tar.gz
mkdir -p build/pkg-config
pushd build/pkg-config
# FIXME: glib's installed, so it'd be nice to figure out how to make
#        things go without --with-internal-glib.
# Wno-int-conversion for https://gitlab.freedesktop.org/pkg-config/pkg-config/-/issues/81
CFLAGS="-Wno-int-conversion" CXXFLAGS="-Wno-int-conversion" \
    ../../pkg-config-0.29.2/configure --prefix=$D --with-internal-glib
make -j10
make install
popd

curl -LO https://www.cairographics.org/releases/pixman-0.42.2.tar.gz
# Apply fix for https://gitlab.freedesktop.org/pixman/pixman/-/issues/46
curl -L -o pixman.patch https://gitlab.freedesktop.org/pixman/pixman/-/merge_requests/71.patch
# The patch touches Makefile.am in a no-op way (sorts files), which requires automake.
# Remove the change to Makefile.am.
sed -i '' 22,37d pixman.patch
# Upstream renamed test/utils.h to test/utils/utils.h, but 42.2 doesn't have that yet.
sed -i '' 's/utils.utils/utils/g' pixman.patch
tar xzf pixman-0.42.2.tar.gz
pushd pixman-0.42.2
patch -p1 < ../pixman.patch
popd
mkdir -p build/pixman
pushd build/pixman
../../pixman-0.42.2/configure --prefix=$D
make -j10
make install
popd

# All deps installed!
PATH=$PATH:$D/bin ../configure --prefix=$D
make -j10
make install
