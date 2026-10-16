# Rewrites the ELF run-time search path (RUNPATH/RPATH) of the given files with
# CMake's built-in ELF editor, so packaging does not need patchelf:
#
#   cmake -DFILES="<f1>;<f2>;..." -DRPATH='$ORIGIN' -P set_rpath.cmake
#
# CMake can only replace an existing entry in place, so the new path must not be
# longer than the old one (true here: absolute build paths -> $ORIGIN...).
# Files without any RUNPATH/RPATH entry are reported and left unchanged.
cmake_minimum_required(VERSION 3.21)
if(NOT DEFINED FILES OR NOT DEFINED RPATH)
  message(FATAL_ERROR "set_rpath.cmake: FILES and RPATH are required")
endif()
foreach(_f IN LISTS FILES)
  file(RPATH_SET FILE "${_f}" NEW_RPATH "${RPATH}")
endforeach()
