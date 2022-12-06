set(dicm_SOURCES dicm_io.c dicm_item.c dicm_parser.c)

add_library(dicm SHARED ${dicm_SOURCES})
set_target_properties(dicm PROPERTIES VERSION ${libdicm_VERSION}
	SOVERSION ${libdicm_SOVERSION})

target_include_directories(dicm PUBLIC ${libdicm_SOURCE_DIR}/include)

install(
  TARGETS dicm
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  COMPONENT libraries)

