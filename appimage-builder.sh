#!/bin/bash
# reg_server, 2024-01-09

set -e

# check arguments
if [[ $# != 2 ]]; then
    >&2 echo "Invalid arguments!"
    echo "Usage: $0 torzu|sumi <build dir>"
    exit 1
fi

BUILD_APP="$1"
if [[ "${BUILD_APP}" != "torzu" && "${BUILD_APP}" != "sumi" ]]; then
    >&2 echo "Invalid arguments!"
    echo "Usage: $0 torzu|sumi <build dir>"
    exit 1
fi

BUILD_DIR=$(realpath "$2")
if [[ ! -d "${BUILD_DIR}" ]]; then
    >&2 echo "Invalid arguments!"
    echo "'$2' is not a directory"
    exit 1
fi

DEPLOY_LINUX_FOLDER="${BUILD_DIR}/deploy-linux"
DEPLOY_LINUX_APPDIR_FOLDER="${BUILD_DIR}/deploy-linux/AppDir"
BIN_FOLDER="${BUILD_DIR}/bin"
BIN_EXE="${BIN_FOLDER}/${BUILD_APP//torzu/yuzu}"

CPU_ARCH=$(uname -m)

#export DISPLAYVERSION="1.2.3" # before cmake

BIN_EXE_MIME_TYPE=$(file -b --mime-type "${BIN_EXE}")
if [[ "${BIN_EXE_MIME_TYPE}" != "application/x-pie-executable" && "${BIN_EXE_MIME_TYPE}" != "application/x-executable" ]]; then
    >&2 echo "Invalid or missing main executable (${BIN_EXE})!"
    exit 1
fi

mkdir -p "${DEPLOY_LINUX_FOLDER}"
rm -rf "${DEPLOY_LINUX_APPDIR_FOLDER}"

cd "${BUILD_DIR}"

# deploy/install to deploy-linux/AppDir
DESTDIR="${DEPLOY_LINUX_APPDIR_FOLDER}" ninja install

cd "${DEPLOY_LINUX_FOLDER}"

# remove -cmd executable, not needed for AppImage
rm -fv "${DEPLOY_LINUX_APPDIR_FOLDER}"/usr/bin/"${BUILD_APP//torzu/yuzu}"-cmd

curl -fsSLo ./linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${CPU_ARCH}.AppImage"
chmod +x ./linuxdeploy

curl -fsSLo ./linuxdeploy-plugin-qt "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${CPU_ARCH}.AppImage"
chmod +x ./linuxdeploy-plugin-qt

curl -fsSLo ./linuxdeploy-plugin-checkrt.sh https://github.com/darealshinji/linuxdeploy-plugin-checkrt/releases/download/continuous/linuxdeploy-plugin-checkrt.sh
chmod +x ./linuxdeploy-plugin-checkrt.sh

# Add Qt 6 specific environment variables
export QT_QPA_PLATFORM="wayland;xcb"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqxcb.so"
export EXTRA_QT_PLUGINS="svg;wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration;waylandcompositor;xcb-gl-integration;platformthemes/libqt6ct.so"

# Update linuxdeploy commands for Qt 6
export QMAKE="/usr/bin/qmake6"
export QT_SELECT=6

# remove NO_STRIP=1 if linuxdeploy is updated, see: https://github.com/linuxdeploy/linuxdeploy/issues/272
NO_STRIP=1 APPIMAGE_EXTRACT_AND_RUN=1 ./linuxdeploy --appdir ./AppDir --plugin qt --plugin checkrt

# remove libwayland-client because it has platform-dependent exports and breaks other OSes
rm -fv ./AppDir/usr/lib/libwayland-client.so*

# remove libvulkan because it causes issues with gamescope
rm -fv ./AppDir/usr/lib/libvulkan.so*

rm -rf ./linuxdeploy-squashfs-root
./linuxdeploy --appimage-extract
mv -v ./squashfs-root/ ./linuxdeploy-squashfs-root/

./linuxdeploy-squashfs-root/plugins/linuxdeploy-plugin-appimage/usr/bin/appimagetool ./AppDir -g

#APPIMAGE_SUFFIX="linux_${CPU_ARCH}"
APPIMAGE_SUFFIX="${CPU_ARCH}"
#COMM_TAG="${DISPLAYVERSION}"
COMM_COUNT="$(git rev-list --count HEAD)"
COMM_HASH="$(git rev-parse --short=9 HEAD)"
BUILD_DATE=$(date +"%Y%m%d")
#APPIMAGE_NAME="${BUILD_APP}-v${COMM_TAG}-${BUILD_DATE}-${COMM_COUNT}-${COMM_HASH}-${APPIMAGE_SUFFIX}.AppImage"
APPIMAGE_NAME="${BUILD_APP}-nightly-${BUILD_DATE}-${COMM_COUNT}-${COMM_HASH}-${APPIMAGE_SUFFIX}.AppImage"

LATEST_APPIMAGE=$(ls -1t ${BUILD_APP}*.AppImage | head -n 1) # find the most recent AppImage
if [[ -z "${LATEST_APPIMAGE}" ]]; then
    >&2 echo "Error: No AppImage found for ${BUILD_APP}"
    exit 1
fi

mv -v "${LATEST_APPIMAGE}" "${APPIMAGE_NAME}"

FILESIZE=$(stat -c %s "./${APPIMAGE_NAME}")
SHA256SUM=$(sha256sum "./${APPIMAGE_NAME}" | awk '{ print $1 }')

echo "${APPIMAGE_NAME}"
echo "${SHA256SUM};${FILESIZE}B"
