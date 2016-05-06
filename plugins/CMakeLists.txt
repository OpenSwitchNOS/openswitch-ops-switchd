# Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

cmake_minimum_required (VERSION 2.8)

set (LIB_PLUGINS switchd_plugins)
project (${LIB_PLUGINS})

# Set library version
set(PLUGINS_VERSION_MAJOR 1)
set(PLUGINS_VERSION_MINOR 0)
set(PLUGINS_VERSION_PATCH 0)
set(PLUGINS_VERSION_STRING ${PLUGINS_VERSION_MAJOR}.${PLUGINS_VERSION_MINOR}.${PLUGINS_VERSION_PATCH})

set (libdir "/usr/lib")
set (SRC_DIR src)
set (INCLUDE_DIR include)

# Rules to locate needed libraries
pkg_check_modules(YAML REQUIRED yaml-0.1)

# Define compile flags
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${YAML_INCLUDE}")

# Configure ops-dirs file
configure_file("ops-dirs.c.in"
               "ops-dirs.c")

# Source files to build switchd
set (SOURCES ${CMAKE_CURRENT_BINARY_DIR}/ops-dirs.c
             reconfigure-blocks.c
             reconfigure-blocks.h
             run-blocks.c
             run-blocks.h
             plugin-extensions.c
             plugin-extensions.h
             plugins_yaml.c
             plugins_yaml.h
             ops-dirs.h
             plugins.c
             plugins.h
             stats-blocks.c
             stats-blocks.h
             )

set (HEADERS asic-plugin.h
             reconfigure-blocks.h
             run-blocks.h
             plugin-extensions.h
             plugins.h
             qos-asic-provider.h
             stats-blocks.h
             copp-asic-provider.h
             )

# Rules to build switchd
add_library (${LIB_PLUGINS} SHARED ${SOURCES})
target_link_libraries (${LIB_PLUGINS} ${YAML_LIBRARIES})

# Add library properties and versioning
add_definitions(-DYAML_PATH=$(sysconfdir)/openswitch/platform)
set_target_properties(${LIB_PLUGINS} PROPERTIES VERSION ${PLUGINS_VERSION_STRING}
  SOVERSION ${PLUGINS_VERSION_MAJOR})

install(FILES ${HEADERS} DESTINATION include)

# Rules to install plugins library in rootfs
install (TARGETS ${LIB_PLUGINS}
    LIBRARY DESTINATION lib)
