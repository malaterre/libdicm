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
if(DICM_ENABLE_STRUCTURE_EXPLICT_BE)
  list(APPEND dicm_SOURCES evrbe_item.c)
endif()
if(DICM_ENABLE_STRUCTURE_EXPLICT_LE)
  list(APPEND dicm_SOURCES evrle_item.c)
endif()
if(DICM_ENABLE_STRUCTURE_IMPLICT)
  list(APPEND dicm_SOURCES ivrle_item.c)
endif()

add_library(dicm SHARED ${dicm_SOURCES})
set_target_properties(dicm PROPERTIES VERSION ${DICM_VERSION} SOVERSION
                                                              ${DICM_SOVERSION})

target_include_directories(dicm PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# https://stackoverflow.com/questions/25676277/cmake-target-include-directories-prints-an-error-when-i-try-to-add-the-source
target_include_directories(
  dicm PUBLIC $<BUILD_INTERFACE:${DICM_SOURCE_DIR}/include>
              $<INSTALL_INTERFACE:include/dicm> # <prefix>/include/dicm
)
install(
  TARGETS dicm
  EXPORT dicm_targets
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  COMPONENT libraries)
