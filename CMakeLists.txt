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

set (SWITCHD ops-switchd)
project (${SWITCHD})
set (SRC_DIR src)

# Define compile flags
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DOPS -DOPS_TEMP -I$(OVS_INCLUDE) -std=gnu99 -Werror")

# Rules to locate needed libraries
include(FindPkgConfig)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)
pkg_check_modules(OVSDB REQUIRED libovsdb)
pkg_check_modules(OPENVSWITCH REQUIRED libopenvswitch libofproto libsflow libplugins)


# Source files to build switchd
set (SOURCES ${SRC_DIR}/bridge.c
             ${SRC_DIR}/bridge.h
             ${SRC_DIR}/bufmon.c
             ${SRC_DIR}/bufmon-provider.c
             ${SRC_DIR}/bufmon-provider.h
             ${SRC_DIR}/openswitch-dflt.h
             ${SRC_DIR}/openswitch-idl.h
             ${SRC_DIR}/ops-dirs.h
             ${SRC_DIR}/ovs-vswitchd.c
             ${SRC_DIR}/subsystem.c
             ${SRC_DIR}/subsystem.h
             ${SRC_DIR}/system-stats.c
             ${SRC_DIR}/system-stats.h
             ${SRC_DIR}/vrf.c
             ${SRC_DIR}/vrf.h)

# Rules to build switchd
add_executable (${SWITCHD} ${SOURCES})
target_link_libraries (${SWITCHD} ${OVSCOMMON_LIBRARIES}
                       ${OVSDB_LIBRARIES}
                       ${OPENVSWITCH_LIBRARIES}
                       -lpthread -lrt)

# Rules to install switchd binary in rootfs
install (TARGETS ${SWITCHD}
    RUNTIME DESTINATION sbin)
