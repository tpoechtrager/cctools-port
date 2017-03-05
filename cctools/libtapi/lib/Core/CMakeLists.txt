add_tapi_library(tapiCore
  ArchitectureSupport.cpp
  InterfaceFile.cpp
  MachODylibReader.cpp
  Registry.cpp
  Symbol.cpp
  TextStub_v1.cpp
  TextStub_v2.cpp
  YAMLReaderWriter.cpp
  )

target_link_libraries(tapiCore INTERFACE
  LLVMSupport
  LLVMObject
  )
