#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Boost::context" for configuration "Release"
set_property(TARGET Boost::context APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Boost::context PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/boost_context-vc143-mt-x64-1_85.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/boost_context-vc143-mt-x64-1_85.dll"
  )

list(APPEND _cmake_import_check_targets Boost::context )
list(APPEND _cmake_import_check_files_for_Boost::context "${_IMPORT_PREFIX}/lib/boost_context-vc143-mt-x64-1_85.lib" "${_IMPORT_PREFIX}/bin/boost_context-vc143-mt-x64-1_85.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
