cmake_minimum_required(VERSION 2.8.12.2)

if(NOT APPLE)
  message(FATAL_ERROR "Unsupported configuration.")
endif()

set(TAPI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(TAPI_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(TAPI_VERSION "1.30.0" CACHE STRING "Specify tapi version.")
message(STATUS "TAPI version: ${TAPI_VERSION}")

string(REGEX REPLACE "([0-9]+)\\.[0-9]+(\\.[0-9]+)?" "\\1" TAPI_VERSION_MAJOR
  ${TAPI_VERSION})
string(REGEX REPLACE "[0-9]+\\.([0-9]+)(\\.[0-9]+)?" "\\1" TAPI_VERSION_MINOR
  ${TAPI_VERSION})
if (${TAPI_VERSION} MATCHES "[0-9]+\\.[0-9]+\\.[0-9]+")
  string(REGEX REPLACE "[0-9]+\\.[0-9]+\\.([0-9]+)" "\\1" TAPI_VERSION_PATCH
         ${TAPI_VERSION})
else()
  set(TAPI_VERSION_PATCH 0)
endif()

set(TAPI_VENDOR ${PACKAGE_VENDOR} CACHE STRING
  "Vendor-specific text for showing with version information.")

if (TAPI_VENDOR )
  add_definitions( -DTAPI_VENDOR="${TAPI_VENDOR} " )
endif()
set(BACKEND_PACKAGE_STRING "${PACKAGE_STRING}")

# Configure the Version.inc file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tapi/Version.inc.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/tapi/Version.inc)

macro(add_tapi_library name)
  cmake_parse_arguments(ARG
    "SHARED"
    ""
    "ADDITIONAL_HEADERS"
    ${ARGN})
  set(srcs)
  if(MSVC_IDE OR XCODE)
    # Add public headers
    file(RELATIVE_PATH lib_path
      ${TAPI_SOURCE_DIR}/lib/
      ${CMAKE_CURRENT_SOURCE_DIR}
    )
    if(NOT lib_path MATCHES "^[.][.]")
      file( GLOB_RECURSE headers
        ${TAPI_SOURCE_DIR}/include/tapi/${lib_path}/*.h
        ${TAPI_SOURCE_DIR}/include/tapi/${lib_path}/*.def
      )
      set_source_files_properties(${headers} PROPERTIES HEADER_FILE_ONLY ON)

      file( GLOB_RECURSE tds
        ${TAPI_SOURCE_DIR}/include/tapi/${lib_path}/*.td
      )
      source_group("TableGen descriptions" FILES ${tds})
      set_source_files_properties(${tds}} PROPERTIES HEADER_FILE_ONLY ON)

      if(headers OR tds)
        set(srcs ${headers} ${tds})
      endif()
    endif()
  endif(MSVC_IDE OR XCODE)

  if(srcs OR ARG_ADDITIONAL_HEADERS)
    set(srcs
      ADDITIONAL_HEADERS
      ${srcs}
      ${ARG_ADDITIONAL_HEADERS} # It may contain unparsed unknown args.
    )
  endif()

  if(ARG_SHARED)
    set(ARG_ENABLE_SHARED SHARED)
  endif()
  llvm_add_library(${name} ${ARG_ENABLE_SHARED} ${ARG_UNPARSED_ARGUMENTS} ${srcs})

  if(TARGET ${name})
    target_link_libraries(${name} ${cmake_2_8_12_INTERFACE} ${LLVM_COMMON_LIBS})

    if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
      install(TARGETS ${name}
        COMPONENT ${name}
        LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX}
        ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX}
        RUNTIME DESTINATION bin
      )

      if (${ARG_SHARED} AND NOT CMAKE_CONFIGURATION_TYPES)
        add_custom_target(install-${name}
          DEPENDS ${name}
          COMMAND "${CMAKE_COMMAND}"
          -DCMAKE_INSTALL_COMPONENT=${name}
          -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")
      endif()
    endif()
  else()
    # Add empty "phony" target
    add_custom_target(${name})
  endif()

  set_target_properties(${name} PROPERTIES FOLDER "tapi libraries")
endmacro(add_tapi_library)

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(tools)
