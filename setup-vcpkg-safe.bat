@echo off
setlocal enabledelayedexpansion

set submodule_paths=externals\enet externals\dynarmic externals\libusb\libusb externals\discord-rpc externals\vulkan-headers externals\sirit externals\mbedtls externals\xbyak externals\opus externals\cpp-httplib externals\ffmpeg\ffmpeg externals\cpp-jwt externals\libadrenotools externals\VulkanMemoryAllocator externals\breakpad externals\simpleini externals\oaknut externals\Vulkan-Utility-Libraries externals\vcpkg externals\nx_tzdb\tzdb_to_nx externals\cubeb externals\SDL3

@REM for %%i in (%submodule_paths%) do (
@REM     if exist %%i (
@REM         echo Deleting existing folder: %%i
@REM         rmdir /s /q %%i
@REM     )
@REM )

git init

@REM git submodule add https://github.com/lsalzman/enet externals\enet
@REM git submodule add https://github.com/sumi-emu/dynarmic externals\dynarmic
@REM git submodule add https://github.com/libusb/libusb externals\libusb\libusb
@REM git submodule add https://github.com/sumi-emu/discord-rpc externals\discord-rpc
@REM git submodule add https://github.com/KhronosGroup/Vulkan-Headers externals\vulkan-headers
@REM git submodule add https://github.com/sumi-emu/sirit externals\sirit
@REM git submodule add https://github.com/sumi-emu/mbedtls externals\mbedtls
@REM git submodule add https://github.com/herumi/xbyak externals\xbyak
@REM git submodule add https://github.com/xiph/opus externals\opus
@REM git submodule add https://github.com/yhirose/cpp-httplib externals\cpp-httplib
@REM git submodule add https://github.com/FFmpeg/FFmpeg externals\ffmpeg\ffmpeg
@REM git submodule add https://github.com/arun11299/cpp-jwt externals\cpp-jwt
@REM git submodule add https://github.com/bylaws/libadrenotools externals\libadrenotools
@REM git submodule add https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator externals\VulkanMemoryAllocator
@REM git submodule add https://github.com/sumi-emu/breakpad externals\breakpad
@REM git submodule add https://github.com/brofield/simpleini externals\simpleini
@REM git submodule add https://github.com/sumi-emu/oaknut externals\oaknut
@REM git submodule add https://github.com/KhronosGroup/Vulkan-Utility-Libraries externals\Vulkan-Utility-Libraries
@REM git submodule add https://github.com/microsoft/vcpkg externals\vcpkg
@REM git submodule add https://github.com/lat9nq/tzdb_to_nx externals\nx_tzdb\tzdb_to_nx
@REM git submodule add https://github.com/mozilla/cubeb externals\cubeb
@REM git submodule add https://github.com/libsdl-org/sdl externals\SDL3

git submodule update --init --recursive

cd externals\cpp-httplib && git checkout 65ce51aed7f15e40e8fb6d2c0a8efb10bcb40126

endlocal