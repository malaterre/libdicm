configure_file(dicm_configure.h.in dicm_configure.h @ONLY)
set(dicm_SOURCES
    dicm_dst.c
    dicm_emitter.c
    dicm_item.c
    dicm_log.c
    dicm_object.c
    dicm_parser.c
    dicm_src.c
    dicm_version.c)

add_library(dicm SHARED ${dicm_SOURCES})
set_target_properties(dicm PROPERTIES VERSION ${DICM_VERSION} SOVERSION
                                                              ${DICM_SOVERSION})

target_include_directories(dicm PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(dicm PUBLIC ${DICM_SOURCE_DIR}/include)

install(
  TARGETS dicm
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  COMPONENT libraries)
