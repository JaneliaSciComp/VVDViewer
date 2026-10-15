# Collects third-party license notices (and, optionally, the source code of the
# copyleft components) for a VVDViewer binary package. Shared by package.ps1
# (Windows) and package.sh (Linux/macOS):
#
#   cmake -DINSTALLED_DIR=<build>/vcpkg_installed/<triplet>
#         -DVCPKG_ROOT=<vcpkg>
#         -DSTAGING=<package dir>
#         -DFFMPEG_MODE=LGPL|GPL
#         -DPACKAGE_NAME=<name>
#         [-DSOURCES_DIR=<dir>]         copy (L)GPL/MPL sources + vcpkg port files here
#         [-DSOURCES_ARCHIVE=<name>]    file name mentioned in the notices
#         [-DVULKAN_SDK=<sdk dir>]      add the Vulkan SDK license notice
#         [-DEXTRA_NOTICES=<a;b>]       extra "name|license|text file[|name in licenses/]"
#         -P collect_licenses.cmake
#
# Output in STAGING: THIRD_PARTY_NOTICES.txt and licenses/<component>.txt.
# The data comes from what vcpkg installs per port: share/<port>/copyright and
# share/<port>/vcpkg.spdx.json (version, license expression, source location).
cmake_minimum_required(VERSION 3.21)

foreach(_req INSTALLED_DIR VCPKG_ROOT STAGING FFMPEG_MODE PACKAGE_NAME)
  if(NOT DEFINED ${_req} OR "${${_req}}" STREQUAL "")
    message(FATAL_ERROR "collect_licenses.cmake: ${_req} is required")
  endif()
endforeach()
foreach(_p INSTALLED_DIR VCPKG_ROOT STAGING SOURCES_DIR VULKAN_SDK)
  if(${_p})
    file(TO_CMAKE_PATH "${${_p}}" ${_p})   # Windows callers pass backslashes
  endif()
endforeach()
if(NOT IS_DIRECTORY "${INSTALLED_DIR}/share")
  message(FATAL_ERROR "No vcpkg share directory: ${INSTALLED_DIR}/share")
endif()

# string(JSON GET) yields "...-NOTFOUND" for a missing member and "null" for
# a JSON null; normalize both to "".
function(_json_get out json)
  string(JSON _v ERROR_VARIABLE _err GET "${json}" ${ARGN})
  if(_err OR _v MATCHES "NOTFOUND$" OR _v STREQUAL "null")
    set(_v "")
  endif()
  set(${out} "${_v}" PARENT_SCOPE)
endfunction()

# A license expression is copyleft (source must be offered) unless at least one
# OR-alternative is free of GPL/LGPL/MPL. wxWidgets' exception allows binary
# distribution under the distributor's own terms.
function(_is_copyleft out expr copyright_file)
  set(_res FALSE)
  if(expr AND NOT expr MATCHES "^(LicenseRef-vcpkg-null|NOASSERTION)$")
    if(NOT expr MATCHES "WxWindows-exception")
      string(REPLACE "(" "" _e "${expr}")
      string(REPLACE ")" "" _e "${_e}")
      string(REPLACE " OR " ";" _alts "${_e}")
      set(_res TRUE)
      foreach(_a IN LISTS _alts)
        if(NOT _a MATCHES "GPL|MPL")
          set(_res FALSE)
        endif()
      endforeach()
    endif()
  else()
    # No SPDX expression (e.g. ffmpeg, libCZI): look at the license text itself.
    file(READ "${copyright_file}" _head LIMIT 3000)
    if(_head MATCHES "GNU (LESSER |LIBRARY |)GENERAL PUBLIC LICENSE|Lesser General Public License|General Public License")
      set(_res TRUE)
    endif()
  endif()
  set(${out} ${_res} PARENT_SCOPE)
endfunction()

# First meaningful line of a license text, used when the SPDX field is empty.
function(_license_hint out copyright_file)
  file(STRINGS "${copyright_file}" _lines LIMIT_COUNT 15)
  set(_hint "")
  foreach(_l IN LISTS _lines)
    string(STRIP "${_l}" _l)
    if(_l)
      set(_hint "${_l}")
      break()
    endif()
  endforeach()
  string(LENGTH "${_hint}" _len)
  if(_len GREATER 60)
    string(SUBSTRING "${_hint}" 0 57 _hint)
    set(_hint "${_hint}...")
  endif()
  set(${out} "${_hint}" PARENT_SCOPE)
