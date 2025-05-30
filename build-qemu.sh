#!/bin/bash

# Script for building qemu + deps on macOS, without using brew
# Run in `build` subdirectory of a qemu checkout.

# Tested with qemu a9cd5bc6399a80fcf233ed0fffe6067b731227d8 (HEAD as of 2025 Apr 15)

set -e

mkdir -p local-install
D=$PWD/local-install

curl -LO https://github.com/libffi/libffi/releases/download/v3.4.7/libffi-3.4.7.tar.gz
tar xzf libffi-3.4.7.tar.gz
mkdir -p build/libffi
pushd build/libffi
../../libffi-3.4.7/configure --prefix=$D
make -j10
make install
popd

# Use a mirror because ftp.gnu.org is unreachable often :/
curl -LO https://ftp.gnu.org/pub/gnu/gettext/gettext-0.21.1.tar.gz
#curl -LO https://mirrors.dotsrc.org/ftp.gnu.org/gnu/gettext/gettext-0.21.1.tar.gz
tar xzf gettext-0.21.1.tar.gz
mkdir -p build/gettext
pushd build/gettext
(
export am_cv_func_iconv_works=yes
../../gettext-0.21.1/configure --prefix=$D
)
make -j10
make install
popd

curl -LO https://github.com/mesonbuild/meson/releases/download/1.1.0/meson-1.1.0.tar.gz
tar xzf meson-1.1.0.tar.gz
MESON=$PWD/meson-1.1.0/meson.py

curl -LO https://download.gnome.org/sources/glib/2.76/glib-2.76.2.tar.xz
tar xJf glib-2.76.2.tar.xz
mkdir -p build/glib
$MESON setup --prefix $D build/glib glib-2.76.2
$MESON compile -C build/glib
$MESON install -C build/glib

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
$MESON setup --prefix $D build/pixman pixman-0.43.4
$MESON compile -C build/pixman
$MESON install -C build/pixman

# qemu 5890258aeeba303704ec1adca415e46067800777 dropped support for built-in slirp;
# we have to build it ourselves if we want networking.
# (qemu builds fine without this, but diags
# "network backend 'user' is not compiled into this binary" when using -netdev
curl -LO https://gitlab.freedesktop.org/slirp/libslirp/-/archive/v4.8.0/libslirp-v4.8.0.tar.gz
tar xzf libslirp-v4.8.0.tar.gz
mkdir -p build/libslirp
# Needs pkg-config on path
PATH=$PATH:$D/bin $MESON setup --prefix $D build/libslirp libslirp-v4.8.0
$MESON compile -C build/libslirp
$MESON install -C build/libslirp

# tomli
python3 -m venv qemu-pyenv
. qemu-pyenv/bin/activate
pip install tomli

# All deps installed!
PATH=$PATH:$D/bin ../configure --prefix=$D
make -j10
make install
