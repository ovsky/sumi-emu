# Add to target_sources:
target_sources(video_core_src PRIVATE
    filters/cas.cpp
)

# Add shaders to compilation:
set(SHADER_FILES
    shaders/filters/full_screen.vert
    shaders/filters/cas.frag
    # ... other shaders ...
)

foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(SHADER_SPV "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND glslangValidator -V ${SHADER} -o ${SHADER_SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER}"
        VERBATIM
    )

    list(APPEND SHADER_SPV_FILES ${SHADER_SPV})
endforeach()

target_sources(video_core_src PRIVATE ${SHADER_SPV_FILES})