endfunction()

# vcpkg's SPDX file can keep the portfile's variables unexpanded in resource
# names/URLs (".../sources/${PORT}/${VERSION_MAJOR_MINOR}/${PORT}-2.38.0.tar.xz",
# "git+https://github.com/${LIBCZI_REPO_NAME}@${LIBCZI_REPO_REF}"). Expand PORT,
# VERSION, VERSION_MAJOR_MINOR and simple set(NAME value) lines of the portfile.
function(_expand_port_vars out text port version)
  string(REGEX REPLACE "#.*$" "" _v "${version}")
  string(REGEX MATCH "^[0-9]+\\.[0-9]+" _mm "${_v}")
  string(REGEX MATCH "^[0-9]+" _major "${_v}")
  string(REGEX REPLACE "^[0-9]+\\.([0-9]+).*$" "\\1" _minor "${_v}")
  string(REPLACE "\${PORT}" "${port}" text "${text}")
  # VERSION_MAJOR_MINOR / VERSION_SHORT: the usual portfile names for "X.Y"
  string(REPLACE "\${VERSION_MAJOR_MINOR}" "${_mm}" text "${text}")
  string(REPLACE "\${VERSION_SHORT}" "${_mm}" text "${text}")
  string(REPLACE "\${VERSION_MAJOR}" "${_major}" text "${text}")
  string(REPLACE "\${VERSION_MINOR}" "${_minor}" text "${text}")
  string(REPLACE "\${VERSION}" "${_v}" text "${text}")
  if(text MATCHES "\\\$\\{" AND EXISTS "${VCPKG_ROOT}/ports/${port}/portfile.cmake")
    file(STRINGS "${VCPKG_ROOT}/ports/${port}/portfile.cmake" _sets
         REGEX "^[ \t]*set[ \t]*\\([ \t]*[A-Za-z0-9_]+[ \t]+\"?[^\" \t()$]+\"?[ \t]*\\)")
    foreach(_s IN LISTS _sets)
      if(_s MATCHES "set[ \t]*\\([ \t]*([A-Za-z0-9_]+)[ \t]+\"?([^\" \t()$]+)\"?[ \t]*\\)")
        string(REPLACE "\${${CMAKE_MATCH_1}}" "${CMAKE_MATCH_2}" text "${text}")
      endif()
    endforeach()
  endif()
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

# SHA-512 -> file index of the vcpkg download cache, built on first use.
function(_download_by_hash out sha512)
  get_property(_built GLOBAL PROPERTY _VVD_HASH_INDEX_BUILT)
  if(NOT _built)
    file(GLOB _all LIST_DIRECTORIES false "${VCPKG_ROOT}/downloads/*")
    foreach(_c IN LISTS _all)
      file(SHA512 "${_c}" _h)
      set_property(GLOBAL PROPERTY "_VVD_SHA_${_h}" "${_c}")
    endforeach()
    set_property(GLOBAL PROPERTY _VVD_HASH_INDEX_BUILT TRUE)
  endif()
  get_property(_f GLOBAL PROPERTY "_VVD_SHA_${sha512}")
  set(${out} "${_f}" PARENT_SCOPE)
endfunction()

