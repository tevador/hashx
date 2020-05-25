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

set(HASHX_SIZE CACHE STRING "Hash function output size in bytes")

if(HASHX_SIZE)
  if(HASHX_SIZE GREATER 32)
    message(SEND_ERROR "The maximum hash size is 32 bytes")
  else()
    add_definitions(-DHASHX_SIZE=${HASHX_SIZE})
  endif()
endif()

set(HASHX_SALT CACHE STRING "Implementation-specific salt value")

if(HASHX_SALT)
  string(LENGTH ${HASHX_SALT} HASHX_SALT_LENGTH)
  if(HASHX_SALT_LENGTH GREATER 15)
    message(SEND_ERROR "The maximum salt length is 15 characters")
  else()
    add_definitions(-DHASHX_SALT=${HASHX_SALT})
  endif()
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
