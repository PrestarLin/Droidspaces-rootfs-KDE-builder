#!/bin/bash
# Rebuild and reinstall the custom ksystemstats plugins:
#   1. ksystemstats_plugin_kgslgpu.so      - Qualcomm Adreno (KGSL) GPU
#   2. ksystemstats_plugin_containerio.so  - disk + network from /proc (container-safe)
# Run as the normal user; prompts for sudo password when needed.
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-g++}
MOC=/usr/lib/qt6/libexec/moc
QTDIR=/usr/include/aarch64-linux-gnu/qt6
KF6DIR=/usr/include/KF6/KCoreAddons
SYSSTATS_LIB=/usr/lib/aarch64-linux-gnu/libKSysGuardSystemStats.so.6.6.5
PLUGDIR=/usr/lib/aarch64-linux-gnu/qt6/plugins/ksystemstats

BASE_INC="-I. -I$QTDIR -I$QTDIR/QtCore -I$QTDIR/QtDBus -I$KF6DIR"

build_one() {
    local srcdir="$1" src="$2" name="$3" extra_inc="$4" extra_lib="$5"
    echo "== building $name"
    ( cd "$srcdir"
      "$MOC" $BASE_INC $extra_inc "$src" -o "${src%.cpp}.moc"
      "$CXX" -std=c++17 -fPIC -Wno-deprecated-declarations $BASE_INC $extra_inc \
          -I"$OLDPWD" -c "$src" -o "$src.o"
      "$CXX" -fPIC -shared -o "$name.so" "$src.o" \
          -Wl,-rpath-link,/usr/lib/aarch64-linux-gnu "$SYSSTATS_LIB" \
          -lKF6CoreAddons -lQt6DBus -lQt6Core $extra_lib
      rm -f "${src%.cpp}.moc" "$src.o"
    )
    echo "   installing $name"
    sudo cp "$srcdir/$name.so" "$PLUGDIR/"
    sudo chmod 644 "$PLUGDIR/$name.so"
}

build_one . kgslgpuplugin.cpp ksystemstats_plugin_kgslgpu "" ""
build_one container containerplugin.cpp ksystemstats_plugin_containerio \
    "-I$QTDIR/QtNetwork" "-lQt6Network"

echo "== restarting ksystemstats"
systemctl --user restart plasma-ksystemstats.service
sleep 2
echo "== done. GPU/disk/network sensors now available."