# Locates (or downloads) one source archive recorded in a port's SPDX file and
# copies it to <dest>. Returns the file name, or "" when it could not be found.
function(_fetch_source out name location sha512 dest)
  set(${out} "" PARENT_SCOPE)
  string(TOLOWER "${name}" _prefix)
  string(REPLACE "/" "-" _prefix "${_prefix}")
  file(GLOB _cands LIST_DIRECTORIES false "${VCPKG_ROOT}/downloads/*")
  foreach(_c IN LISTS _cands)
    get_filename_component(_b "${_c}" NAME)
    string(TOLOWER "${_b}" _bl)
    string(FIND "${_bl}" "${_prefix}" _pos)
    if(_pos EQUAL 0)
      if(sha512)
        file(SHA512 "${_c}" _h)
        if(NOT _h STREQUAL sha512)
          continue()
        endif()
      endif()
      file(COPY_FILE "${_c}" "${dest}/${_b}")
      set(${out} "${_b}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  if(sha512)
    _download_by_hash(_hit "${sha512}")
    if(_hit)
      get_filename_component(_b "${_hit}" NAME)
      file(COPY_FILE "${_hit}" "${dest}/${_b}")
      set(${out} "${_b}" PARENT_SCOPE)
      return()
    endif()
  endif()
  # Not in the local download cache (e.g. the port came from the binary cache):
  # rebuild the URL vcpkg used and download it, verifying the recorded hash.
  set(_url "")
  if(location MATCHES "^git\\+https://github\\.com/([^@]+)@(.+)$")
    set(_url "https://github.com/${CMAKE_MATCH_1}/archive/${CMAKE_MATCH_2}.tar.gz")
    string(REPLACE "/" "-" _file "${CMAKE_MATCH_1}-${CMAKE_MATCH_2}.tar.gz")
  elseif(location MATCHES "^git\\+https://(bitbucket\\.org|bitbucket\\.com)/([^@]+)@(.+)$")
    set(_url "https://bitbucket.org/${CMAKE_MATCH_2}/get/${CMAKE_MATCH_3}.tar.gz")
    string(REPLACE "/" "-" _file "${CMAKE_MATCH_2}-${CMAKE_MATCH_3}.tar.gz")
  elseif(location MATCHES "^git\\+https://([^/]+)/+([^@]+)@(.+)$")
    # Any other host: vcpkg_from_gitlab (gitlab.com, gitlab.freedesktop.org,
    # code.videolan.org, ...), whose archive URL scheme this reproduces.
    set(_host "${CMAKE_MATCH_1}")
    set(_path "${CMAKE_MATCH_2}")
    set(_ref "${CMAKE_MATCH_3}")
    get_filename_component(_repo "${_path}" NAME)
    set(_url "https://${_host}/${_path}/-/archive/${_ref}/${_repo}-${_ref}.tar.gz")
    string(REPLACE "/" "-" _file "${_path}-${_ref}.tar.gz")
  elseif(location MATCHES "^https?://")
    set(_url "${location}")
    get_filename_component(_file "${name}" NAME)
  endif()
  if(NOT _url OR _url MATCHES "\\\$\\{")
    return()
  endif()
  message(STATUS "  downloading ${_url}")
  # (no EXPECTED_HASH: it turns a failed download into a fatal error; the
  # hash is checked below and a failure is reported with the other misses)
  file(DOWNLOAD "${_url}" "${dest}/${_file}" STATUS _st TLS_VERIFY ON)
  list(GET _st 0 _code)
  if(_code EQUAL 0 AND sha512)
    file(SHA512 "${dest}/${_file}" _h)
    if(NOT _h STREQUAL sha512)
      message(STATUS "  SHA-512 mismatch for ${_url}")
      set(_code 1)
    endif()
  endif()
  if(_code EQUAL 0)
    set(${out} "${_file}" PARENT_SCOPE)
  else()
    file(REMOVE "${dest}/${_file}")
  endif()
endfunction()

file(REMOVE_RECURSE "${STAGING}/licenses")
file(MAKE_DIRECTORY "${STAGING}/licenses")
if(SOURCES_DIR)
  # (the caller starts from an empty directory and may already have added
  # sources of components that are not built by vcpkg)
  file(MAKE_DIRECTORY "${SOURCES_DIR}")
endif()

file(GLOB _ports LIST_DIRECTORIES true RELATIVE "${INSTALLED_DIR}/share" "${INSTALLED_DIR}/share/*")
list(SORT _ports)

set(_table "")
set(_copyleft_names "")
set(_missing_sources "")
set(_count 0)
foreach(port IN LISTS _ports)
  set(_dir "${INSTALLED_DIR}/share/${port}")
  if(NOT EXISTS "${_dir}/copyright")
    continue()
  endif()
  math(EXPR _count "${_count}+1")
  file(COPY_FILE "${_dir}/copyright" "${STAGING}/licenses/${port}.txt")

  set(_ver "")
  set(_home "")
  set(_lic "")
  set(_resources "")
  if(EXISTS "${_dir}/vcpkg.spdx.json")
    file(READ "${_dir}/vcpkg.spdx.json" _json)
    string(JSON _n ERROR_VARIABLE _err LENGTH "${_json}" packages)
    if(NOT _err AND _n GREATER 0)
      math(EXPR _last "${_n}-1")
      foreach(_i RANGE 0 ${_last})
        string(JSON _pkg GET "${_json}" packages ${_i})
        _json_get(_id "${_pkg}" SPDXID)
        if(_id STREQUAL "SPDXRef-port")
          _json_get(_ver "${_pkg}" versionInfo)
          _json_get(_home "${_pkg}" homepage)
          _json_get(_lic "${_pkg}" licenseConcluded)
        elseif(_id MATCHES "^SPDXRef-resource-")
          _json_get(_rname "${_pkg}" name)
          _json_get(_rloc "${_pkg}" downloadLocation)
          _expand_port_vars(_rname "${_rname}" "${port}" "${_ver}")
          _expand_port_vars(_rloc "${_rloc}" "${port}" "${_ver}")
          _json_get(_rsha "${_pkg}" checksums 0 checksumValue)
          string(TOLOWER "${_rsha}" _rsha)
          list(APPEND _resources "${_rname}|${_rloc}|${_rsha}")
        endif()
      endforeach()
    endif()
  endif()

  _is_copyleft(_cl "${_lic}" "${_dir}/copyright")
  set(_lic_show "${_lic}")
  if(NOT _lic_show OR _lic_show MATCHES "^(LicenseRef-vcpkg-null|NOASSERTION)$")
    _license_hint(_hint "${_dir}/copyright")
    set(_lic_show "see licenses/${port}.txt (${_hint})")
  endif()
  set(_src_show "")
  foreach(_r IN LISTS _resources)
    string(REPLACE "|" ";" _parts "${_r}")
    list(GET _parts 1 _loc)
    if(_loc)
      string(APPEND _src_show "${_loc} ")
    endif()
  endforeach()
  string(STRIP "${_src_show}" _src_show)

  set(_flag "")
  if(_cl)
    set(_flag " [source provided]")
    list(APPEND _copyleft_names "${port}")
    if(SOURCES_DIR)
      set(_pdest "${SOURCES_DIR}/${port}")
      file(MAKE_DIRECTORY "${_pdest}")
      # The vcpkg port (portfile + patches) documents every modification made
      # to the upstream source before building.
      if(IS_DIRECTORY "${VCPKG_ROOT}/ports/${port}")
        file(COPY "${VCPKG_ROOT}/ports/${port}/" DESTINATION "${_pdest}/vcpkg-port")
      endif()
      file(COPY_FILE "${_dir}/copyright" "${_pdest}/LICENSE.txt")
      foreach(_r IN LISTS _resources)
        string(REPLACE "|" ";" _parts "${_r}")
        list(GET _parts 0 _rname)
        list(GET _parts 1 _rloc)
        list(GET _parts 2 _rsha)
        _fetch_source(_got "${_rname}" "${_rloc}" "${_rsha}" "${_pdest}")
        if(NOT _got)
          list(APPEND _missing_sources "${port}: ${_rname} (${_rloc})")
        endif()
      endforeach()
    endif()
  endif()

  string(APPEND _table
    "${port} ${_ver}${_flag}\n"
    "    License: ${_lic_show}\n")
  if(_home)
    string(APPEND _table "    Homepage: ${_home}\n")
  endif()
  if(_src_show)
    string(APPEND _table "    Source:   ${_src_show}\n")
  endif()
  string(APPEND _table "\n")
endforeach()

# Extra, non-vcpkg components (Vulkan SDK libraries, Microsoft VC++ runtime, ...).
set(_extra_table "")
if(VULKAN_SDK)
  foreach(_cand "${VULKAN_SDK}/Licenses/LICENSE.txt" "${VULKAN_SDK}/LICENSE.txt"
                "${VULKAN_SDK}/../LICENSE.txt" "${VULKAN_SDK}/../Licenses/LICENSE.txt")
    if(EXISTS "${_cand}")
      file(COPY_FILE "${_cand}" "${STAGING}/licenses/vulkan-sdk.txt")
      string(APPEND _extra_table
        "Vulkan SDK components (glslang, SPIRV-Tools, Vulkan loader, MoltenVK)\n"
        "    License: MIT / Apache-2.0 / BSD-3-Clause; see licenses/vulkan-sdk.txt and\n"
        "             the LunarG licensing registry at https://vulkan.lunarg.com/\n\n")
      break()
    endif()
  endforeach()
endif()
foreach(_e IN LISTS EXTRA_NOTICES)
  string(REPLACE "|" ";" _parts "${_e}")
  list(LENGTH _parts _np)
  list(GET _parts 0 _ename)
  list(GET _parts 1 _elic)
  list(GET _parts 2 _efile)
  file(TO_CMAKE_PATH "${_efile}" _efile)
  if(_np GREATER 3)
    list(GET _parts 3 _ebase)          # file name inside licenses/
  else()
    get_filename_component(_ebase "${_efile}" NAME)
  endif()
  file(COPY_FILE "${_efile}" "${STAGING}/licenses/${_ebase}")
  string(APPEND _extra_table "${_ename}\n    License: ${_elic}; see licenses/${_ebase}\n\n")
endforeach()

if(FFMPEG_MODE STREQUAL "GPL")
  set(_mode_text
"ffmpeg in this package is built WITH the GPL x264/x265 encoders. This binary
package as a whole is therefore distributed under the terms of the GNU General
Public License, version 2 or later (licenses/ffmpeg.txt). VVDViewer's own code
remains available under the BSD 3-Clause license (LICENSE).")
else()
  set(_mode_text
"ffmpeg in this package is the LGPL build (no GPL components). ffmpeg and
libCZI are shipped as separate dynamic libraries that may be replaced by
compatible builds of your own.")
endif()

set(_src_text "")
if(_copyleft_names)
  list(JOIN _copyleft_names ", " _cl_join)
  if(SOURCES_ARCHIVE)
    set(_where "in ${SOURCES_ARCHIVE}, published at the same location as this package")
  else()
    set(_where "on request, and from the upstream locations listed below")
  endif()
  set(_src_text
"The complete source code of the components marked [source provided]
(${_cl_join}), together with the vcpkg port files that
document every modification applied before building, is available ${_where}.
The source code of VVDViewer itself is available at
https://github.com/JaneliaSciComp/VVDViewer (see BUILDINFO.txt for the commit).
")
endif()

file(WRITE "${STAGING}/THIRD_PARTY_NOTICES.txt"
"${PACKAGE_NAME} - third-party software notices
================================================================================

This package contains VVDViewer (BSD 3-Clause, see LICENSE and
LICENSE_DEPENDENCIES.md) and the third-party components listed below. The full
license text of every component is in the licenses/ directory.

${_mode_text}

${_src_text}
--------------------------------------------------------------------------------
Components built by vcpkg (${_count})
--------------------------------------------------------------------------------

${_table}--------------------------------------------------------------------------------
Other components
--------------------------------------------------------------------------------

${_extra_table}")

if(SOURCES_DIR AND _copyleft_names)
  file(WRITE "${SOURCES_DIR}/README.txt"
"Source code of the copyleft (GPL/LGPL/MPL) third-party components of
${PACKAGE_NAME}.

Each directory contains the upstream source archive(s) exactly as downloaded by
vcpkg (verified against the SHA-512 recorded at build time), the component's
license (LICENSE.txt) and the vcpkg port (vcpkg-port/: portfile.cmake and the
patches applied before building). VVDViewer builds its dependencies with the
vcpkg commit and triplet listed in BUILDINFO.txt of the binary package.
")
endif()

list(LENGTH _copyleft_names _ncl)
message(STATUS "Third-party notices: ${_count} components, ${_ncl} copyleft (${_copyleft_names})")
if(_missing_sources)
  list(JOIN _missing_sources "\n  " _ms)
  message(FATAL_ERROR "Source archives not found for:\n  ${_ms}")
endif()
