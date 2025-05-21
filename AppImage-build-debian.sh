#! /bin/bash
set -e

# Get yuzu source dir
SUMI_SOURCE_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
echo "Source dir is $SUMI_SOURCE_DIR"
rm -rf "$SUMI_SOURCE_DIR/AppImageBuilder/build"

# Generate debian rootfs
cd /tmp
rm -rf rootfs-yuzu-appimage-build
debootstrap stable rootfs-yuzu-appimage-build http://deb.debian.org/debian/
bwrap --bind rootfs-yuzu-appimage-build / \
        --unshare-pid \
        --dev-bind /dev /dev --proc /proc --tmpfs /tmp --ro-bind /sys /sys --dev-bind /run /run \
        --tmpfs /var/tmp \
        --chmod 1777 /tmp \
        --ro-bind /etc/resolv.conf /etc/resolv.conf \
        --ro-bind "$SUMI_SOURCE_DIR" /tmp/yuzu-src \
        --chdir / \
        --tmpfs /home \
        --setenv HOME /home \
        --bind /tmp /tmp/hosttmp \
        /tmp/yuzu-src/AppImage-build-debian-inner.sh
appimagetool /tmp/yuzu-debian-appimage-rootfs /tmp/yuzu.AppImage
echo "AppImage generated at /tmp/yuzu.AppImage! Cleaning up..."
exec rm -rf /tmp/yuzu-debian-appimage-rootfs /tmp/rootfs-yuzu-appimage-build
