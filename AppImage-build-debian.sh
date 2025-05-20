#! /bin/bash
set -e

# Get sumi source dir
sumi_SOURCE_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
echo "Source dir is $sumi_SOURCE_DIR"
rm -rf "$sumi_SOURCE_DIR/AppImageBuilder/build"

# Generate debian rootfs
cd /tmp
rm -rf rootfs-sumi-appimage-build
debootstrap stable rootfs-sumi-appimage-build http://deb.debian.org/debian/
bwrap --bind rootfs-sumi-appimage-build / \
        --unshare-pid \
        --dev-bind /dev /dev --proc /proc --tmpfs /tmp --ro-bind /sys /sys --dev-bind /run /run \
        --tmpfs /var/tmp \
        --chmod 1777 /tmp \
        --ro-bind /etc/resolv.conf /etc/resolv.conf \
        --ro-bind "$sumi_SOURCE_DIR" /tmp/sumi-src \
        --chdir / \
        --tmpfs /home \
        --setenv HOME /home \
        --bind /tmp /tmp/hosttmp \
        /tmp/sumi-src/AppImage-build-debian-inner.sh
appimagetool /tmp/sumi-debian-appimage-rootfs /tmp/sumi.AppImage
echo "AppImage generated at /tmp/sumi.AppImage! Cleaning up..."
exec rm -rf /tmp/sumi-debian-appimage-rootfs /tmp/rootfs-sumi-appimage-build
