set(HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/tapi/APIVersion.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tapi/Defines.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tapi/LinkerInterfaceFile.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tapi/PackedVersion32.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tapi/Symbol.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tapi/tapi.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tapi/Version.h
  ${CMAKE_CURRENT_BINARY_DIR}/tapi/Version.inc
  )
set(TAPI_CXX_API_HEADERS ${HEADERS}
  CACHE INTERNAL "TAPI API Headers" PARENT_SCOPE
  )

install(FILES ${HEADERS}
  DESTINATION include/tapi
  COMPONENT tapi-headers
  )

add_custom_target(tapi-headers)
add_custom_target(install-tapi-headers
  COMMAND "${CMAKE_COMMAND}" -DCMAKE_INSTALL_COMPONENT=tapi-headers
          -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
  )
