
# ==================================================================================================
# This file is part of the CLTune project.
#
# Author: cedric.nugteren@surfsara.nl (Cedric Nugteren)
#
# -------------------------------------------------------------------------------------------------
#
# Copyright 2014 SURFsara
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#  http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# ==================================================================================================

# CMake project
cmake_minimum_required(VERSION 3.1)
project("cltune" CXX)
set(cltune_VERSION_MAJOR 1)
set(cltune_VERSION_MINOR 7)
set(cltune_VERSION_PATCH 0)

# Options
option(SAMPLES "Enable compilation of sample programs" ON)
option(TESTS "Enable compilation of the Google tests" OFF)

# ==================================================================================================

# RPATH settings
set(CMAKE_SKIP_BUILD_RPATH false) # Use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_BUILD_WITH_INSTALL_RPATH false) # When building, don't use the install RPATH already
set(CMAKE_INSTALL_RPATH "") # The RPATH to be used when installing
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH false) # Don't add the automatically determined parts

# ==================================================================================================

# Compiler-version check (requires at least CMake 2.8.10)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.7)
    message(FATAL_ERROR "GCC version must be at least 4.7")
  endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
    message(FATAL_ERROR "Clang version must be at least 3.3")
  endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0)
    message(FATAL_ERROR "Clang version must be at least 5.0")
  endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14.0)
    message(FATAL_ERROR "ICC version must be at least 14.0")
  endif()
elseif (MSVC)
  if (MSVC_VERSION LESS 1800)
    message(FATAL_ERROR "MS Visual Studio version must be at least 18.0")
  endif()
endif()

# C++ compiler settings
if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS /Os /fp:fast /arch:AVX /EHsc)
else()
add_definitions(-O3)
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(FLAGS "${FLAGS} -Wall -Wno-comment")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8.4)
    set(FLAGS "${FLAGS} -Wno-attributes")
  endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  set(FLAGS "${FLAGS} -Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded")
  set(FLAGS "${FLAGS} -Wno-missing-prototypes -Wno-float-equal -Wno-weak-vtables")
  set(FLAGS "${FLAGS} -Wno-exit-time-destructors -Wno-global-constructors -Wno-missing-prototypes")
  set(FLAGS "${FLAGS} -Wno-missing-noreturn -Wno-covered-switch-default")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAGS}")

# ==================================================================================================

# Package scripts location
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

# Requires OpenCL (FindOpenCL is included as part of this project)
find_package(OpenCL REQUIRED)

# ==================================================================================================

# The includes
include_directories(${cltune_SOURCE_DIR}/include ${OPENCL_INCLUDE_DIRS})

# Gathers all source-files
set(TUNER
    src/cltune.cc
    src/tuner_impl.cc
    src/kernel_info.cc
    src/searcher.cc
    src/searchers/full_search.cc
    src/searchers/random_search.cc
    src/searchers/annealing.cc
    src/searchers/pso.cc
    src/ml_model.cc
    src/ml_models/linear_regression.cc)

# Creates and links the library
add_library(cltune STATIC ${TUNER})
target_link_libraries(cltune ${OPENCL_LIBRARIES})

# Installs the library
install(TARGETS cltune DESTINATION lib)
install(FILES include/cltune.h DESTINATION include)

# ==================================================================================================
# Optional: Enables compilation of sample programs
if (SAMPLES)

  # Adds sample programs
  add_executable(sample_simple samples/simple/simple.cc)
  add_executable(sample_gemm samples/gemm/gemm.cc)
  add_executable(sample_conv samples/conv/conv.cc)
  target_link_libraries(sample_simple cltune ${OPENCL_LIBRARIES} ${OpenMP_LIBRARY})
  target_link_libraries(sample_gemm cltune ${OPENCL_LIBRARIES} ${OpenMP_LIBRARY})
  target_link_libraries(sample_conv cltune ${OPENCL_LIBRARIES} ${OpenMP_LIBRARY})

  # Note: these are not installed because they depend on their separate OpenCL kernel files

endif()
# ==================================================================================================
# Optional: Enables compilation of the Google tests
if (TESTS)

  # The tests use specific flags to reduce the amount of warnings from GTest.
  #set(CMAKE_CXX_FLAGS "-O3 -std=c++11")

  # Enables Google Test tests (source-code is shipped with the project)
  add_subdirectory(external/gtest-1.7.0)
  enable_testing()
  include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

  # Compiles the tests
  add_executable(unit_tests test/tuner.cc test/kernel_info.cc)
  target_link_libraries(unit_tests gtest gtest_main cltune ${OPENCL_LIBRARIES})

  # Adds the tests
  add_test(name unit_tests command unit_tests)
endif()
# ==================================================================================================
