# (c) Copyright 2016 Hewlett Packard Enterprise Development LP
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License. You may obtain
# a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations
# under the License.

cmake_minimum_required(VERSION 2.8)

# Setup project variables
project(logical_switch_plugin)
set (APPLICATION_NAME "switchd_logical_switch_plugin")
set (APPLICATION_CODENAME "${PROJECT_NAME}")
set (APPLICATION_COPYRIGHT_YEARS "2016")
set (APPLICATION_VERSION_MAJOR 0)
set (APPLICATION_VERSION_MINOR 1)
set (APPLICATION_VERSION_PATCH 0)
set (APPLICATION_VERSION_TYPE SNAPSHOT)
set (APPLICATION_VERSION_STRING "${APPLICATION_VERSION_MAJOR}.${APPLICATION_VERSION_MINOR}.${APPLICATION_VERSION_PATCH}-${APPLICATION_VERSION_TYPE}")

#Set variables of the directory project
set(LIB_NAME "logical_switch_plugin")
set(INCLUDE_DIR include)
set(SRC_DIR src)

# Define compile variables
set(CMAKE_C_FLAGS
  "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Werror")
set(CMAKE_CXX_FLAGS
  "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Werror")

#set the files that will be compiled
set (SOURCES
  ${SRC_DIR}/${LIB_NAME}.c)

# Define and locate needed libraries and includes
include(FindPkgConfig)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)

link_directories(${OVSCOMMON_LIBRARY_DIRS})

# Specify include directory
include_directories(
  ${INCLUDE_DIR}
  ${PROJECT_SOURCE_DIR}/${INCLUDE_DIR}
  ${OVSCOMMON_INCLUDE_DIRS})

# Create our library
add_library (${LIB_NAME} SHARED ${SOURCES})

# Include external libraries to link
target_link_libraries(${LIB_NAME}
  ${OVSCOMMON_LIBRARIES})

# Installation
install(TARGETS ${LIB_NAME}
  LIBRARY DESTINATION lib/openvswitch/plugins)

install(FILES
  DESTINATION include/plugins)
