cmake_minimum_required(VERSION 3.18.4)
project(
  DICM
  VERSION 0.0.1
  LANGUAGES C)

set(DICM_SOVERSION "${DICM_VERSION_MAJOR}")

# only export limited set of symbols
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

# require c11
if(NOT DEFINED CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 11)
endif()
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(examples)

# need to be at top-level
enable_testing()
add_subdirectory(tests)
