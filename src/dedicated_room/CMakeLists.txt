# SPDX-FileCopyrightText: 2017 Citra Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(sumi-room
    precompiled_headers.h
    sumi_room.cpp
    sumi_room.rc
)

target_link_libraries(sumi-room PRIVATE common network)
if (ENABLE_WEB_SERVICE)
    target_compile_definitions(sumi-room PRIVATE -DENABLE_WEB_SERVICE)
    target_link_libraries(sumi-room PRIVATE web_service)
endif()

target_link_libraries(sumi-room PRIVATE mbedtls mbedcrypto)
if (MSVC)
    target_link_libraries(sumi-room PRIVATE getopt)
endif()
target_link_libraries(sumi-room PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

if(UNIX AND NOT APPLE)
    install(TARGETS sumi-room)
endif()

if (SUMI_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(sumi-room PRIVATE precompiled_headers.h)
endif()

create_target_directory_groups(sumi-room)
