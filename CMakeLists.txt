# Copyright (c) 2020 tevador <tevador@gmail.com>
# See LICENSE for licensing information

cmake_minimum_required(VERSION 2.8.7)

project(hashx)

set(hashx_sources
src/blake2.c 
src/compiler.c
src/compiler_x86.c
src/context.c
src/hashx.c
src/program.c
src/program_exec.c
src/siphash.c
src/siphash_rng.c
src/virtual_memory.c)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
  message(STATUS "Setting default build type: ${CMAKE_BUILD_TYPE}")
endif()

option(HASHX_BLOCK_MODE "Hash function for block mode" OFF)

if(HASHX_BLOCK_MODE)
  add_definitions(-DHASHX_BLOCK_MODE)
endif()

add_library(${PROJECT_NAME} ${hashx_sources})

set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET ${PROJECT_NAME} PROPERTY PUBLIC_HEADER include/hashx.h)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include_directories(${PROJECT_NAME}
  include/)

add_executable(hashx-tests
  src/tests.c)
include_directories(hashx-tests
  include/)
target_link_libraries(hashx-tests
  PRIVATE ${PROJECT_NAME})
