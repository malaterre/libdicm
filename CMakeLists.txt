cmake_minimum_required(VERSION 3.18.4)
project(
  DICM
  VERSION 0.0.1
  DESCRIPTION "libdicm description"
  HOMEPAGE_URL https://github.com/malaterre/libdicm
  LANGUAGES C)

set(DICM_SOVERSION "${DICM_VERSION_MAJOR}")

option(DICM_BUILD_EXAMPLES "Build DICM examples directory" OFF)

# Which structures allowed:
set(DICM_ENABLE_STRUCTURE_ENCAPSULATED ON)
set(DICM_ENABLE_STRUCTURE_IMPLICT ON)
set(DICM_ENABLE_STRUCTURE_EXPLICT_LE ON)
set(DICM_ENABLE_STRUCTURE_EXPLICT_BE ON)

# only export limited set of symbols
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

# require c11
if(NOT DEFINED CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 11)
endif()
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(UNIX)
  include(GNUInstallDirs)
endif()

add_subdirectory(include)
add_subdirectory(src)
if(DICM_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# need to be at top-level
enable_testing()
# retrieve valgrind:
include(CTest)
# testdata:
find_path(
  DICM_TEST_DATA_ROOT "truncated/single_byte.dcm"
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
mark_as_advanced(DICM_TEST_DATA_ROOT)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# write dicm-config file (handle `Config` mode):
set(namespace "${PROJECT_NAME}::")
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/dicm-config-version.cmake"
  COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dicm-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dicm")
install(
  EXPORT dicm_targets
  NAMESPACE "${namespace}"
  FILE dicm-config.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dicm")
