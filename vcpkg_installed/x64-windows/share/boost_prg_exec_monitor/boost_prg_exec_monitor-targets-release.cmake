#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Boost::prg_exec_monitor" for configuration "Release"
set_property(TARGET Boost::prg_exec_monitor APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Boost::prg_exec_monitor PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/boost_prg_exec_monitor-vc143-mt-x64-1_85.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/boost_prg_exec_monitor-vc143-mt-x64-1_85.dll"
  )

list(APPEND _cmake_import_check_targets Boost::prg_exec_monitor )
list(APPEND _cmake_import_check_files_for_Boost::prg_exec_monitor "${_IMPORT_PREFIX}/lib/boost_prg_exec_monitor-vc143-mt-x64-1_85.lib" "${_IMPORT_PREFIX}/bin/boost_prg_exec_monitor-vc143-mt-x64-1_85.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
