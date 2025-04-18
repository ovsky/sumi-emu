# SPDX-FileCopyrightText: 2024 Sumi HomeBrew Emulator Project

function(copy_sumi_Qt6_deps target_dir)
    include(WindowsCopyFiles)
    if (MSVC)
        set(DLL_DEST "$<TARGET_FILE_DIR:${target_dir}>/")
        set(Qt6_DLL_DIR "${Qt6_DIR}/../../../bin")
    else()
        set(DLL_DEST "${CMAKE_BINARY_DIR}/bin/")
        set(Qt6_DLL_DIR "${Qt6_DIR}/../../../lib/")
    endif()
    set(Qt6_PLATFORMS_DIR "${Qt6_DIR}/../../../plugins/platforms/")
    set(Qt6_STYLES_DIR "${Qt6_DIR}/../../../plugins/styles/")
    set(Qt6_IMAGEFORMATS_DIR "${Qt6_DIR}/../../../plugins/imageformats/")
    set(Qt6_RESOURCES_DIR "${Qt6_DIR}/../../../resources/")
    set(PLATFORMS ${DLL_DEST}plugins/platforms/)
    set(STYLES ${DLL_DEST}plugins/styles/)
    set(IMAGEFORMATS ${DLL_DEST}plugins/imageformats/)

    if (MSVC)
        windows_copy_files(${target_dir} ${Qt6_DLL_DIR} ${DLL_DEST}
            Qt6Core$<$<CONFIG:Debug>:d>.*
            Qt6Gui$<$<CONFIG:Debug>:d>.*
            Qt6Widgets$<$<CONFIG:Debug>:d>.*
            Qt6Network$<$<CONFIG:Debug>:d>.*
        )
        if (SUMI_USE_QT_MULTIMEDIA)
            windows_copy_files(${target_dir} ${Qt6_DLL_DIR} ${DLL_DEST}
                Qt6Multimedia$<$<CONFIG:Debug>:d>.*
            )
        endif()
        if (SUMI_USE_QT_WEB_ENGINE)
            windows_copy_files(${target_dir} ${Qt6_DLL_DIR} ${DLL_DEST}
                Qt6WebEngineCore$<$<CONFIG:Debug>:d>.*
                Qt6WebEngineWidgets$<$<CONFIG:Debug>:d>.*
                QtWebEngineProcess$<$<CONFIG:Debug>:d>.*
            )
            windows_copy_files(${target_dir} ${Qt6_RESOURCES_DIR} ${DLL_DEST}
                icudtl.dat
                qtwebengine_devtools_resources.pak
                qtwebengine_resources.pak
                qtwebengine_resources_100p.pak
                qtwebengine_resources_200p.pak
            )
        endif()
        windows_copy_files(sumi ${Qt6_PLATFORMS_DIR} ${PLATFORMS} qwindows$<$<CONFIG:Debug>:d>.*)
        windows_copy_files(sumi ${Qt6_STYLES_DIR} ${STYLES} qwindowsvistastyle$<$<CONFIG:Debug>:d>.*)
        windows_copy_files(sumi ${Qt6_IMAGEFORMATS_DIR} ${IMAGEFORMATS}
            qjpeg$<$<CONFIG:Debug>:d>.*
            qgif$<$<CONFIG:Debug>:d>.*
        )
    else()
        # Update for non-MSVC platforms if needed
    endif()

    # Create an empty qt.conf file
    add_custom_command(TARGET sumi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E touch ${DLL_DEST}qt.conf
    )
endfunction(copy_sumi_Qt6_deps)
