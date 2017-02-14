set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/libtapi.exports)

add_tapi_library(libtapi
  SHARED
  APIVersion.cpp
  LinkerInterfaceFile.cpp
  Version.cpp
  ${TAPI_CXX_API_HEADERS}

  LINK_LIBS
  tapiCore
  )

set_target_properties(libtapi
  PROPERTIES
  OUTPUT_NAME "tapi"
  )

set_property(TARGET libtapi APPEND_STRING
  PROPERTY
  LINK_FLAGS " -current_version ${TAPI_VERSION_MAJOR}.${TAPI_VERSION_MINOR}.${TAPI_VERSION_PATCH} -compatibility_version 1 -allowable_client ld"
  )
