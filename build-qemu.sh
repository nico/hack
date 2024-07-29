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

curl -LO https://www.cairographics.org/releases/pixman-0.43.4.tar.gz
tar xzf pixman-0.43.4.tar.gz
mkdir -p build/pixman
meson-1.1.0/meson.py setup --prefix $D build/pixman pixman-0.43.4
meson-1.1.0/meson.py compile -C build/pixman
meson-1.1.0/meson.py install -C build/pixman

# qemu 5890258aeeba303704ec1adca415e46067800777 dropped support for built-in slirp;
# we have to build it ourselves if we want networking.
# (qemu builds fine without this, but diags
# "network backend 'user' is not compiled into this binary" when using -netdev
curl -LO https://gitlab.freedesktop.org/slirp/libslirp/-/archive/v4.8.0/libslirp-v4.8.0.tar.gz
tar xzf libslirp-v4.8.0.tar.gz
mkdir -p build/libslirp
# Needs pkg-config on path
PATH=$PATH:$D/bin meson-1.1.0/meson.py setup --prefix $D build/libslirp libslirp-v4.8.0
meson-1.1.0/meson.py compile -C build/libslirp
meson-1.1.0/meson.py install -C build/libslirp

# All deps installed!
PATH=$PATH:$D/bin ../configure --prefix=$D
make -j10
make install
