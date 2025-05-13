#! /bin/bash
set -e

# Make sure script is called from inside our container
test -e /tmp/torzu-src || (echo "Script MUST NOT be called directly!" ; exit 1)

# Install dependencies
apt -y install cmake ninja-build build-essential pkg-config locales wget git file
apt -y install libfmt-dev libenet-dev liblz4-dev nlohmann-json3-dev zlib1g-dev libopus-dev libsimpleini-dev libstb-dev libzstd-dev libusb-1.0-0-dev libcubeb-dev libcpp-jwt-dev libvulkan-dev gamemode-dev libasound2-dev libglu1-mesa-dev libxext-dev mesa-common-dev qtbase5-dev qtmultimedia5-dev qtbase5-private-dev libva-dev glslang-tools libavcodec-dev libavfilter-dev libavutil-dev libswscale-dev

# Install correct version of boost
cd /tmp
wget https://web.archive.org/web/20241120101759id_/https://boostorg.jfrog.io/artifactory/main/release/1.84.0/source/boost_1_84_0.tar.bz2
tar xf boost_1_84_0.tar.bz2
cd boost_1_84_0
./bootstrap.sh
./b2 install --with-{headers,context} link=static
cd ..
rm -rf boost_1_84_0

# Build Torzu
cd /tmp
mkdir torzu-build
cd torzu-build
cmake /tmp/torzu-src -GNinja -DCMAKE_BUILD_TYPE=Release -DYUZU_TESTS=OFF -DENABLE_QT_TRANSLATION=OFF -DSPIRV_WERROR=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_FIND_LIBRARY_SUFFIXES=".a;.so" -DSPIRV-Headers_SOURCE_DIR=/tmp/torzu-src/externals/SPIRV-Headers
ninja

# Generate AppImage
cp -rv /tmp/torzu-src/AppImageBuilder /tmp/AppImageBuilder
cd /tmp/AppImageBuilder
./build.sh /tmp/torzu-build /tmp/torzu.AppImage || echo "This error is known. Using workaround..."
cp /lib/$(uname -m)-linux-gnu/libICE.so.6 build/
mv build /tmp/hosttmp/torzu-debian-appimage-rootfs
