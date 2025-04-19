#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Boost::date_time" for configuration "Debug"
set_property(TARGET Boost::date_time APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Boost::date_time PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/boost_date_time-vc143-mt-gd-x64-1_85.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/boost_date_time-vc143-mt-gd-x64-1_85.dll"
  )

list(APPEND _cmake_import_check_targets Boost::date_time )
list(APPEND _cmake_import_check_files_for_Boost::date_time "${_IMPORT_PREFIX}/debug/lib/boost_date_time-vc143-mt-gd-x64-1_85.lib" "${_IMPORT_PREFIX}/debug/bin/boost_date_time-vc143-mt-gd-x64-1_85.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
